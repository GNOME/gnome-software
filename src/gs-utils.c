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

void
gs_app_notify_installed (GsApp *app)
{
	gchar *summary;
	GNotification *n;

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
	g_object_unref (n);
	g_free (summary);
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

static GtkIconTheme *icon_theme_singleton;
static GMutex        icon_theme_lock;
static GHashTable   *icon_theme_paths;

static GtkIconTheme *
icon_theme_get (void)
{
	if (icon_theme_singleton == NULL)
		icon_theme_singleton = gtk_icon_theme_new ();

	return icon_theme_singleton;
}

static void
icon_theme_add_path (const gchar *path)
{
	if (path == NULL)
		return;

	if (icon_theme_paths == NULL)
		icon_theme_paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	if (!g_hash_table_contains (icon_theme_paths, path)) {
		gtk_icon_theme_prepend_search_path (icon_theme_get (), path);
		g_hash_table_add (icon_theme_paths, g_strdup (path));
	}
}

/**
 * gs_pixbuf_load:
 **/
GdkPixbuf *
gs_pixbuf_load (const gchar *icon_name,
		const gchar *icon_path,
		guint icon_size,
		GError **error)
{
	GdkPixbuf *pixbuf = NULL;

	g_return_val_if_fail (icon_name != NULL, NULL);
	g_return_val_if_fail (icon_size > 0, NULL);

	if (icon_name[0] == '\0') {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "Icon name not specified");
	} else if (icon_name[0] == '/') {
		pixbuf = gdk_pixbuf_new_from_file_at_size (icon_name,
							   icon_size,
							   icon_size,
							   error);
	} else {
		g_mutex_lock (&icon_theme_lock);
		icon_theme_add_path (icon_path);
		pixbuf = gtk_icon_theme_load_icon (icon_theme_get (),
						   icon_name,
						   icon_size,
						   GTK_ICON_LOOKUP_USE_BUILTIN |
						   GTK_ICON_LOOKUP_FORCE_SIZE,
						   error);
		g_mutex_unlock (&icon_theme_lock);
	}
	return pixbuf;
}

static void
reboot_done (GObject *source, GAsyncResult *res, gpointer data)
{
	GCallback reboot_failed = data;
	GVariant *ret;
	GError *error = NULL;

	ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
	if (ret) {
		g_variant_unref (ret);
		return;
	}

	if (error) {
		g_warning ("Calling org.gnome.SessionManager.Reboot failed: %s", error->message);
		g_error_free (error);
	}

	reboot_failed ();
}

void
gs_reboot (GCallback reboot_failed)
{
	GDBusConnection *bus;

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
	g_object_unref (bus);
}

/* vim: set noexpandtab: */
