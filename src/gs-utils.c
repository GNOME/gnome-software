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
	const gchar *title;
	g_autoptr(GString) msg = NULL;

	/* TRANSLATORS: install or removed failed */
	title = _("Sorry, this did not work");

	/* say what we tried to do */
	msg = g_string_new ("");
	switch (action) {
	case GS_PLUGIN_LOADER_ACTION_INSTALL:
	case GS_PLUGIN_LOADER_ACTION_UPGRADE_DOWNLOAD:
		/* TRANSLATORS: this is when the install fails */
		g_string_append_printf (msg, _("Installation of %s failed."),
					gs_app_get_name (app));
		break;
	case GS_PLUGIN_LOADER_ACTION_REMOVE:
		/* TRANSLATORS: this is when the remove fails */
		g_string_append_printf (msg, _("Removal of %s failed."),
					gs_app_get_name (app));
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	gs_utils_show_error_dialog (parent_window, title, msg->str, error->message);
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

/**
 * gs_utils_is_current_desktop:
 */
gboolean
gs_utils_is_current_desktop (const gchar *name)
{
	const gchar *tmp;
	g_auto(GStrv) names = NULL;
	tmp = g_getenv ("XDG_CURRENT_DESKTOP");
	if (tmp == NULL)
		return FALSE;
	names = g_strsplit (tmp, ":", -1);
	return g_strv_contains ((const gchar * const *) names, name);
}

/**
 * gs_utils_widget_css_parsing_error_cb:
 */
static void
gs_utils_widget_css_parsing_error_cb (GtkCssProvider *provider,
				      GtkCssSection *section,
				      GError *error,
				      gpointer user_data)
{
	g_warning ("CSS parse error %i:%i: %s",
		   gtk_css_section_get_start_line (section),
		   gtk_css_section_get_start_position (section),
		   error->message);
}

/**
 * gs_utils_widget_set_custom_css:
 **/
void
gs_utils_widget_set_custom_css (GtkWidget *widget, const gchar *css)
{
	GString *str = g_string_sized_new (1024);
	GtkStyleContext *context;
	g_autofree gchar *class_name = NULL;
	g_autoptr(GtkCssProvider) provider = NULL;

	/* invalid */
	if (css == NULL)
		return;

	/* make into a proper CSS class */
	class_name = g_strdup_printf ("themed-widget_%p", widget);
	g_string_append_printf (str, ".%s {\n", class_name);
	g_string_append_printf (str, "%s\n", css);
	g_string_append (str, "}");

	g_string_append_printf (str, ".%s:hover {\n", class_name);
	g_string_append (str, "  opacity: 0.9;\n");
	g_string_append (str, "}\n");

	g_debug ("using custom CSS %s", str->str);

	/* set the custom CSS class */
	context = gtk_widget_get_style_context (widget);
	gtk_style_context_add_class (context, class_name);

	/* set up custom provider and store on the widget */
	provider = gtk_css_provider_new ();
	g_signal_connect (provider, "parsing-error",
			  G_CALLBACK (gs_utils_widget_css_parsing_error_cb), NULL);
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (provider),
						   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	gtk_css_provider_load_from_data (provider, str->str, -1, NULL);
	g_object_set_data_full (G_OBJECT (widget),
				"GnomeSoftware::provider",
				g_object_ref (provider),
				g_object_unref);
}

static void
do_not_expand (GtkWidget *child, gpointer data)
{
	gtk_container_child_set (GTK_CONTAINER (gtk_widget_get_parent (child)),
				 child, "expand", FALSE, "fill", FALSE, NULL);
}

static gboolean
unset_focus (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	if (GTK_IS_WINDOW (widget))
		gtk_window_set_focus (GTK_WINDOW (widget), NULL);
	return FALSE;
}

/**
 * insert_details_widget:
 * @dialog: the message dialog where the widget will be inserted
 * @details: the detailed message text to display
 *
 * Inserts a widget displaying the detailed message into the message dialog.
 */
static void
insert_details_widget (GtkMessageDialog *dialog, const gchar *details)
{
	GtkWidget *message_area, *sw, *label;
	GtkWidget *box, *tv;
	GtkTextBuffer *buffer;
	GList *children;
	g_autoptr(GString) msg = NULL;

	g_assert (GTK_IS_MESSAGE_DIALOG (dialog));
	g_assert (details != NULL);

	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

	msg = g_string_new ("");
	g_string_append_printf (msg, "%s\n\n%s",
	                        /* TRANSLATORS: these are show_detailed_error messages from the
	                         * package manager no mortal is supposed to understand,
	                         * but google might know what they mean */
	                        _("Detailed errors from the package manager follow:"),
	                        details);

	message_area = gtk_message_dialog_get_message_area (dialog);
	g_assert (GTK_IS_BOX (message_area));
	/* make the hbox expand */
	box = gtk_widget_get_parent (message_area);
	gtk_container_child_set (GTK_CONTAINER (gtk_widget_get_parent (box)), box,
	                         "expand", TRUE, "fill", TRUE, NULL);
	/* make the labels not expand */
	gtk_container_foreach (GTK_CONTAINER (message_area), do_not_expand, NULL);

	/* Find the secondary label and set its width_chars.   */
	/* Otherwise the label will tend to expand vertically. */
	children = gtk_container_get_children (GTK_CONTAINER (message_area));
	if (children && children->next && GTK_IS_LABEL (children->next->data)) {
		gtk_label_set_width_chars (GTK_LABEL (children->next->data), 40);
	}

	label = gtk_label_new (_("Details"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_visible (label, TRUE);
	gtk_box_pack_start (GTK_BOX (message_area), label, FALSE, FALSE, 0);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
	                                     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
	                                GTK_POLICY_NEVER,
	                                GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sw), 150);
	gtk_widget_set_visible (sw, TRUE);

	tv = gtk_text_view_new ();
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv));
	gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD);
	gtk_style_context_add_class (gtk_widget_get_style_context (tv),
	                             "update-failed-details");
	gtk_text_buffer_set_text (buffer, msg->str, -1);
	gtk_widget_set_visible (tv, TRUE);

	gtk_container_add (GTK_CONTAINER (sw), tv);
	gtk_box_pack_end (GTK_BOX (message_area), sw, TRUE, TRUE, 0);

	g_signal_connect (dialog, "map-event", G_CALLBACK (unset_focus), NULL);
}

/**
 * gs_utils_show_error_dialog:
 * @parent: transient parent, or NULL for none
 * @title: the title for the dialog
 * @msg: the message for the dialog
 * @details: (allow-none): the detailed error message, or NULL for none
 *
 * Shows a message dialog for displaying error messages.
 */
void
gs_utils_show_error_dialog (GtkWindow *parent,
                            const gchar *title,
                            const gchar *msg,
                            const gchar *details)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new_with_markup (parent,
	                                             0,
	                                             GTK_MESSAGE_INFO,
	                                             GTK_BUTTONS_CLOSE,
	                                             "<big><b>%s</b></big>", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
	                                          "%s", msg);
	if (details != NULL)
		insert_details_widget (GTK_MESSAGE_DIALOG (dialog), details);

	g_signal_connect_swapped (dialog, "response",
	                          G_CALLBACK (gtk_widget_destroy),
	                          dialog);
	gtk_widget_show (dialog);
}

/* vim: set noexpandtab: */
