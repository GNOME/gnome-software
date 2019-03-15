/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Endless Mobile, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-metered
 * @title: Metered Data Utilities
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: Utility functions to help with metered data handling
 *
 * Metered data handling is provided by Mogwai, which implements a download
 * scheduler to control when, and in which order, large downloads happen on
 * the system.
 *
 * All large downloads from gs_plugin_download() or gs_plugin_download_app()
 * calls should be scheduled using Mogwai, which will notify gnome-software
 * when those downloads can start and stop, according to system policy.
 *
 * The functions in this file make interacting with the scheduling daemon a
 * little simpler. Since all #GsPlugin method calls happen in worker threads,
 * typically without a #GMainContext, all interaction with the scheduler should
 * be blocking. libmogwai-schedule-client was designed to be asynchronous; so
 * these helpers make it synchronous.
 *
 * Since: 2.34
 */

#include "config.h"

#define GS_ENABLE_EXPERIMENTAL_MOGWAI
#include "gs-metered.h"

#include <glib.h>
#include <libmogwai-schedule-client/scheduler.h>


static void
download_now_cb (GObject    *obj,
                 GParamSpec *pspec,
                 gpointer    user_data)
{
	gboolean *out_download_now = user_data;
	*out_download_now = mwsc_schedule_entry_get_download_now (MWSC_SCHEDULE_ENTRY (obj));
}

static void
invalidated_cb (MwscScheduleEntry *entry,
                const GError      *error,
                gpointer           user_data)
{
	GError **out_error = user_data;
	*out_error = g_error_copy (error);
}

/**
 * gs_metered_block_on_download_scheduler:
 * @parameters: (nullable): a #GVariant of type `a{sv}` specifying parameters
 *    for the schedule entry, or %NULL to pass no parameters
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Create a schedule entry with the given @parameters, and block until
 * permission is given to download.
 *
 * FIXME: This will currently ignore later revocations of that download
 * permission, and does not support creating a schedule entry per app.
 *
 * If a schedule entry cannot be created, or if @cancellable is cancelled,
 * an error will be set and %FALSE returned.
 *
 * The keys understood by @parameters are listed in the documentation for
 * mwsc_scheduler_schedule_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 2.34
 */
gboolean
gs_metered_block_on_download_scheduler (GVariant      *parameters,
                                        GCancellable  *cancellable,
                                        GError       **error)
{
	g_autoptr(MwscScheduler) scheduler = NULL;
	g_autoptr(MwscScheduleEntry) schedule_entry = NULL;

	/* Wait until the download can be scheduled.
	 * FIXME: In future, downloads could be split up by app, so they can all
	 * be scheduled separately and, for example, higher priority ones could
	 * be scheduled with a higher priority. This would have to be aware of
	 * dependencies. */
	scheduler = mwsc_scheduler_new (cancellable, error);
	if (scheduler == NULL)
		return FALSE;

	/* Create a schedule entry for the group of downloads.
	 * FIXME: The underlying OSTree code supports resuming downloads
	 * (at a granularity of individual objects), so it should be
	 * possible to plumb through here. */
	schedule_entry = mwsc_scheduler_schedule (scheduler, parameters, cancellable,
						  error);
	if (schedule_entry == NULL)
		return FALSE;

	/* Wait until the download is allowed to proceed. */
	if (!mwsc_schedule_entry_get_download_now (schedule_entry)) {
		gboolean download_now = FALSE;
		g_autoptr(GError) invalidated_error = NULL;
		gulong notify_id, invalidated_id;
		g_autoptr(GMainContext) context = NULL;

		context = g_main_context_new ();
		g_main_context_push_thread_default (context);

		notify_id = g_signal_connect (schedule_entry, "notify::download-now",
					      (GCallback) download_now_cb, &download_now);
		invalidated_id = g_signal_connect (schedule_entry, "invalidated",
						   (GCallback) invalidated_cb, &invalidated_error);

		while (!download_now && invalidated_error == NULL &&
		       !g_cancellable_is_cancelled (cancellable))
			g_main_context_iteration (context, TRUE);

		g_signal_handler_disconnect (schedule_entry, invalidated_id);
		g_signal_handler_disconnect (schedule_entry, notify_id);

		g_main_context_pop_thread_default (context);

		if (!download_now && invalidated_error != NULL) {
			g_propagate_error (error, g_steal_pointer (&invalidated_error));
			return FALSE;
		} else if (!download_now && g_cancellable_set_error_if_cancelled (cancellable, error)) {
			return FALSE;
		}

		g_assert (download_now);
	}

	return TRUE;
}