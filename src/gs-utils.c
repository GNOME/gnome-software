/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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
#include "gs-cleanup.h"
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
	_cleanup_free_ gchar *summary = NULL;
	_cleanup_object_unref_ GNotification *n = NULL;

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
	_cleanup_free_ gchar *msg = NULL;

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

/**
 * gs_app_notify_unavailable_nonfree:
 **/
static GtkResponseType
gs_app_notify_unavailable_nonfree (GsApp *app, GtkWindow *parent)
{
	GtkResponseType response;
	GtkWidget *dialog;
	_cleanup_object_unref_ GSettings *settings = NULL;
	_cleanup_string_free_ GString *body = NULL;
	_cleanup_string_free_ GString *title = NULL;

	/* check if the user has already dismissed */
	settings = g_settings_new ("org.gnome.software");
	if (!g_settings_get_boolean (settings, "prompt-for-nonfree"))
		return GTK_RESPONSE_OK;

	title = g_string_new ("");
	g_string_append_printf (title, "<b>%s</b>",
				/* TRANSLATORS: window title, nonfree in this
				 * case refers to Free and Open Source */
				_("You're About to Install Non-Free Software"));
	dialog = gtk_message_dialog_new (parent,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CANCEL,
					 NULL);
	gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), title->str);

	body = g_string_new ("");
	g_string_append_printf (body,
				/* TRANSLATORS: the replacements are as follows:
				 * 1. Application name, e.g. "Firefox"
				 * 2. Start of hypertext e.g. <a>
				 * 3. End of hypertext e.g. </a> */
				_("%s is not %sfree and open source software%s."),
				gs_app_get_name (app),
				"<a href=\"http://en.wikipedia.org/wiki/Free_and_open-source_software\">",
				"</a>");
	g_string_append (body, " ");
	g_string_append (body,
			/* TRANSLATORS: Laws are geographical, urgh... */
			_("Depending on your country of residence, "
			  "installing it could make you liable to prosecution."));
	g_string_append (body, "\n");
	g_string_append (body,
			/* TRANSLATORS: ...and you need to ask for advice */
			_("If you are uncertain about this, "
			  "you should obtain legal advice."));

	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", body->str);
	/* TRANSLATORS: this is button text to not ask about non-free content again */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Don't Warn Again"), GTK_RESPONSE_YES);
	/* TRANSLATORS: this is button text to enable the repo and install the app */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Install"), GTK_RESPONSE_OK);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_YES) {
		response = GTK_RESPONSE_OK;
		g_settings_set_boolean (settings, "prompt-for-nonfree", FALSE);
	}
	gtk_widget_destroy (dialog);
	return response;
}

/**
 * gs_app_notify_unavailable_other:
 **/
static GtkResponseType
gs_app_notify_unavailable_other (GsApp *app, GtkWindow *parent)
{
	GtkResponseType response;
	GtkWidget *dialog;
	_cleanup_string_free_ GString *body = NULL;
	_cleanup_string_free_ GString *title = NULL;

	title = g_string_new ("");
	g_string_append_printf (title, "<b>%s</b>",
				/* TRANSLATORS: window title, additional in this
				 * case means not-currently-enabled */
				_("Enable Additional Software Source?"));
	dialog = gtk_message_dialog_new (parent,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CANCEL,
					 NULL);
	gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), title->str);

	body = g_string_new ("");
	g_string_append_printf (body,
				/* TRANSLATORS: the replacements are as follows:
				 * 1. Application name, e.g. "Firefox"
				 * 2. Software source name, e.g. fedora-optional */
				_("%s is provided by %s."),
				gs_app_get_name (app),
				gs_app_get_origin (app));
	g_string_append (body, "\n");
	g_string_append (body,
			/* TRANSLATORS: once the source is enabled it stays enabled */
			_("Do you want to enable it?"));

	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", body->str);
	/* TRANSLATORS: this is button text to enable the repo and install the app */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Enable"), GTK_RESPONSE_OK);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	return response;
}

/**
 * gs_app_notify_unavailable:
 **/
GtkResponseType
gs_app_notify_unavailable (GsApp *app, GtkWindow *parent)
{
	/* this is very crude */
	if (g_strstr_len (gs_app_get_licence (app), -1, "Proprietary") != NULL)
		return gs_app_notify_unavailable_nonfree (app, parent);
	return gs_app_notify_unavailable_other (app, parent);
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
	_cleanup_free_ gchar *parent = NULL;

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

static void
reboot_done (GObject *source, GAsyncResult *res, gpointer data)
{
	GCallback reboot_failed = data;
	_cleanup_variant_unref_ GVariant *ret = NULL;
	_cleanup_error_free_ GError *error = NULL;

	ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
	if (ret)
		return;

	if (error != NULL) {
		g_warning ("Calling org.gnome.SessionManager.Reboot failed: %s",
			   error->message);
	}

	reboot_failed ();
}

void
gs_reboot (GCallback reboot_failed)
{
	_cleanup_object_unref_ GDBusConnection *bus = NULL;

	g_debug ("calling org.gnome.SessionManager.Reboot");

	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	g_dbus_connection_call (bus,
				"org.gnome.SessionManager",
				"/org/gnome/SessionManager",
				"org.gnome.SessionManager",
				"Reboot",
				NULL,
				NULL,
				G_DBUS_CALL_FLAGS_NONE,
				G_MAXINT,
				NULL,
				reboot_done,
				reboot_failed);
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

/* vim: set noexpandtab: */
