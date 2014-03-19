/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include <string.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>
#include <gsettings-desktop-schemas/gdesktop-enums.h>

#include "gs-update-monitor.h"
#include "gs-utils.h"
#include "gs-offline-updates.h"

struct _GsUpdateMonitor {
	GObject		 parent;

	GApplication	*application;
	GCancellable    *cancellable;

	guint		 check_hourly_id;
	GDateTime	*check_timestamp;
	GDateTime	*install_timestamp;
	gboolean         refresh_cache_due;
	gboolean	 get_updates_due;
	gboolean	 network_available;
	gchar		**pending_downloads;
	PkTask		*task;
	PkControl	*control;
	GSettings	*settings;

	GFile 		*offline_update_file;
	GFileMonitor 	*offline_update_monitor;
	gboolean	 offline_update_notified;
	guint		 reenable_offline_update_id;
	guint		 check_offline_update_id;
};

struct _GsUpdateMonitorClass {
	GObjectClass	 parent_class;
};

G_DEFINE_TYPE (GsUpdateMonitor, gs_update_monitor, G_TYPE_OBJECT)

static void notify_offline_update_available (GsUpdateMonitor *monitor);

static gboolean
reenable_offline_update_notification (gpointer data)
{
	GsUpdateMonitor *monitor = data;

	monitor->offline_update_notified = FALSE;

	monitor->reenable_offline_update_id = 0;

	notify_offline_update_available (monitor);

	return G_SOURCE_REMOVE;
}

static void
notify_offline_update_available (GsUpdateMonitor *monitor)
{
	GNotification *n;
	const gchar *title;
	const gchar *body;

	if (!g_file_query_exists (monitor->offline_update_file, NULL))
		return;

	if (monitor->offline_update_notified)
		return;

	if (gs_application_has_active_window (GS_APPLICATION (monitor->application)))
		return;

	monitor->offline_update_notified = TRUE;

	/* Notify in another hour, if the user does nothing (closes the notification) */
	monitor->reenable_offline_update_id = g_timeout_add_seconds (3600, reenable_offline_update_notification, monitor);
	g_source_set_name_by_id (monitor->reenable_offline_update_id, "[gnome-software] reenable_offline_update_notification");

	title = _("Software Updates Available");
	body = _("Important OS and application updates are ready to be installed");
	n = g_notification_new (title);
	g_notification_set_body (n, body);
	g_notification_add_button_with_target (n, _("View"), "app.set-mode", "s", "updates");
	g_notification_add_button (n, _("Not Now"), "app.reschedule-updates");
	g_notification_set_default_action_and_target (n, "app.set-mode", "s", "updates");
	g_application_send_notification (monitor->application, "updates-available", n);
	g_object_unref (n);
}

static void
offline_update_monitor_cb (GFileMonitor      *file_monitor,
			   GFile             *file,
			   GFile             *other_file,
			   GFileMonitorEvent  event_type,
			   GsUpdateMonitor   *monitor)
{
	notify_offline_update_available (monitor);
}

static void
start_monitoring_offline_updates (GsUpdateMonitor *monitor)
{
	monitor->offline_update_file = g_file_new_for_path ("/var/lib/PackageKit/prepared-update");
	monitor->offline_update_monitor = g_file_monitor_file (monitor->offline_update_file, 0, NULL, NULL);

	g_signal_connect (monitor->offline_update_monitor, "changed",
			  G_CALLBACK (offline_update_monitor_cb), monitor);
        notify_offline_update_available (monitor);
}

static gboolean
check_offline_update_cb (gpointer user_data)
{
	GsUpdateMonitor *monitor = user_data;
	const gchar *message;
	const gchar *title;
	gboolean success;
	guint num_packages = 1;
	GNotification *notification;
	GIcon *icon;

	if (!gs_offline_updates_get_status (&success, &num_packages, NULL, NULL))
		goto out;

	if (success) {
		title = ngettext ("Software Update Installed",
				  "Software Updates Installed",
				  num_packages);
		/* TRANSLATORS: message when we've done offline updates */
		message = ngettext ("An important OS update has been installed.",
				    "Important OS updates have been installed.",
				    num_packages);
	} else {

		title = _("Software Updates Failed");
		/* TRANSLATORS: message when we offline updates have failed */
		message = _("An important OS update failed to be installed.");
	}

	notification = g_notification_new (title);
	g_notification_set_body (notification, message);
	icon = g_themed_icon_new ("gnome-software-symbolic");
	g_notification_set_icon (notification, icon);
	g_object_unref (icon);
	if (success)
		g_notification_add_button_with_target (notification, _("Review"), "app.set-mode", "s", "updated");
	else
		g_notification_add_button (notification, _("Show Details"), "app.show-offline-update-error");
	g_notification_add_button (notification, _("OK"), "app.clear-offline-updates");

	g_application_send_notification (monitor->application, "offline-updates", notification);
	g_object_unref (notification);

out:
	start_monitoring_offline_updates (monitor);

        monitor->check_offline_update_id = 0;

        return G_SOURCE_REMOVE;
}

static gboolean
has_important_updates (GPtrArray *packages)
{
	guint i;
	PkPackage *pkg;

	for (i = 0; i < packages->len; i++) {
		pkg = g_ptr_array_index (packages, i);
		if (pk_package_get_info (pkg) == PK_INFO_ENUM_SECURITY ||
		    pk_package_get_info (pkg) == PK_INFO_ENUM_IMPORTANT)
			return TRUE;	
	}

	return FALSE;
}

static gboolean
no_updates_for_a_week (GsUpdateMonitor *monitor)
{
	GDateTime *last_update;
	GDateTime *now;
	GTimeSpan d;
	gint64 tmp;

	g_settings_get (monitor->settings, "install-timestamp", "x", &tmp);
	if (tmp == 0)
		return TRUE;

	last_update = g_date_time_new_from_unix_local (tmp);
	if (last_update == NULL) {
		g_warning ("failed to set timestamp %" G_GINT64_FORMAT, tmp);
		return TRUE;
	}

	now = g_date_time_new_now_local ();
	d = g_date_time_difference (now, last_update);
	g_date_time_unref (last_update);
	g_date_time_unref (now);

	if (d >= 7 * G_TIME_SPAN_DAY)
		return TRUE;

	return FALSE;
}

static void
package_download_finished_cb (GObject *object,
			      GAsyncResult *res,
			      gpointer data)
{
	GsUpdateMonitor *monitor = data;
	PkResults *results;
	GError *error = NULL;
	PkError *error_code;

	results = pk_client_generic_finish (PK_CLIENT (object), res, &error);
	if (results == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_warning ("failed to download: %s", error->message);
		}
		g_error_free (error);
		return;
	}

        error_code = pk_results_get_error_code (results);
        if (error_code != NULL) {
                g_warning ("failed to download: %s, %s",
                           pk_error_enum_to_string (pk_error_get_code (error_code)),
                           pk_error_get_details (error_code));
		g_object_unref (error_code);
		g_object_unref (results);
		return;
	}

	g_debug ("Downloaded updates");

	g_clear_pointer (&monitor->pending_downloads, g_strfreev);
	g_object_unref (results);
}

static void
download_updates (GsUpdateMonitor *monitor)
{
	if (monitor->pending_downloads == NULL)
		return;

	if (!monitor->network_available)
		return;

	g_debug ("Downloading updates");

	pk_task_update_packages_async (monitor->task,
				       monitor->pending_downloads,
				       monitor->cancellable,
				       NULL, NULL,
				       package_download_finished_cb,
				       monitor);
}

static void
get_updates_finished_cb (GObject *object,
			 GAsyncResult *res,
			 gpointer data)
{
	GsUpdateMonitor *monitor = data;
	PkResults *results;
	PkError *error_code;
	GError *error = NULL;
	GPtrArray *packages;
	guint i;
	PkPackage *pkg;

	results = pk_client_generic_finish (PK_CLIENT (object), res, &error);
	if (results == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_warning ("failed to get updates: %s", error->message);
		}
		g_error_free (error);
		return;
	}

	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get updates: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		g_object_unref (error_code);
		g_object_unref (results);
		return;
	}

	/* we succeeded */
	monitor->get_updates_due = FALSE;

	packages = pk_results_get_package_array (results);

	g_debug ("Got %d updates", packages->len);

	if (has_important_updates (packages) ||
	    no_updates_for_a_week (monitor)) {

		monitor->pending_downloads = g_new0 (gchar *, packages->len + 1);
		for (i = 0; i < packages->len; i++) {
			pkg = (PkPackage *)g_ptr_array_index (packages, i);
			monitor->pending_downloads[i] = g_strdup (pk_package_get_id (pkg));
		}
		monitor->pending_downloads[packages->len] = NULL;

		download_updates (monitor);
	}

	g_ptr_array_unref (packages);
	g_object_unref (results);
}

static void
get_updates (GsUpdateMonitor *monitor)
{
	if (monitor->refresh_cache_due)
		return;

	if (!monitor->get_updates_due)
		return;

	g_debug ("Getting updates");

	pk_client_get_updates_async (PK_CLIENT (monitor->task),
				     pk_bitfield_value (PK_FILTER_ENUM_NONE),
				     monitor->cancellable,
				     NULL, NULL,
				     get_updates_finished_cb,
				     monitor);
}

static void
refresh_cache_finished_cb (GObject *object,
			   GAsyncResult *res,
			   gpointer data)
{
	GsUpdateMonitor *monitor = data;
	PkResults *results;
	PkError *error_code;
	GError *error = NULL;

	results = pk_client_generic_finish (PK_CLIENT (object), res, &error);
	if (results == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_warning ("failed to refresh the cache: %s", error->message);
		}
		g_error_free (error);
		return;
	}

	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to refresh the cache: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		g_object_unref (error_code);
		g_object_unref (results);
		return;
	}

	monitor->refresh_cache_due = FALSE;

	g_object_unref (results);

	get_updates (monitor);
}

static void
refresh_cache (GsUpdateMonitor *monitor)
{
	if (!monitor->refresh_cache_due)
		return;

	if (!monitor->network_available)
		return;

	g_debug ("Refreshing cache");

	pk_client_refresh_cache_async (PK_CLIENT (monitor->task),
				       TRUE,
				       monitor->cancellable,
				       NULL, NULL,
				       refresh_cache_finished_cb,
				       monitor);
}

static gboolean
check_hourly_cb (gpointer data)
{
	GsUpdateMonitor *monitor = data;

	g_debug ("Hourly updates check");

	/* no need to check again */	
	if (monitor->refresh_cache_due)
		return G_SOURCE_CONTINUE;

	if (monitor->check_timestamp != NULL) {
		GDateTime *now;
		gint now_year, now_month, now_day, now_hour;
		gint year, month, day;

		now = g_date_time_new_now_local ();

		g_date_time_get_ymd (now, &now_year, &now_month, &now_day);
		now_hour = g_date_time_get_hour (now);
		g_date_time_unref (now);

		g_date_time_get_ymd (monitor->check_timestamp, &year, &month, &day);

		/* check that it is the next day */
		if (!((now_year > year) ||
		      (now_year == year && now_month > month) ||
		      (now_year == year && now_month == month && now_day > day)))
			return G_SOURCE_CONTINUE;

		/* ...and past 6am */
		if (!(now_hour >= 6))
			return G_SOURCE_CONTINUE;

		g_clear_pointer (&monitor->check_timestamp, g_date_time_unref);
	}

	g_debug ("Daily update check due");

	monitor->check_timestamp = g_date_time_new_now_local ();
	g_settings_set (monitor->settings, "check-timestamp", "x",
			g_date_time_to_unix (monitor->check_timestamp));

	monitor->refresh_cache_due = TRUE;
	monitor->get_updates_due = TRUE;

	refresh_cache (monitor);

	return G_SOURCE_CONTINUE;
}

static void
notify_network_state_cb (PkControl *control,
			 GParamSpec *pspec,
			 GsUpdateMonitor *monitor)
{
	PkNetworkEnum network_state;
	gboolean available;

	g_object_get (control, "network-state", &network_state, NULL);
	
	if (network_state == PK_NETWORK_ENUM_OFFLINE ||
	    network_state == PK_NETWORK_ENUM_MOBILE)
		available = FALSE;
	else
		available = TRUE;

	if (monitor->network_available != available) {
		monitor->network_available = available;

		refresh_cache (monitor);
		get_updates (monitor);
		download_updates (monitor);
	}
}

static void
gs_update_monitor_init (GsUpdateMonitor *monitor)
{
	gint64 tmp;

	monitor->check_offline_update_id = 
		g_timeout_add_seconds (15, check_offline_update_cb, monitor);
	g_source_set_name_by_id (monitor->check_offline_update_id,
				 "[gnome-software] check_offline_update_cb");

	monitor->settings = g_settings_new ("org.gnome.software");
	g_settings_get (monitor->settings, "check-timestamp", "x", &tmp);
	monitor->check_timestamp = g_date_time_new_from_unix_local (tmp);

	monitor->check_hourly_id =
		g_timeout_add_seconds (3600, check_hourly_cb, monitor);
	g_source_set_name_by_id (monitor->check_hourly_id,
				 "[gnome-software] check_hourly_cb");

	monitor->cancellable = g_cancellable_new ();
	monitor->task = pk_task_new ();
	g_object_set (monitor->task,
		      "background", TRUE,
		      "interactive", FALSE,
		      "only-download", TRUE,
		      NULL);

        monitor->network_available = FALSE;
	monitor->control = pk_control_new ();
	g_signal_connect (monitor->control, "notify::network-state",
			  G_CALLBACK (notify_network_state_cb), monitor);
}

static void
gs_update_monitor_finalize (GObject *object)
{
	GsUpdateMonitor *monitor = GS_UPDATE_MONITOR (object);

	if (monitor->cancellable) {
		g_cancellable_cancel (monitor->cancellable);
		g_clear_object (&monitor->cancellable);
	}
	if (monitor->check_hourly_id != 0) {
		g_source_remove (monitor->check_hourly_id);
		monitor->check_hourly_id = 0;
	}
	if (monitor->check_offline_update_id != 0) {
		g_source_remove (monitor->check_offline_update_id);
		monitor->check_offline_update_id = 0;
	}
	if (monitor->reenable_offline_update_id != 0) {
		g_source_remove (monitor->reenable_offline_update_id);
		monitor->reenable_offline_update_id = 0;
	}
	g_clear_pointer (&monitor->pending_downloads, g_strfreev);
	g_clear_pointer (&monitor->check_timestamp, g_date_time_unref);
	g_clear_object (&monitor->task);
	g_clear_object (&monitor->control);
	g_clear_object (&monitor->offline_update_file);
	g_clear_object (&monitor->offline_update_monitor);
	g_clear_object (&monitor->settings);
	g_application_release (monitor->application);

	G_OBJECT_CLASS (gs_update_monitor_parent_class)->finalize (object);
}

static void
gs_update_monitor_class_init (GsUpdateMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_update_monitor_finalize;
}

static void
remove_stale_notifications (GsUpdateMonitor *monitor)
{
	if (gs_offline_updates_results_available ()) {
		g_debug ("Withdrawing stale notifications");

		g_application_withdraw_notification (monitor->application,
		                                     "updates-available");
		g_application_withdraw_notification (monitor->application,
		                                     "offline-updates");
	}
}

static void
do_reschedule_updates (GSimpleAction   *action,
		       GVariant        *parameter,
		       GsUpdateMonitor *monitor)
{
	if (monitor->reenable_offline_update_id)
		g_source_remove (monitor->reenable_offline_update_id);

	/* The user explicitly clicked "Not now". Don't bother him
	   for another 6 hours */

	monitor->offline_update_notified = TRUE;
	monitor->reenable_offline_update_id = g_timeout_add_seconds (6 * 3600, reenable_offline_update_notification, monitor);
	g_source_set_name_by_id (monitor->reenable_offline_update_id, "[gnome-software] reenable_offline_update_notification (longer)");
}

GsUpdateMonitor *
gs_update_monitor_new (GsApplication *application)
{
	GsUpdateMonitor *monitor;
	GSimpleAction *reschedule_updates;

	monitor = GS_UPDATE_MONITOR (g_object_new (GS_TYPE_UPDATE_MONITOR, NULL));
	monitor->application = G_APPLICATION (application);
	g_application_hold (monitor->application);

	remove_stale_notifications (monitor);

	reschedule_updates = g_simple_action_new ("reschedule-updates", NULL);
	g_signal_connect_object (reschedule_updates, "activate",
				 G_CALLBACK (do_reschedule_updates), monitor, 0);
	g_action_map_add_action (G_ACTION_MAP (application), G_ACTION (reschedule_updates));

	return monitor;
}

/* vim: set noexpandtab: */
