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
#include <libnotify/notify.h>
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

	if (opacity >= 1.0)
		return G_SOURCE_REMOVE;

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
	guint id;

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

static void
launch_app (NotifyNotification *n, gchar *action, gpointer data)
{
	GsApp *app = data;
	GdkDisplay *display;
	GAppLaunchContext *context;
	gchar *id;
	GAppInfo *appinfo;
	GError *error = NULL;

	notify_notification_close (n, NULL);
	if (g_strcmp0 (action, "launch") == 0) {
		display = gdk_display_get_default ();
		id = g_strconcat (gs_app_get_id (app), ".desktop", NULL);
		appinfo = G_APP_INFO (g_desktop_app_info_new (id));
		if (!appinfo) {
			g_warning ("no such desktop file: %s", id);
			goto out;
		}
		g_free (id);

		context = G_APP_LAUNCH_CONTEXT (gdk_display_get_app_launch_context (display));
		if (!g_app_info_launch (appinfo, NULL, context, &error)) {
			g_warning ("launching %s failed: %s",
				   gs_app_get_id (app), error->message);
			g_error_free (error);
		}

		g_object_unref (appinfo);
		g_object_unref (context);
	}
out: ;
}

static void
on_notification_closed (NotifyNotification *n, gpointer data)
{
	g_object_unref (n);
}

void
gs_app_notify_installed (GsApp *app)
{
	gchar *summary;
	NotifyNotification *n;

	/* TRANSLATORS: this is the summary of a notification that an application
	 * has been successfully installed */
	summary = g_strdup_printf (_("%s is now installed"), gs_app_get_name (app));
	n = notify_notification_new (summary, NULL, "gnome-software");
	/* TRANSLATORS: this is button that opens the newly installed application */
	notify_notification_add_action (n, "launch", _("Launch"),
					launch_app, g_object_ref (app), g_object_unref);
	g_signal_connect (n, "closed", G_CALLBACK (on_notification_closed), NULL);
	notify_notification_show (n, NULL);

	g_free (summary);
}

/**
 * gs_app_notify_failed_modal_response_cb:
 **/
static void
gs_app_notify_failed_modal_response_cb (GtkDialog *dialog,
					gint response_id,
					gpointer user_data)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

/**
 * gs_app_notify_failed_modal:
 **/
void
gs_app_notify_failed_modal (GtkBuilder *builder,
			    GsApp *app,
			    GsPluginLoaderAction action,
			    const GError *error)
{
	GtkWidget *dialog;
	GtkWindow *window;
	gchar *title;
	gchar *msg;

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
	window = GTK_WINDOW (gtk_builder_get_object (builder, "window_software"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL |
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", msg);
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gs_app_notify_failed_modal_response_cb), NULL);
	gtk_window_present (GTK_WINDOW (dialog));
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
	gboolean ret = TRUE;
	gchar *parent;

	parent = g_path_get_dirname (path);
	if (g_mkdir_with_parents (parent, 0755) == -1) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to create '%s': %s",
			     parent, g_strerror (errno));
	}

	g_free (parent);
	return ret;
}

/**
 * gs_pixbuf_load:
 **/
GdkPixbuf *
gs_pixbuf_load (const gchar *icon_name, guint icon_size, GError **error)
{
	GdkPixbuf *pixbuf = NULL;

	if (icon_name[0] == '/') {
		pixbuf = gdk_pixbuf_new_from_file_at_size (icon_name,
							   icon_size,
							   icon_size,
							   error);
	} else if (g_strstr_len (icon_name, -1, ".") == NULL) {
		pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						   icon_name,
						   icon_size,
						   GTK_ICON_LOOKUP_USE_BUILTIN |
						   GTK_ICON_LOOKUP_FORCE_SIZE,
						   error);
	} else {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to load icon %s", icon_name);
	}
	return pixbuf;
}

/* vim: set noexpandtab: */
