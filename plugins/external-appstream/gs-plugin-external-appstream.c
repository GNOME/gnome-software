/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016-2018 Endless Mobile, Inc.
 *
 * Authors: Joaquim Rocha <jrocha@endlessm.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>
#include <glib/gi18n.h>

#include <gnome-software.h>
#include "gs-external-appstream-utils.h"

struct GsPluginData {
	GSettings *settings;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	const gchar *system_dir = gs_external_appstream_utils_get_system_dir ();

	priv->settings = g_settings_new ("org.gnome.software");

	/* run it before the appstream plugin */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "appstream");

	g_debug ("appstream system dir: %s", system_dir);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->settings);
}

static gboolean
gs_plugin_external_appstream_check (const gchar *appstream_path,
					    guint cache_age)
{
	g_autoptr(GFile) file = g_file_new_for_path (appstream_path);
	guint appstream_file_age = gs_utils_get_file_age (file);
	return appstream_file_age >= cache_age;
}

static gboolean
gs_plugin_external_appstream_install (const gchar *appstream_file,
				      GCancellable *cancellable,
				      GError **error)
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
gs_plugin_external_appstream_get_modification_date (const gchar *file_path)
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
gs_plugin_external_appstream_refresh_sys (GsPlugin *plugin,
					  const gchar *url,
					  guint cache_age,
					  GCancellable *cancellable,
					  GError **error)
{
	GOutputStream *outstream = NULL;
	SoupSession *soup_session;
	guint status_code;
	gboolean file_written;
	g_autofree gchar *tmp_file_path = NULL;
	g_autofree gchar *file_name = NULL;
	g_autofree gchar *local_mod_date = NULL;
	g_autofree gchar *target_file_path = NULL;
	g_autofree gchar *tmp_file_tmpl = NULL;
	g_autoptr(GFileIOStream) iostream = NULL;
	g_autoptr(GFile) tmp_file = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* check age */
	file_name = g_path_get_basename (url);
	target_file_path = gs_external_appstream_utils_get_file_cache_path (file_name);
	if (!gs_plugin_external_appstream_check (target_file_path, cache_age)) {
		g_debug ("skipping updating external appstream file %s: "
			 "cache age is older than file",
			 target_file_path);
		return TRUE;
	}

	msg = soup_message_new (SOUP_METHOD_GET, url);

	/* Set the If-Modified-Since header if the target file exists */
	local_mod_date = gs_plugin_external_appstream_get_modification_date (target_file_path);
	if (local_mod_date != NULL) {
		g_debug ("Requesting contents of %s if modified since %s",
			 url, local_mod_date);
		soup_message_headers_append (msg->request_headers,
					     "If-Modified-Since",
					     local_mod_date);
	}

	/* get the data */
	soup_session = gs_plugin_get_soup_session (plugin);
	status_code = soup_session_send_message (soup_session, msg);
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
						     file_name,
						     GS_UTILS_CACHE_FLAG_WRITEABLE,
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
	file_written = g_output_stream_write_all (outstream,
						  msg->response_body->data,
						  msg->response_body->length,
						  NULL, cancellable, error);

	/* close the file */
	g_output_stream_close (outstream, cancellable, NULL);

	/* install file systemwide */
	if (file_written) {
		if (gs_plugin_external_appstream_install (tmp_file_path,
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
gs_plugin_external_appstream_refresh_user (GsPlugin *plugin,
					   const gchar *url,
					   guint cache_age,
					   GCancellable *cancellable,
					   GError **error)
{
	guint file_age;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *fullpath = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

	/* check age */
	basename = g_path_get_basename (url);
	fullpath = g_build_filename (g_get_user_data_dir (),
				     "app-info",
				     "xmls",
				     basename,
				     NULL);
	file = g_file_new_for_path (fullpath);
	file_age = gs_utils_get_file_age (file);
	if (file_age < cache_age) {
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
gs_plugin_external_appstream_refresh_url (GsPlugin *plugin,
					  const gchar *url,
					  guint cache_age,
					  GCancellable *cancellable,
					  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (g_settings_get_strv (priv->settings, "external-appstream-urls")) {
		return gs_plugin_external_appstream_refresh_sys (plugin, url,
								 cache_age,
								 cancellable,
								 error);
	}
	return gs_plugin_external_appstream_refresh_user (plugin, url, cache_age,
							  cancellable, error);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_auto(GStrv) appstream_urls = NULL;

	appstream_urls = g_settings_get_strv (priv->settings,
					      "external-appstream-urls");
	for (guint i = 0; appstream_urls[i] != NULL; ++i) {
		g_autoptr(GError) error_local = NULL;
		if (!g_str_has_prefix (appstream_urls[i], "https")) {
			g_warning ("Not considering %s as an external "
				   "appstream source: please use an https URL",
				   appstream_urls[i]);
			continue;
		}
		if (!gs_plugin_external_appstream_refresh_url (plugin,
							       appstream_urls[i],
							       cache_age,
							       cancellable,
							       &error_local)) {
			g_warning ("Failed to update external appstream file: %s",
				   error_local->message);
		}
	}

	return TRUE;
}
