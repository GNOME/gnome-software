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
	GDBusProxy		*proxy;
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
	g_ptr_array_unref (plugin->priv->to_download);
	g_ptr_array_unref (plugin->priv->to_ignore);
	if (plugin->priv->proxy != NULL)
		g_object_unref (plugin->priv->proxy);
}

/**
 * gs_plugin_fwupd_changed_cb:
 */
static void
gs_plugin_fwupd_changed_cb (GDBusProxy *proxy,
			    const gchar *sender_name,
			    const gchar *signal_name,
			    GVariant *parameters,
			    GsPlugin *plugin)
{
	if (g_strcmp0 (signal_name, "Changed") == 0)
		gs_plugin_updates_changed (plugin);
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	gsize len;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *data = NULL;
	g_autoptr(GDBusConnection) conn = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin->priv->mutex);

	/* register D-Bus errors */
	fwupd_error_quark ();

	conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (conn == NULL)
		return FALSE;
	plugin->priv->proxy = g_dbus_proxy_new_sync (conn,
						     G_DBUS_PROXY_FLAGS_NONE,
						     NULL,
						     FWUPD_DBUS_SERVICE,
						     FWUPD_DBUS_PATH,
						     FWUPD_DBUS_INTERFACE,
						     NULL,
						     &error_local);
	if (plugin->priv->proxy == NULL) {
		g_warning ("Failed to start fwupd: %s", error_local->message);
		return TRUE;
	}
	g_signal_connect (plugin->priv->proxy, "g-signal",
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
 * gs_plugin_fwupd_set_app_from_kv:
 */
static void
gs_plugin_fwupd_set_app_from_kv (GsApp *app, const gchar *key, GVariant *val)
{
	g_debug ("key %s", key);

	if (g_strcmp0 (key, "AppstreamId") == 0) {
		gs_app_set_id (app, g_variant_get_string (val, NULL));
		return;
	}
	if (g_strcmp0 (key, "Guid") == 0) {
		gs_app_set_metadata (app, "GUID", g_variant_get_string (val, NULL));
		return;
	}
	if (g_strcmp0 (key, "Name") == 0) {
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
				 g_variant_get_string (val, NULL));
		return;
	}
	if (g_strcmp0 (key, "Summary") == 0) {
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
				    g_variant_get_string (val, NULL));
		return;
	}
	if (g_strcmp0 (key, "Version") == 0) {
		gs_app_set_version (app, g_variant_get_string (val, NULL));
		return;
	}
	if (g_strcmp0 (key, "Size") == 0) {
		gs_app_set_size (app, g_variant_get_uint64 (val));
		return;
	}
	if (g_strcmp0 (key, "Created") == 0) {
		gs_app_set_install_date (app, g_variant_get_uint64 (val));
		return;
	}
	if (g_strcmp0 (key, "UpdateVersion") == 0) {
		gs_app_set_update_version (app, g_variant_get_string (val, NULL));
		return;
	}
	if (g_strcmp0 (key, "License") == 0) {
		gs_app_set_license (app,
				    GS_APP_QUALITY_NORMAL,
				    g_variant_get_string (val, NULL));
		return;
	}
	if (g_strcmp0 (key, "UpdateDescription") == 0) {
		g_autofree gchar *tmp = NULL;
		tmp = as_markup_convert (g_variant_get_string (val, NULL),
					 AS_MARKUP_CONVERT_FORMAT_SIMPLE, NULL);
		if (tmp != NULL)
			gs_app_set_update_details (app, tmp);
		return;
	}
}

/**
 * gs_plugin_add_update_app:
 */
static gboolean
gs_plugin_add_update_app (GsPlugin *plugin,
			  GList **list,
			  const gchar *id,
			  GVariantIter *iter_device,
			  GError **error)
{
	FwupdDeviceFlags flags = 0;
	GVariant *variant;
	const gchar *key;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *filename_cache = NULL;
	g_autofree gchar *update_hash = NULL;
	g_autofree gchar *update_uri = NULL;
	g_autoptr(AsIcon) icon = NULL;
	g_autoptr(GsApp) app = NULL;

	app = gs_app_new (NULL);
	while (g_variant_iter_next (iter_device, "{&sv}", &key, &variant)) {
		gs_plugin_fwupd_set_app_from_kv (app, key, variant);
		if (g_strcmp0 (key, "UpdateHash") == 0)
			update_hash = g_variant_dup_string (variant, NULL);
		else if (g_strcmp0 (key, "UpdateUri") == 0)
			update_uri = g_variant_dup_string (variant, NULL);
		else if (g_strcmp0 (key, "Flags") == 0)
			flags = g_variant_get_uint64 (variant);
		g_variant_unref (variant);
	}

	/* update unsupported */
	if ((flags & FU_DEVICE_FLAG_ALLOW_ONLINE) == 0 &&
	    (flags & FU_DEVICE_FLAG_ALLOW_OFFLINE) == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s [%s] cannot be updated",
			     gs_app_get_name (app), gs_app_get_id (app));
		return FALSE;
	}

	/* some missing */
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
	gs_app_set_management_plugin (app, "fwupd");
	gs_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	gs_app_add_source_id (app, filename_cache);
	gs_app_add_category (app, "System");
	gs_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	gs_app_set_metadata (app, "fwupd::DeviceID", id);
	gs_plugin_add_app (list, app);

	/* create icon */
	icon = as_icon_new ();
	as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
	as_icon_set_name (icon, "application-x-firmware");
	gs_app_set_icon (app, icon);

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
	GVariant *variant;
	const gchar *key;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GVariantIter) iter = NULL;
	g_autoptr(GVariant) val = NULL;

	/* set up plugin */
	if (plugin->priv->proxy == NULL) {
		if (!gs_plugin_startup (plugin, cancellable, error))
			return FALSE;
	}
	if (plugin->priv->proxy == NULL)
		return TRUE;

	/* get historical updates */
	val = g_dbus_proxy_call_sync (plugin->priv->proxy,
				      "GetResults",
				      g_variant_new ("(s)", FWUPD_DEVICE_ID_ANY),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error_local);
	if (val == NULL) {
		if (g_error_matches (error_local,
				     G_DBUS_ERROR,
				     G_DBUS_ERROR_SERVICE_UNKNOWN)) {
			/* the fwupd service might be unavailable, continue in that case */
			g_debug ("fwupd: Could not get historical updates, service is unknown.");
			return TRUE;
		}
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
	app = gs_app_new (NULL);
	gs_app_set_management_plugin (app, "fwupd");
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	gs_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	g_variant_get (val, "(a{sv})", &iter);
	while (g_variant_iter_next (iter, "{&sv}", &key, &variant)) {
		gs_plugin_fwupd_set_app_from_kv (app, key, variant);
		g_variant_unref (variant);
	}
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
	const gchar *id;
	GVariantIter *iter_device;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariantIter) iter = NULL;
	g_autoptr(GVariant) val = NULL;

	/* set up plugin */
	if (plugin->priv->proxy == NULL) {
		if (!gs_plugin_startup (plugin, cancellable, error))
			return FALSE;
	}
	if (plugin->priv->proxy == NULL)
		return TRUE;

	/* get current list of updates */
	val = g_dbus_proxy_call_sync (plugin->priv->proxy,
				      "GetUpdates",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error_local);
	if (val == NULL) {
		if (g_error_matches (error_local,
				     G_DBUS_ERROR,
				     G_DBUS_ERROR_SERVICE_UNKNOWN)) {
			/* the fwupd service might be unavailable, continue in that case */
			g_debug ("fwupd: Could not get updates, service is unknown.");
			return TRUE;
		}
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
	g_variant_get (val, "(a{sa{sv}})", &iter);
	while (g_variant_iter_next (iter, "{&sa{sv}}", &id, &iter_device)) {
		g_autoptr(GError) error_local2 = NULL;
		if (!gs_plugin_add_update_app (plugin, list,
					       id, iter_device,
					       &error_local2))
			g_debug ("%s", error_local2->message);
		g_variant_iter_free (iter_device);
	}

	return TRUE;
}

/**
 * gs_plugin_fwupd_update_lvfs_metadata:
 */
static gboolean
gs_plugin_fwupd_update_lvfs_metadata (const gchar *data_fn, const gchar *sig_fn, GError **error)
{
	GVariant *body;
	gint fd_sig;
	gint fd_data;
	gint retval;
	g_autoptr(GDBusConnection) conn = NULL;
	g_autoptr(GDBusMessage) message = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (conn == NULL)
		return FALSE;

	/* open files */
	fd_data = open (data_fn, O_RDONLY);
	if (fd_data < 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to open %s",
			     data_fn);
		return FALSE;
	}
	fd_sig = open (sig_fn, O_RDONLY);
	if (fd_sig < 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to open %s",
			     sig_fn);
		close (fd_data);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd_data, NULL);
	g_assert (retval != -1);
	retval = g_unix_fd_list_append (fd_list, fd_sig, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "UpdateMetadata");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd_data);
	close (fd_sig);

	/* send message */
	body = g_variant_new ("(hh)", 0, 1);
	g_dbus_message_set_body (request, body);
	message = g_dbus_connection_send_message_with_reply_sync (conn,
								  request,
								  G_DBUS_SEND_MESSAGE_FLAGS_NONE,
								  -1,
								  NULL,
								  NULL,
								  error);
	if (message == NULL) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
	}
	if (g_dbus_message_to_gerror (message, error)) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
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
	if (!gs_plugin_fwupd_update_lvfs_metadata (cache_fn_data,
						   plugin->priv->lvfs_sig_fn,
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
	if (plugin->priv->proxy == NULL) {
		if (!gs_plugin_startup (plugin, cancellable, error))
			return FALSE;
	}
	if (plugin->priv->proxy == NULL)
		return TRUE;

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
 * gs_plugin_fwupd_upgrade:
 */
static gboolean
gs_plugin_fwupd_upgrade (GsPlugin *plugin,
			 const gchar *filename,
			 const gchar *device_id,
			 gboolean do_offline,
			 GCancellable *cancellable,
			 GError **error)
{
	GVariant *body;
	GVariantBuilder builder;
	gint fd;
	gint retval;
	g_autoptr(GDBusConnection) conn = NULL;
	g_autoptr(GDBusMessage) message = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;

	conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (conn == NULL)
		return FALSE;

	/* set options */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder, "{sv}",
			       "reason", g_variant_new_string ("system-update"));
	g_variant_builder_add (&builder, "{sv}",
			       "filename", g_variant_new_string (filename));
	if (do_offline) {
		g_variant_builder_add (&builder, "{sv}",
				       "offline", g_variant_new_boolean (TRUE));
	}

	/* open file */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to open %s",
			     filename);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "Install");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);

	/* send message */
	body = g_variant_new ("(sha{sv})", device_id, 0, &builder);
	g_dbus_message_set_body (request, body);
	message = g_dbus_connection_send_message_with_reply_sync (conn,
								  request,
								  G_DBUS_SEND_MESSAGE_FLAGS_NONE,
								  -1,
								  NULL,
								  NULL,
								  error);
	if (message == NULL) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
	}
	if (g_dbus_message_to_gerror (message, error)) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
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

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "fwupd") != 0)
		return TRUE;

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
	if (!gs_plugin_fwupd_upgrade (plugin, filename, device_id, TRUE,
				      cancellable, error))
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
	const gchar *filename;
	gboolean offline = TRUE;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "fwupd") != 0)
		return TRUE;

	filename = gs_app_get_source_id_default (app);
	if (filename == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "not enough data for fwupd %s",
			     filename);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (gs_app_get_kind (app) == AS_APP_KIND_FIRMWARE)
		offline = FALSE;
	if (!gs_plugin_fwupd_upgrade (plugin, filename, FWUPD_DEVICE_ID_ANY, offline,
				      cancellable, error))
		return FALSE;
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

/**
 * gs_plugin_fwupd_unlock:
 */
static gboolean
gs_plugin_fwupd_unlock (GsPlugin *plugin,
			const gchar *device_id,
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariant) val = NULL;

	/* set up plugin */
	if (plugin->priv->proxy == NULL) {
		if (!gs_plugin_startup (plugin, cancellable, error))
			return FALSE;
	}
	if (plugin->priv->proxy == NULL)
		return TRUE;

	/* unlock device */
	val = g_dbus_proxy_call_sync (plugin->priv->proxy,
				      "Unlock",
				      g_variant_new ("(s)", device_id),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error_local);
	if (val == NULL) {
		if (g_error_matches (error_local,
				     G_DBUS_ERROR,
				     G_DBUS_ERROR_SERVICE_UNKNOWN)) {
			/* the fwupd service might be unavailable */
			g_debug ("fwupd: could not unlock, service is unknown");
			return TRUE;
		}
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     error_local->message);
		return FALSE;
	}
	return TRUE;
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
		return gs_plugin_fwupd_unlock (plugin,
					       device_id,
					       cancellable,
					       error);
	}

	return gs_plugin_app_install (plugin, app, cancellable, error);
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
	GVariant *body;
	GVariant *val;
	GVariant *variant;
	const gchar *key;
	gboolean supported;
	gint fd;
	gint retval;
	g_autoptr(AsIcon) icon = NULL;
	g_autoptr(GDBusConnection) conn = NULL;
	g_autoptr(GDBusMessage) message = NULL;
	g_autoptr(GDBusMessage) request = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GVariantIter) iter = NULL;

	/* does this match any of the mimetypes we support */
	if (!gs_plugin_fwupd_content_type_matches (filename,
						   &supported,
						   cancellable,
						   error))
		return FALSE;
	if (!supported)
		return TRUE;

	/* get request */
	conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (conn == NULL)
		return FALSE;

	/* open file */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to open %s",
			     filename);
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd, NULL);
	g_assert (retval != -1);
	request = g_dbus_message_new_method_call (FWUPD_DBUS_SERVICE,
						  FWUPD_DBUS_PATH,
						  FWUPD_DBUS_INTERFACE,
						  "GetDetails");
	g_dbus_message_set_unix_fd_list (request, fd_list);

	/* g_unix_fd_list_append did a dup() already */
	close (fd);

	/* send message */
	body = g_variant_new ("(h)", 0);
	g_dbus_message_set_body (request, body);
	message = g_dbus_connection_send_message_with_reply_sync (conn,
								  request,
								  G_DBUS_SEND_MESSAGE_FLAGS_NONE,
								  -1,
								  NULL,
								  NULL,
								  error);
	if (message == NULL) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
	}
	if (g_dbus_message_to_gerror (message, error)) {
		g_dbus_error_strip_remote_error (*error);
		return FALSE;
	}

	/* get results */
	app = gs_app_new (NULL);
	gs_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	gs_app_set_management_plugin (app, "fwupd");
	gs_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_add_source_id (app, filename);
	gs_app_add_category (app, "System");
	val = g_dbus_message_get_body (message);
	g_variant_get (val, "(a{sv})", &iter);
	while (g_variant_iter_next (iter, "{&sv}", &key, &variant)) {
		gs_plugin_fwupd_set_app_from_kv (app, key, variant);
		g_variant_unref (variant);
	}

	/* create icon */
	icon = as_icon_new ();
	as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
	as_icon_set_name (icon, "application-x-firmware");
	gs_app_set_icon (app, icon);

	gs_plugin_add_app (list, app);
	return TRUE;
}
