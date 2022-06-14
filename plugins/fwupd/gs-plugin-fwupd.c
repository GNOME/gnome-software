/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
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
#include "gs-metered.h"

#include "gs-plugin-fwupd.h"

/*
 * SECTION:
 * Queries for new firmware and schedules it to be installed as required.
 *
 * This plugin calls UpdatesChanged() if any updatable devices are
 * added or removed or if a device has been updated live.
 *
 * Since fwupd is a daemon accessible over D-Bus, this plugin basically
 * translates every job into one or more D-Bus calls, and all the real work is
 * done in the fwupd daemon. FIXME: This means the plugin can therefore execute
 * entirely in the main thread, making asynchronous D-Bus calls, once all the
 * vfuncs have been ported.
 */

struct _GsPluginFwupd {
	GsPlugin		 parent;

	FwupdClient		*client;
	GsApp			*app_current;
	GsApp			*cached_origin;
};

G_DEFINE_TYPE (GsPluginFwupd, gs_plugin_fwupd, GS_TYPE_PLUGIN)

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

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gdbus (perror))
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
		case FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW:
			error->code = GS_PLUGIN_ERROR_BATTERY_LEVEL_TOO_LOW;
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

static void
gs_plugin_fwupd_init (GsPluginFwupd *self)
{
	self->client = fwupd_client_new ();

	/* set name of MetaInfo file */
	gs_plugin_set_appstream_id (GS_PLUGIN (self), "org.gnome.Software.Plugin.Fwupd");
}

static void
gs_plugin_fwupd_dispose (GObject *object)
{
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (object);

	g_clear_object (&self->cached_origin);
	g_clear_object (&self->client);

	G_OBJECT_CLASS (gs_plugin_fwupd_parent_class)->dispose (object);
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_FIRMWARE)
		gs_app_set_management_plugin (app, plugin);
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
gs_plugin_fwupd_notify_percentage_cb (GObject    *object,
                                      GParamSpec *pspec,
                                      gpointer    user_data)
{
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (user_data);

	/* nothing in progress */
	if (self->app_current == NULL) {
		g_debug ("fwupd percentage: %u%%",
			 fwupd_client_get_percentage (self->client));
		return;
	}
	g_debug ("fwupd percentage for %s: %u%%",
		 gs_app_get_unique_id (self->app_current),
		 fwupd_client_get_percentage (self->client));
	gs_app_set_progress (self->app_current,
			     fwupd_client_get_percentage (self->client));
}

static void
gs_plugin_fwupd_notify_status_cb (GObject    *object,
                                  GParamSpec *pspec,
                                  gpointer    user_data)
{
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (user_data);

	/* nothing in progress */
	if (self->app_current == NULL) {
		g_debug ("fwupd status: %s",
			 fwupd_status_to_string (fwupd_client_get_status (self->client)));
		return;
	}

	g_debug ("fwupd status for %s: %s",
		 gs_app_get_unique_id (self->app_current),
		 fwupd_status_to_string (fwupd_client_get_status (self->client)));
	switch (fwupd_client_get_status (self->client)) {
	case FWUPD_STATUS_DECOMPRESSING:
	case FWUPD_STATUS_DEVICE_RESTART:
	case FWUPD_STATUS_DEVICE_WRITE:
	case FWUPD_STATUS_DEVICE_VERIFY:
		gs_app_set_state (self->app_current, GS_APP_STATE_INSTALLING);
		break;
	case FWUPD_STATUS_IDLE:
		g_clear_object (&self->app_current);
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

static void setup_connect_cb (GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data);
static void setup_features_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data);

static void
gs_plugin_fwupd_setup_async (GsPlugin            *plugin,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_fwupd_setup_async);

	/* connect a proxy */
	fwupd_client_connect_async (self->client, cancellable, setup_connect_cb,
				    g_steal_pointer (&task));
}

static void
setup_connect_cb (GObject      *source_object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginFwupd *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) local_error = NULL;

	if (!fwupd_client_connect_finish (self->client, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* send our implemented feature set */
	fwupd_client_set_feature_flags_async (self->client,
					      FWUPD_FEATURE_FLAG_UPDATE_ACTION |
					      FWUPD_FEATURE_FLAG_DETACH_ACTION,
					      cancellable, setup_features_cb,
					      g_steal_pointer (&task));
}

static void
setup_features_cb (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginFwupd *self = g_task_get_source_object (task);
	GsPlugin *plugin = GS_PLUGIN (self);
	g_autoptr(GError) local_error = NULL;

	if (!fwupd_client_set_feature_flags_finish (self->client, result, &local_error))
		g_debug ("Failed to set front-end features: %s", local_error->message);
	g_clear_error (&local_error);

	/* we know the runtime daemon version now */
	fwupd_client_set_user_agent_for_package (self->client, PACKAGE_NAME, PACKAGE_VERSION);
	if (!fwupd_client_ensure_networking (self->client, &local_error)) {
		gs_plugin_fwupd_error_convert (&local_error);
		g_prefix_error (&local_error, "Failed to setup networking: ");
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* add source */
	self->cached_origin = gs_app_new (gs_plugin_get_name (plugin));
	gs_app_set_kind (self->cached_origin, AS_COMPONENT_KIND_REPOSITORY);
	gs_app_set_bundle_kind (self->cached_origin, AS_BUNDLE_KIND_CABINET);
	gs_app_set_management_plugin (self->cached_origin, plugin);

	/* add the source to the plugin cache which allows us to match the
	 * unique ID to a GsApp when creating an event */
	gs_plugin_cache_add (plugin,
			     gs_app_get_unique_id (self->cached_origin),
			     self->cached_origin);

	/* register D-Bus errors */
	fwupd_error_quark ();
	g_signal_connect (self->client, "changed",
			  G_CALLBACK (gs_plugin_fwupd_changed_cb), plugin);
	g_signal_connect (self->client, "device-added",
			  G_CALLBACK (gs_plugin_fwupd_device_changed_cb), plugin);
	g_signal_connect (self->client, "device-removed",
			  G_CALLBACK (gs_plugin_fwupd_device_changed_cb), plugin);
	g_signal_connect (self->client, "device-changed",
			  G_CALLBACK (gs_plugin_fwupd_device_changed_cb), plugin);
	g_signal_connect (self->client, "notify::percentage",
			  G_CALLBACK (gs_plugin_fwupd_notify_percentage_cb), self);
	g_signal_connect (self->client, "notify::status",
			  G_CALLBACK (gs_plugin_fwupd_notify_status_cb), self);

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_fwupd_setup_finish (GsPlugin      *plugin,
                              GAsyncResult  *result,
                              GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static GsApp *
gs_plugin_fwupd_new_app_from_device (GsPlugin *plugin, FwupdDevice *dev)
{
	FwupdRelease *rel = fwupd_device_get_release_default (dev);
	GsApp *app;
	g_autofree gchar *id = NULL;
	g_autoptr(GIcon) icon = NULL;

	/* older versions of fwups didn't record this for historical devices */
	if (fwupd_release_get_appstream_id (rel) == NULL)
		return NULL;

	/* get from cache */
	id = gs_utils_build_unique_id (AS_COMPONENT_SCOPE_SYSTEM,
				       AS_BUNDLE_KIND_UNKNOWN,
				       NULL, /* origin */
				       fwupd_release_get_appstream_id (rel),
				       NULL);
	app = gs_plugin_cache_lookup (plugin, id);
	if (app == NULL) {
		app = gs_app_new (id);
		gs_plugin_cache_add (plugin, id, app);
	}

	/* default stuff */
	gs_app_set_kind (app, AS_COMPONENT_KIND_FIRMWARE);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_CABINET);
	gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
	gs_app_add_quirk (app, GS_APP_QUIRK_DO_NOT_AUTO_UPDATE);
	gs_app_set_management_plugin (app, plugin);
	gs_app_add_category (app, "System");
	gs_fwupd_app_set_device_id (app, fwupd_device_get_id (dev));

	/* create icon */
	icon = g_themed_icon_new ("system-component-firmware");
	gs_app_add_icon (app, icon);
	gs_fwupd_app_set_from_device (app, dev);
	gs_fwupd_app_set_from_release (app, rel);

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
	gs_app_set_kind (app, AS_COMPONENT_KIND_FIRMWARE);
	gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
	gs_app_set_state (app, GS_APP_STATE_INSTALLED);
	gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
	gs_app_add_quirk (app, GS_APP_QUIRK_DO_NOT_AUTO_UPDATE);
	gs_app_set_version (app, fwupd_device_get_version (device));
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, fwupd_device_get_name (device));
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, fwupd_device_get_summary (device));
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST, fwupd_device_get_description (device));
	gs_app_set_origin (app, fwupd_device_get_vendor (device));
	gs_fwupd_app_set_device_id (app, fwupd_device_get_id (device));
	gs_app_set_management_plugin (app, plugin);

	/* create icon */
	icons = fwupd_device_get_icons (device);
	for (guint j = 0; j < icons->len; j++) {
		const gchar *icon_str = g_ptr_array_index (icons, j);
		g_autoptr(GIcon) icon = NULL;
		if (g_str_has_prefix (icon_str, "/")) {
			g_autoptr(GFile) icon_file = g_file_new_for_path (icon_str);
			icon = g_file_icon_new (icon_file);
		} else {
			icon = g_themed_icon_new (icon_str);
		}
		gs_app_add_icon (app, icon);
	}
	return g_steal_pointer (&app);
}

static GsApp *
gs_plugin_fwupd_new_app (GsPlugin *plugin, FwupdDevice *dev, GError **error)
{
	FwupdRelease *rel = fwupd_device_get_release_default (dev);
	GPtrArray *checksums;
	GPtrArray *locations = fwupd_release_get_locations (rel);
	const gchar *update_uri = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *filename_cache = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app = NULL;

	/* update unsupported */
	app = gs_plugin_fwupd_new_app_from_device (plugin, dev);
	if (gs_app_get_state (app) != GS_APP_STATE_UPDATABLE_LIVE) {
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

	/* typically the first URI will be the main HTTP mirror, and we
	 * don't have the capability to use an IPFS/IPNS URL anyway */
	if (locations->len > 0)
		update_uri = g_ptr_array_index (locations, 0);

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
						      GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
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
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(FwupdDevice) dev = NULL;

	/* get historical updates */
	dev = fwupd_client_get_results (self->client,
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
		g_propagate_error (error, g_steal_pointer (&error_local));
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
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* get current list of updates */
	devices = fwupd_client_get_devices (self->client, cancellable, &error_local);
	if (devices == NULL) {
		if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO) ||
		    g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED) ||
		    g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug ("no devices (%s)", error_local->message);
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
		rels = fwupd_client_get_upgrades (self->client,
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
				desc = as_markup_convert_simple (fwupd_release_get_description (rel), NULL);
				if (desc == NULL)
					continue;
				g_string_append_printf (update_desc,
							"Version %s:\n%s\n\n",
							fwupd_release_get_version (rel),
							desc);
			}
			if (update_desc->len > 2) {
				g_string_truncate (update_desc, update_desc->len - 2);
				gs_app_set_update_details_text (app, update_desc->str);
			}
		}
		gs_app_list_add (list, app);
	}
	return TRUE;
}

static gboolean
remote_cache_is_expired (FwupdRemote *remote,
                         guint64      cache_age_secs)
{
	/* check cache age */
	if (cache_age_secs > 0) {
		guint64 age = fwupd_remote_get_age (remote);
		if (age < cache_age_secs) {
			g_debug ("fwupd remote is only %" G_GUINT64_FORMAT " seconds old, so ignoring refresh", age);
			return FALSE;
		}
	}

	return TRUE;
}

typedef struct {
	/* Input data. */
	guint64 cache_age_secs;

	/* In-progress state. */
	guint n_operations_pending;
	GError *error;  /* (owned) (nullable) */
} RefreshMetadataData;

static void
refresh_metadata_data_free (RefreshMetadataData *data)
{
	g_clear_error (&data->error);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RefreshMetadataData, refresh_metadata_data_free)

static void get_remotes_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data);
static void refresh_remote_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data);
static void finish_refresh_metadata_op (GTask *task);

static void
gs_plugin_fwupd_refresh_metadata_async (GsPlugin                     *plugin,
                                        guint64                       cache_age_secs,
                                        GsPluginRefreshMetadataFlags  flags,
                                        GCancellable                 *cancellable,
                                        GAsyncReadyCallback           callback,
                                        gpointer                      user_data)
{
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);
	g_autoptr(GTask) task = NULL;
	g_autoptr(RefreshMetadataData) data = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_fwupd_refresh_metadata_async);

	data = g_new0 (RefreshMetadataData, 1);
	data->cache_age_secs = cache_age_secs;
	g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) refresh_metadata_data_free);

	/* get the list of enabled remotes */
	fwupd_client_get_remotes_async (self->client, cancellable, get_remotes_cb, g_steal_pointer (&task));
}

static void
get_remotes_cb (GObject      *source_object,
                GAsyncResult *result,
                gpointer      user_data)
{
	FwupdClient *client = FWUPD_CLIENT (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	RefreshMetadataData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) remotes = NULL;

	remotes = fwupd_client_get_remotes_finish (client, result, &error_local);

	if (remotes == NULL) {
		g_debug ("No remotes found: %s", error_local ? error_local->message : "Unknown error");
		if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO) ||
		    g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED) ||
		    g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_task_return_boolean (task, TRUE);
			return;
		}

		gs_plugin_fwupd_error_convert (&error_local);
		g_task_return_error (task, g_steal_pointer (&error_local));
		return;
	}

	/* Refresh each of the remotes in parallel. Keep the pending operation
	 * count incremented until all operations have been started, so that
	 * the overall operation doesn’t complete too early. */
	data->n_operations_pending = 1;

	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);

		if (!fwupd_remote_get_enabled (remote))
			continue;
		if (fwupd_remote_get_kind (remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
			continue;
		if (!remote_cache_is_expired (remote, data->cache_age_secs))
			continue;

		data->n_operations_pending++;
		fwupd_client_refresh_remote_async (client, remote, cancellable,
						   refresh_remote_cb, g_object_ref (task));
	}

	finish_refresh_metadata_op (task);
}

static void
refresh_remote_cb (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
	FwupdClient *client = FWUPD_CLIENT (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	RefreshMetadataData *data = g_task_get_task_data (task);
	g_autoptr(GError) local_error = NULL;

	if (!fwupd_client_refresh_remote_finish (client, result, &local_error)) {
		gs_plugin_fwupd_error_convert (&local_error);
		if (data->error == NULL)
			data->error = g_steal_pointer (&local_error);
		else
			g_debug ("Another remote refresh error: %s", local_error->message);
	}

	finish_refresh_metadata_op (task);
}

static void
finish_refresh_metadata_op (GTask *task)
{
	RefreshMetadataData *data = g_task_get_task_data (task);

	g_assert (data->n_operations_pending > 0);
	data->n_operations_pending--;

	if (data->n_operations_pending == 0) {
		if (data->error != NULL)
			g_task_return_error (task, g_steal_pointer (&data->error));
		else
			g_task_return_boolean (task, TRUE);
	}
}

static gboolean
gs_plugin_fwupd_refresh_metadata_finish (GsPlugin      *plugin,
                                         GAsyncResult  *result,
                                         GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
gs_plugin_fwupd_install (GsPluginFwupd  *self,
                         GsApp          *app,
                         GCancellable   *cancellable,
                         GError        **error)
{
	const gchar *device_id;
	FwupdInstallFlags install_flags = 0;
	GFile *local_file;
	g_autofree gchar *filename = NULL;
	gboolean downloaded_to_cache = FALSE;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(GError) error_local = NULL;

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
		g_autoptr(GFile) file = g_file_new_for_path (filename);
		gs_app_set_state (app, GS_APP_STATE_INSTALLING);
		if (!fwupd_client_download_file (self->client,
						 uri, file,
						 FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
						 cancellable,
						 error)) {
			gs_plugin_fwupd_error_convert (error);
			return FALSE;
		}

		downloaded_to_cache = TRUE;
	}

	/* limit to single device? */
	device_id = gs_fwupd_app_get_device_id (app);
	if (device_id == NULL)
		device_id = FWUPD_DEVICE_ID_ANY;

	/* set the last object */
	g_set_object (&self->app_current, app);

	/* only offline supported */
	if (gs_app_get_metadata_item (app, "fwupd::OnlyOffline") != NULL)
		install_flags |= FWUPD_INSTALL_FLAG_OFFLINE;

	gs_app_set_state (app, GS_APP_STATE_INSTALLING);
	if (!fwupd_client_install (self->client, device_id,
				   filename, install_flags,
				   cancellable, error)) {
		gs_plugin_fwupd_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* delete the file from the cache */
	gs_app_set_state (app, GS_APP_STATE_INSTALLED);
	if (downloaded_to_cache) {
		if (!g_file_delete (local_file, cancellable, error))
			return FALSE;
	}

	/* does the device have an update message */
	dev = fwupd_client_get_device_by_id (self->client, device_id,
					     cancellable, &error_local);
	if (dev == NULL) {
		/* NOTE: this is probably entirely fine; some devices do not
		 * re-enumerate until replugged manually or the machine is
		 * rebooted -- and the metadata to know that is only available
		 * in a too-new-to-depend-on fwupd version */
		g_debug ("failed to find device after install: %s", error_local->message);
	} else {
		if (fwupd_device_get_update_message (dev) != NULL) {
			g_autoptr(AsScreenshot) ss = as_screenshot_new ();

			/* image is optional */
			if (fwupd_device_get_update_image (dev) != NULL) {
				g_autoptr(AsImage) im = as_image_new ();
				as_image_set_kind (im, AS_IMAGE_KIND_SOURCE);
				as_image_set_url (im, fwupd_device_get_update_image (dev));
				as_screenshot_add_image (ss, im);
			}

			/* caption is required */
			as_screenshot_set_kind (ss, AS_SCREENSHOT_KIND_DEFAULT);
			as_screenshot_set_caption (ss, fwupd_device_get_update_message (dev), NULL);
			gs_app_set_action_screenshot (app, ss);

			/* require the dialog */
			gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_USER_ACTION);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
gs_plugin_fwupd_modify_source (GsPluginFwupd  *self,
                               GsApp          *app,
                               gboolean        enabled,
                               GCancellable   *cancellable,
                               GError        **error)
{
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
	                  GS_APP_STATE_INSTALLING : GS_APP_STATE_REMOVING);
	if (!fwupd_client_modify_remote (self->client,
	                                 remote_id,
	                                 "Enabled",
	                                 enabled ? "true" : "false",
	                                 cancellable,
	                                 error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, enabled ?
	                  GS_APP_STATE_INSTALLED : GS_APP_STATE_AVAILABLE);

	gs_plugin_repository_changed (GS_PLUGIN (self), app);

	return TRUE;
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);
	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin))
		return TRUE;

	/* source -> remote, handled by dedicated function */
	g_return_val_if_fail (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY, FALSE);

	/* firmware */
	return gs_plugin_fwupd_install (self, app, cancellable, error);
}

gboolean
gs_plugin_download_app (GsPlugin *plugin,
			GsApp *app,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);
	GFile *local_file;
	g_autofree gchar *filename = NULL;
	gpointer schedule_entry_handle = NULL;
	g_autoptr(GError) error_local = NULL;

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin))
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
		g_autoptr(GFile) file = g_file_new_for_path (filename);
		gboolean download_success;

		if (!gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE)) {
			if (!gs_metered_block_app_on_download_scheduler (app, &schedule_entry_handle, cancellable, &error_local)) {
				g_warning ("Failed to block on download scheduler: %s",
					   error_local->message);
				g_clear_error (&error_local);
			}
		}

		download_success = fwupd_client_download_file (self->client,
							       uri, file,
							       FWUPD_CLIENT_DOWNLOAD_FLAG_NONE,
							       cancellable,
							       error);
		if (!download_success)
			gs_plugin_fwupd_error_convert (error);

		if (!gs_metered_remove_from_download_scheduler (schedule_entry_handle, NULL, &error_local))
			g_warning ("Failed to remove schedule entry: %s", error_local->message);

		if (!download_success)
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
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);
	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin))
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
		if (!fwupd_client_unlock (self->client, device_id,
					  cancellable, error)) {
			gs_plugin_fwupd_error_convert (error);
			return FALSE;
		}
		return TRUE;
	}

	/* update means install */
	if (!gs_plugin_fwupd_install (self, app, cancellable, error)) {
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
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);
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
	devices = fwupd_client_get_details (self->client,
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

		/* we *might* have no update view for local files */
		gs_app_set_version (app, gs_app_get_update_version (app));
		gs_app_set_description (app, GS_APP_QUALITY_LOWEST,
					gs_app_get_update_details_markup (app));
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
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);
	g_autoptr(GPtrArray) remotes = NULL;

	/* find all remotes */
	remotes = fwupd_client_get_remotes (self->client, cancellable, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		g_autofree gchar *id = NULL;
		g_autoptr(GsApp) app = NULL;

		/* ignore these, they're built in */
		if (fwupd_remote_get_kind (remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
			continue;

		/* create something that we can use to enable/disable */
		id = g_strdup_printf ("org.fwupd.%s.remote", fwupd_remote_get_id (remote));
		app = gs_app_new (id);
		gs_app_set_kind (app, AS_COMPONENT_KIND_REPOSITORY);
		gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
		gs_app_set_state (app, fwupd_remote_get_enabled (remote) ?
				  GS_APP_STATE_INSTALLED : GS_APP_STATE_AVAILABLE);
		gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST,
				 fwupd_remote_get_title (remote));
		gs_app_set_agreement (app, fwupd_remote_get_agreement (remote));
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
				fwupd_remote_get_metadata_uri (remote));
		gs_app_set_metadata (app, "fwupd::remote-id",
				     fwupd_remote_get_id (remote));
		gs_app_set_management_plugin (app, plugin);
		gs_app_set_metadata (app, "GnomeSoftware::PackagingFormat", "fwupd");
		gs_app_set_metadata (app, "GnomeSoftware::SortKey", "800");
		gs_app_set_origin_ui (app, _("Firmware"));
		gs_app_list_add (list, app);
	}
	return TRUE;
}

static gboolean
gs_plugin_fwupd_refresh_single_remote (GsPluginFwupd *self,
				       GsApp *repo,
				       guint cache_age,
				       GCancellable *cancellable,
				       GError **error)
{
	g_autoptr(GPtrArray) remotes = NULL;
	g_autoptr(GError) error_local = NULL;
	const gchar *remote_id;

	remote_id = gs_app_get_metadata_item (repo, "fwupd::remote-id");
	g_return_val_if_fail (remote_id != NULL, FALSE);

	remotes = fwupd_client_get_remotes (self->client, cancellable, &error_local);
	if (remotes == NULL) {
		g_debug ("No remotes found: %s", error_local ? error_local->message : "Unknown error");
		if (g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO) ||
		    g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED) ||
		    g_error_matches (error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		gs_plugin_fwupd_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (g_strcmp0 (remote_id, fwupd_remote_get_id (remote)) == 0) {
			if (fwupd_remote_get_enabled (remote) &&
			    fwupd_remote_get_kind (remote) != FWUPD_REMOTE_KIND_LOCAL &&
			    !remote_cache_is_expired (remote, cache_age) &&
			    !fwupd_client_refresh_remote (self->client, remote, cancellable, error)) {
				gs_plugin_fwupd_error_convert (error);
				return FALSE;
			}
			break;
		}
	}

	return TRUE;
}

gboolean
gs_plugin_enable_repo (GsPlugin *plugin,
		       GsApp *repo,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);

	/* only process this app if it was created by this plugin */
	if (!gs_app_has_management_plugin (repo, plugin))
		return TRUE;

	/* source -> remote */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	if (!gs_plugin_fwupd_modify_source (self, repo, TRUE, cancellable, error))
		return FALSE;

	/* This can fail silently, it's only to update necessary caches, to provide
	 * up-to-date information after the successful repository enable/install. */
	gs_plugin_fwupd_refresh_single_remote (self, repo, 1, cancellable, NULL);

	return TRUE;
}

gboolean
gs_plugin_disable_repo (GsPlugin *plugin,
			GsApp *repo,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginFwupd *self = GS_PLUGIN_FWUPD (plugin);

	/* only process this app if it was created by this plugin */
	if (!gs_app_has_management_plugin (repo, plugin))
		return TRUE;

	/* source -> remote */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	return gs_plugin_fwupd_modify_source (self, repo, FALSE, cancellable, error);
}

static void
gs_plugin_fwupd_class_init (GsPluginFwupdClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_fwupd_dispose;

	plugin_class->setup_async = gs_plugin_fwupd_setup_async;
	plugin_class->setup_finish = gs_plugin_fwupd_setup_finish;
	plugin_class->refresh_metadata_async = gs_plugin_fwupd_refresh_metadata_async;
	plugin_class->refresh_metadata_finish = gs_plugin_fwupd_refresh_metadata_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_FWUPD;
}
