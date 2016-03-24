/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <fwupd.h>
#include <appstream-glib.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#include <gs-plugin.h>

#include "gs-utils.h"

/*
 * SECTION:
 * Queries for new firmware and schedules it to be installed as required.
 *
 * This plugin calls UpdatesChanged() if any updatable devices are
 * added or removed or if a device has been updated live.
 */

struct GsPluginPrivate {
	GMutex			 mutex;
	FwupdClient		*client;
	GPtrArray		*to_download;
	GPtrArray		*to_ignore;
	gchar			*cachedir;
	gchar			*lvfs_sig_fn;
	gchar			*lvfs_sig_hash;
	gchar			*config_fn;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "fwupd";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->client = fwupd_client_new ();
	plugin->priv->to_download = g_ptr_array_new_with_free_func (g_free);
	plugin->priv->to_ignore = g_ptr_array_new_with_free_func (g_free);
	plugin->priv->config_fn = g_build_filename (SYSCONFDIR, "fwupd.conf", NULL);
	if (!g_file_test (plugin->priv->config_fn, G_FILE_TEST_EXISTS)) {
		g_free (plugin->priv->config_fn);
		plugin->priv->config_fn = g_strdup ("/etc/fwupd.conf");
	}
	if (!g_file_test (plugin->priv->config_fn, G_FILE_TEST_EXISTS)) {
		g_debug ("fwupd configuration not found, disabling plugin.");
		gs_plugin_set_enabled (plugin, FALSE);
	}
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_free (plugin->priv->cachedir);
	g_free (plugin->priv->lvfs_sig_fn);
	g_free (plugin->priv->lvfs_sig_hash);
	g_free (plugin->priv->config_fn);
	g_object_unref (plugin->priv->client);
	g_ptr_array_unref (plugin->priv->to_download);
	g_ptr_array_unref (plugin->priv->to_ignore);
}

/**
 * gs_plugin_fwupd_changed_cb:
 */
static void
gs_plugin_fwupd_changed_cb (FwupdClient *client, GsPlugin *plugin)
{
	gs_plugin_updates_changed (plugin);
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	gsize len;
	g_autofree gchar *data = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin->priv->mutex);

	/* already done */
	if (plugin->priv->cachedir != NULL)
		return TRUE;

	/* register D-Bus errors */
	fwupd_error_quark ();
	g_signal_connect (plugin->priv->client, "changed",
			  G_CALLBACK (gs_plugin_fwupd_changed_cb), plugin);

	/* create the cache location */
	plugin->priv->cachedir = gs_utils_get_cachedir ("firmware", error);
	if (plugin->priv->cachedir == NULL)
		return FALSE;

	/* get the hash of the previously downloaded file */
	plugin->priv->lvfs_sig_fn = g_build_filename (plugin->priv->cachedir,
						      "firmware.xml.gz.asc",
						      NULL);
	if (g_file_test (plugin->priv->lvfs_sig_fn, G_FILE_TEST_EXISTS)) {
		if (!g_file_get_contents (plugin->priv->lvfs_sig_fn,
					  &data, &len, error))
			return FALSE;
		plugin->priv->lvfs_sig_hash =
			g_compute_checksum_for_data (G_CHECKSUM_SHA1, (guchar *) data, len);
	}

	return TRUE;
}

/**
 * gs_plugin_fwupd_add_required_location:
 */
static void
gs_plugin_fwupd_add_required_location (GsPlugin *plugin, const gchar *location)
{
	const gchar *tmp;
	guint i;
	for (i = 0; i < plugin->priv->to_ignore->len; i++) {
		tmp = g_ptr_array_index (plugin->priv->to_ignore, i);
		if (g_strcmp0 (tmp, location) == 0)
			return;
	}
	for (i = 0; i < plugin->priv->to_download->len; i++) {
		tmp = g_ptr_array_index (plugin->priv->to_download, i);
		if (g_strcmp0 (tmp, location) == 0)
			return;
	}
	g_ptr_array_add (plugin->priv->to_download, g_strdup (location));
}

/**
 * gs_plugin_fwupd_get_file_checksum:
 */
static gchar *
gs_plugin_fwupd_get_file_checksum (const gchar *filename,
				   GChecksumType checksum_type,
				   GError **error)
{
	gsize len;
	g_autofree gchar *data = NULL;

	if (!g_file_get_contents (filename, &data, &len, error))
		return NULL;
	return g_compute_checksum_for_data (checksum_type, (const guchar *)data, len);
}

/**
 * gs_plugin_fwupd_new_app_from_results:
 */
static GsApp *
gs_plugin_fwupd_new_app_from_results (FwupdResult *res)
{
	GsApp *app;
	g_autoptr(AsIcon) icon = NULL;

	/* default stuff */
	app = gs_app_new (fwupd_result_get_update_id (res));
	gs_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	gs_app_set_management_plugin (app, "fwupd");
	gs_app_add_category (app, "System");
	gs_app_set_metadata (app, "fwupd::DeviceID",
			     fwupd_result_get_device_id (res));

	/* create icon */
	icon = as_icon_new ();
	as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
	as_icon_set_name (icon, "application-x-firmware");
	gs_app_set_icon (app, icon);

	if (fwupd_result_get_update_id (res) != NULL) {
		gs_app_set_id (app, fwupd_result_get_update_id (res));
	}
	if (fwupd_result_get_guid (res) != NULL) {
		gs_app_set_metadata (app, "fwupd::Guid",
				     fwupd_result_get_guid (res));
	}
	if (fwupd_result_get_update_name (res) != NULL) {
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
				 fwupd_result_get_update_name (res));
	}
	if (fwupd_result_get_update_summary (res) != NULL) {
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
				    fwupd_result_get_update_summary (res));
	}
	if (fwupd_result_get_device_version (res) != NULL) {
		gs_app_set_version (app, fwupd_result_get_device_version (res));
	}
	if (fwupd_result_get_update_size (res) != 0) {
		gs_app_set_size (app, fwupd_result_get_update_size (res));
	}
	if (fwupd_result_get_device_created (res) != 0) {
		gs_app_set_install_date (app, fwupd_result_get_device_created (res));
	}
	if (fwupd_result_get_update_version (res) != NULL) {
		gs_app_set_update_version (app, fwupd_result_get_update_version (res));
	}
	if (fwupd_result_get_update_license (res) != NULL) {
		gs_app_set_license (app, GS_APP_QUALITY_NORMAL,
				    fwupd_result_get_update_license (res));
	}
	if (fwupd_result_get_update_description (res) != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = as_markup_convert (fwupd_result_get_update_description (res),
					 AS_MARKUP_CONVERT_FORMAT_SIMPLE, NULL);
		if (tmp != NULL)
			gs_app_set_update_details (app, tmp);
	}

	/* the same as we have already */
	if (g_strcmp0 (fwupd_result_get_device_version (res),
		       fwupd_result_get_update_version (res)) == 0) {
		g_warning ("same firmware version as installed");
	}

	return app;
}

/**
 * gs_plugin_add_update_app:
 */
static gboolean
gs_plugin_add_update_app (GsPlugin *plugin,
			  GList **list,
			  FwupdResult *res,
			  GError **error)
{
	FwupdDeviceFlags flags = 0;
	const gchar *update_hash;
	const gchar *update_uri;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *filename_cache = NULL;
	g_autoptr(GsApp) app = NULL;

	/* update unsupported */
	app = gs_plugin_fwupd_new_app_from_results (res);
	flags = fwupd_result_get_device_flags (res);
	if (flags & FU_DEVICE_FLAG_ALLOW_ONLINE) {
		gs_app_set_metadata (app, "fwupd::InstallMethod", "online");
	} else if (flags & FU_DEVICE_FLAG_ALLOW_OFFLINE) {
		gs_app_set_metadata (app, "fwupd::InstallMethod", "offline");
	} else {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s [%s] cannot be updated",
			     gs_app_get_name (app), gs_app_get_id (app));
		return FALSE;
	}

	/* some missing */
	update_hash = fwupd_result_get_update_checksum (res);
	if (gs_app_get_id (app) == NULL) {
		g_warning ("fwupd: No id! for %s!", update_hash);
		return TRUE;
	}
	if (gs_app_get_version (app) == NULL) {
		g_warning ("fwupd: No version! for %s!", gs_app_get_id (app));
		return TRUE;
	}
	if (gs_app_get_update_version (app) == NULL) {
		g_warning ("fwupd: No update-version! for %s!", gs_app_get_id (app));
		return TRUE;
	}

	/* devices that are locked need unlocking */
	if (flags & FU_DEVICE_FLAG_LOCKED) {
		gs_app_set_metadata (app, "fwupd::IsLocked", "");
	} else {
		if (update_hash == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "%s [%s] (%s) has no checksum, ignoring as unsafe",
				     gs_app_get_name (app),
				     gs_app_get_id (app),
				     gs_app_get_update_version (app));
			return FALSE;
		}
		update_uri = fwupd_result_get_update_uri (res);
		if (update_uri == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no location available for %s [%s]",
				     gs_app_get_name (app), gs_app_get_id (app));
			return FALSE;
		}

		/* does the firmware already exist in the cache? */
		basename = g_path_get_basename (update_uri);
		filename_cache = g_build_filename (plugin->priv->cachedir,
						   basename, NULL);
		if (!g_file_test (filename_cache, G_FILE_TEST_EXISTS)) {
			gs_plugin_fwupd_add_required_location (plugin, update_uri);
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "%s does not yet exist, wait patiently",
				     filename_cache);
			return FALSE;
		}

		/* does the checksum match */
		checksum = gs_plugin_fwupd_get_file_checksum (filename_cache,
							      G_CHECKSUM_SHA1,
							      error);
		if (checksum == NULL)
			return FALSE;
		if (g_strcmp0 (update_hash, checksum) != 0) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "%s does not match checksum, expected %s got %s",
				     filename_cache, update_hash, checksum);
			g_unlink (filename_cache);
			return FALSE;
		}
	}

	/* can be done live */
	if (flags & FU_DEVICE_FLAG_ALLOW_ONLINE) {
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
	} else {
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	}

	/* actually add the application */
	gs_app_add_source_id (app, filename_cache);
	gs_plugin_add_app (list, app);

	return TRUE;
}

/**
 * gs_plugin_add_updates_historical:
 */
gboolean
gs_plugin_add_updates_historical (GsPlugin *plugin,
				  GList **list,
				  GCancellable *cancellable,
				  GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(FwupdResult) res = NULL;

	/* set up plugin */
	if (!gs_plugin_startup (plugin, cancellable, error))
		return FALSE;

	/* get historical updates */
	res = fwupd_client_get_results (plugin->priv->client,
					FWUPD_DEVICE_ID_ANY,
					cancellable,
					&error_local);
	if (res == NULL) {
		if (g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO))
			return TRUE;
		if (g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND))
			return TRUE;
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     error_local->message);
		return FALSE;
	}

	/* parse */
	app = gs_plugin_fwupd_new_app_from_results (res);
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	gs_plugin_add_app (list, app);
	return TRUE;
}

/**
 * gs_plugin_add_updates:
 */
gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	guint i;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) results = NULL;

	/* set up plugin */
	if (!gs_plugin_startup (plugin, cancellable, error))
		return FALSE;

	/* get current list of updates */
	results = fwupd_client_get_updates (plugin->priv->client,
					    cancellable, &error_local);
	if (results == NULL) {
		if (g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO))
			return TRUE;
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     error_local->message);
		return FALSE;
	}

	/* parse */
	for (i = 0; i < results->len; i++) {
		FwupdResult *res = g_ptr_array_index (results, i);
		g_autoptr(GError) error_local2 = NULL;
		if (!gs_plugin_add_update_app (plugin, list, res, &error_local2))
			g_debug ("%s", error_local2->message);
	}

	return TRUE;
}

/**
 * gs_plugin_fwupd_check_lvfs_metadata:
 */
static gboolean
gs_plugin_fwupd_check_lvfs_metadata (GsPlugin *plugin,
				     guint cache_age,
				     GCancellable *cancellable,
				     GError **error)
{
	guint status_code;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *basename_data = NULL;
	g_autofree gchar *cache_fn_data = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *url_data = NULL;
	g_autofree gchar *url_sig = NULL;
	g_autoptr(GKeyFile) config = NULL;
	g_autoptr(SoupMessage) msg_data = NULL;
	g_autoptr(SoupMessage) msg_sig = NULL;

	/* read config file */
	config = g_key_file_new ();
	if (!g_key_file_load_from_file (config, plugin->priv->config_fn, G_KEY_FILE_NONE, error))
		return FALSE;

	/* check cache age */
	if (cache_age > 0) {
		guint tmp;
		g_autoptr(GFile) file = NULL;
		file = g_file_new_for_path (plugin->priv->lvfs_sig_fn);
		tmp = gs_utils_get_file_age (file);
		if (tmp < cache_age) {
			g_debug ("%s is only %i seconds old, so ignoring refresh",
				 plugin->priv->lvfs_sig_fn, tmp);
			return TRUE;
		}
	}

	/* download the signature */
	url_data = g_key_file_get_string (config, "fwupd", "DownloadURI", error);
	if (url_data == NULL)
		return FALSE;

	/* download the signature first, it's smaller */
	url_sig = g_strdup_printf ("%s.asc", url_data);
	msg_sig = soup_message_new (SOUP_METHOD_GET, url_sig);
	status_code = soup_session_send_message (plugin->soup_session, msg_sig);
	if (status_code != SOUP_STATUS_OK) {
		g_warning ("Failed to download %s, ignoring: %s",
			   url_sig, soup_status_get_phrase (status_code));
		return TRUE;
	}

	/* is the signature hash the same as we had before? */
	checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA1,
						(const guchar *) msg_sig->response_body->data,
						msg_sig->response_body->length);
	if (g_strcmp0 (checksum, plugin->priv->lvfs_sig_hash) == 0) {
		g_debug ("signature of %s is unchanged", url_sig);
		return TRUE;
	}

	/* save to a file */
	g_debug ("saving new LVFS signature to %s:", plugin->priv->lvfs_sig_fn);
	if (!g_file_set_contents (plugin->priv->lvfs_sig_fn,
				  msg_sig->response_body->data,
				  msg_sig->response_body->length,
				  &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to save firmware: %s",
			     error_local->message);
		return FALSE;
	}

	/* save the new checksum so we don't downoad the payload unless it's changed */
	g_free (plugin->priv->lvfs_sig_hash);
	plugin->priv->lvfs_sig_hash = g_strdup (checksum);

	/* download the payload */
	msg_data = soup_message_new (SOUP_METHOD_GET, url_data);
	status_code = soup_session_send_message (plugin->soup_session, msg_data);
	if (status_code != SOUP_STATUS_OK) {
		g_warning ("Failed to download %s, ignoring: %s",
			   url_data, soup_status_get_phrase (status_code));
		return TRUE;
	}

	/* save to a file */
	basename_data = g_path_get_basename (url_data);
	cache_fn_data = g_build_filename (plugin->priv->cachedir, basename_data, NULL);
	g_debug ("saving new LVFS data to %s:", cache_fn_data);
	if (!g_file_set_contents (cache_fn_data,
				  msg_data->response_body->data,
				  msg_data->response_body->length,
				  &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to save firmware: %s",
			     error_local->message);
		return FALSE;
	}

	/* phew, lets send all this to fwupd */
	if (!fwupd_client_update_metadata (plugin->priv->client,
					   cache_fn_data,
					   plugin->priv->lvfs_sig_fn,
					   cancellable,
					   error))
		return FALSE;

	return TRUE;
}

/**
 * gs_plugin_refresh:
 */
gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	const gchar *tmp;
	guint i;

	/* set up plugin */
	if (!gs_plugin_startup (plugin, cancellable, error))
		return FALSE;

	/* get the metadata and signature file */
	if (!gs_plugin_fwupd_check_lvfs_metadata (plugin, cache_age, cancellable, error))
		return FALSE;

	/* download the files to the cachedir */
	for (i = 0; i < plugin->priv->to_download->len; i++) {
		guint status_code;
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *basename = NULL;
		g_autofree gchar *filename_cache = NULL;
		g_autoptr(SoupMessage) msg = NULL;

		tmp = g_ptr_array_index (plugin->priv->to_download, i);
		basename = g_path_get_basename (tmp);
		filename_cache = g_build_filename (plugin->priv->cachedir, basename, NULL);
		g_debug ("downloading %s to %s", tmp, filename_cache);

		/* set sync request */
		msg = soup_message_new (SOUP_METHOD_GET, tmp);
		status_code = soup_session_send_message (plugin->soup_session, msg);
		if (status_code != SOUP_STATUS_OK) {
			g_warning ("Failed to download %s, ignoring: %s",
				   tmp, soup_status_get_phrase (status_code));
			g_ptr_array_remove_index (plugin->priv->to_download, i--);
			g_ptr_array_add (plugin->priv->to_ignore, g_strdup (tmp));
			continue;
		}

		/* save binary file */
		if (!g_file_set_contents (filename_cache,
					  msg->response_body->data,
					  msg->response_body->length,
					  &error_local)) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "Failed to save firmware: %s",
				     error_local->message);
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * gs_plugin_app_upgrade:
 */
static gboolean
gs_plugin_app_upgrade (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	const gchar *device_id;
	const gchar *filename;

	filename = gs_app_get_source_id_default (app);
	device_id = gs_app_get_metadata_item (app, "fwupd::DeviceID");
	if (filename == NULL || device_id == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "not enough data for fwupd %s:%s",
			     filename, device_id);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!fwupd_client_install (plugin->priv->client, device_id, filename,
				   FWUPD_INSTALL_FLAG_OFFLINE, cancellable, error))
		return FALSE;
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

/**
 * gs_plugin_offline_update:
 */
gboolean
gs_plugin_offline_update (GsPlugin *plugin,
                          GList *apps,
                          GCancellable *cancellable,
                          GError **error)
{
	GList *l;

	for (l = apps; l != NULL; l = l->next) {
		gboolean ret = gs_plugin_app_upgrade (plugin, GS_APP (l->data), cancellable, error);
		if (!ret)
			return FALSE;
	}

	return TRUE;
}

/**
 * gs_plugin_fwupd_install:
 */
static gboolean
gs_plugin_fwupd_install (GsPlugin *plugin,
			 GsApp *app,
			 GCancellable *cancellable,
			 GError **error)
{
	const gchar *install_method;
	const gchar *filename;
	FwupdInstallFlags install_flags = 0;

	filename = gs_app_get_source_id_default (app);
	if (filename == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "not enough data for fwupd %s",
			     filename);
		return FALSE;
	}

	/* only offline supported */
	install_method = gs_app_get_metadata_item (app, "fwupd::InstallMethod");
	if (g_strcmp0 (install_method, "offline") == 0)
		install_flags |= FWUPD_INSTALL_FLAG_OFFLINE;

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!fwupd_client_install (plugin->priv->client, FWUPD_DEVICE_ID_ANY,
				   filename, install_flags, cancellable, error))
		return FALSE;
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

/**
 * gs_plugin_app_install:
 *
 * Called when a user double clicks on a .cab file
 */
gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	return gs_plugin_fwupd_install (plugin, app, cancellable, error);
}

/**
 * gs_plugin_app_update:
 *
 * This is only called when updating device firmware live.
 */
gboolean
gs_plugin_app_update (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	/* set up plugin */
	if (!gs_plugin_startup (plugin, cancellable, error))
		return FALSE;

	/* locked devices need unlocking, rather than installing */
	if (gs_app_get_metadata_item (app, "fwupd::IsLocked") != NULL) {
		const gchar *device_id;
		device_id = gs_app_get_metadata_item (app, "fwupd::DeviceID");
		if (device_id == NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "not enough data for fwupd unlock");
			return FALSE;
		}
		return fwupd_client_unlock (plugin->priv->client, device_id,
					    cancellable, error);
	}

	return gs_plugin_fwupd_install (plugin, app, cancellable, error);
}

/**
 * gs_plugin_fwupd_content_type_matches:
 */
static gboolean
gs_plugin_fwupd_content_type_matches (const gchar *filename,
				      gboolean *matches,
				      GCancellable *cancellable,
				      GError **error)
{
	const gchar *tmp;
	guint i;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;
	const gchar *mimetypes[] = {
		"application/vnd.ms-cab-compressed",
		NULL };

	/* get content type */
	file = g_file_new_for_path (filename);
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  G_FILE_QUERY_INFO_NONE,
				  cancellable,
				  error);
	if (info == NULL)
		return FALSE;
	tmp = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);

	/* match any */
	*matches = FALSE;
	for (i = 0; mimetypes[i] != NULL; i++) {
		if (g_strcmp0 (tmp, mimetypes[i]) == 0) {
			*matches = TRUE;
			break;
		}
	}
	return TRUE;
}

/**
 * gs_plugin_filename_to_app:
 */
gboolean
gs_plugin_filename_to_app (GsPlugin *plugin,
			   GList **list,
			   const gchar *filename,
			   GCancellable *cancellable,
			   GError **error)
{
	FwupdDeviceFlags flags;
	gboolean supported;
	g_autoptr(FwupdResult) res = NULL;
	g_autoptr(GsApp) app = NULL;

	/* does this match any of the mimetypes we support */
	if (!gs_plugin_fwupd_content_type_matches (filename,
						   &supported,
						   cancellable,
						   error))
		return FALSE;
	if (!supported)
		return TRUE;

	/* get results */
	res = fwupd_client_get_details (plugin->priv->client,
					filename,
					cancellable,
					error);
	if (res == NULL)
		return FALSE;
	app = gs_plugin_fwupd_new_app_from_results (res);
	gs_app_add_source_id (app, filename);

	/* we have no update view for local files */
	gs_app_set_version (app, gs_app_get_update_version (app));
	gs_app_set_description (app, GS_APP_QUALITY_NORMAL,
				gs_app_get_update_details (app));

	/* can we install on-line, off-line, or not at all */
	flags = fwupd_result_get_device_flags (res);
	if (flags & FU_DEVICE_FLAG_ALLOW_ONLINE) {
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
		gs_app_set_metadata (app, "fwupd::InstallMethod", "online");
	} else if (flags & FU_DEVICE_FLAG_ALLOW_OFFLINE) {
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
		gs_app_set_metadata (app, "fwupd::InstallMethod", "offline");
	} else {
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
	}
	gs_plugin_add_app (list, app);
	return TRUE;
}
