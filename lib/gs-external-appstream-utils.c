 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2018 Endless Mobile, Inc.
 *
 * Authors: Joaquim Rocha <jrocha@endlessm.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
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
gs_external_appstream_check (const gchar *appstream_path,
                             guint64      cache_age_secs)
{
	g_autoptr(GFile) file = g_file_new_for_path (appstream_path);
	guint64 appstream_file_age = gs_utils_get_file_age (file);
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

static gchar *
gs_external_appstream_get_modification_date (const gchar *file_path)
{
#ifndef GLIB_VERSION_2_62
	GTimeVal time_val;
#endif
	g_autoptr(GDateTime) date_time = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	file = g_file_new_for_path (file_path);
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_TIME_MODIFIED,
				  G_FILE_QUERY_INFO_NONE,
				  NULL,
				  NULL);
	if (info == NULL)
		return NULL;
#ifdef GLIB_VERSION_2_62
	date_time = g_file_info_get_modification_date_time (info);
#else
	g_file_info_get_modification_time (info, &time_val);
	date_time = g_date_time_new_from_timeval_local (&time_val);
#endif
	return g_date_time_format (date_time, "%a, %d %b %Y %H:%M:%S %Z");
}

static gboolean
gs_external_appstream_refresh_sys (GsPlugin      *plugin,
                                   const gchar   *url,
                                   const gchar   *basename,
                                   SoupSession   *soup_session,
                                   guint64        cache_age_secs,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
	GOutputStream *outstream = NULL;
	guint status_code;
	gboolean file_written;
	gconstpointer downloaded_data;
	gsize downloaded_data_length;
	g_autofree gchar *tmp_file_path = NULL;
	g_autofree gchar *local_mod_date = NULL;
	g_autofree gchar *target_file_path = NULL;
	g_autoptr(GFileIOStream) iostream = NULL;
	g_autoptr(GFile) tmp_file = NULL;
	g_autoptr(SoupMessage) msg = NULL;
#if SOUP_CHECK_VERSION(3, 0, 0)
	g_autoptr(GBytes) bytes = NULL;
#endif

	/* check age */
	target_file_path = gs_external_appstream_utils_get_file_cache_path (basename);
	if (!gs_external_appstream_check (target_file_path, cache_age_secs)) {
		g_debug ("skipping updating external appstream file %s: "
			 "cache age is older than file",
			 target_file_path);
		return TRUE;
	}

	msg = soup_message_new (SOUP_METHOD_GET, url);

	/* Set the If-Modified-Since header if the target file exists */
	local_mod_date = gs_external_appstream_get_modification_date (target_file_path);
	if (local_mod_date != NULL) {
		g_debug ("Requesting contents of %s if modified since %s",
			 url, local_mod_date);
		soup_message_headers_append (
#if SOUP_CHECK_VERSION(3, 0, 0)
					     soup_message_get_request_headers (msg),
#else
					     msg->request_headers,
#endif
					     "If-Modified-Since",
					     local_mod_date);
	}

	/* get the data */
#if SOUP_CHECK_VERSION(3, 0, 0)
	bytes = soup_session_send_and_read (soup_session, msg, cancellable, error);
	if (bytes != NULL) {
		downloaded_data = g_bytes_get_data (bytes, &downloaded_data_length);
	} else {
		downloaded_data = NULL;
		downloaded_data_length = 0;
	}
	status_code = soup_message_get_status (msg);
#else
	status_code = soup_session_send_message (soup_session, msg);
	downloaded_data = msg->response_body ? msg->response_body->data : NULL;
	downloaded_data_length = msg->response_body ? msg->response_body->length : 0;
#endif
	if (status_code != SOUP_STATUS_OK) {
		if (status_code == SOUP_STATUS_NOT_MODIFIED) {
			g_debug ("Not updating %s has not modified since %s",
				 target_file_path, local_mod_date);
			return TRUE;
		}

		g_set_error (error, GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
			     "Failed to download appstream file %s: %s",
			     url, soup_status_get_phrase (status_code));
		return FALSE;
	}

	/* write the download contents into a file that will be copied into
	 * the system */
	tmp_file_path = gs_utils_get_cache_filename ("external-appstream",
						     basename,
						     GS_UTILS_CACHE_FLAG_WRITEABLE |
						     GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
						     error);
	if (tmp_file_path == NULL)
		return FALSE;

	tmp_file = g_file_new_for_path (tmp_file_path);

	/* ensure the file doesn't exist */
	if (g_file_query_exists (tmp_file, cancellable) &&
	    !g_file_delete (tmp_file, cancellable, error))
		return FALSE;

	iostream = g_file_create_readwrite (tmp_file, G_FILE_CREATE_NONE,
					    cancellable, error);

	if (iostream == NULL)
		return FALSE;

	g_debug ("Downloaded appstream file %s", tmp_file_path);

	/* write to file */
	outstream = g_io_stream_get_output_stream (G_IO_STREAM (iostream));
	file_written = g_output_stream_write_all (outstream, downloaded_data, downloaded_data_length,
						  NULL, cancellable, error);

	/* close the file */
	g_output_stream_close (outstream, cancellable, NULL);

	/* install file systemwide */
	if (file_written) {
		if (gs_external_appstream_install (tmp_file_path,
						   cancellable,
						   error)) {
			g_debug ("Installed appstream file %s", tmp_file_path);
		} else {
			file_written = FALSE;
		}
	}

	/* clean up the temporary file */
	g_file_delete (tmp_file, cancellable, NULL);
	return file_written;
}

static gboolean
gs_external_appstream_refresh_user (GsPlugin      *plugin,
                                    const gchar   *url,
                                    const gchar   *basename,
                                    guint64        cache_age_secs,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
	guint64 file_age;
	g_autofree gchar *fullpath = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

	/* check age */
	fullpath = g_build_filename (g_get_user_data_dir (),
				     "app-info",
				     "xmls",
				     basename,
				     NULL);
	file = g_file_new_for_path (fullpath);
	file_age = gs_utils_get_file_age (file);
	if (file_age < cache_age_secs) {
		g_debug ("skipping %s: cache age is older than file", fullpath);
		return TRUE;
	}

	/* download file */
	gs_app_set_summary_missing (app_dl,
				    /* TRANSLATORS: status text when downloading */
				    _("Downloading extra metadata files…"));
	return gs_plugin_download_file (plugin, app_dl, url, fullpath,
					cancellable, error);
}

static gboolean
gs_external_appstream_refresh_url (GsPlugin      *plugin,
                                   GSettings     *settings,
                                   const gchar   *url,
                                   SoupSession   *soup_session,
                                   guint64        cache_age_secs,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
	g_autofree gchar *basename = NULL;
	g_autofree gchar *basename_url = g_path_get_basename (url);
	/* make sure different uris with same basenames differ */
	g_autofree gchar *hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1,
								url, -1);
	if (hash == NULL) {
		g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			     "Failed to hash url %s", url);
		return FALSE;
	}
	basename = g_strdup_printf ("%s-%s", hash, basename_url);

	if (g_settings_get_boolean (settings, "external-appstream-system-wide")) {
		return gs_external_appstream_refresh_sys (plugin, url,
							  basename,
							  soup_session,
							  cache_age_secs,
							  cancellable,
							  error);
	}
	return gs_external_appstream_refresh_user (plugin, url, basename,
						   cache_age_secs,
						   cancellable, error);
}

/**
 * gs_external_appstream_refresh:
 * @plugin: the #GsPlugin calling this refresh operation
 * @cache_age_secs: as passed to gs_plugin_refresh()
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Refresh any configured external appstream files, if the cache is too old.
 * This is intended to be called from a gs_plugin_refresh() function.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_external_appstream_refresh (GsPlugin      *plugin,
                               guint64        cache_age_secs,
                               GCancellable  *cancellable,
                               GError       **error)
{
	g_autoptr(GSettings) settings = NULL;
	g_auto(GStrv) appstream_urls = NULL;
	g_autoptr(SoupSession) soup_session = NULL;

	settings = g_settings_new ("org.gnome.software");
	soup_session = gs_build_soup_session ();
	appstream_urls = g_settings_get_strv (settings,
					      "external-appstream-urls");
	for (guint i = 0; appstream_urls[i] != NULL; ++i) {
		g_autoptr(GError) error_local = NULL;
		if (!g_str_has_prefix (appstream_urls[i], "https")) {
			g_warning ("Not considering %s as an external "
				   "appstream source: please use an https URL",
				   appstream_urls[i]);
			continue;
		}
		if (!gs_external_appstream_refresh_url (plugin,
							settings,
							appstream_urls[i],
							soup_session,
							cache_age_secs,
							cancellable,
							&error_local)) {
			g_warning ("Failed to update external appstream file: %s",
				   error_local->message);
		}
	}

	return TRUE;
}
