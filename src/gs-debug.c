/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <stdio.h>
#include <unistd.h>

#include "gs-debug.h"

/**
 * gs_debug_handler_cb:
 **/
static void
gs_debug_handler_cb (const gchar *log_domain,
		     GLogLevelFlags log_level,
		     const gchar *message,
		     gpointer user_data)
{
	GsDebug *debug = (GsDebug *) user_data;
	guint i;
	g_autofree gchar *tmp = NULL;
	g_autoptr(GString) domain = NULL;
	g_autoptr(GMutexLocker) locker = NULL;

	/* enabled */
	if (g_getenv ("GS_DEBUG") == NULL && log_level == G_LOG_LEVEL_DEBUG)
		return;

	/* make threadsafe */
	locker = g_mutex_locker_new (&debug->mutex);

	/* time header */
	if (debug->use_time) {
		g_autoptr(GDateTime) dt = g_date_time_new_now_utc ();
		tmp = g_strdup_printf ("%02i:%02i:%02i:%04i",
				       g_date_time_get_hour (dt),
				       g_date_time_get_minute (dt),
				       g_date_time_get_second (dt),
				       g_date_time_get_microsecond (dt) / 1000);
	}

	/* make these shorter */
	if (g_strcmp0 (log_domain, "PackageKit") == 0) {
		log_domain = "PK";
	} else if (g_strcmp0 (log_domain, "GsPlugin") == 0) {
		log_domain = "Gs";
	}

	/* pad out domain */
	domain = g_string_new (log_domain);
	for (i = domain->len; i < 3; i++)
		g_string_append (domain, " ");

	/* to file */
	if (!debug->use_color) {
		if (tmp != NULL)
			g_print ("%s ", tmp);
		g_print ("%s ", domain->str);
		g_print ("%s\n", message);
		return;
	}

	/* to screen */
	switch (log_level) {
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_LEVEL_WARNING:
		/* critical in red */
		if (tmp != NULL)
			g_print ("%c[%dm%s ", 0x1B, 32, tmp);
		g_print ("%s ", domain->str);
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 31, message, 0x1B, 0);
		break;
	default:
		/* debug in blue */
		if (tmp != NULL)
			g_print ("%c[%dm%s ", 0x1B, 32, tmp);
		g_print ("%s ", domain->str);
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, 34, message, 0x1B, 0);
		break;
	}
}

/**
 * gs_debug_new:
 **/
GsDebug *
gs_debug_new (void)
{
	GsDebug *debug = g_new0 (GsDebug, 1);
	guint i;
	const gchar *log_domains[] = {
		G_LOG_DOMAIN,
		"As",
		"Gtk",
		"GsPlugin",
		"PackageKit",
		NULL };

	debug->use_time = g_getenv ("GS_DEBUG_NO_TIME") == NULL;
	debug->use_color = (isatty (fileno (stdout)) == 1);
	g_mutex_init (&debug->mutex);
	for (i = 0; log_domains[i] != NULL; i++) {
		g_log_set_handler (log_domains[i],
				   G_LOG_LEVEL_ERROR |
				   G_LOG_LEVEL_CRITICAL |
				   G_LOG_LEVEL_DEBUG |
				   G_LOG_LEVEL_WARNING,
				   gs_debug_handler_cb, debug);
	}
	return debug;
}

/**
 * gs_debug_free:
 **/
void
gs_debug_free (GsDebug *debug)
{
	g_mutex_clear (&debug->mutex);
	g_free (debug);
}
