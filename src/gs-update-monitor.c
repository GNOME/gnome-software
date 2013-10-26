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

#include "gs-update-monitor.h"
#include "gs-utils.h"
#include "gs-offline-updates.h"

#define GS_UPDATES_CHECK_OFFLINE_TIMEOUT    30 /* seconds */
#define GS_REENABLE_OFFLINE_UPDATE_TIMEOUT 300 /* seconds */

#define GS_UPDATES_ICON_NORMAL "software-update-available-symbolic"
#define GS_UPDATES_ICON_URGENT "software-update-urgent-symbolic"

struct _GsUpdateMonitor {
	GObject		 parent;

	GsApplication	*application;
	GFile 		*offline_update_file;
	GFileMonitor 	*offline_update_monitor;
	gboolean	 offline_update_notified;

	guint		 check_offline_update_id;
};

struct _GsUpdateMonitorClass {
	GObjectClass	 parent_class;
};

G_DEFINE_TYPE (GsUpdateMonitor, gs_update_monitor, G_TYPE_OBJECT)

static gboolean
reenable_offline_update (gpointer data)
{
	GsUpdateMonitor *monitor = data;

	monitor->offline_update_notified = FALSE;

	return G_SOURCE_REMOVE;
}

static void
notify_offline_update_available (GsUpdateMonitor *monitor)
{
	guint id;
	GNotification *n;
	const gchar *title;
	const gchar *body;

	if (!g_file_query_exists (monitor->offline_update_file, NULL))
		return;

	if (monitor->offline_update_notified)
		return;

	monitor->offline_update_notified = TRUE;

	/* don't notify more often than every 5 minutes */
	id = g_timeout_add_seconds (GS_REENABLE_OFFLINE_UPDATE_TIMEOUT, reenable_offline_update, monitor);
	g_source_set_name_by_id (id, "[gnome-software] reenable_offline_update");

	title = _("Software Updates Available");
	body = _("Important OS and application updates are ready to be installed");
	n = g_notification_new (title);
	g_notification_set_body (n, body);
	g_notification_add_button_with_target (n, _("View"), "app.set-mode", "s", "updates");
	g_notification_add_button (n, _("Not Now"), "app.nop");
	g_notification_set_default_action_and_target (n, "app.set-mode", "s", "updates");
	g_application_send_notification (g_application_get_default (), "updates-available", n);
	g_object_unref (n);
}

static void
offline_update_cb (GFileMonitor      *file_monitor,
		   GFile             *file,
		   GFile             *other_file,
		   GFileMonitorEvent  event_type,
		   GsUpdateMonitor   *monitor)
{
	notify_offline_update_available (monitor);
}

static gboolean
initial_offline_update_check (gpointer data)
{
	GsUpdateMonitor *monitor = data;

        notify_offline_update_available (monitor);

        return G_SOURCE_REMOVE;
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

                gs_offline_updates_clear_status ();

        } else {

		title = _("Software Updates Failed");
		/* TRANSLATORS: message when we offline updates have failed */
		message = _("An important OS update failed to be installed.");
	}

	notification = g_notification_new (title);
	g_notification_set_body (notification, message);
	icon = g_themed_icon_new (GS_UPDATES_ICON_URGENT);
	g_notification_set_icon (notification, icon);
	g_object_unref (icon);
	if (success)
		g_notification_add_button_with_target (notification, _("Review"), "app.set-mode", "s", "updated");
	else
		g_notification_add_button (notification, _("Show Details"), "app.show-offline-update-error");
	g_notification_add_button (notification, _("OK"), "app.clear-offline-updates");

	g_application_send_notification (g_application_get_default (), "offline-updates", notification);
	g_object_unref (notification);

out:
        monitor->check_offline_update_id = 0;

        return G_SOURCE_REMOVE;
}

static void
gs_update_monitor_init (GsUpdateMonitor *monitor)
{
	guint id;

	monitor->offline_update_file = g_file_new_for_path ("/var/lib/PackageKit/prepared-update");
	monitor->offline_update_monitor = g_file_monitor_file (monitor->offline_update_file, 0, NULL, NULL);

	g_signal_connect (monitor->offline_update_monitor, "changed",
			  G_CALLBACK (offline_update_cb), monitor);

	id = g_timeout_add_seconds (GS_REENABLE_OFFLINE_UPDATE_TIMEOUT,
				    initial_offline_update_check,
				    monitor);
	g_source_set_name_by_id (id, "[gnome-software] initial_offline_update_check");

	monitor->check_offline_update_id = 
		g_timeout_add_seconds (GS_UPDATES_CHECK_OFFLINE_TIMEOUT,
                                       check_offline_update_cb,
                                       monitor);
	g_source_set_name_by_id (monitor->check_offline_update_id,
				 "[gnpome-software] check_offline_update_cb");
}

static void
gs_update_monitor_finalize (GObject *object)
{
	GsUpdateMonitor *monitor = GS_UPDATE_MONITOR (object);

	if (monitor->check_offline_update_id != 0) {
		g_source_remove (monitor->check_offline_update_id);
		monitor->check_offline_update_id = 0;
	}
	g_clear_object (&monitor->offline_update_file);
	g_clear_object (&monitor->offline_update_monitor);
	g_application_release (G_APPLICATION (monitor->application));

	G_OBJECT_CLASS (gs_update_monitor_parent_class)->finalize (object);
}

static void
gs_update_monitor_class_init (GsUpdateMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_update_monitor_finalize;
}

GsUpdateMonitor *
gs_update_monitor_new (GsApplication *application)
{
	GsUpdateMonitor *monitor;

	monitor = GS_UPDATE_MONITOR (g_object_new (GS_TYPE_UPDATE_MONITOR, NULL));
	monitor->application = application;
	g_application_hold (G_APPLICATION (application));

	return monitor;
}

/* vim: set noexpandtab: */
