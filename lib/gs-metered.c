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

#include <glib.h>

#ifdef HAVE_MOGWAI
#include <libmogwai-schedule-client/scheduler.h>
#endif

#include "gs-metered.h"


#ifdef HAVE_MOGWAI

/* FIXME: Backported from https://gitlab.gnome.org/GNOME/glib/merge_requests/983.
 * Drop once we can depend on a version of GLib which includes it .*/
typedef void MainContextPusher;

static inline MainContextPusher *
main_context_pusher_new (GMainContext *main_context)
{
  g_main_context_push_thread_default (main_context);
  return (MainContextPusher *) main_context;
}

static inline void
main_context_pusher_free (MainContextPusher *pusher)
{
  g_main_context_pop_thread_default ((GMainContext *) pusher);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MainContextPusher, main_context_pusher_free)


typedef struct
{
	gboolean *out_download_now;  /* (unowned) */
	GMainContext *context;  /* (unowned) */
} DownloadNowData;

static void
download_now_cb (GObject    *obj,
                 GParamSpec *pspec,
                 gpointer    user_data)
{
	DownloadNowData *data = user_data;
	*data->out_download_now = mwsc_schedule_entry_get_download_now (MWSC_SCHEDULE_ENTRY (obj));
	g_main_context_wakeup (data->context);
}

typedef struct
{
	GError **out_error;  /* (unowned) */
	GMainContext *context;  /* (unowned) */
} InvalidatedData;

static void
invalidated_cb (MwscScheduleEntry *entry,
                const GError      *error,
                gpointer           user_data)
{
	InvalidatedData *data = user_data;
	*data->out_error = g_error_copy (error);
	g_main_context_wakeup (data->context);
}

#endif  /* HAVE_MOGWAI */

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
 * This function will likely be called from a #GsPluginLoader worker thread.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 2.34
 */
gboolean
gs_metered_block_on_download_scheduler (GVariant      *parameters,
                                        GCancellable  *cancellable,
                                        GError       **error)
{
#ifdef HAVE_MOGWAI
	g_autoptr(MwscScheduler) scheduler = NULL;
	g_autoptr(MwscScheduleEntry) schedule_entry = NULL;
	g_autofree gchar *parameters_str = NULL;
	g_autoptr(GMainContext) context = NULL;
	g_autoptr(MainContextPusher) pusher = NULL;

	parameters_str = (parameters != NULL) ? g_variant_print (parameters, TRUE) : g_strdup ("(none)");
	g_debug ("%s: Waiting with parameters: %s", G_STRFUNC, parameters_str);

	/* Push the context early so that the #MwscScheduler is created to run within it. */
	context = g_main_context_new ();
	pusher = main_context_pusher_new (context);

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
		DownloadNowData download_now_data = { &download_now, context };
		InvalidatedData invalidated_data = { &invalidated_error, context };

		notify_id = g_signal_connect (schedule_entry, "notify::download-now",
					      (GCallback) download_now_cb, &download_now_data);
		invalidated_id = g_signal_connect (schedule_entry, "invalidated",
						   (GCallback) invalidated_cb, &invalidated_data);

		while (!download_now && invalidated_error == NULL &&
		       !g_cancellable_is_cancelled (cancellable))
			g_main_context_iteration (context, TRUE);

		g_signal_handler_disconnect (schedule_entry, invalidated_id);
		g_signal_handler_disconnect (schedule_entry, notify_id);

		if (!download_now && invalidated_error != NULL) {
			g_propagate_error (error, g_steal_pointer (&invalidated_error));
			return FALSE;
		} else if (!download_now && g_cancellable_set_error_if_cancelled (cancellable, error)) {
			return FALSE;
		}

		g_assert (download_now);
	}

	g_debug ("%s: Allowed to download", G_STRFUNC);
#else  /* if !HAVE_MOGWAI */
	g_debug ("%s: Allowed to download (Mogwai support compiled out)", G_STRFUNC);
#endif  /* !HAVE_MOGWAI */

	return TRUE;
}

/**
 * gs_metered_block_app_on_download_scheduler:
 * @app: a #GsApp to get the scheduler parameters from
 * @cancellable: a #GCancellable, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Version of gs_metered_block_on_download_scheduler() which extracts the
 * download parameters from the given @app.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 2.34
 */
gboolean
gs_metered_block_app_on_download_scheduler (GsApp         *app,
                                            GCancellable  *cancellable,
                                            GError       **error)
{
	g_auto(GVariantDict) parameters_dict = G_VARIANT_DICT_INIT (NULL);
	g_autoptr(GVariant) parameters = NULL;
	guint64 download_size = gs_app_get_size_download (app);

	/* Currently no plugins support resumable downloads. This may change in
	 * future, in which case this parameter should be refactored. */
	g_variant_dict_insert (&parameters_dict, "resumable", "b", FALSE);

	if (download_size != 0 && download_size != GS_APP_SIZE_UNKNOWABLE) {
		g_variant_dict_insert (&parameters_dict, "size-minimum", "t", download_size);
		g_variant_dict_insert (&parameters_dict, "size-maximum", "t", download_size);
	}

	parameters = g_variant_ref_sink (g_variant_dict_end (&parameters_dict));

	return gs_metered_block_on_download_scheduler (parameters, cancellable, error);
}

/**
 * gs_metered_block_app_list_on_download_scheduler:
 * @app_list: a #GsAppList to get the scheduler parameters from
 * @cancellable: a #GCancellable, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Version of gs_metered_block_on_download_scheduler() which extracts the
 * download parameters from the apps in the given @app_list.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 2.34
 */
gboolean
gs_metered_block_app_list_on_download_scheduler (GsAppList     *app_list,
                                                 GCancellable  *cancellable,
                                                 GError       **error)
{
	g_auto(GVariantDict) parameters_dict = G_VARIANT_DICT_INIT (NULL);
	g_autoptr(GVariant) parameters = NULL;

	/* Currently no plugins support resumable downloads. This may change in
	 * future, in which case this parameter should be refactored. */
	g_variant_dict_insert (&parameters_dict, "resumable", "b", FALSE);

	/* FIXME: Currently this creates a single Mogwai schedule entry for the
	 * entire app list. Eventually, we probably want one schedule entry per
	 * app being downloaded, so that they can be individually prioritised.
	 * However, that requires much deeper integration into the download
	 * code, and Mogwai does not currently support that level of
	 * prioritisation, so go with this simple implementation for now. */
	parameters = g_variant_ref_sink (g_variant_dict_end (&parameters_dict));

	return gs_metered_block_on_download_scheduler (parameters, cancellable, error);
}
