/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
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
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <gnome-software.h>

#include "gs-fwupd-app.h"

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
	GsApp			*cached_origin;
	GHashTable		*remote_asc_hash;
	gchar			*config_fn;
};

static void
gs_plugin_fwupd_error_convert (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return;

	/* already correct */
	if (error->domain == GS_PLUGIN_ERROR)
		return;

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gio (perror))
		return;

	/* custom to this plugin */
	if (error->domain == FWUPD_ERROR) {
		switch (error->code) {
		case FWUPD_ERROR_ALREADY_PENDING:
		case FWUPD_ERROR_INVALID_FILE:
		case FWUPD_ERROR_NOT_SUPPORTED:
			error->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
			break;
		case FWUPD_ERROR_AUTH_FAILED:
			error->code = GS_PLUGIN_ERROR_AUTH_INVALID;
			break;
		case FWUPD_ERROR_SIGNATURE_INVALID:
			error->code = GS_PLUGIN_ERROR_NO_SECURITY;
			break;
		case FWUPD_ERROR_AC_POWER_REQUIRED:
			error->code = GS_PLUGIN_ERROR_AC_POWER_REQUIRED;
			break;
		default:
			error->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}
	} else {
		g_warning ("can't reliably fixup error from domain %s",
			   g_quark_to_string (error->domain));
		error->code = GS_PLUGIN_ERROR_FAILED;
	}
	error->domain = GS_PLUGIN_ERROR;
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->client = fwupd_client_new ();
	priv->to_download = g_ptr_array_new_with_free_func (g_free);
	priv->to_ignore = g_ptr_array_new_with_free_func (g_free);
	priv->remote_asc_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
						       g_free, g_free);
	priv->config_fn = g_build_filename (SYSCONFDIR, "fwupd.conf", NULL);
	if (!g_file_test (priv->config_fn, G_FILE_TEST_EXISTS)) {
		g_free (priv->config_fn);
		priv->config_fn = g_strdup ("/etc/fwupd.conf");
	}
	if (!g_file_test (priv->config_fn, G_FILE_TEST_EXISTS)) {
		g_debug ("fwupd configuration not found, disabling plugin.");
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	/* unique to us */
	gs_plugin_set_app_gtype (plugin, GS_TYPE_FWUPD_APP);

	/* set name of MetaInfo file */
	gs_plugin_set_appstream_id (plugin, "org.gnome.Software.Plugin.Fwupd");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->cached_origin != NULL)
		g_object_unref (priv->cached_origin);
	g_hash_table_unref (priv->remote_asc_hash);
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
}

static void
gs_plugin_fwupd_device_changed_cb (FwupdClient *client,
				   FwupdResult *res,
				   GsPlugin *plugin)
{
	FwupdDevice *dev = fwupd_result_get_device (res);

	/* fwupd >= 0.7.1 supports per-device signals, and also the
	 * SUPPORTED flag -- so we can limit number of UI refreshes */
	if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED)) {
		g_debug ("%s changed (not supported) so ignoring",
			 fwupd_device_get_id (dev));
		return;
	}

	/* If the flag is set the device matches something in the
	 * metadata as therefor is worth refreshing the update list */
	g_debug ("%s changed (supported) so reloading",
		 fwupd_device_get_id (dev));
	gs_plugin_updates_changed (plugin);
}

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

static gchar *
gs_plugin_fwupd_get_file_checksum (const gchar *filename,
				   GChecksumType checksum_type,
				   GError **error)
{
	gsize len;
	g_autofree gchar *data = NULL;

	if (!g_file_get_contents (filename, &data, &len, error)) {
		gs_utils_error_convert_gio (error);
		return NULL;
	}
	return g_compute_checksum_for_data (checksum_type, (const guchar *)data, len);
}

static gboolean
gs_plugin_fwupd_setup_remote (GsPlugin *plugin, FwupdRemote *remote, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *filename_asc = NULL;

	/* we do not need to refresh local remotes */
	if (fwupd_remote_get_kind (remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
		return TRUE;

	/* find the name of the signature file in the cache */
	filename_asc = gs_utils_get_cache_filename ("firmware",
						    fwupd_remote_get_filename_asc (remote),
						    GS_UTILS_CACHE_FLAG_WRITEABLE,
						    error);
	if (filename_asc == NULL)
		return FALSE;

	/* if it exists, add the hash */
	if (g_file_test (filename_asc, G_FILE_TEST_EXISTS)) {
		g_autofree gchar *hash = NULL;
		hash = gs_plugin_fwupd_get_file_checksum (filename_asc,
							  G_CHECKSUM_SHA1,
							  error);
		if (hash == NULL)
			return FALSE;
		g_hash_table_insert (priv->remote_asc_hash,
				     g_steal_pointer (&filename_asc),
				     g_steal_pointer (&hash));
	}

	return TRUE;
}

static gboolean
gs_plugin_fwupd_setup_remotes (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GPtrArray) remotes = NULL;

	/* find all enabled remotes */
	remotes = fwupd_client_get_remotes (priv->client, cancellable, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (!fwupd_remote_get_enabled (remote))
			continue;
		if (!gs_plugin_fwupd_setup_remote (plugin, remote, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* add source */
	priv->cached_origin = gs_app_new (gs_plugin_get_name (plugin));
	gs_app_set_kind (priv->cached_origin, AS_APP_KIND_SOURCE);
	gs_app_set_bundle_kind (priv->cached_origin, AS_BUNDLE_KIND_CABINET);

	/* add the source to the plugin cache which allows us to match the
	 * unique ID to a GsApp when creating an event */
	gs_plugin_cache_add (plugin,
			     gs_app_get_unique_id (priv->cached_origin),
			     priv->cached_origin);

	/* register D-Bus errors */
	fwupd_error_quark ();
	g_signal_connect (priv->client, "changed",
			  G_CALLBACK (gs_plugin_fwupd_changed_cb), plugin);
	g_signal_connect (priv->client, "device-added",
			  G_CALLBACK (gs_plugin_fwupd_device_changed_cb), plugin);
	g_signal_connect (priv->client, "device-removed",
			  G_CALLBACK (gs_plugin_fwupd_device_changed_cb), plugin);
	g_signal_connect (priv->client, "device-changed",
			  G_CALLBACK (gs_plugin_fwupd_device_changed_cb), plugin);
	g_signal_connect (priv->client, "notify::percentage",
			  G_CALLBACK (gs_plugin_fwupd_notify_percentage_cb), plugin);
	g_signal_connect (priv->client, "notify::status",
			  G_CALLBACK (gs_plugin_fwupd_notify_status_cb), plugin);

	/* get the hashes of the previously downloaded asc files */
	return gs_plugin_fwupd_setup_remotes (plugin, cancellable, error);
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

static GsApp *
gs_plugin_fwupd_new_app_from_results (GsPlugin *plugin, FwupdResult *res)
{
	FwupdDevice *dev = fwupd_result_get_device (res);
	FwupdRelease *rel = fwupd_result_get_release (res);
	GsApp *app;
	const gchar *id;
	g_autoptr(AsIcon) icon = NULL;

	/* get from cache */
	id = fwupd_result_get_unique_id (res);
	app = gs_plugin_cache_lookup (plugin, id);
	if (app == NULL) {
		app = gs_plugin_app_new (plugin, id);
		gs_plugin_cache_add (plugin, id, app);
	}

	/* default stuff */
	gs_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
	gs_app_set_management_plugin (app, "fwupd");
	gs_app_add_category (app, "System");
	gs_fwupd_app_set_device_id (app, fwupd_device_get_id (dev));

	/* create icon */
	icon = as_icon_new ();
	as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
	as_icon_set_name (icon, "application-x-firmware");
	gs_app_add_icon (app, icon);
	gs_fwupd_app_set_from_release (app, rel);
	gs_fwupd_app_set_from_device (app, dev);

	if (fwupd_release_get_appstream_id (rel) != NULL)
		gs_app_set_id (app, fwupd_release_get_appstream_id (rel));

	/* the same as we have already */
	if (g_strcmp0 (fwupd_device_get_version (dev),
		       fwupd_release_get_version (rel)) == 0) {
		g_warning ("same firmware version as installed");
	}

	return app;
}

static gboolean
gs_plugin_add_update_app (GsPlugin *plugin,
			  GsAppList *list,
			  FwupdResult *res,
			  gboolean is_downloaded,
			  GError **error)
{
	FwupdRelease *rel = fwupd_result_get_release (res);
	GPtrArray *checksums;
	const gchar *update_uri;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *filename_cache = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app = NULL;

	/* update unsupported */
	app = gs_plugin_fwupd_new_app_from_results (plugin, res);
	if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE_LIVE) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "%s [%s] cannot be updated",
			     gs_app_get_name (app), gs_app_get_id (app));
		return FALSE;
	}

	/* some missing */
	if (gs_app_get_id (app) == NULL) {
		g_warning ("fwupd: No id for firmware");
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
	checksums = fwupd_release_get_checksums (rel);
	if (checksums->len == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NO_SECURITY,
			     "%s [%s] (%s) has no checksums, ignoring as unsafe",
			     gs_app_get_name (app),
			     gs_app_get_id (app),
			     gs_app_get_update_version (app));
		return FALSE;
	}
	update_uri = fwupd_release_get_uri (rel);
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

	/* delete the file if the checksum does not match */
	if (g_file_test (filename_cache, G_FILE_TEST_EXISTS)) {
		const gchar *checksum_tmp = NULL;
		g_autofree gchar *checksum = NULL;

		/* we can migrate to something better than SHA1 when the LVFS
		 * starts producing metadata with multiple hash types */
		checksum_tmp = fwupd_checksum_get_by_kind (checksums,
							   G_CHECKSUM_SHA1);
		if (checksum_tmp == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "No valid checksum for %s",
				     filename_cache);
		}
		checksum = gs_plugin_fwupd_get_file_checksum (filename_cache,
							      G_CHECKSUM_SHA1,
							      error);
		if (checksum == NULL)
			return FALSE;
		if (g_strcmp0 (checksum_tmp, checksum) != 0) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "%s does not match checksum, expected %s got %s",
				     filename_cache, checksum_tmp, checksum);
			g_unlink (filename_cache);
			return FALSE;
		}
	}

	/* already downloaded, so overwrite */
	if (g_file_test (filename_cache, G_FILE_TEST_EXISTS))
		gs_app_set_size_download (app, 0);

	/* only return things in the right state */
	if (is_downloaded != g_file_test (filename_cache, G_FILE_TEST_EXISTS)) {
		g_debug ("%s does not exist for %s, ignoring",
			 filename_cache,
			 gs_app_get_unique_id (app));
		gs_plugin_fwupd_add_required_location (plugin, update_uri);
		return TRUE;
	}

	/* actually add the application */
	file = g_file_new_for_path (filename_cache);
	gs_app_set_local_file (app, file);
	gs_app_list_add (list, app);

	/* schedule for download */
	if (!g_file_test (filename_cache, G_FILE_TEST_EXISTS))
		gs_plugin_fwupd_add_required_location (plugin, update_uri);

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
		g_propagate_error (error, error_local);
		error_local = NULL;
		gs_plugin_fwupd_error_convert (error);
		return FALSE;
	}

	/* parse */
	app = gs_plugin_fwupd_new_app_from_results (plugin, res);
	gs_app_list_add (list, app);
	return TRUE;
}

static gboolean
gs_plugin_fwupd_add_updates (GsPlugin *plugin,
			     GsAppList *list,
			     gboolean is_downloaded,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
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
		g_propagate_error (error, error_local);
		error_local = NULL;
		gs_plugin_fwupd_error_convert (error);
		return FALSE;
	}

	/* parse */
	for (guint i = 0; i < results->len; i++) {
		FwupdResult *res = g_ptr_array_index (results, i);
		FwupdDevice *dev = fwupd_result_get_device (res);
		g_autoptr(GError) error_local2 = NULL;

		/* locked device that needs unlocking */
		if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_LOCKED)) {
			g_autoptr(GsApp) app = NULL;
			if (!is_downloaded)
				continue;
			app = gs_plugin_fwupd_new_app_from_results (plugin, res);
			gs_fwupd_app_set_is_locked (app, TRUE);
			gs_app_list_add (list, app);
			continue;
		}

		/* normal device update */
		if (!gs_plugin_add_update_app (plugin, list, res,
					       is_downloaded, &error_local2))
			g_debug ("%s", error_local2->message);
	}

	return TRUE;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	return gs_plugin_fwupd_add_updates (plugin, list, TRUE, cancellable, error);
}

gboolean
gs_plugin_add_updates_pending (GsPlugin *plugin,
			       GsAppList *list,
			       GCancellable *cancellable,
			       GError **error)
{
	return gs_plugin_fwupd_add_updates (plugin, list, FALSE, cancellable, error);
}

static gboolean
gs_plugin_fwupd_refresh_remote (GsPlugin *plugin,
				FwupdRemote *remote,
				guint cache_age,
				GCancellable *cancellable,
				GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *checksum_old;
	const gchar *url_asc = NULL;
	const gchar *url = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *filename_asc = NULL;
	g_autofree gchar *checksum = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

	/* sanity check */
	if (fwupd_remote_get_filename_asc (remote) == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "remote %s has no filename signature",
			     fwupd_remote_get_id (remote));
		return FALSE;
	}

	/* check cache age */
	filename_asc = gs_utils_get_cache_filename ("firmware",
						    fwupd_remote_get_filename_asc (remote),
						    GS_UTILS_CACHE_FLAG_WRITEABLE,
						    error);
	if (cache_age > 0) {
		guint64 age = fwupd_remote_get_age (remote);
		guint tmp = age < G_MAXUINT ? (guint) age : G_MAXUINT;
		if (tmp < cache_age) {
			g_debug ("%s is only %u seconds old, so ignoring refresh",
				 filename_asc, tmp);
			return TRUE;
		}
	}

	/* download the signature first, it's smaller */
	url_asc = fwupd_remote_get_metadata_uri_sig (remote);
	gs_app_set_summary_missing (app_dl,
				    /* TRANSLATORS: status text when downloading */
				    _("Downloading firmware update signature…"));
	data = gs_plugin_download_data (plugin, app_dl, url_asc, cancellable, error);
	if (data == NULL) {
		gs_utils_error_add_unique_id (error, priv->cached_origin);
		return FALSE;
	}

	/* is the signature hash the same as we had before? */
	checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA1,
						(const guchar *) g_bytes_get_data (data, NULL),
						g_bytes_get_size (data));
	checksum_old = g_hash_table_lookup (priv->remote_asc_hash, filename_asc);
	if (g_strcmp0 (checksum, checksum_old) == 0) {
		g_debug ("signature of %s is unchanged", url_asc);
		return TRUE;
	}

	/* save to a file */
	g_debug ("saving new remote signature to %s:", filename_asc);
	if (!g_file_set_contents (filename_asc,
				  g_bytes_get_data (data, NULL),
				  (guint) g_bytes_get_size (data),
				  &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_WRITE_FAILED,
			     "Failed to save firmware signature: %s",
			     error_local->message);
		return FALSE;
	}

	/* save the new checksum so we don't downoad the payload unless it's changed */
	g_hash_table_insert (priv->remote_asc_hash,
			     g_strdup (filename_asc),
			     g_steal_pointer (&checksum));

	/* download the payload and save to file */
	filename = gs_utils_get_cache_filename ("firmware",
						fwupd_remote_get_filename (remote),
						GS_UTILS_CACHE_FLAG_WRITEABLE,
						error);
	if (filename == NULL)
		return FALSE;
	g_debug ("saving new firmware metadata to %s:", filename);
	gs_app_set_summary_missing (app_dl,
				    /* TRANSLATORS: status text when downloading */
				    _("Downloading firmware update metadata…"));
	url = fwupd_remote_get_metadata_uri (remote);
	if (!gs_plugin_download_file (plugin, app_dl, url, filename,
				      cancellable, error)) {
		gs_utils_error_add_unique_id (error, priv->cached_origin);
		return FALSE;
	}

	/* phew, lets send all this to fwupd */
	if (!fwupd_client_update_metadata_with_id (priv->client,
						   fwupd_remote_get_id (remote),
						   filename,
						   filename_asc,
						   cancellable,
						   error)) {
		gs_plugin_fwupd_error_convert (error);
		return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_fwupd_refresh_remotes (GsPlugin *plugin,
				 guint cache_age,
				 GCancellable *cancellable,
				 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GPtrArray) remotes = NULL;

	/* get the list of enabled remotes */
	remotes = fwupd_client_get_remotes (priv->client, cancellable, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (!fwupd_remote_get_enabled (remote))
			continue;
		if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_LOCAL)
			continue;
		if (!gs_plugin_fwupd_refresh_remote (plugin, remote, cache_age,
						     cancellable, error))
			return FALSE;
	}
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
		if (!gs_plugin_fwupd_refresh_remotes (plugin,
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
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *basename = NULL;
		g_autofree gchar *filename_cache = NULL;
		g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

		tmp = g_ptr_array_index (priv->to_download, i);
		basename = g_path_get_basename (tmp);
		filename_cache = gs_utils_get_cache_filename ("firmware",
							      basename,
							      GS_UTILS_CACHE_FLAG_WRITEABLE,
							      error);
		if (filename_cache == NULL)
			return FALSE;

		/* download file */
		gs_app_set_summary_missing (app_dl,
					    /* TRANSLATORS: status text when downloading */
					    _("Downloading firmware update…"));
		if (!gs_plugin_download_file (plugin, app_dl,
					      tmp, /* url */
					      filename_cache,
					      cancellable,
					      &error_local)) {
			g_warning ("Failed to download %s, ignoring: %s",
				   tmp, error_local->message);
			g_ptr_array_remove_index (priv->to_download, i--);
			g_ptr_array_add (priv->to_ignore, g_strdup (tmp));
			continue;
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
	GFile *local_file;
	g_autofree gchar *filename = NULL;

	/* not set */
	local_file = gs_app_get_local_file (app);
	if (local_file == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "not enough data for fwupd %s",
			     filename);
		return FALSE;
	}

	/* file does not yet exist */
	filename = g_file_get_path (local_file);
	if (!g_file_query_exists (local_file, cancellable)) {
		const gchar *uri = gs_fwupd_app_get_update_uri (app);
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		if (!gs_plugin_download_file (plugin, app, uri, filename,
					      cancellable, error))
			return FALSE;
	}

	/* limit to single device? */
	device_id = gs_fwupd_app_get_device_id (app);
	if (device_id == NULL)
		device_id = FWUPD_DEVICE_ID_ANY;

	/* set the last object */
	g_set_object (&priv->app_current, app);

	/* only offline supported */
	if (gs_app_get_metadata_item (app, "fwupd::OnlyOffline") != NULL)
		install_flags |= FWUPD_INSTALL_FLAG_OFFLINE;

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!fwupd_client_install (priv->client, device_id,
				   filename, install_flags,
				   cancellable, error)) {
		gs_plugin_fwupd_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

static gboolean
gs_plugin_fwupd_modify_source (GsPlugin *plugin, GsApp *app, gboolean enabled,
			       GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *remote_id = gs_app_get_metadata_item (app, "fwupd::remote-id");
	if (remote_id == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "not enough data for fwupd %s",
			     gs_app_get_unique_id (app));
		return FALSE;
	}
	return fwupd_client_modify_remote (priv->client,
					   remote_id,
					   "Enabled",
					   enabled ? "true" : "false",
					   cancellable,
					   error);
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

	/* source -> remote */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE) {
		return gs_plugin_fwupd_modify_source (plugin, app, TRUE,
						      cancellable, error);
	}

	/* firmware */
	return gs_plugin_fwupd_install (plugin, app, cancellable, error);
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin, GsApp *app,
		      GCancellable *cancellable, GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* source -> remote */
	return gs_plugin_fwupd_modify_source (plugin, app, TRUE, cancellable, error);
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
	if (gs_fwupd_app_get_is_locked (app)) {
		const gchar *device_id;
		device_id = gs_fwupd_app_get_device_id (app);
		if (device_id == NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_INVALID_FORMAT,
					     "not enough data for fwupd unlock");
			return FALSE;
		}
		if (!fwupd_client_unlock (priv->client, device_id,
					  cancellable, error)) {
			gs_plugin_fwupd_error_convert (error);
			return FALSE;
		}
		return TRUE;
	}

	/* update means install */
	if (!gs_plugin_fwupd_install (plugin, app, cancellable, error)) {
		gs_plugin_fwupd_error_convert (error);
		return FALSE;
	}
	return TRUE;
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
	g_autoptr(GPtrArray) results = NULL;
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
	results = fwupd_client_get_details_local (priv->client,
						  filename,
						  cancellable,
						  error);
	if (results == NULL) {
		gs_plugin_fwupd_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < results->len; i++) {
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

	return TRUE;
}

gboolean
gs_plugin_add_sources (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GPtrArray) remotes = NULL;

	/* find all remotes */
	remotes = fwupd_client_get_remotes (priv->client, cancellable, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		g_autofree gchar *id = NULL;
		g_autoptr(GsApp) app = NULL;

		/* ignore these, they're built in */
		if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_LOCAL)
			continue;

		/* create something that we can use to enable/disable */
		id = g_strdup_printf ("org.fwupd.%s.remote", fwupd_remote_get_id (remote));
		app = gs_app_new (id);
		gs_app_set_kind (app, AS_APP_KIND_SOURCE);
		gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
		gs_app_set_state (app, fwupd_remote_get_enabled (remote) ?
				  AS_APP_STATE_INSTALLED : AS_APP_STATE_AVAILABLE);
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST,
				 fwupd_remote_get_id (remote));
		gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
				    fwupd_remote_get_title (remote));
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
				fwupd_remote_get_metadata_uri (remote));
		gs_app_set_metadata (app, "fwupd::remote-id",
				     fwupd_remote_get_id (remote));
		gs_app_list_add (list, app);
	}
	return TRUE;
}
