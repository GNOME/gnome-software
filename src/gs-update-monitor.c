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
	GNotification *n;
	const gchar *title;
	const gchar *body;

	if (!g_file_query_exists (monitor->offline_update_file, NULL))
		return;

	if (monitor->offline_update_notified)
		return;

	monitor->offline_update_notified = TRUE;

	/* don't notify more often than every 5 minutes */
	g_timeout_add_seconds (300, reenable_offline_update, monitor);

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
