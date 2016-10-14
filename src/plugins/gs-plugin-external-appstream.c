/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Endless Mobile, Inc.
 *
 * Authors: Joaquim Rocha <jrocha@endlessm.com>
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

#include <config.h>

#include <gnome-software.h>

#define APPSTREAM_SYSTEM_DIR LOCALSTATEDIR "/cache/app-info/xmls"

struct GsPluginData {
	GSettings *settings;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	priv->settings = g_settings_new ("org.gnome.software");

	/* run it before the appstream plugin */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "appstream");

	g_debug ("appstream system dir: %s", APPSTREAM_SYSTEM_DIR);
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
				      const gchar *target_file_name,
				      GCancellable *cancellable,
				      GError **error)
{
	g_autoptr(GSubprocess) subprocess = NULL;
	const gchar *argv[] = { "pkexec",
				LIBEXECDIR "/gnome-software-install-appstream",
				appstream_file, target_file_name, NULL};
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
	GTimeVal time_val;
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
	g_file_info_get_modification_time (info, &time_val);
	date_time = g_date_time_new_from_timeval_local (&time_val);
	return g_date_time_format (date_time, "%a, %d %b %Y %H:%M:%S %Z");
}

static gboolean
gs_plugin_external_appstream_refresh_url (GsPlugin *plugin,
					  const gchar *url,
					  guint cache_age,
					  GCancellable *cancellable,
					  GError **error)
{
	GOutputStream *outstream = NULL;
	SoupSession *soup_session;
	const gchar *tmp_file_path = NULL;
	guint status_code;
	gboolean file_written;
	g_autofree gchar *file_name = NULL;
	g_autofree gchar *local_mod_date = NULL;
	g_autofree gchar *target_file_path = NULL;
	g_autofree gchar *tmp_file_tmpl = NULL;
	g_autoptr(GFileIOStream) iostream = NULL;
	g_autoptr(GFile) tmp_file = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* check age */
	file_name = g_path_get_basename (url);
	target_file_path = g_build_filename (APPSTREAM_SYSTEM_DIR, file_name, NULL);
	if (!gs_plugin_external_appstream_check (target_file_path, cache_age)) {
		g_debug ("skipping updating external appstream file %s: "
			 "cache age is older than file",
			 target_file_path);
		return TRUE;
	}

	msg = soup_message_new (SOUP_METHOD_GET, url);

	/* Set the If-Modified-Since header if the target file exists */
	target_file_path = g_build_filename (APPSTREAM_SYSTEM_DIR, file_name,
					     NULL);
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

	/* write the download contents into a temporary file that will be
	 * moved into the system */
	tmp_file_tmpl = g_strdup_printf ("XXXXXX_%s", file_name);
	tmp_file = g_file_new_tmp (tmp_file_tmpl, &iostream, error);
	if (tmp_file == NULL)
		return FALSE;

	tmp_file_path = g_file_get_path (tmp_file);

	g_debug ("Downloaded appstream file %s", tmp_file_path);

	/* write to file */
	outstream = g_io_stream_get_output_stream (G_IO_STREAM (iostream));
	file_written = g_output_stream_write_all (outstream,
						  msg->response_body->data,
						  msg->response_body->length,
						  NULL, cancellable, error);

	/* close the file */
	g_output_stream_close (outstream, cancellable, NULL);

	if (!file_written) {
		/* clean up the temporary file if we could not write */
		g_file_delete (tmp_file, NULL, NULL);
		return FALSE;
	}

	/* install file systemwide */
	if (!gs_plugin_external_appstream_install (tmp_file_path,
						   file_name,
						   cancellable,
						   error))
		return FALSE;
	g_debug ("Installed appstream file %s as %s", tmp_file_path, file_name);

	return TRUE;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	guint i;
	g_auto(GStrv) appstream_urls = NULL;

	if ((flags & GS_PLUGIN_REFRESH_FLAGS_METADATA) == 0)
		return TRUE;

	appstream_urls = g_settings_get_strv (priv->settings,
					      "external-appstream-urls");
	for (i = 0; appstream_urls[i] != NULL; ++i) {
		g_autoptr(GError) local_error = NULL;
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
							       &local_error)) {
			g_warning ("Failed to update external appstream file: %s",
				   local_error->message);
		}
	}

	return TRUE;
}
