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

#ifdef HAVE_POLKIT
#include <polkit/polkit.h>
#endif

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
	if (gs_app_get_kind (app) == AS_APP_KIND_DESKTOP) {
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
	GS_APP_LICENSE_FREE		= 0,
	GS_APP_LICENSE_NONFREE		= 1,
	GS_APP_LICENSE_PATENT_CONCERN	= 2
} GsAppLicenseHint;

/**
 * gs_app_notify_unavailable:
 **/
GtkResponseType
gs_app_notify_unavailable (GsApp *app, GtkWindow *parent)
{
	GsAppLicenseHint hint = GS_APP_LICENSE_FREE;
	GtkResponseType response;
	GtkWidget *dialog;
	const gchar *license;
	gboolean already_enabled = FALSE;	/* FIXME */
	guint i;
	struct {
		const gchar	*str;
		GsAppLicenseHint hint;
	} keywords[] = {
		{ "NonFree",		GS_APP_LICENSE_NONFREE },
		{ "PatentConcern",	GS_APP_LICENSE_PATENT_CONCERN },
		{ "Proprietary",	GS_APP_LICENSE_NONFREE },
		{ NULL, 0 }
	};
	g_autofree gchar *origin_url = NULL;
	g_autoptr(GSettings) settings = NULL;
	g_autoptr(GString) body = NULL;
	g_autoptr(GString) title = NULL;

	/* this is very crude */
	license = gs_app_get_license (app);
	if (license != NULL) {
		for (i = 0; keywords[i].str != NULL; i++) {
			if (g_strstr_len (license, -1, keywords[i].str) != NULL)
				hint |= keywords[i].hint;
		}
	} else {
		/* use the worst-case assumption */
		hint = GS_APP_LICENSE_NONFREE | GS_APP_LICENSE_PATENT_CONCERN;
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
	if (hint & GS_APP_LICENSE_NONFREE) {
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
	if (hint & GS_APP_LICENSE_PATENT_CONCERN) {
		g_string_append (body, "\n\n");
		if (gs_app_get_kind (app) != AS_APP_KIND_CODEC) {
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

	salted = g_strdup_printf ("gnome-software[%s:%s]",
				  g_get_user_name (), data);
	return g_compute_checksum_for_string (G_CHECKSUM_SHA1, salted, -1);
}

/**
 * gs_utils_get_permission:
 **/
GPermission *
gs_utils_get_permission (const gchar *id)
{
#ifdef HAVE_POLKIT
	g_autoptr(GPermission) permission = NULL;
	g_autoptr(GError) error = NULL;

	permission = polkit_permission_new_sync (id, NULL, NULL, &error);
	if (permission == NULL) {
		g_warning ("Failed to create permission %s: %s", id, error->message);
		return NULL;
	}
	return g_steal_pointer (&permission);
#else
	g_debug ("no PolicyKit, so can't return GPermission for %s", id);
	return NULL;
#endif
}

#if AS_CHECK_VERSION(0,5,12)
/**
 * gs_utils_get_content_rating:
 *
 * Note: These are strings marked for translation for comment.
 * This functionality is not currently used.
 **/
const gchar *
gs_utils_get_content_rating (void)
{
	struct {
		const gchar		*id;
		AsContentRatingValue	 value;
		const gchar		*desc;
	} content_rating_oars[] =  {
	{ "violence-cartoon",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating violence-cartoon", "None") },
	{ "violence-cartoon",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Cartoon characters in unsafe situations") },
	{ "violence-cartoon",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Cartoon characters in aggressive conflict") },
	{ "violence-cartoon",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Graphic violence involving cartoon characters") },
	{ "violence-fantasy",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating violence-fantasy", "None") },
	{ "violence-fantasy",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Characters in unsafe situations easily distinguishable from reality") },
	{ "violence-fantasy",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Characters in aggressive conflict easily distinguishable from reality") },
	{ "violence-fantasy",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Graphic violence easily distinguishable from reality") },
	{ "violence-realistic",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating violence-realistic", "None") },
	{ "violence-realistic",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Mild realistic characters in unsafe situations") },
	{ "violence-realistic",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Depictions of realistic characters in aggressive conflict") },
	{ "violence-realistic",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Graphic violence involving realistic characters") },
	{ "violence-bloodshed",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating violence-bloodshed", "None") },
	{ "violence-bloodshed",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Unrealistic bloodshed") },
	{ "violence-bloodshed",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Realistic bloodshed") },
	{ "violence-bloodshed",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Depictions of bloodshed and the mutilation of body parts") },
	{ "violence-sexual",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating violence-sexual", "None") },
	{ "violence-sexual",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Rape or other violent sexual behavior") },
	{ "drugs-alcohol",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating drugs-alcohol", "None") },
	{ "drugs-alcohol",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("References to alcoholic beverages") },
	{ "drugs-alcohol",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Use of alcoholic beverages") },
	{ "drugs-narcotics",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating drugs-narcotics", "None") },
	{ "drugs-narcotics",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("References to illicit drugs") },
	{ "drugs-narcotics",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Use of illicit drugs") },
	{ "drugs-tobacco",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("References to tobacco products") },
	{ "drugs-tobacco",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Use of tobacco products") },
	{ "sex-nudity",		AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating sex-nudity", "None") },
	{ "sex-nudity",		AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Brief artistic nudity") },
	{ "sex-nudity",		AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Prolonged nudity") },
	{ "sex-themes",		AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating sex-themes", "None") },
	{ "sex-themes",		AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Provocative references or depictions") },
	{ "sex-themes",		AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Sexual references or depictions") },
	{ "sex-themes",		AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Graphic sexual behavior") },
	{ "language-profanity",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating language-profanity", "None") },
	{ "language-profanity",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Mild or infrequent use of profanity") },
	{ "language-profanity",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Moderate use of profanity") },
	{ "language-profanity",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Strong or frequent use of profanity") },
	{ "language-humor",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating language-humor", "None") },
	{ "language-humor",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Slapstick humor") },
	{ "language-humor",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Vulgar or bathroom humor") },
	{ "language-humor",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Mature or sexual humor") },
	{ "language-discrimination", AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating language-discrimination", "None") },
	{ "language-discrimination", AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Negativity towards a specific group of people") },
	{ "language-discrimination", AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Discrimation designed to cause emotional harm") },
	{ "language-discrimination", AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Explicit discrimination based on gender, sexuality, race or religion") },
	{ "money-advertising", AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating money-advertising", "None") },
	{ "money-advertising", AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Product placement") },
	{ "money-advertising", AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Explicit references to specific brands or trademarked products") },
	{ "money-advertising", AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Players are encouraged to purchase specific real-world items") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating money-gambling", "None") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Gambling on random events using tokens or credits") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Gambling using \"play\" money") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Gambling using real money") },
	{ "money-purchasing",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating money-purchasing", "None") },
	{ "money-purchasing",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Ability to spend real money in-game") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating social-chat", "None") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Player-to-player game interactions without chat functionality") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Player-to-player preset interactions without chat functionality") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Uncontrolled chat functionality between players") },
	{ "social-audio",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating social-audio", "None") },
	{ "social-audio",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Uncontrolled audio or video chat functionality between players") },
	{ "social-contacts",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating social-contacts", "None") },
	{ "social-contacts",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Sharing social network usernames or email addresses") },
	{ "social-info",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating social-info", "None") },
	{ "social-info",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Sharing user information with 3rd parties") },
	{ "social-location",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	C_("content rating social-location", "None") },
	{ "social-location",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description: comments welcome */
	_("Sharing physical location to other users") },
	{ NULL, 0, NULL } };
	return content_rating_oars[0].desc;
}
#endif

/**
 * gs_utils_get_content_type:
 */
gchar *
gs_utils_get_content_type (const gchar *filename,
			   GCancellable *cancellable,
			   GError **error)
{
	const gchar *tmp;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	/* get content type */
	file = g_file_new_for_path (filename);
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  G_FILE_QUERY_INFO_NONE,
				  cancellable,
				  error);
	if (info == NULL)
		return NULL;
	tmp = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
	if (tmp == NULL)
		return NULL;
	return g_strdup (tmp);
}

/* vim: set noexpandtab: */
