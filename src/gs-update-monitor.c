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

#include "gs-cleanup.h"
#include "gs-update-monitor.h"
#include "gs-utils.h"
#include "gs-offline-updates.h"

struct _GsUpdateMonitor {
	GObject		 parent;

	GApplication	*application;
	GCancellable    *cancellable;

	guint		 check_hourly_id;
	guint		 start_hourly_checks_id;
	GDateTime	*check_timestamp;
	GDateTime	*install_timestamp;
	gboolean	 refresh_cache_due;
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

static gboolean
reenable_offline_update_notification (gpointer data)
{
	GsUpdateMonitor *monitor = data;

	monitor->offline_update_notified = FALSE;

	monitor->reenable_offline_update_id = 0;

	return G_SOURCE_REMOVE;
}

static void
notify_offline_update_available (GsUpdateMonitor *monitor)
{
	const gchar *title;
	const gchar *body;
	guint64 elapsed_security = 0;
	guint64 security_timestamp = 0;
	_cleanup_object_unref_ GNotification *n = NULL;

	if (!g_file_query_exists (monitor->offline_update_file, NULL))
		return;

	if (monitor->offline_update_notified)
		return;

	if (gs_application_has_active_window (GS_APPLICATION (monitor->application)))
		return;

	monitor->offline_update_notified = TRUE;

	/* rate limit update notifications to once per hour */
	monitor->reenable_offline_update_id = g_timeout_add_seconds (3600, reenable_offline_update_notification, monitor);

	/* get time in days since we saw the first unapplied security update */
	g_settings_get (monitor->settings,
			"security-timestamp", "x", &security_timestamp);
	if (security_timestamp > 0) {
		elapsed_security = g_get_monotonic_time () - security_timestamp;
		elapsed_security /= G_USEC_PER_SEC;
		elapsed_security /= 60 * 60 * 24;
	}

	/* only show the scary warning after the user has ignored
	 * security updates for a full day */
	if (elapsed_security > 1) {
		title = _("Security Updates Pending");
		body = _("It is recommended that you install important updates now");
		n = g_notification_new (title);
		g_notification_set_body (n, body);
		g_notification_add_button (n, _("Restart & Install"), "app.reboot-and-install");
		g_notification_set_default_action_and_target (n, "app.set-mode", "s", "updates");
		g_application_send_notification (monitor->application, "updates-available", n);
	} else {
		title = _("Software Updates Available");
		body = _("Important OS and application updates are ready to be installed");
		n = g_notification_new (title);
		g_notification_set_body (n, body);
		g_notification_add_button (n, _("Not Now"), "app.nop");
		g_notification_add_button_with_target (n, _("View"), "app.set-mode", "s", "updates");
		g_notification_set_default_action_and_target (n, "app.set-mode", "s", "updates");
		g_application_send_notification (monitor->application, "updates-available", n);
	}
}

static void
offline_update_monitor_cb (GFileMonitor      *file_monitor,
			   GFile	     *file,
			   GFile	     *other_file,
			   GFileMonitorEvent  event_type,
			   GsUpdateMonitor   *monitor)
{
	if (!g_file_query_exists (monitor->offline_update_file, NULL)) {
		g_debug ("prepared update removed; withdrawing updates-available notification");
		g_application_withdraw_notification (monitor->application,
						     "updates-available");
		return;
	}

	notify_offline_update_available (monitor);
}

static void
start_monitoring_offline_updates (GsUpdateMonitor *monitor)
{
	monitor->offline_update_monitor = g_file_monitor_file (monitor->offline_update_file, 0, NULL, NULL);

	g_signal_connect (monitor->offline_update_monitor, "changed",
			  G_CALLBACK (offline_update_monitor_cb), monitor);
}

static void
show_installed_updates_notification (GsUpdateMonitor *monitor)
{
	const gchar *message;
	const gchar *title;
	_cleanup_object_unref_ GNotification *notification = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	results = pk_offline_get_results (NULL);
	if (results == NULL)
		return;
	if (pk_results_get_exit_code (results) == PK_EXIT_ENUM_SUCCESS) {
		GPtrArray *packages;
		packages = pk_results_get_package_array (results);
		title = ngettext ("Software Update Installed",
				  "Software Updates Installed",
				  packages->len);
		/* TRANSLATORS: message when we've done offline updates */
		message = ngettext ("An important OS update has been installed.",
				    "Important OS updates have been installed.",
				    packages->len);
		g_ptr_array_unref (packages);
	} else {

		title = _("Software Updates Failed");
		/* TRANSLATORS: message when we offline updates have failed */
		message = _("An important OS update failed to be installed.");
	}

	notification = g_notification_new (title);
	g_notification_set_body (notification, message);
	if (pk_results_get_exit_code (results) == PK_EXIT_ENUM_SUCCESS) {
		g_notification_add_button_with_target (notification, _("Review"), "app.set-mode", "s", "updated");
		g_notification_set_default_action_and_target (notification, "app.set-mode", "s", "updated");
	} else {
		g_notification_add_button (notification, _("Show Details"), "app.show-offline-update-error");
		g_notification_set_default_action (notification, "app.show-offline-update-error");
	}

	g_application_send_notification (monitor->application, "offline-updates", notification);
}

static gboolean
check_offline_update_cb (gpointer user_data)
{
	GsUpdateMonitor *monitor = user_data;
	guint64 time_last_notified;
	guint64 time_update_completed;

	g_settings_get (monitor->settings,
			"install-timestamp", "x", &time_last_notified);

	time_update_completed = pk_offline_get_results_mtime (NULL);
	if (time_update_completed > 0) {
		if (time_last_notified < time_update_completed)
			show_installed_updates_notification (monitor);

		g_settings_set (monitor->settings,
				"install-timestamp", "x", time_update_completed);
	}

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
	GTimeSpan d;
	gint64 tmp;
	_cleanup_date_time_unref_ GDateTime *last_update = NULL;
	_cleanup_date_time_unref_ GDateTime *now = NULL;

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
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkError *error_code = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	results = pk_client_generic_finish (PK_CLIENT (object), res, &error);
	if (results == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to download: %s", error->message);
		return;
	}

	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to download: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		return;
	}

	g_debug ("Downloaded updates");
	g_clear_pointer (&monitor->pending_downloads, g_strfreev);
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
	guint64 security_timestamp = 0;
	guint64 security_timestamp_old = 0;
	guint i;
	PkPackage *pkg;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkError *error_code = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *packages = NULL;

	results = pk_client_generic_finish (PK_CLIENT (object), res, &error);
	if (results == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to get updates: %s", error->message);
		return;
	}

	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get updates: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		return;
	}

	/* we succeeded */
	monitor->get_updates_due = FALSE;

	/* find security updates, or clear timestamp if there are now none */
	packages = pk_results_get_package_array (results);
	g_settings_get (monitor->settings,
			"security-timestamp", "x", &security_timestamp_old);
	for (i = 0; i < packages->len; i++) {
		pkg = (PkPackage *)g_ptr_array_index (packages, i);
		if (pk_package_get_info (pkg) == PK_INFO_ENUM_SECURITY) {
			security_timestamp = g_get_monotonic_time ();
			break;
		}
	}
	if (security_timestamp_old != security_timestamp) {
		g_settings_set (monitor->settings,
				"security-timestamp", "x", security_timestamp);
	}

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
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkError *error_code = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	results = pk_client_generic_finish (PK_CLIENT (object), res, &error);
	if (results == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to refresh the cache: %s", error->message);
		return;
	}

	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to refresh the cache: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		return;
	}

	monitor->refresh_cache_due = FALSE;

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

static void
check_updates (GsUpdateMonitor *monitor)
{
	/* no need to check again */	
	if (monitor->refresh_cache_due)
		return;

	if (monitor->check_timestamp != NULL) {
		gint now_year, now_month, now_day, now_hour;
		gint year, month, day;
		_cleanup_date_time_unref_ GDateTime *now = NULL;

		now = g_date_time_new_now_local ();

		g_date_time_get_ymd (now, &now_year, &now_month, &now_day);
		now_hour = g_date_time_get_hour (now);

		g_date_time_get_ymd (monitor->check_timestamp, &year, &month, &day);

		/* check that it is the next day */
		if (!((now_year > year) ||
		      (now_year == year && now_month > month) ||
		      (now_year == year && now_month == month && now_day > day)))
			return;

		/* ...and past 6am */
		if (!(now_hour >= 6))
			return;

		g_clear_pointer (&monitor->check_timestamp, g_date_time_unref);
	}

	g_debug ("Daily update check due");

	monitor->check_timestamp = g_date_time_new_now_local ();
	g_settings_set (monitor->settings, "check-timestamp", "x",
			g_date_time_to_unix (monitor->check_timestamp));

	monitor->refresh_cache_due = TRUE;
	monitor->get_updates_due = TRUE;

	refresh_cache (monitor);
}

static gboolean
check_hourly_cb (gpointer data)
{
	GsUpdateMonitor *monitor = data;

	g_debug ("Hourly updates check");
	check_updates (monitor);

	return G_SOURCE_CONTINUE;
}

static gboolean
start_hourly_checks_cb (gpointer data)
{
	GsUpdateMonitor *monitor = data;

	g_debug ("First hourly updates check");
	check_updates (monitor);

	monitor->check_hourly_id =
		g_timeout_add_seconds (3600, check_hourly_cb, monitor);

	monitor->start_hourly_checks_id = 0;
	return G_SOURCE_REMOVE;
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

		/* resume any pending operations */
		refresh_cache (monitor);
		get_updates (monitor);
		download_updates (monitor);
	}
}

static void
gs_update_monitor_init (GsUpdateMonitor *monitor)
{
	gint64 tmp;

	monitor->offline_update_file = g_file_new_for_path ("/var/lib/PackageKit/prepared-update");
	monitor->check_offline_update_id = 
		g_timeout_add_seconds (5, check_offline_update_cb, monitor);

	monitor->settings = g_settings_new ("org.gnome.software");
	g_settings_get (monitor->settings, "check-timestamp", "x", &tmp);
	monitor->check_timestamp = g_date_time_new_from_unix_local (tmp);

	monitor->start_hourly_checks_id =
		g_timeout_add_seconds (60, start_hourly_checks_cb, monitor);

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
gs_update_monitor_dispose (GObject *object)
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
	if (monitor->start_hourly_checks_id != 0) {
		g_source_remove (monitor->start_hourly_checks_id);
		monitor->start_hourly_checks_id = 0;
	}
	if (monitor->check_offline_update_id != 0) {
		g_source_remove (monitor->check_offline_update_id);
		monitor->check_offline_update_id = 0;
	}
	if (monitor->reenable_offline_update_id != 0) {
		g_source_remove (monitor->reenable_offline_update_id);
		monitor->reenable_offline_update_id = 0;
	}
	if (monitor->control != NULL) {
		g_signal_handlers_disconnect_by_func (monitor->control, notify_network_state_cb, monitor);
		g_clear_object (&monitor->control);
	}
	if (monitor->offline_update_monitor != NULL) {
		g_signal_handlers_disconnect_by_func (monitor->offline_update_monitor, offline_update_monitor_cb, monitor);
		g_clear_object (&monitor->offline_update_monitor);
	}
	g_clear_pointer (&monitor->pending_downloads, g_strfreev);
	g_clear_pointer (&monitor->check_timestamp, g_date_time_unref);
	g_clear_object (&monitor->task);
	g_clear_object (&monitor->offline_update_file);
	g_clear_object (&monitor->settings);

	G_OBJECT_CLASS (gs_update_monitor_parent_class)->dispose (object);
}

static void
gs_update_monitor_finalize (GObject *object)
{
	GsUpdateMonitor *monitor = GS_UPDATE_MONITOR (object);

	g_application_release (monitor->application);

	G_OBJECT_CLASS (gs_update_monitor_parent_class)->finalize (object);
}

static void
gs_update_monitor_class_init (GsUpdateMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_update_monitor_dispose;
	object_class->finalize = gs_update_monitor_finalize;
}

static void
remove_stale_notifications (GsUpdateMonitor *monitor)
{
	if (!g_file_query_exists (monitor->offline_update_file, NULL)) {
		g_debug ("Withdrawing stale updates-available notification");
		g_application_withdraw_notification (monitor->application,
						     "updates-available");
	}

	if (pk_offline_get_results_mtime (NULL) == 0) {
		g_debug ("Withdrawing stale offline-updates notification");
		g_application_withdraw_notification (monitor->application,
						     "offline-updates");
	}
}

GsUpdateMonitor *
gs_update_monitor_new (GsApplication *application)
{
	GsUpdateMonitor *monitor;

	monitor = GS_UPDATE_MONITOR (g_object_new (GS_TYPE_UPDATE_MONITOR, NULL));
	monitor->application = G_APPLICATION (application);
	g_application_hold (monitor->application);

	remove_stale_notifications (monitor);

	return monitor;
}

/* vim: set noexpandtab: */
