 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2018 Endless Mobile, Inc.
 *
 * Authors: Joaquim Rocha <jrocha@endlessm.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/*
 * SECTION:gs-external-appstream-urls
 * @short_description: Provides support for downloading external AppStream files.
 *
 * This downloads the set of configured external AppStream files, and caches
 * them locally.
 *
 * According to the `external-appstream-system-wide` GSetting, the files will
 * either be downloaded to a per-user cache, or to a system-wide cache. In the
 * case of a system-wide cache, they are downloaded to a temporary file writable
 * by the user, and then the suexec binary `gnome-software-install-appstream` is
 * run to copy them to the system location.
 *
 * All the downloads are done in the default #GMainContext for the thread which
 * calls gs_external_appstream_refresh_async(). They are done in parallel and
 * the async refresh function will only complete once the last download is
 * complete.
 *
 * Progress data is reported via a callback, and gives the total progress of all
 * parallel downloads. Internally this is done by updating #ProgressTuple
 * structs as each download progresses. A periodic timeout callback sums these
 * and reports the total progress to the caller. That means that progress
 * reports from gs_external_appstream_refresh_async() are done at a constant
 * frequency.
 *
 * To test this code locally you will probably want to change your GSettings
 * configuration to add some external AppStream URIs:
 * ```
 * gsettings set org.gnome.software external-appstream-urls '["https://example.com/appdata.xml.gz"]'
 * ```
 *
 * When you are done with development, run the following command to use the real
 * external AppStream list again:
 * ```
 * gsettings reset org.gnome.software external-appstream-urls
 * ```
 *
 * Since: 42
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <libsoup/soup.h>

#include "gs-external-appstream-utils.h"

#define APPSTREAM_SYSTEM_DIR LOCALSTATEDIR "/cache/app-info/xmls"

gchar *
gs_external_appstream_utils_get_file_cache_path (const gchar *file_name)
{
	g_autofree gchar *prefixed_file_name = g_strdup_printf (EXTERNAL_APPSTREAM_PREFIX "-%s",
								file_name);
	return g_build_filename (APPSTREAM_SYSTEM_DIR, prefixed_file_name, NULL);
}

const gchar *
gs_external_appstream_utils_get_system_dir (void)
{
	return APPSTREAM_SYSTEM_DIR;
}

static gboolean
gs_external_appstream_check (GFile   *appstream_file,
                             guint64  cache_age_secs)
{
	guint64 appstream_file_age = gs_utils_get_file_age (appstream_file);
	return appstream_file_age >= cache_age_secs;
}

static gboolean
gs_external_appstream_install (const gchar   *appstream_file,
                               GCancellable  *cancellable,
                               GError       **error)
{
	g_autoptr(GSubprocess) subprocess = NULL;
	const gchar *argv[] = { "pkexec",
				LIBEXECDIR "/gnome-software-install-appstream",
				appstream_file, NULL};
	g_debug ("Installing the appstream file %s in the system",
		 appstream_file);
	subprocess = g_subprocess_newv (argv,
					G_SUBPROCESS_FLAGS_STDOUT_PIPE |
					G_SUBPROCESS_FLAGS_STDIN_PIPE, error);
	if (subprocess == NULL)
		return FALSE;
	return g_subprocess_wait_check (subprocess, cancellable, error);
}

static void download_system_cb (GObject      *source_object,
                                GAsyncResult *result,
                                gpointer      user_data);
static void download_user_cb (GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data);

/* A tuple to store the last-received progress data for a single download.
 * Each download (refresh_url_async()) has a pointer to the relevant
 * #ProgressTuple for its download. These are stored in an array in #RefreshData
 * and a timeout callback periodically sums them all and reports progress to the
 * caller. */
typedef struct {
	gsize bytes_downloaded;
	gsize total_download_size;
} ProgressTuple;

static void
refresh_url_progress_cb (gsize    bytes_downloaded,
                         gsize    total_download_size,
                         gpointer user_data)
{
	ProgressTuple *tuple = user_data;

	tuple->bytes_downloaded = bytes_downloaded;
	tuple->total_download_size = total_download_size;

	/* The timeout callback in progress_cb() periodically sums these. No
	 * need to notify of progress from here. */
}

static void
refresh_url_async (GSettings           *settings,
                   const gchar         *url,
                   SoupSession         *soup_session,
                   guint64              cache_age_secs,
                   ProgressTuple       *progress_tuple,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
	g_autoptr(GTask) task = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *basename_url = g_path_get_basename (url);
	/* make sure different uris with same basenames differ */
	g_autofree gchar *hash = NULL;
	g_autofree gchar *target_file_path = NULL;
	g_autoptr(GFile) target_file = NULL;
	g_autoptr(GFile) tmp_file = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new ("external-appstream");
	g_autoptr(GError) local_error = NULL;
	gboolean system_wide;

	task = g_task_new (NULL, cancellable, callback, user_data);
	g_task_set_source_tag (task, refresh_url_async);

	/* Calculate the basename of the target file. */
	hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1, url, -1);
	if (hash == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_FAILED,
					 "Failed to hash url %s", url);
		return;
	}
	basename = g_strdup_printf ("%s-%s", hash, basename_url);

	/* Are we downloading for the user, or the system? */
	system_wide = g_settings_get_boolean (settings, "external-appstream-system-wide");

	/* Check cache file age. */
	if (system_wide)
		target_file_path = gs_external_appstream_utils_get_file_cache_path (basename);
	else
		target_file_path = g_build_filename (g_get_user_data_dir (),
						     "app-info",
						     "xmls",
						     basename,
						     NULL);

	target_file = g_file_new_for_path (target_file_path);

	if (!gs_external_appstream_check (target_file, cache_age_secs)) {
		g_debug ("skipping updating external appstream file %s: "
			 "cache age is older than file",
			 target_file_path);
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* If downloading system wide, write the download contents into a
	 * temporary file that will be copied into the system location later. */
	if (system_wide) {
		g_autofree gchar *tmp_file_path = NULL;

		tmp_file_path = gs_utils_get_cache_filename ("external-appstream",
							     basename,
							     GS_UTILS_CACHE_FLAG_WRITEABLE |
							     GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
							     &local_error);
		if (tmp_file_path == NULL) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		tmp_file = g_file_new_for_path (tmp_file_path);
	} else {
		tmp_file = g_object_ref (target_file);
	}

	g_task_set_task_data (task, g_object_ref (tmp_file), g_object_unref);

	gs_app_set_summary_missing (app_dl,
				    /* TRANSLATORS: status text when downloading */
				    _("Downloading extra metadata files…"));

	/* Do the download. */
	gs_download_file_async (soup_session, url, tmp_file, G_PRIORITY_LOW,
				refresh_url_progress_cb,
				progress_tuple,
				cancellable,
				system_wide ? download_system_cb : download_user_cb,
				g_steal_pointer (&task));
}

static void
download_system_cb (GObject      *source_object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
	SoupSession *soup_session = SOUP_SESSION (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GCancellable *cancellable = g_task_get_cancellable (task);
	GFile *tmp_file = g_task_get_task_data (task);
	g_autoptr(GError) local_error = NULL;

	if (!gs_download_file_finish (soup_session, result, &local_error)) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
					 "%s", local_error->message);
		return;
	}

	g_debug ("Downloaded appstream file %s", g_file_peek_path (tmp_file));

	/* install file systemwide */
	if (gs_external_appstream_install (g_file_peek_path (tmp_file),
					   cancellable,
					   &local_error)) {
		g_debug ("Installed appstream file %s", g_file_peek_path (tmp_file));
		g_task_return_boolean (task, TRUE);
	} else {
		g_task_return_error (task, g_steal_pointer (&local_error));
	}
}

static void
download_user_cb (GObject      *source_object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
	SoupSession *soup_session = SOUP_SESSION (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GFile *tmp_file = g_task_get_task_data (task);
	g_autoptr(GError) local_error = NULL;

	if (!gs_download_file_finish (soup_session, result, &local_error)) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
					 "%s", local_error->message);
		return;
	}

	g_debug ("Downloaded appstream file %s", g_file_peek_path (tmp_file));

	g_task_return_boolean (task, TRUE);
}

static gboolean
refresh_url_finish (GAsyncResult  *result,
                    GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void refresh_cb (GObject      *source_object,
                        GAsyncResult *result,
                        gpointer      user_data);
static gboolean progress_cb (gpointer user_data);
static void finish_refresh_op (GTask  *task,
                               GError *error);

typedef struct {
	/* Input data. */
	guint64 cache_age_secs;

	/* In-progress data. */
	guint n_pending_ops;
	GError *error;  /* (nullable) (owned) */
	gsize n_appstream_urls;
	GsDownloadProgressCallback progress_callback;  /* (nullable) */
	gpointer progress_user_data;  /* (closure progress_callback) */
	ProgressTuple *progress_tuples;  /* (array length=n_appstream_urls) (owned) */
	GSource *progress_source;  /* (owned) */
} RefreshData;

static void
refresh_data_free (RefreshData *data)
{
	g_assert (data->n_pending_ops == 0);

	/* If this was set it should have been stolen for g_task_return_error()
	 * by now. */
	g_assert (data->error == NULL);

	/* Similarly, progress reporting should have been stopped by now. */
	g_assert (g_source_is_destroyed (data->progress_source));
	g_source_unref (data->progress_source);

	g_free (data->progress_tuples);

	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RefreshData, refresh_data_free)

/**
 * gs_external_appstream_refresh_async:
 * @cache_age_secs: cache age, in seconds, as passed to gs_plugin_refresh()
 * @progress_callback: (nullable): callback to call with progress information
 * @progress_user_data: (nullable) (closure progress_callback): data to pass
 *   to @progress_callback
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function call when the asynchronous operation is complete
 * @user_data: data to pass to @callback
 *
 * Refresh any configured external appstream files, if the cache is too old.
 *
 * Since: 42
 */
void
gs_external_appstream_refresh_async (guint64                     cache_age_secs,
                                     GsDownloadProgressCallback  progress_callback,
                                     gpointer                    progress_user_data,
                                     GCancellable               *cancellable,
                                     GAsyncReadyCallback         callback,
                                     gpointer                    user_data)
{
	g_autoptr(GSettings) settings = NULL;
	g_auto(GStrv) appstream_urls = NULL;
	gsize n_appstream_urls;
	g_autoptr(SoupSession) soup_session = NULL;
	g_autoptr(GTask) task = NULL;
	RefreshData *data;
	g_autoptr(RefreshData) data_owned = NULL;

	/* Chosen to allow a few UI updates per second without updating the
	 * progress label so often it’s unreadable. */
	const guint progress_update_period_ms = 300;

	task = g_task_new (NULL, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_external_appstream_refresh_async);

	settings = g_settings_new ("org.gnome.software");
	soup_session = gs_build_soup_session ();
	appstream_urls = g_settings_get_strv (settings,
					      "external-appstream-urls");
	n_appstream_urls = g_strv_length (appstream_urls);

	data = data_owned = g_new0 (RefreshData, 1);
	data->progress_callback = progress_callback;
	data->progress_user_data = progress_user_data;
	data->n_appstream_urls = n_appstream_urls;
	data->progress_tuples = g_new0 (ProgressTuple, n_appstream_urls);
	data->progress_source = g_timeout_source_new (progress_update_period_ms);
	g_task_set_task_data (task, g_steal_pointer (&data_owned), (GDestroyNotify) refresh_data_free);

	/* Set up the progress timeout. This periodically sums up the progress
	 * tuples in `data->progress_tuples` and reports them to the calling
	 * function via @progress_callback, giving an overall progress for all
	 * the parallel operations. */
	g_source_set_callback (data->progress_source, progress_cb, g_object_ref (task), g_object_unref);
	g_source_attach (data->progress_source, g_main_context_get_thread_default ());

	/* Refresh all the URIs in parallel. */
	data->n_pending_ops = 1;

	for (gsize i = 0; i < n_appstream_urls; i++) {
		if (!g_str_has_prefix (appstream_urls[i], "https")) {
			g_warning ("Not considering %s as an external "
				   "appstream source: please use an https URL",
				   appstream_urls[i]);
			continue;
		}

		data->n_pending_ops++;
		refresh_url_async (settings,
				   appstream_urls[i],
				   soup_session,
				   cache_age_secs,
				   &data->progress_tuples[i],
				   cancellable,
				   refresh_cb,
				   g_object_ref (task));
	}

	finish_refresh_op (task, NULL);
}

static void
refresh_cb (GObject      *source_object,
            GAsyncResult *result,
            gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;

	refresh_url_finish (result, &local_error);
	finish_refresh_op (task, g_steal_pointer (&local_error));
}

static gboolean
progress_cb (gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	RefreshData *data = g_task_get_task_data (task);
	gsize parallel_bytes_downloaded = 0, parallel_total_download_size = 0;

	/* Sum up the progress numerator and denominator for all parallel
	 * downloads. */
	for (gsize i = 0; i < data->n_appstream_urls; i++) {
		const ProgressTuple *progress_tuple = &data->progress_tuples[i];

		if (!g_size_checked_add (&parallel_bytes_downloaded,
					 parallel_bytes_downloaded,
					 progress_tuple->bytes_downloaded))
			parallel_bytes_downloaded = G_MAXSIZE;
		if (!g_size_checked_add (&parallel_total_download_size,
					 parallel_total_download_size,
					 progress_tuple->total_download_size))
			parallel_total_download_size = G_MAXSIZE;
	}

	/* Report progress to the calling function. */
	if (data->progress_callback != NULL)
		data->progress_callback (parallel_bytes_downloaded,
					 parallel_total_download_size,
					 data->progress_user_data);

	return G_SOURCE_CONTINUE;
}

/* @error is (transfer full) if non-%NULL */
static void
finish_refresh_op (GTask  *task,
                   GError *error)
{
	RefreshData *data = g_task_get_task_data (task);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);

	if (data->error == NULL && error_owned != NULL)
		data->error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while refreshing external appstream: %s", error_owned->message);

	g_assert (data->n_pending_ops > 0);
	data->n_pending_ops--;

	if (data->n_pending_ops > 0)
		return;

	/* Emit one final progress update, then stop any further ones. */
	progress_cb (task);
	g_source_destroy (data->progress_source);

	/* All complete. */
	if (data->error != NULL)
		g_task_return_error (task, g_steal_pointer (&data->error));
	else
		g_task_return_boolean (task, TRUE);
}

/**
 * gs_external_appstream_refresh_finish:
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finish an asynchronous refresh operation started with
 * gs_external_appstream_refresh_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 42
 */
gboolean
gs_external_appstream_refresh_finish (GAsyncResult  *result,
                                      GError       **error)
{
	g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}
