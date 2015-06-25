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
#include <libsoup/soup.h>
#include <glib/gstdio.h>

#include <gs-plugin.h>

#include "gs-cleanup.h"

struct GsPluginPrivate {
	gsize			 done_init;
	GDBusProxy		*proxy;
	GPtrArray		*to_download;
	AsStore			*store;
	GPtrArray		*to_ignore;
	SoupSession		*session;
	gchar			*cachedir;
	gchar			*lvfs_sig_fn;
	gchar			*lvfs_sig_hash;
};

/**
 * gs_plugin_fwupd_setup_networking:
 */
static gboolean
gs_plugin_fwupd_setup_networking (GsPlugin *plugin, GError **error)
{
	/* already set up */
	if (plugin->priv->session != NULL)
		return TRUE;

	/* set up a session */
	plugin->priv->session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT,
	                                                       "gnome-software",
	                                                       NULL);
	if (plugin->priv->session == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s: failed to setup networking",
			     plugin->name);
		return FALSE;
	}
	/* this disables the double-compression of the firmware.xml.gz file */
	soup_session_remove_feature_by_type (plugin->priv->session,
					     SOUP_TYPE_CONTENT_DECODER);
	return TRUE;
}

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
	plugin->priv->store = as_store_new ();
	plugin->priv->to_download = g_ptr_array_new_with_free_func (g_free);
	plugin->priv->to_ignore = g_ptr_array_new_with_free_func (g_free);
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
	g_object_unref (plugin->priv->store);
	g_ptr_array_unref (plugin->priv->to_download);
	g_ptr_array_unref (plugin->priv->to_ignore);
	if (plugin->priv->proxy != NULL)
		g_object_unref (plugin->priv->proxy);
	if (plugin->priv->session != NULL)
		g_object_unref (plugin->priv->session);
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
	gint rc;
	gsize len;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_free_ gchar *data = NULL;
	_cleanup_object_unref_ GDBusConnection *conn = NULL;

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
	plugin->priv->cachedir = g_build_filename (g_get_user_cache_dir (),
						   "gnome-software",
						   "firmware",
						   NULL);
	rc = g_mkdir_with_parents (plugin->priv->cachedir, 0700);
	if (rc != 0) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "Could not create firmware cache");
		return FALSE;
	}

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

	/* only load firmware from the system */
	as_store_add_filter (plugin->priv->store, AS_ID_KIND_FIRMWARE);
	if (!as_store_load (plugin->priv->store,
			    AS_STORE_LOAD_FLAG_APP_INFO_SYSTEM,
			    cancellable, error))
		return FALSE;

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
	_cleanup_free_ gchar *data = NULL;

	if (!g_file_get_contents (filename, &data, &len, error))
		return NULL;
	return g_compute_checksum_for_data (checksum_type, (const guchar *)data, len);
}

/**
 * gs_plugin_fwupd_add_device:
 */
static gboolean
gs_plugin_fwupd_add_device (GsPlugin *plugin,
			    const gchar *device_id,
			    const gchar *guid,
			    const gchar *version,
			    GList **list,
			    GError **error)
{
	AsApp *item;
	AsRelease *rel;
	GPtrArray *releases;
	const gchar *tmp;
	guint i;
	_cleanup_free_ gchar *basename = NULL;
	_cleanup_free_ gchar *checksum = NULL;
	_cleanup_free_ gchar *checksum2 = NULL;
	_cleanup_free_ gchar *filename_cache = NULL;
	_cleanup_free_ gchar *update_location = NULL;
	_cleanup_free_ gchar *update_version = NULL;
	_cleanup_object_unref_ AsIcon *icon = NULL;
	_cleanup_object_unref_ GsApp *app = NULL;
	_cleanup_string_free_ GString *update_desc = NULL;

	/* find the device */
	item = as_store_get_app_by_id (plugin->priv->store, guid);
	if (item == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "device id %s not found in metadata",
			     guid);
		return FALSE;
	}

	/* are any releases newer than what we have here */
	g_debug ("device id %s found in metadata", guid);
	update_desc = g_string_new ("");
	releases = as_app_get_releases (item);
	for (i = 0; i < releases->len; i++) {
		_cleanup_free_ gchar *md = NULL;

		/* check if actually newer */
		rel = g_ptr_array_index (releases, i);
		if (as_utils_vercmp (as_release_get_version (rel), version) <= 0)
			continue;

		/* get checksum */
		tmp = as_release_get_checksum (rel, G_CHECKSUM_SHA1);
		if (tmp == NULL) {
			g_warning ("%s [%s] has no checksum, ignoring as unsafe",
				   as_app_get_id (item),
				   as_release_get_version (rel));
			continue;
		}

		/* get the update text, if it exists */
		if (update_version == NULL) {
			checksum = g_strdup (tmp);
			tmp = as_release_get_version (rel);
			if (g_strstr_len (tmp, -1, ".") != NULL) {
				update_version = g_strdup (tmp);
			} else {
				GDateTime *dt;
				dt = g_date_time_new_from_unix_utc (as_release_get_timestamp (rel));
				update_version = g_strdup_printf ("0.0.%s-%04i%02i%02i",
								  tmp,
								  g_date_time_get_year (dt),
								  g_date_time_get_month (dt),
								  g_date_time_get_day_of_month (dt));
				g_date_time_unref (dt);
			}
			update_location = g_strdup (as_release_get_location_default (rel));
		}
		tmp = as_release_get_description (rel, NULL);
		if (tmp == NULL)
			continue;
		md = as_markup_convert (tmp, -1,
					AS_MARKUP_CONVERT_FORMAT_MARKDOWN,
					NULL);
		if (md != NULL)
			g_string_append_printf (update_desc, "%s\n", md);
	}

	/* no updates for this hardware */
	if (update_version == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no updates available");
		return FALSE;
	}

	/* nowhere to download the update from */
	if (update_location == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no location available for firmware");
		return FALSE;
	}

	/* does the firmware already exist in the cache? */
	basename = g_path_get_basename (update_location);
	filename_cache = g_build_filename (plugin->priv->cachedir, basename, NULL);
	if (!g_file_test (filename_cache, G_FILE_TEST_EXISTS)) {
		gs_plugin_fwupd_add_required_location (plugin, update_location);
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s does not yet exist, wait patiently",
			     filename_cache);
		return FALSE;
	}

	/* does the checksum match */
	checksum2 = gs_plugin_fwupd_get_file_checksum (filename_cache,
						       G_CHECKSUM_SHA1,
						       error);
	if (checksum2 == NULL)
		return FALSE;
	if (g_strcmp0 (checksum, checksum2) != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s does not match checksum, expected %s, got %s",
			     filename_cache, checksum, checksum2);
		g_unlink (filename_cache);
		return FALSE;
	}

	/* remove trailing newline */
	if (update_desc->len > 0)
		g_string_truncate (update_desc, update_desc->len - 1);

	/* actually addd the application */
	app = gs_app_new (guid);
	gs_app_set_management_plugin (app, "fwupd");
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	gs_app_set_id_kind (app, AS_ID_KIND_FIRMWARE);
	gs_app_set_update_details (app, update_desc->str);
	gs_app_set_update_version (app, update_version);
	gs_app_add_source_id (app, filename_cache);
	gs_app_add_source (app, as_app_get_name (item, NULL));
	gs_app_add_category (app, "System");
	gs_app_set_kind (app, GS_APP_KIND_SYSTEM);
	gs_app_set_metadata (app, "fwupd::DeviceID", device_id);
	gs_app_set_metadata (app, "DataDir::desktop-icon", "application-x-firmware");
	gs_plugin_add_app (list, app);

	/* create icon */
	icon = as_icon_new ();
	as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
	as_icon_set_name (icon, "application-x-firmware", -1);
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
	gboolean ret;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_object_unref_ GsApp *app = NULL;
	_cleanup_variant_iter_free_ GVariantIter *iter = NULL;
	_cleanup_variant_unref_ GVariant *val = NULL;

	/* watch the file in case it comes or goes */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	/* could not connect */
	if (plugin->priv->proxy == NULL)
		return TRUE;
	val = g_dbus_proxy_call_sync (plugin->priv->proxy,
				      "GetResults",
				      g_variant_new ("(s)", FWUPD_DEVICE_ID_ANY),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error_local);
	if (val == NULL) {
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
	gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
	g_variant_get (val, "(a{sv})", &iter);
	while (g_variant_iter_next (iter, "{&sv}", &key, &variant)) {
		g_debug ("key %s", key);
		if (g_strcmp0 (key, "Guid") == 0) {
			gs_app_set_id (app, g_variant_get_string (variant, NULL));
			continue;
		}
		if (g_strcmp0 (key, "VersionNew") == 0) {
			gs_app_set_update_version (app, g_variant_get_string (variant, NULL));
			continue;
		}
		if (g_strcmp0 (key, "Name") == 0) {
			gs_app_add_source (app, g_variant_get_string (variant, NULL));
			continue;
		}
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
	gboolean ret;
	GVariantIter *iter_device;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_variant_iter_free_ GVariantIter *iter = NULL;
	_cleanup_variant_unref_ GVariant *val = NULL;

	/* watch the file in case it comes or goes */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	/* could not connect */
	if (plugin->priv->proxy == NULL)
		return TRUE;
	val = g_dbus_proxy_call_sync (plugin->priv->proxy,
				      "GetDevices",
				      NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error_local);
	if (val == NULL) {
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
		GVariant *variant;
		const gchar *key;
		_cleanup_free_ gchar *guid = NULL;
		_cleanup_free_ gchar *version = NULL;

		while (g_variant_iter_next (iter_device, "{&sv}", &key, &variant)) {
			g_debug ("%s has key %s", id, key);
			if (g_strcmp0 (key, "Guid") == 0) {
				guid = g_variant_dup_string (variant, NULL);
			} else if (g_strcmp0 (key, "Version") == 0) {
				version = g_variant_dup_string (variant, NULL);
			}
			g_variant_unref (variant);
		}

		/* we got all we needed */
		if (guid != NULL && version != NULL) {
			_cleanup_error_free_ GError *error_local2 = NULL;
			if (!gs_plugin_fwupd_add_device (plugin,
							 id,
							 guid,
							 version,
							 list,
							 &error_local2)) {
				g_debug ("cannot add device %s: %s",
					 id, error_local2->message);
			}
		}

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
	_cleanup_object_unref_ GDBusConnection *conn = NULL;
	_cleanup_object_unref_ GDBusMessage *message = NULL;
	_cleanup_object_unref_ GDBusMessage *request = NULL;
	_cleanup_object_unref_ GUnixFDList *fd_list = NULL;

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
		return FALSE;
	}

	/* set out of band file descriptor */
	fd_list = g_unix_fd_list_new ();
	retval = g_unix_fd_list_append (fd_list, fd_data, NULL);
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
	const gchar *url_data = "https://beta-lvfs.rhcloud.com/downloads/firmware.xml.gz";
	guint status_code;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_free_ gchar *basename_data = NULL;
	_cleanup_free_ gchar *cache_fn_data = NULL;
	_cleanup_free_ gchar *checksum = NULL;
	_cleanup_free_ gchar *url_sig = NULL;
	_cleanup_object_unref_ SoupMessage *msg_data = NULL;
	_cleanup_object_unref_ SoupMessage *msg_sig = NULL;

	/* download the signature first, it's smaller */
	url_sig = g_strdup_printf ("%s.asc", url_data);
	msg_sig = soup_message_new (SOUP_METHOD_GET, url_sig);
	status_code = soup_session_send_message (plugin->priv->session, msg_sig);
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
	status_code = soup_session_send_message (plugin->priv->session, msg_data);
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
	gboolean ret;
	guint i;

	/* set up plugin */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	/* ensure networking is set up */
	if (!gs_plugin_fwupd_setup_networking (plugin, error))
		return FALSE;

	/* get the metadata and signature file */
	if (!gs_plugin_fwupd_check_lvfs_metadata (plugin, cache_age, cancellable, error))
		return FALSE;

	/* download the files to the cachedir */
	for (i = 0; i < plugin->priv->to_download->len; i++) {
		guint status_code;
		_cleanup_error_free_ GError *error_local = NULL;
		_cleanup_free_ gchar *basename = NULL;
		_cleanup_free_ gchar *filename_cache = NULL;
		_cleanup_object_unref_ SoupMessage *msg = NULL;

		tmp = g_ptr_array_index (plugin->priv->to_download, i);
		basename = g_path_get_basename (tmp);
		filename_cache = g_build_filename (plugin->priv->cachedir, basename, NULL);
		g_debug ("downloading %s to %s", tmp, filename_cache);

		/* set sync request */
		msg = soup_message_new (SOUP_METHOD_GET, tmp);
		status_code = soup_session_send_message (plugin->priv->session, msg);
		if (status_code != SOUP_STATUS_OK) {
			g_warning ("Failed to download %s, ignoring: %s",
				   tmp, soup_status_get_phrase (status_code));
			g_ptr_array_remove (plugin->priv->to_download, (gpointer) tmp);
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
			 GCancellable *cancellable,
			 GError **error)
{
	GVariant *body;
	GVariantBuilder builder;
	gint fd;
	gint retval;
	_cleanup_object_unref_ GDBusConnection *conn = NULL;
	_cleanup_object_unref_ GDBusMessage *message = NULL;
	_cleanup_object_unref_ GDBusMessage *request = NULL;
	_cleanup_object_unref_ GUnixFDList *fd_list = NULL;

	conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (conn == NULL)
		return FALSE;

	/* set options */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder, "{sv}",
			       "reason", g_variant_new_string ("system-update"));
	g_variant_builder_add (&builder, "{sv}",
			       "filename", g_variant_new_string (filename));
	g_variant_builder_add (&builder, "{sv}",
			       "offline", g_variant_new_boolean (TRUE));

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
						  "Update");
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
	if (!gs_plugin_fwupd_upgrade (plugin, filename, device_id,
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
 */
gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	const gchar *filename;

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
	if (!gs_plugin_fwupd_upgrade (plugin, filename, FWUPD_DEVICE_ID_ANY,
				      cancellable, error))
		return FALSE;
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
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
	_cleanup_object_unref_ GFile *file = NULL;
	_cleanup_object_unref_ GFileInfo *info = NULL;
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
	_cleanup_object_unref_ AsIcon *icon = NULL;
	_cleanup_object_unref_ GDBusConnection *conn = NULL;
	_cleanup_object_unref_ GDBusMessage *message = NULL;
	_cleanup_object_unref_ GDBusMessage *request = NULL;
	_cleanup_object_unref_ GsApp *app = NULL;
	_cleanup_object_unref_ GUnixFDList *fd_list = NULL;
	_cleanup_variant_iter_free_ GVariantIter *iter = NULL;

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
	gs_app_set_metadata (app, "DataDir::desktop-icon", "application-x-firmware");
	gs_app_set_id_kind (app, AS_ID_KIND_FIRMWARE);
	gs_app_set_management_plugin (app, "fwupd");
	gs_app_set_kind (app, GS_APP_KIND_SYSTEM);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_add_source_id (app, filename);
	gs_app_add_category (app, "System");
	val = g_dbus_message_get_body (message);
	g_variant_get (val, "(a{sv})", &iter);
	while (g_variant_iter_next (iter, "{&sv}", &key, &variant)) {
		if (g_strcmp0 (key, "Version") == 0) {
			gs_app_set_version (app, g_variant_get_string (variant, NULL));
		} else if (g_strcmp0 (key, "Vendor") == 0) {
			gs_app_set_origin (app, g_variant_get_string (variant, NULL));
		} else if (g_strcmp0 (key, "Guid") == 0) {
			gs_app_set_id (app, g_variant_get_string (variant, NULL));
		} else if (g_strcmp0 (key, "Name") == 0) {
			gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
					 g_variant_get_string (variant, NULL));
		} else if (g_strcmp0 (key, "Summary") == 0) {
			gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
					    g_variant_get_string (variant, NULL));
		} else if (g_strcmp0 (key, "Description") == 0) {
			_cleanup_free_ gchar *tmp = NULL;
			tmp = as_markup_convert (g_variant_get_string (variant, NULL), -1,
						 AS_MARKUP_CONVERT_FORMAT_SIMPLE, NULL);
			if (tmp != NULL)
				gs_app_set_description (app, GS_APP_QUALITY_HIGHEST, tmp);
		} else if (g_strcmp0 (key, "UrlHomepage") == 0) {
			gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
					g_variant_get_string (variant, NULL));
		} else if (g_strcmp0 (key, "License") == 0) {
			gs_app_set_licence (app, g_variant_get_string (variant, NULL));
		} else if (g_strcmp0 (key, "Size") == 0) {
			gs_app_set_size (app, g_variant_get_uint64 (variant));
		}
		g_variant_unref (variant);
	}

	/* create icon */
	icon = as_icon_new ();
	as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
	as_icon_set_name (icon, "application-x-firmware", -1);
	gs_app_set_icon (app, icon);

	gs_plugin_add_app (list, app);
	return TRUE;
}
