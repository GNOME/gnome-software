/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
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
	GsApp			*app_current;
	GsApp			*cached_origin;
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
	g_autofree gchar *user_agent = NULL;
	g_autoptr(SoupSession) soup_session = NULL;

	priv->client = fwupd_client_new ();

	/* use a custom user agent to provide the fwupd version */
	user_agent = fwupd_build_user_agent (PACKAGE_NAME, PACKAGE_VERSION);
	soup_session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, user_agent,
						      SOUP_SESSION_TIMEOUT, 10,
						      NULL);
	soup_session_remove_feature_by_type (soup_session,
					     SOUP_TYPE_CONTENT_DECODER);
	gs_plugin_set_soup_session (plugin, soup_session);

	/* set name of MetaInfo file */
	gs_plugin_set_appstream_id (plugin, "org.gnome.Software.Plugin.Fwupd");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->cached_origin != NULL)
		g_object_unref (priv->cached_origin);
	g_object_unref (priv->client);
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
				   FwupdDevice *dev,
				   GsPlugin *plugin)
{
	/* limit number of UI refreshes */
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
	return TRUE;
}

static GsApp *
gs_plugin_fwupd_new_app_from_device (GsPlugin *plugin, FwupdDevice *dev)
{
	FwupdRelease *rel = fwupd_device_get_release_default (dev);
	GsApp *app;
	g_autofree gchar *id = NULL;
	g_autoptr(AsIcon) icon = NULL;

	/* older versions of fwups didn't record this for historical devices */
	if (fwupd_release_get_appstream_id (rel) == NULL)
		return NULL;

	/* get from cache */
	id = as_utils_unique_id_build (AS_APP_SCOPE_SYSTEM,
				       AS_BUNDLE_KIND_UNKNOWN,
				       NULL, /* origin */
				       AS_APP_KIND_FIRMWARE,
				       fwupd_release_get_appstream_id (rel),
				       NULL);
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

static gchar *
gs_plugin_fwupd_build_device_id (FwupdDevice *dev)
{
	g_autofree gchar *tmp = g_strdup (fwupd_device_get_id (dev));
	g_strdelimit (tmp, "/", '_');
	return g_strdup_printf ("org.fwupd.%s.device", tmp);
}

static GsApp *
gs_plugin_fwupd_new_app_from_device_raw (GsPlugin *plugin, FwupdDevice *device)
{
	GPtrArray *icons;
	g_autofree gchar *id = NULL;
	g_autoptr(GsApp) app = NULL;

	/* create a GsApp based on the device, not the release */
	id = gs_plugin_fwupd_build_device_id (device);
	app = gs_app_new (id);
	gs_app_set_kind (app, AS_APP_KIND_FIRMWARE);
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
	gs_app_set_version (app, fwupd_device_get_version (device));
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, fwupd_device_get_name (device));
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, fwupd_device_get_summary (device));
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST, fwupd_device_get_description (device));
	gs_app_set_origin (app, fwupd_device_get_vendor (device));
	gs_fwupd_app_set_device_id (app, fwupd_device_get_id (device));
	gs_app_set_management_plugin (app, "fwupd");

	/* create icon */
	icons = fwupd_device_get_icons (device);
	for (guint j = 0; j < icons->len; j++) {
		const gchar *icon = g_ptr_array_index (icons, j);
		g_autoptr(AsIcon) icon_tmp = as_icon_new ();
		if (g_str_has_prefix (icon, "/")) {
			as_icon_set_kind (icon_tmp, AS_ICON_KIND_LOCAL);
			as_icon_set_filename (icon_tmp, icon);
		} else {
			as_icon_set_kind (icon_tmp, AS_ICON_KIND_STOCK);
			as_icon_set_name (icon_tmp, icon);
		}
		gs_app_add_icon (app, icon_tmp);
	}
	return g_steal_pointer (&app);
}

static GsApp *
gs_plugin_fwupd_new_app (GsPlugin *plugin, FwupdDevice *dev, GError **error)
{
	FwupdRelease *rel = fwupd_device_get_release_default (dev);
	GPtrArray *checksums;
	const gchar *update_uri;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *filename_cache = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app = NULL;

	/* update unsupported */
	app = gs_plugin_fwupd_new_app_from_device (plugin, dev);
	if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE_LIVE) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "%s [%s] cannot be updated",
			     gs_app_get_name (app), gs_app_get_id (app));
		return NULL;
	}

	/* some missing */
	if (gs_app_get_id (app) == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "fwupd: No id for firmware");
		return NULL;
	}
	if (gs_app_get_version (app) == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "fwupd: No version! for %s!", gs_app_get_id (app));
		return NULL;
	}
	if (gs_app_get_update_version (app) == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "fwupd: No update-version! for %s!", gs_app_get_id (app));
		return NULL;
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
		return NULL;
	}
	update_uri = fwupd_release_get_uri (rel);
	if (update_uri == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "no location available for %s [%s]",
			     gs_app_get_name (app), gs_app_get_id (app));
		return NULL;
	}

	/* does the firmware already exist in the cache? */
	basename = g_path_get_basename (update_uri);
	filename_cache = gs_utils_get_cache_filename ("fwupd",
						      basename,
						      GS_UTILS_CACHE_FLAG_NONE,
						      error);
	if (filename_cache == NULL)
		return NULL;

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
			return NULL;
		if (g_strcmp0 (checksum_tmp, checksum) != 0) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "%s does not match checksum, expected %s got %s",
				     filename_cache, checksum_tmp, checksum);
			g_unlink (filename_cache);
			return NULL;
		}
	}

	/* already downloaded, so overwrite */
	if (g_file_test (filename_cache, G_FILE_TEST_EXISTS))
		gs_app_set_size_download (app, 0);

	/* actually add the application */
	file = g_file_new_for_path (filename_cache);
	gs_app_set_local_file (app, file);
	return g_steal_pointer (&app);
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
	g_autoptr(FwupdDevice) dev = NULL;

	/* get historical updates */
	dev = fwupd_client_get_results (priv->client,
					FWUPD_DEVICE_ID_ANY,
					cancellable,
					&error_local);
	if (dev == NULL) {
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
	app = gs_plugin_fwupd_new_app_from_device (plugin, dev);
	if (app == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to build result for %s",
			     fwupd_device_get_id (dev));
		return FALSE;
	}
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
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get current list of updates */
	devices = fwupd_client_get_devices (priv->client, cancellable, &error_local);
	if (devices == NULL) {
		if (g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO)) {
			g_debug ("no devices");
			return TRUE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));
		gs_plugin_fwupd_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		FwupdRelease *rel_newest;
		g_autoptr(GError) error_local2 = NULL;
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GsApp) app = NULL;

		/* locked device that needs unlocking */
		if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_LOCKED)) {
			app = gs_plugin_fwupd_new_app_from_device_raw (plugin, dev);
			gs_fwupd_app_set_is_locked (app, TRUE);
			gs_app_list_add (list, app);
			continue;
		}

		/* not going to have results, so save a D-Bus round-trip */
		if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;

		/* get the releases for this device and filter for validity */
		rels = fwupd_client_get_upgrades (priv->client,
						  fwupd_device_get_id (dev),
						  cancellable, &error_local2);
		if (rels == NULL) {
			if (g_error_matches (error_local2,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOTHING_TO_DO)) {
				g_debug ("no updates for %s", fwupd_device_get_id (dev));
				continue;
			}
			if (g_error_matches (error_local2,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED)) {
				g_debug ("not supported for %s", fwupd_device_get_id (dev));
				continue;
			}
			g_warning ("failed to get upgrades for %s: %s]",
				   fwupd_device_get_id (dev),
				   error_local2->message);
			continue;
		}

		/* normal device update */
		rel_newest = g_ptr_array_index (rels, 0);
		fwupd_device_add_release (dev, rel_newest);
		app = gs_plugin_fwupd_new_app (plugin, dev, &error_local2);
		if (app == NULL) {
			g_debug ("%s", error_local2->message);
			continue;
		}

		/* add update descriptions for all releases inbetween */
		if (rels->len > 1) {
			g_autoptr(GString) update_desc = g_string_new (NULL);
			for (guint j = 0; j < rels->len; j++) {
				FwupdRelease *rel = g_ptr_array_index (rels, j);
				g_autofree gchar *desc = NULL;
				if (fwupd_release_get_description (rel) == NULL)
					continue;
				desc = as_markup_convert (fwupd_release_get_description (rel),
							  AS_MARKUP_CONVERT_FORMAT_SIMPLE,
							  NULL);
				if (desc == NULL)
					continue;
				g_string_append_printf (update_desc,
							"Version %s:\n%s\n\n",
							fwupd_release_get_version (rel),
							desc);
			}
			if (update_desc->len > 2) {
				g_string_truncate (update_desc, update_desc->len - 2);
				gs_app_set_update_details (app, update_desc->str);
			}
		}
		gs_app_list_add (list, app);
	}
	return TRUE;
}

static gboolean
gs_plugin_fwupd_refresh_remote (GsPlugin *plugin,
				FwupdRemote *remote,
				guint cache_age,
				GCancellable *cancellable,
				GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GChecksumType checksum_kind;
	const gchar *url_sig = NULL;
	const gchar *url = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *basename_sig = NULL;
	g_autofree gchar *cache_id = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *filename_sig = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

	/* sanity check */
	if (fwupd_remote_get_filename_cache_sig (remote) == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "remote %s has no cache signature",
			     fwupd_remote_get_id (remote));
		return FALSE;
	}

	/* check cache age */
	if (cache_age > 0) {
		guint64 age = fwupd_remote_get_age (remote);
		guint tmp = age < G_MAXUINT ? (guint) age : G_MAXUINT;
		if (tmp < cache_age) {
			g_debug ("%s is only %u seconds old, so ignoring refresh",
				 filename_sig, tmp);
			return TRUE;
		}
	}

	/* download the signature first, it's smaller */
	cache_id = g_strdup_printf ("fwupd/remotes.d/%s", fwupd_remote_get_id (remote));
	basename_sig = g_path_get_basename (fwupd_remote_get_filename_cache_sig (remote));
	filename_sig = gs_utils_get_cache_filename (cache_id, basename_sig,
						    GS_UTILS_CACHE_FLAG_WRITEABLE,
						    error);

	/* download the signature first, it's smaller */
	url_sig = fwupd_remote_get_metadata_uri_sig (remote);
	gs_app_set_summary_missing (app_dl,
				    /* TRANSLATORS: status text when downloading */
				    _("Downloading firmware update signature…"));
	data = gs_plugin_download_data (plugin, app_dl, url_sig, cancellable, error);
	if (data == NULL) {
		gs_utils_error_add_unique_id (error, priv->cached_origin);
		return FALSE;
	}

	/* is the signature hash the same as we had before? */
	checksum_kind = fwupd_checksum_guess_kind (fwupd_remote_get_checksum (remote));
	checksum = g_compute_checksum_for_data (checksum_kind,
						(const guchar *) g_bytes_get_data (data, NULL),
						g_bytes_get_size (data));
	if (g_strcmp0 (checksum, fwupd_remote_get_checksum (remote)) == 0) {
		g_debug ("signature of %s is unchanged", url_sig);
		return TRUE;
	}

	/* save to a file */
	g_debug ("saving new remote signature to %s:", filename_sig);
	if (!g_file_set_contents (filename_sig,
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

	/* download the payload and save to file */
	basename = g_path_get_basename (fwupd_remote_get_filename_cache (remote));
	filename = gs_utils_get_cache_filename (cache_id, basename,
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
	if (!fwupd_client_update_metadata (priv->client,
					   fwupd_remote_get_id (remote),
					   filename,
					   filename_sig,
					   cancellable,
					   error)) {
		gs_plugin_fwupd_error_convert (error);
		return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
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
	gs_app_set_state (app, enabled ?
	                  AS_APP_STATE_INSTALLING : AS_APP_STATE_REMOVING);
	if (!fwupd_client_modify_remote (priv->client,
	                                 remote_id,
	                                 "Enabled",
	                                 enabled ? "true" : "false",
	                                 cancellable,
	                                 error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, enabled ?
	                  AS_APP_STATE_INSTALLED : AS_APP_STATE_AVAILABLE);
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
	return gs_plugin_fwupd_modify_source (plugin, app, FALSE, cancellable, error);
}

gboolean
gs_plugin_download_app (GsPlugin *plugin,
			GsApp *app,
			GCancellable *cancellable,
			GError **error)
{
	GFile *local_file;
	g_autofree gchar *filename = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

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
		if (!gs_plugin_download_file (plugin, app, uri, filename,
					      cancellable, error))
			return FALSE;
	}
	gs_app_set_size_download (app, 0);
	return TRUE;
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
	g_autoptr(GPtrArray) devices = NULL;
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
	devices = fwupd_client_get_details (priv->client,
					    filename,
					    cancellable,
					    error);
	if (devices == NULL) {
		gs_plugin_fwupd_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		g_autoptr(GsApp) app = NULL;

		/* create each app */
		app = gs_plugin_fwupd_new_app_from_device (plugin, dev);

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
				 fwupd_remote_get_title (remote));
#if FWUPD_CHECK_VERSION(1,0,7)
		gs_app_set_agreement (app, fwupd_remote_get_agreement (remote));
#endif
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
				fwupd_remote_get_metadata_uri (remote));
		gs_app_set_metadata (app, "fwupd::remote-id",
				     fwupd_remote_get_id (remote));
		gs_app_set_management_plugin (app, "fwupd");
		gs_app_list_add (list, app);
	}
	return TRUE;
}

static gboolean
gs_plugin_fwupd_add_releases (GsPlugin *plugin, GsApp *app,
			      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	releases = fwupd_client_get_releases (priv->client,
					      gs_fwupd_app_get_device_id (app),
					      cancellable,
					      &error_local);
	if (releases == NULL) {
		/* just ignore empty array */
		if (g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOTHING_TO_DO)) {
			return TRUE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint j = 0; j < releases->len; j++) {
		FwupdRelease *rel = g_ptr_array_index (releases, j);
		g_autoptr(GsApp) app2 = gs_app_new (NULL);
		gs_fwupd_app_set_from_release (app2, rel);
		gs_app_add_history (app, app2);
	}
	return TRUE;
}

static gboolean
gs_plugin_fwupd_add_devices (GsPlugin *plugin, GsAppList *list,
			     GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GPtrArray) devices = NULL;

	/* get devices */
	devices = fwupd_client_get_devices (priv->client, cancellable, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index (devices, i);
		g_autoptr(GsApp) app = NULL;

		/* ignore these, we can't do anything */
		if (!fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE))
			continue;

		/* add releases */
		app = gs_plugin_fwupd_new_app_from_device_raw (plugin, device);
		if (!gs_plugin_fwupd_add_releases (plugin, app, cancellable, error))
			return FALSE;

		/* add all */
		gs_app_list_add (list, g_steal_pointer (&app));
	}
	return TRUE;
}

gboolean
gs_plugin_add_search (GsPlugin *plugin, gchar **values, GsAppList *list,
		      GCancellable *cancellable, GError **error)
{
	if (g_strv_contains ((const gchar * const *) values, "fwupd"))
		return gs_plugin_fwupd_add_devices (plugin, list, cancellable, error);
	return TRUE;
}
