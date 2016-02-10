/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>
#include <errno.h>

#include "gs-app.h"
#include "gs-utils.h"
#include "gs-plugin.h"

#define SPINNER_DELAY 500

static gboolean
fade_in (gpointer data)
{
	GtkWidget *spinner = data;
	gdouble opacity;

	opacity = gtk_widget_get_opacity (spinner);
	opacity = opacity + 0.1;
	gtk_widget_set_opacity (spinner, opacity);

	if (opacity >= 1.0) {
		g_object_steal_data (G_OBJECT (spinner), "fade-timeout");
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void
remove_source (gpointer data)
{
	g_source_remove (GPOINTER_TO_UINT (data));
}

static gboolean
start_spinning (gpointer data)
{
	GtkWidget *spinner = data;
	guint id;

	gtk_widget_set_opacity (spinner, 0);
	gtk_spinner_start (GTK_SPINNER (spinner));
	id = g_timeout_add (100, fade_in, spinner);
	g_object_set_data_full (G_OBJECT (spinner), "fade-timeout",
				GUINT_TO_POINTER (id), remove_source);

	/* don't try to remove this source in the future */
	g_object_steal_data (G_OBJECT (spinner), "start-timeout");
	return G_SOURCE_REMOVE;
}

void
gs_stop_spinner (GtkSpinner *spinner)
{
	g_object_set_data (G_OBJECT (spinner), "start-timeout", NULL);
	gtk_spinner_stop (spinner);
}

void
gs_start_spinner (GtkSpinner *spinner)
{
	gboolean active;
	guint id;

	/* Don't do anything if it's already spinning */
	g_object_get (spinner, "active", &active, NULL);
	if (active || g_object_get_data (G_OBJECT (spinner), "start-timeout") != NULL)
		return;

	gtk_widget_set_opacity (GTK_WIDGET (spinner), 0);
	id = g_timeout_add (SPINNER_DELAY, start_spinning, spinner);
	g_object_set_data_full (G_OBJECT (spinner), "start-timeout",
				GUINT_TO_POINTER (id), remove_source);
}

static void
remove_all_cb (GtkWidget *widget, gpointer user_data)
{
	GtkContainer *container = GTK_CONTAINER (user_data);
	gtk_container_remove (container, widget);
}

void
gs_container_remove_all (GtkContainer *container)
{
	gtk_container_foreach (container, remove_all_cb, container);
}

static void
grab_focus (GtkWidget *widget)
{
	g_signal_handlers_disconnect_by_func (widget, grab_focus, NULL);
	gtk_widget_grab_focus (widget);
}

void
gs_grab_focus_when_mapped (GtkWidget *widget)
{
	if (gtk_widget_get_mapped (widget))
		gtk_widget_grab_focus (widget);
	else
		g_signal_connect_after (widget, "map",
					G_CALLBACK (grab_focus), NULL);
}

void
gs_app_notify_installed (GsApp *app)
{
	g_autofree gchar *summary = NULL;
	g_autoptr(GNotification) n = NULL;

	/* TRANSLATORS: this is the summary of a notification that an application
	 * has been successfully installed */
	summary = g_strdup_printf (_("%s is now installed"), gs_app_get_name (app));
	n = g_notification_new (summary);
	if (gs_app_get_id_kind (app) == AS_ID_KIND_DESKTOP) {
		/* TRANSLATORS: this is button that opens the newly installed application */
		g_notification_add_button_with_target (n, _("Launch"),
						       "app.launch", "s",
						       gs_app_get_id (app));
	}
	g_notification_set_default_action_and_target  (n, "app.details", "(ss)",
						       gs_app_get_id (app), "");
	g_application_send_notification (g_application_get_default (), "installed", n);
}

/**
 * gs_app_notify_failed_modal:
 **/
void
gs_app_notify_failed_modal (GsApp *app,
			    GtkWindow *parent_window,
			    GsPluginLoaderAction action,
			    const GError *error)
{
	GtkWidget *dialog;
	const gchar *title;
	g_autofree gchar *msg = NULL;

	title = _("Sorry, this did not work");
	switch (action) {
	case GS_PLUGIN_LOADER_ACTION_INSTALL:
		/* TRANSLATORS: this is when the install fails */
		msg = g_strdup_printf (_("Installation of %s failed."),
				       gs_app_get_name (app));
		break;
	case GS_PLUGIN_LOADER_ACTION_REMOVE:
		/* TRANSLATORS: this is when the remove fails */
		msg = g_strdup_printf (_("Removal of %s failed."),
				       gs_app_get_name (app));
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	dialog = gtk_message_dialog_new (parent_window,
					 GTK_DIALOG_MODAL |
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", msg);
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gtk_widget_destroy), NULL);
	gtk_window_present (GTK_WINDOW (dialog));
}

typedef enum {
	GS_APP_LICENCE_FREE		= 0,
	GS_APP_LICENCE_NONFREE		= 1,
	GS_APP_LICENCE_PATENT_CONCERN	= 2
} GsAppLicenceHint;

/**
 * gs_app_notify_unavailable:
 **/
GtkResponseType
gs_app_notify_unavailable (GsApp *app, GtkWindow *parent)
{
	GsAppLicenceHint hint = GS_APP_LICENCE_FREE;
	GtkResponseType response;
	GtkWidget *dialog;
	const gchar *licence;
	gboolean already_enabled = FALSE;	/* FIXME */
	guint i;
	struct {
		const gchar	*str;
		GsAppLicenceHint hint;
	} keywords[] = {
		{ "NonFree",		GS_APP_LICENCE_NONFREE },
		{ "PatentConcern",	GS_APP_LICENCE_PATENT_CONCERN },
		{ "Proprietary",	GS_APP_LICENCE_NONFREE },
		{ NULL, 0 }
	};
	g_autofree gchar *origin_url = NULL;
	g_autoptr(GSettings) settings = NULL;
	g_autoptr(GString) body = NULL;
	g_autoptr(GString) title = NULL;

	/* this is very crude */
	licence = gs_app_get_licence (app);
	if (licence != NULL) {
		for (i = 0; keywords[i].str != NULL; i++) {
			if (g_strstr_len (licence, -1, keywords[i].str) != NULL)
				hint |= keywords[i].hint;
		}
	} else {
		/* use the worst-case assumption */
		hint = GS_APP_LICENCE_NONFREE | GS_APP_LICENCE_PATENT_CONCERN;
	}

	/* check if the user has already dismissed */
	settings = g_settings_new ("org.gnome.software");
	if (!g_settings_get_boolean (settings, "prompt-for-nonfree"))
		return GTK_RESPONSE_OK;

	title = g_string_new ("");
	if (already_enabled) {
		g_string_append_printf (title, "<b>%s</b>",
					/* TRANSLATORS: window title */
					_("Install Third-Party Software?"));
	} else {
		g_string_append_printf (title, "<b>%s</b>",
					/* TRANSLATORS: window title */
					_("Enable Third-Party Software Source?"));
	}
	dialog = gtk_message_dialog_new (parent,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CANCEL,
					 NULL);
	gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), title->str);

	/* FIXME: get the URL somehow... */
	origin_url = g_strdup_printf ("<a href=\"\">%s</a>", gs_app_get_origin (app));
	body = g_string_new ("");
	if (hint & GS_APP_LICENCE_NONFREE) {
		g_string_append_printf (body,
					/* TRANSLATORS: the replacements are as follows:
					 * 1. Application name, e.g. "Firefox"
					 * 2. Software source name, e.g. fedora-optional
					 */
					_("%s is not <a href=\"https://en.wikipedia.org/wiki/Free_and_open-source_software\">"
					  "free and open source software</a>, "
					  "and is provided by “%s”."),
					gs_app_get_name (app),
					origin_url);
	} else {
		g_string_append_printf (body,
					/* TRANSLATORS: the replacements are as follows:
					 * 1. Application name, e.g. "Firefox"
					 * 2. Software source name, e.g. fedora-optional */
					_("%s is provided by “%s”."),
					gs_app_get_name (app),
					origin_url);
	}

	/* tell the use what needs to be done */
	if (!already_enabled) {
		g_string_append (body, " ");
		g_string_append (body,
				/* TRANSLATORS: a software source is a repo */
				_("This software source must be "
				  "enabled to continue installation."));
	}

	/* be aware of patent clauses */
	if (hint & GS_APP_LICENCE_PATENT_CONCERN) {
		g_string_append (body, "\n\n");
		if (gs_app_get_id_kind (app) != AS_ID_KIND_CODEC) {
			g_string_append_printf (body,
						/* TRANSLATORS: Laws are geographical, urgh... */
						_("It may be illegal to install "
						  "or use %s in some countries."),
						gs_app_get_name (app));
		} else {
			g_string_append (body,
					/* TRANSLATORS: Laws are geographical, urgh... */
					_("It may be illegal to install or use "
					  "this codec in some countries."));
		}
	}

	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", body->str);
	/* TRANSLATORS: this is button text to not ask about non-free content again */
	if (0) gtk_dialog_add_button (GTK_DIALOG (dialog), _("Don't Warn Again"), GTK_RESPONSE_YES);
	if (already_enabled) {
		gtk_dialog_add_button (GTK_DIALOG (dialog),
				       /* TRANSLATORS: button text */
				       _("Install"),
				       GTK_RESPONSE_OK);
	} else {
		gtk_dialog_add_button (GTK_DIALOG (dialog),
				       /* TRANSLATORS: button text */
				       _("Enable and Install"),
				       GTK_RESPONSE_OK);
	}
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_YES) {
		response = GTK_RESPONSE_OK;
		g_settings_set_boolean (settings, "prompt-for-nonfree", FALSE);
	}
	gtk_widget_destroy (dialog);
	return response;
}

void
gs_app_show_url (GsApp *app, AsUrlKind kind)
{
	const gchar *url;
	g_autoptr(GError) error = NULL;

	url = gs_app_get_url (app, kind);
	if (!gtk_show_uri (NULL, url, GDK_CURRENT_TIME, &error))
		g_warning ("spawn of '%s' failed", url);
}

guint
gs_string_replace (GString *string, const gchar *search, const gchar *replace)
{
	gchar *tmp;
	guint count = 0;
	guint replace_len;
	guint search_len;

	search_len = strlen (search);
	replace_len = strlen (replace);

	do {
		tmp = g_strstr_len (string->str, -1, search);
		if (tmp == NULL)
			goto out;

		/* reallocate the string if required */
		if (search_len > replace_len) {
			g_string_erase (string,
					tmp - string->str,
					search_len - replace_len);
		}
		if (search_len < replace_len) {
			g_string_insert_len (string,
					    tmp - string->str,
					    search,
					    replace_len - search_len);
		}

		/* just memcmp in the new string */
		memcpy (tmp, replace, replace_len);
		count++;
	} while (TRUE);
out:
	return count;
}

/**
 * gs_mkdir_parent:
 **/
gboolean
gs_mkdir_parent (const gchar *path, GError **error)
{
	g_autofree gchar *parent = NULL;

	parent = g_path_get_dirname (path);
	if (g_mkdir_with_parents (parent, 0755) == -1) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to create '%s': %s",
			     parent, g_strerror (errno));
		return FALSE;
	}
	return TRUE;
}

/**
 * gs_image_set_from_pixbuf_with_scale:
 **/
void
gs_image_set_from_pixbuf_with_scale (GtkImage *image, const GdkPixbuf *pixbuf, gint scale)
{
	cairo_surface_t *surface;
	surface = gdk_cairo_surface_create_from_pixbuf (pixbuf, scale, NULL);
	gtk_image_set_from_surface (image, surface);
	cairo_surface_destroy (surface);
}

/**
 * gs_image_set_from_pixbuf:
 **/
void
gs_image_set_from_pixbuf (GtkImage *image, const GdkPixbuf *pixbuf)
{
	gint scale;
	scale = gdk_pixbuf_get_width (pixbuf) / 64;
	gs_image_set_from_pixbuf_with_scale (image, pixbuf, scale);
}

/**
 * gs_utils_get_file_age:
 *
 * Returns: The time in seconds since the file was modified
 */
guint
gs_utils_get_file_age (GFile *file)
{
	guint64 now;
	guint64 mtime;
	g_autoptr(GFileInfo) info = NULL;

	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_TIME_MODIFIED,
				  G_FILE_QUERY_INFO_NONE,
				  NULL,
				  NULL);
	if (info == NULL)
		return G_MAXUINT;
	mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	now = (guint64) g_get_real_time () / G_USEC_PER_SEC;
	if (mtime > now)
		return G_MAXUINT;
	return now - mtime;
}

const gchar *
gs_user_agent (void)
{
	return PACKAGE_NAME "/" PACKAGE_VERSION;
}

/**
 * gs_utils_get_cachedir:
 **/
gchar *
gs_utils_get_cachedir (const gchar *kind, GError **error)
{
	g_autofree gchar *vername = NULL;
	g_autofree gchar *cachedir = NULL;
	g_auto(GStrv) version = g_strsplit (VERSION, ".", 3);
	g_autoptr(GFile) cachedir_file = NULL;

	/* create the cachedir in a per-release location, creating
	 * if it does not already exist */
	vername = g_strdup_printf ("%s.%s", version[0], version[1]);
	cachedir = g_build_filename (g_get_user_cache_dir (),
				      "gnome-software", vername, kind, NULL);
	cachedir_file = g_file_new_for_path (cachedir);
	if (!g_file_query_exists (cachedir_file, NULL) &&
	    !g_file_make_directory_with_parents (cachedir_file, NULL, error))
		return NULL;

	return g_steal_pointer (&cachedir);
}

/**
 * gs_utils_get_user_hash:
 *
 * This SHA1 hash is composed of the contents of machine-id and your
 * usename and is also salted with a hardcoded value.
 *
 * This provides an identifier that can be used to identify a specific
 * user on a machine, allowing them to cast only one vote or perform
 * one review on each application.
 *
 * There is no known way to calculate the machine ID or username from
 * the machine hash and there should be no privacy issue.
 */
gchar *
gs_utils_get_user_hash (GError **error)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *salted = NULL;

	if (!g_file_get_contents ("/etc/machine-id", &data, NULL, error))
		return NULL;

	salted = g_strdup_printf ("XXXYYgnome-software[%s:%s]",
				  g_get_user_name (), data);
	return g_compute_checksum_for_string (G_CHECKSUM_SHA1, salted, -1);
}

/* vim: set noexpandtab: */
