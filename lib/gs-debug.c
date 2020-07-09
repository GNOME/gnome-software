/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <stdio.h>
#include <unistd.h>

#include "gs-debug.h"

struct _GsDebug
{
	GObject		 parent_instance;
	GMutex		 mutex;
	gboolean	 use_time;
};

G_DEFINE_TYPE (GsDebug, gs_debug, G_TYPE_OBJECT)

static GLogWriterOutput
gs_log_writer_console (GLogLevelFlags log_level,
		       const GLogField *fields,
		       gsize n_fields,
		       gpointer user_data)
{
	GsDebug *debug = GS_DEBUG (user_data);
	const gchar *log_domain = NULL;
	const gchar *log_message = NULL;
	g_autofree gchar *tmp = NULL;
	g_autoptr(GMutexLocker) locker = NULL;
	g_autoptr(GString) domain = NULL;

	/* enabled */
	if (g_getenv ("GS_DEBUG") == NULL &&
	    log_level == G_LOG_LEVEL_DEBUG)
		return G_LOG_WRITER_HANDLED;

	/* get data from arguments */
	for (gsize i = 0; i < n_fields; i++) {
		if (g_strcmp0 (fields[i].key, "MESSAGE") == 0) {
			log_message = fields[i].value;
			continue;
		}
		if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0) {
			log_domain = fields[i].value;
			continue;
		}
	}

	/* this is really verbose */
	if (g_strcmp0 (log_domain, "dconf") == 0 &&
	    log_level == G_LOG_LEVEL_DEBUG)
		return G_LOG_WRITER_HANDLED;

	/* make threadsafe */
	locker = g_mutex_locker_new (&debug->mutex);
	g_assert (locker != NULL);

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
	for (guint i = domain->len; i < 3; i++)
		g_string_append (domain, " ");

	switch (log_level) {
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_LEVEL_WARNING:
		/* to screen */
		if (isatty (fileno (stderr)) == 1) {
			/* critical in red */
			if (tmp != NULL)
				g_printerr ("%c[%dm%s ", 0x1B, 32, tmp);
			g_printerr ("%s ", domain->str);
			g_printerr ("%c[%dm%s\n%c[%dm", 0x1B, 31, log_message, 0x1B, 0);
		} else { /* to file */
			if (tmp != NULL)
				g_print ("%s ", tmp);
			g_print ("%s ", domain->str);
			g_print ("%s\n", log_message);
		}
		break;
	default:
		/* to screen */
		if (isatty (fileno (stdout)) == 1) {
			/* debug in blue */
			if (tmp != NULL)
				g_print ("%c[%dm%s ", 0x1B, 32, tmp);
			g_print ("%s ", domain->str);
			g_print ("%c[%dm%s\n%c[%dm", 0x1B, 34, log_message, 0x1B, 0);
			break;
		} else { /* to file */
			if (tmp != NULL)
				g_print ("%s ", tmp);
			g_print ("%s ", domain->str);
			g_print ("%s\n", log_message);
		}
	}

	/* success */
	return G_LOG_WRITER_HANDLED;
}

static GLogWriterOutput
gs_log_writer_journald (GLogLevelFlags log_level,
                        const GLogField *fields,
                        gsize n_fields,
                        gpointer user_data)
{
	/* important enough to force to the journal */
	switch (log_level) {
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_CRITICAL:
	case G_LOG_LEVEL_WARNING:
	case G_LOG_LEVEL_INFO:
		return g_log_writer_journald (log_level, fields, n_fields, user_data);
		break;
	default:
		break;
	}

	return G_LOG_WRITER_UNHANDLED;
}

static GLogWriterOutput
gs_debug_log_writer (GLogLevelFlags log_level,
		     const GLogField *fields,
		     gsize n_fields,
		     gpointer user_data)
{
	if (g_log_writer_is_journald (fileno (stderr)))
		return gs_log_writer_journald (log_level, fields, n_fields, user_data);
	else
		return gs_log_writer_console (log_level, fields, n_fields, user_data);
}

static void
gs_debug_finalize (GObject *object)
{
	GsDebug *debug = GS_DEBUG (object);

	g_mutex_clear (&debug->mutex);

	G_OBJECT_CLASS (gs_debug_parent_class)->finalize (object);
}

static void
gs_debug_class_init (GsDebugClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_debug_finalize;
}

static void
gs_debug_init (GsDebug *debug)
{
	g_mutex_init (&debug->mutex);
	debug->use_time = g_getenv ("GS_DEBUG_NO_TIME") == NULL;
	g_log_set_writer_func (gs_debug_log_writer,
			       g_object_ref (debug),
			       (GDestroyNotify) g_object_unref);
}

GsDebug *
gs_debug_new (void)
{
	return GS_DEBUG (g_object_new (GS_TYPE_DEBUG, NULL));
}
