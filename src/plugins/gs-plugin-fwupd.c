/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#include <gnome-software.h>

/*
 * SECTION:
 * Queries for new firmware and schedules it to be installed as required.
 *
 * This plugin calls UpdatesChanged() if any updatable devices are
 * added or removed or if a device has been updated live.
 */

struct GsPluginData {
	FwupdClient		*client;
	GPtrArray		*to_download;
	GPtrArray		*to_ignore;
	GsApp			*app_current;
	gchar			*lvfs_sig_fn;
	gchar			*lvfs_sig_hash;
	gchar			*config_fn;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->client = fwupd_client_new ();
	priv->to_download = g_ptr_array_new_with_free_func (g_free);
	priv->to_ignore = g_ptr_array_new_with_free_func (g_free);
	priv->config_fn = g_build_filename (SYSCONFDIR, "fwupd.conf", NULL);
	if (!g_file_test (priv->config_fn, G_FILE_TEST_EXISTS)) {
		g_free (priv->config_fn);
		priv->config_fn = g_strdup ("/etc/fwupd.conf");
	}
	if (!g_file_test (priv->config_fn, G_FILE_TEST_EXISTS)) {
		g_debug ("fwupd configuration not found, disabling plugin.");
		gs_plugin_set_enabled (plugin, FALSE);
	}
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_free (priv->lvfs_sig_fn);
	g_free (priv->lvfs_sig_hash);
	g_free (priv->config_fn);
	g_object_unref (priv->client);
	g_ptr_array_unref (priv->to_download);
	g_ptr_array_unref (priv->to_ignore);
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_kind (app) == AS_APP_KIND_FIRMWARE)
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
}

static void
gs_plugin_fwupd_changed_cb (FwupdClient *client, GsPlugin *plugin)
{
#if !FWUPD_CHECK_VERSION(0,7,1)
	/* fwupd < 0.7.1 only supported the ::Changed() signal */
	gs_plugin_updates_changed (plugin);
#endif
}

#if FWUPD_CHECK_VERSION(0,7,1)
static void
gs_plugin_fwupd_device_changed_cb (FwupdClient *client,
				   FwupdResult *device,
				   GsPlugin *plugin)
{
	/* fwupd >= 0.7.1 supports per-device signals, and also the
	 * SUPPORTED flag -- so we can limit number of UI refreshes */
	if (!fwupd_result_has_device_flag (device, FU_DEVICE_FLAG_SUPPORTED)) {
		g_debug ("%s changed (not supported) so ignoring",
			 fwupd_result_get_device_id (device));
		return;
	}

	/* If the flag is set the device matches something in the
	 * metadata as therefor is worth refreshing the update list */
	g_debug ("%s changed (supported) so reloading",
		 fwupd_result_get_device_id (device));
	gs_plugin_updates_changed (plugin);
}
#endif

#if FWUPD_CHECK_VERSION(0,7,3)
static void
gs_plugin_fwupd_notify_percentage_cb (GObject *object,
				      GParamSpec *pspec,
				      GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* nothing in progress */
	if (priv->app_current == NULL) {
		g_debug ("fwupd percentage: %u%%",
			 fwupd_client_get_percentage (priv->client));
		return;
	}
	g_debug ("fwupd percentage for %s: %u%%",
		 gs_app_get_unique_id (priv->app_current),
		 fwupd_client_get_percentage (priv->client));
	gs_app_set_progress (priv->app_current,
			     fwupd_client_get_percentage (priv->client));
}

static void
gs_plugin_fwupd_notify_status_cb (GObject *object,
				  GParamSpec *pspec,
				  GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* nothing in progress */
	if (priv->app_current == NULL) {
		g_debug ("fwupd status: %s",
			 fwupd_status_to_string (fwupd_client_get_status (priv->client)));
		return;
	}

	g_debug ("fwupd status for %s: %s",
		 gs_app_get_unique_id (priv->app_current),
		 fwupd_status_to_string (fwupd_client_get_status (priv->client)));
	switch (fwupd_client_get_status (priv->client)) {
	case FWUPD_STATUS_DECOMPRESSING:
	case FWUPD_STATUS_DEVICE_RESTART:
	case FWUPD_STATUS_DEVICE_WRITE:
	case FWUPD_STATUS_DEVICE_VERIFY:
		gs_app_set_state (priv->app_current, AS_APP_STATE_INSTALLING);
		break;
	case FWUPD_STATUS_IDLE:
		g_clear_object (&priv->app_current);
		break;
	default:
		break;
	}
}
#endif

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	gsize len;
	g_autofree gchar *data = NULL;

	/* register D-Bus errors */
	fwupd_error_quark ();
	g_signal_connect (priv->client, "changed",
			  G_CALLBACK (gs_plugin_fwupd_changed_cb), plugin);
#if FWUPD_CHECK_VERSION(0,7,1)
	g_signal_connect (priv->client, "device-added",
			  G_CALLBACK (gs_plugin_fwupd_device_changed_cb), plugin);
	g_signal_connect (priv->client, "device-removed",
			  G_CALLBACK (gs_plugin_fwupd_device_changed_cb), plugin);
	g_signal_connect (priv->client, "device-changed",
			  G_CALLBACK (gs_plugin_fwupd_device_changed_cb), plugin);
#endif
#if FWUPD_CHECK_VERSION(0,7,3)
	g_signal_connect (priv->client, "notify::percentage",
			  G_CALLBACK (gs_plugin_fwupd_notify_percentage_cb), plugin);
	g_signal_connect (priv->client, "notify::status",
			  G_CALLBACK (gs_plugin_fwupd_notify_status_cb), plugin);
#endif

	/* get the hash of the previously downloaded file */
	priv->lvfs_sig_fn = gs_utils_get_cache_filename ("firmware",
							 "firmware.xml.gz.asc",
							 GS_UTILS_CACHE_FLAG_WRITEABLE,
							 error);
	if (priv->lvfs_sig_fn == NULL)
		return FALSE;
	if (g_file_test (priv->lvfs_sig_fn, G_FILE_TEST_EXISTS)) {
		if (!g_file_get_contents (priv->lvfs_sig_fn,
					  &data, &len, error))
			return FALSE;
		priv->lvfs_sig_hash =
			g_compute_checksum_for_data (G_CHECKSUM_SHA1, (guchar *) data, len);
	}

	return TRUE;
}

static void
gs_plugin_fwupd_add_required_location (GsPlugin *plugin, const gchar *location)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *tmp;
	guint i;
	for (i = 0; i < priv->to_ignore->len; i++) {
		tmp = g_ptr_array_index (priv->to_ignore, i);
		if (g_strcmp0 (tmp, location) == 0)
			return;
	}
	for (i = 0; i < priv->to_download->len; i++) {
		tmp = g_ptr_array_index (priv->to_download, i);
		if (g_strcmp0 (tmp, location) == 0)
			return;
	}
	g_ptr_array_add (priv->to_download, g_strdup (location));
}

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

static GsApp *
gs_plugin_fwupd_new_app_from_results (GsPlugin *plugin, FwupdResult *res)
{
	FwupdDeviceFlags flags;
	GsApp *app;
#if FWUPD_CHECK_VERSION(0,7,2)
	GPtrArray *guids;
#endif
	const gchar *id;
	g_autoptr(AsIcon) icon = NULL;

	/* get from cache */
#if FWUPD_CHECK_VERSION(0,7,3)
	id = fwupd_result_get_unique_id (res);
#else
	id = fwupd_result_get_update_id (res);
#endif
	app = gs_plugin_cache_lookup (plugin, id);
	if (app == NULL) {
		app = gs_app_new (id);
		gs_plugin_cache_add (plugin, id, app);
	}

	/* default stuff */
	gs_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
	gs_app_set_management_plugin (app, "fwupd");
	gs_app_add_category (app, "System");
	gs_app_set_metadata (app, "fwupd::DeviceID",
			     fwupd_result_get_device_id (res));

	/* something can be done */
	flags = fwupd_result_get_device_flags (res);
	if (flags & FU_DEVICE_FLAG_ALLOW_ONLINE ||
	    flags & FU_DEVICE_FLAG_ALLOW_OFFLINE)
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);

	/* can be done live */
	if ((flags & FU_DEVICE_FLAG_ALLOW_ONLINE) == 0)
		gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);

	/* create icon */
	icon = as_icon_new ();
	as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
	as_icon_set_name (icon, "application-x-firmware");
	gs_app_add_icon (app, icon);

	if (fwupd_result_get_update_id (res) != NULL) {
		gs_app_set_id (app, fwupd_result_get_update_id (res));
	}

#if FWUPD_CHECK_VERSION(0,7,2)
	guids = fwupd_result_get_guids (res);
	if (guids->len > 0) {
		guint i;
		g_autofree gchar *guid_str = NULL;
		g_auto(GStrv) tmp = g_new0 (gchar *, guids->len + 1);
		for (i = 0; i < guids->len; i++)
			tmp[i] = g_strdup (g_ptr_array_index (guids, i));
		guid_str = g_strjoinv (",", tmp);
		gs_app_set_metadata (app, "fwupd::Guid", guid_str);
	}
#else
	if (fwupd_result_get_guid (res) != NULL) {
		gs_app_set_metadata (app, "fwupd::Guid",
				     fwupd_result_get_guid (res));
	}
#endif
	if (fwupd_result_get_update_name (res) != NULL) {
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
				 fwupd_result_get_update_name (res));
	}
	if (fwupd_result_get_update_summary (res) != NULL) {
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
				    fwupd_result_get_update_summary (res));
	}
	if (fwupd_result_get_update_homepage (res) != NULL) {
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
				fwupd_result_get_update_homepage (res));
	}
	if (fwupd_result_get_device_version (res) != NULL) {
		gs_app_set_version (app, fwupd_result_get_device_version (res));
	}
	if (fwupd_result_get_update_size (res) != 0) {
		gs_app_set_size_installed (app, 0);
		gs_app_set_size_download (app, fwupd_result_get_update_size (res));
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
	if (fwupd_result_get_update_uri (res) != NULL) {
		gs_app_set_origin_hostname (app,
					    fwupd_result_get_update_uri (res));
	}
	if (fwupd_result_get_device_description (res) != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = as_markup_convert (fwupd_result_get_device_description (res),
					 AS_MARKUP_CONVERT_FORMAT_SIMPLE, NULL);
		if (tmp != NULL)
			gs_app_set_description (app, GS_APP_QUALITY_NORMAL, tmp);
	}
	if (fwupd_result_get_update_description (res) != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = as_markup_convert (fwupd_result_get_update_description (res),
					 AS_MARKUP_CONVERT_FORMAT_SIMPLE, NULL);
		if (tmp != NULL)
			gs_app_set_update_details (app, tmp);
	}

#if FWUPD_CHECK_VERSION(0,7,3)
	/* needs action */
	if (fwupd_result_has_device_flag (res, FU_DEVICE_FLAG_NEEDS_BOOTLOADER))
		gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_USER_ACTION);
	else
		gs_app_remove_quirk (app, AS_APP_QUIRK_NEEDS_USER_ACTION);
#endif

	/* the same as we have already */
	if (g_strcmp0 (fwupd_result_get_device_version (res),
		       fwupd_result_get_update_version (res)) == 0) {
		g_warning ("same firmware version as installed");
	}

	return app;
}

static gboolean
gs_plugin_add_update_app (GsPlugin *plugin,
			  GsAppList *list,
			  FwupdResult *res,
			  GError **error)
{
	FwupdDeviceFlags flags = 0;
	const gchar *update_hash;
	const gchar *update_uri;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *filename_cache = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app = NULL;

	/* update unsupported */
	app = gs_plugin_fwupd_new_app_from_results (plugin, res);
	if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE_LIVE) {
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
	flags = fwupd_result_get_device_flags (res);
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
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no location available for %s [%s]",
				     gs_app_get_name (app), gs_app_get_id (app));
			return FALSE;
		}

		/* does the firmware already exist in the cache? */
		basename = g_path_get_basename (update_uri);
		filename_cache = gs_utils_get_cache_filename ("firmware",
							      basename,
							      GS_UTILS_CACHE_FLAG_NONE,
							      error);
		if (filename_cache == NULL)
			return FALSE;
		if (!g_file_test (filename_cache, G_FILE_TEST_EXISTS)) {
			gs_plugin_fwupd_add_required_location (plugin, update_uri);
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
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
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "%s does not match checksum, expected %s got %s",
				     filename_cache, update_hash, checksum);
			g_unlink (filename_cache);
			return FALSE;
		}
	}

	/* actually add the application */
	file = g_file_new_for_path (filename_cache);
	gs_app_set_local_file (app, file);
	gs_app_list_add (list, app);

	return TRUE;
}

gboolean
gs_plugin_add_updates_historical (GsPlugin *plugin,
				  GsAppList *list,
				  GCancellable *cancellable,
				  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(FwupdResult) res = NULL;

	/* get historical updates */
	res = fwupd_client_get_results (priv->client,
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
	app = gs_plugin_fwupd_new_app_from_results (plugin, res);
	gs_app_list_add (list, app);
	return TRUE;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	guint i;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) results = NULL;

	/* get current list of updates */
	results = fwupd_client_get_updates (priv->client,
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

static gboolean
gs_plugin_fwupd_check_lvfs_metadata (GsPlugin *plugin,
				     guint cache_age,
				     GCancellable *cancellable,
				     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *basename_data = NULL;
	g_autofree gchar *cache_fn_data = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *url_data = NULL;
	g_autofree gchar *url_sig = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GKeyFile) config = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

	/* read config file */
	config = g_key_file_new ();
	if (!g_key_file_load_from_file (config, priv->config_fn, G_KEY_FILE_NONE, error))
		return FALSE;

	/* check cache age */
	if (cache_age > 0) {
		guint tmp;
		g_autoptr(GFile) file = NULL;
		file = g_file_new_for_path (priv->lvfs_sig_fn);
		tmp = gs_utils_get_file_age (file);
		if (tmp < cache_age) {
			g_debug ("%s is only %u seconds old, so ignoring refresh",
				 priv->lvfs_sig_fn, tmp);
			return TRUE;
		}
	}

	/* download the signature */
	url_data = g_key_file_get_string (config, "fwupd", "DownloadURI", error);
	if (url_data == NULL)
		return FALSE;

	/* download the signature first, it's smaller */
	url_sig = g_strdup_printf ("%s.asc", url_data);
	data = gs_plugin_download_data (plugin,
					app_dl,
					url_sig,
					cancellable,
					error);
	if (data == NULL)
		return FALSE;

	/* is the signature hash the same as we had before? */
	checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA1,
						(const guchar *) g_bytes_get_data (data, NULL),
						g_bytes_get_size (data));
	if (g_strcmp0 (checksum, priv->lvfs_sig_hash) == 0) {
		g_debug ("signature of %s is unchanged", url_sig);
		return TRUE;
	}

	/* save to a file */
	g_debug ("saving new LVFS signature to %s:", priv->lvfs_sig_fn);
	if (!g_file_set_contents (priv->lvfs_sig_fn,
				  g_bytes_get_data (data, NULL),
				  (guint) g_bytes_get_size (data),
				  &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_WRITE_FAILED,
			     "Failed to save firmware: %s",
			     error_local->message);
		return FALSE;
	}

	/* save the new checksum so we don't downoad the payload unless it's changed */
	g_free (priv->lvfs_sig_hash);
	priv->lvfs_sig_hash = g_strdup (checksum);

	/* download the payload and save to file */
	basename_data = g_path_get_basename (url_data);
	cache_fn_data = gs_utils_get_cache_filename ("firmware",
						     basename_data,
						     GS_UTILS_CACHE_FLAG_WRITEABLE,
						     error);
	if (cache_fn_data == NULL)
		return FALSE;
	g_debug ("saving new LVFS data to %s:", cache_fn_data);
	if (!gs_plugin_download_file (plugin,
				      app_dl,
				      url_data,
				      cache_fn_data,
				      cancellable,
				      error))
		return FALSE;

	/* phew, lets send all this to fwupd */
	if (!fwupd_client_update_metadata (priv->client,
					   cache_fn_data,
					   priv->lvfs_sig_fn,
					   cancellable,
					   error))
		return FALSE;

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
	const gchar *tmp;
	guint i;

	/* get the metadata and signature file */
	if (flags & GS_PLUGIN_REFRESH_FLAGS_METADATA) {
		if (!gs_plugin_fwupd_check_lvfs_metadata (plugin,
							  cache_age,
							  cancellable,
							  error))
			return FALSE;
	}

	/* no longer interesting */
	if ((flags & GS_PLUGIN_REFRESH_FLAGS_PAYLOAD) == 0)
		return TRUE;

	/* download the files to the cachedir */
	for (i = 0; i < priv->to_download->len; i++) {
		guint status_code;
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *basename = NULL;
		g_autofree gchar *filename_cache = NULL;
		g_autoptr(SoupMessage) msg = NULL;

		tmp = g_ptr_array_index (priv->to_download, i);
		basename = g_path_get_basename (tmp);
		filename_cache = gs_utils_get_cache_filename ("firmware",
							      basename,
							      GS_UTILS_CACHE_FLAG_WRITEABLE,
							      error);
		if (filename_cache == NULL)
			return FALSE;
		g_debug ("downloading %s to %s", tmp, filename_cache);

		/* set sync request */
		msg = soup_message_new (SOUP_METHOD_GET, tmp);
		status_code = soup_session_send_message (gs_plugin_get_soup_session (plugin), msg);
		if (status_code != SOUP_STATUS_OK) {
			g_warning ("Failed to download %s, ignoring: %s",
				   tmp, soup_status_get_phrase (status_code));
			g_ptr_array_remove_index (priv->to_download, i--);
			g_ptr_array_add (priv->to_ignore, g_strdup (tmp));
			continue;
		}

		/* save binary file */
		if (!g_file_set_contents (filename_cache,
					  msg->response_body->data,
					  msg->response_body->length,
					  &error_local)) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_WRITE_FAILED,
				     "Failed to save firmware: %s",
				     error_local->message);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
gs_plugin_fwupd_install (GsPlugin *plugin,
			 GsApp *app,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *device_id;
	FwupdInstallFlags install_flags = 0;
	g_autofree gchar *filename = NULL;

	/* not set */
	if (gs_app_get_local_file (app) == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "not enough data for fwupd %s",
			     filename);
		return FALSE;
	}
	filename = g_file_get_path (gs_app_get_local_file (app));

	/* limit to single device? */
	device_id = gs_app_get_metadata_item (app, "fwupd::DeviceID");
	if (device_id == NULL)
		device_id = FWUPD_DEVICE_ID_ANY;

	/* set the last object */
	g_set_object (&priv->app_current, app);

	/* only offline supported */
	if (gs_app_has_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT))
		install_flags |= FWUPD_INSTALL_FLAG_OFFLINE;

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!fwupd_client_install (priv->client, device_id,
				   filename, install_flags,
				   cancellable, error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	return gs_plugin_fwupd_install (plugin, app, cancellable, error);
}

gboolean
gs_plugin_update_app (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* locked devices need unlocking, rather than installing */
	if (gs_app_get_metadata_item (app, "fwupd::IsLocked") != NULL) {
		const gchar *device_id;
		device_id = gs_app_get_metadata_item (app, "fwupd::DeviceID");
		if (device_id == NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_INVALID_FORMAT,
					     "not enough data for fwupd unlock");
			return FALSE;
		}
		return fwupd_client_unlock (priv->client, device_id,
					    cancellable, error);
	}

	return gs_plugin_fwupd_install (plugin, app, cancellable, error);
}

gboolean
gs_plugin_file_to_app (GsPlugin *plugin,
		       GsAppList *list,
		       GFile *file,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *filename = NULL;
#if FWUPD_CHECK_VERSION(0,7,2)
	guint i;
	g_autoptr(GPtrArray) results = NULL;
#else
	g_autoptr(FwupdResult) res = NULL;
	g_autoptr(GsApp) app = NULL;
#endif
	const gchar *mimetypes[] = {
		"application/vnd.ms-cab-compressed",
		NULL };

	/* does this match any of the mimetypes we support */
	content_type = gs_utils_get_content_type (file, cancellable, error);
	if (content_type == NULL)
		return FALSE;
	if (!g_strv_contains (mimetypes, content_type))
		return TRUE;

	/* get results */
	filename = g_file_get_path (file);
#if FWUPD_CHECK_VERSION(0,7,2)
	results = fwupd_client_get_details_local (priv->client,
						  filename,
						  cancellable,
						  error);
	if (results == NULL)
		return FALSE;
	for (i = 0; i < results->len; i++) {
		FwupdResult *res = g_ptr_array_index (results, i);
		g_autoptr(GsApp) app = NULL;

		/* create each app */
		app = gs_plugin_fwupd_new_app_from_results (plugin, res);

		/* we have no update view for local files */
		gs_app_set_version (app, gs_app_get_update_version (app));
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL,
					gs_app_get_update_details (app));
		gs_app_list_add (list, app);
	}
#else
	res = fwupd_client_get_details (priv->client,
					filename,
					cancellable,
					error);
	if (res == NULL)
		return FALSE;
	app = gs_plugin_fwupd_new_app_from_results (plugin, res);

	/* we have no update view for local files */
	gs_app_set_version (app, gs_app_get_update_version (app));
	gs_app_set_description (app, GS_APP_QUALITY_NORMAL,
				gs_app_get_update_details (app));
	gs_app_list_add (list, app);
#endif

	return TRUE;
}
