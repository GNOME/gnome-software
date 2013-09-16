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
#include <libnotify/notify.h>

#include "gs-update-monitor.h"

#define GSD_UPDATES_ICON_NORMAL "software-update-available-symbolic"

struct _GsUpdateMonitor {
	GObject		 parent;

	GsApplication	*application;
	GFile 		*offline_update_file;
	GFileMonitor 	*offline_update_monitor;
	gboolean	 offline_update_notified;
};

struct _GsUpdateMonitorClass {
	GObjectClass	 parent_class;
};

G_DEFINE_TYPE (GsUpdateMonitor, gs_update_monitor, G_TYPE_OBJECT)

static void
offline_update_action_cb (NotifyNotification *notification,
			  gchar              *action,
			  gpointer            user_data)
{
	GsUpdateMonitor *monitor = user_data;

	notify_notification_close (notification, NULL);

	if (g_strcmp0 (action, "view") == 0) {
		g_action_group_activate_action (G_ACTION_GROUP (monitor->application),
						"set-mode",
						g_variant_new_string ("updates"));
	}
}

static gboolean
reenable_offline_update (gpointer data)
{
	GsUpdateMonitor *monitor = data;

	monitor->offline_update_notified = FALSE;

	return G_SOURCE_REMOVE;
}

static void
on_notification_closed (NotifyNotification *notification, gpointer data)
{
	g_object_unref (notification);
}

static void
notify_offline_update_available (GsUpdateMonitor *monitor)
{
	NotifyNotification *notification;
	const gchar *title;
	const gchar *body;
	gboolean ret;
	GError *error = NULL;

	if (!g_file_query_exists (monitor->offline_update_file, NULL))
		return;

	if (monitor->offline_update_notified)
		return;

	monitor->offline_update_notified = TRUE;

	/* don't notify more often than every 5 minutes */
	g_timeout_add_seconds (300, reenable_offline_update, monitor);

	title = _("Software Updates available");
	body = _("Important OS and application updates are ready to be installed");
	notification = notify_notification_new (title, body,
						GSD_UPDATES_ICON_NORMAL);
	notify_notification_set_hint_string (notification, "desktop-entry", "gnome-software");
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_NORMAL);
	notify_notification_add_action (notification, "ignore",
					/* TRANSLATORS: don't install updates now */
					_("Not Now"),
					offline_update_action_cb,
					monitor, NULL);
	notify_notification_add_action (notification, "view",
               				/* TRANSLATORS: view available updates */
					_("View"),
					offline_update_action_cb,
					monitor, NULL);
	g_signal_connect (notification, "closed",
			  G_CALLBACK (on_notification_closed), NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		g_warning ("error: %s", error->message);
		g_error_free (error);
	}
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

static void
gs_update_monitor_init (GsUpdateMonitor *monitor)
{
	monitor->offline_update_file = g_file_new_for_path ("/var/lib/PackageKit/prepared-update");
	monitor->offline_update_monitor = g_file_monitor_file (monitor->offline_update_file, 0, NULL, NULL);

	g_signal_connect (monitor->offline_update_monitor, "changed",
			  G_CALLBACK (offline_update_cb), monitor);

	g_timeout_add_seconds (300,
			       initial_offline_update_check,
			       monitor);
}

static void
gs_update_monitor_finalize (GObject *object)
{
	GsUpdateMonitor *monitor = GS_UPDATE_MONITOR (object);

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
