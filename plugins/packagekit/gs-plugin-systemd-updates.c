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

#include <packagekit-glib2/packagekit.h>

#include "packagekit-common.h"

#include <gnome-software.h>

/*
 * SECTION:
 * Add previously downloads apps to the update list and also allow
 * scheduling the offline update.
 */

struct GsPluginData {
	GFileMonitor		*monitor;
	GFileMonitor		*monitor_trigger;
	GPermission		*permission;
	gboolean		 is_triggered;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->monitor != NULL)
		g_object_unref (priv->monitor);
	if (priv->monitor_trigger != NULL)
		g_object_unref (priv->monitor_trigger);
}

static void
gs_plugin_systemd_updates_permission_cb (GPermission *permission,
					 GParamSpec *pspec,
					 gpointer data)
{
	GsPlugin *plugin = GS_PLUGIN (data);
	gboolean ret = g_permission_get_allowed (permission) ||
			g_permission_get_can_acquire (permission);
	gs_plugin_set_allow_updates (plugin, ret);
}

static void
gs_plugin_systemd_updates_changed_cb (GFileMonitor *monitor,
				      GFile *file, GFile *other_file,
				      GFileMonitorEvent event_type,
				      gpointer user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);

	/* update UI */
	gs_plugin_updates_changed (plugin);
}

static void
gs_plugin_systemd_updates_refresh_is_triggered (GsPlugin *plugin, GCancellable *cancellable)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GFile) file_trigger = NULL;
	file_trigger = g_file_new_for_path ("/system-update");
	priv->is_triggered = g_file_query_exists (file_trigger, NULL);
	g_debug ("offline trigger is now %s",
		 priv->is_triggered ? "enabled" : "disabled");
}

static void
gs_plugin_systemd_trigger_changed_cb (GFileMonitor *monitor,
				      GFile *file, GFile *other_file,
				      GFileMonitorEvent event_type,
				      gpointer user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);
	gs_plugin_systemd_updates_refresh_is_triggered (plugin, NULL);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GFile) file_trigger = NULL;

	/* watch the prepared file */
	priv->monitor = pk_offline_get_prepared_monitor (cancellable, error);
	if (priv->monitor == NULL) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}
	g_signal_connect (priv->monitor, "changed",
			  G_CALLBACK (gs_plugin_systemd_updates_changed_cb),
			  plugin);

	/* watch the trigger file */
	file_trigger = g_file_new_for_path ("/system-update");
	priv->monitor_trigger = g_file_monitor_file (file_trigger,
						     G_FILE_MONITOR_NONE,
						     NULL,
						     error);
	if (priv->monitor_trigger == NULL) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}
	g_signal_connect (priv->monitor_trigger, "changed",
			  G_CALLBACK (gs_plugin_systemd_trigger_changed_cb),
			  plugin);

	/* check if we have permission to trigger the update */
	priv->permission = gs_utils_get_permission (
		"org.freedesktop.packagekit.trigger-offline-update");
	if (priv->permission != NULL) {
		g_signal_connect (priv->permission, "notify",
				  G_CALLBACK (gs_plugin_systemd_updates_permission_cb),
				  plugin);
	}
	return TRUE;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	guint i;
	g_autoptr(GError) error_local = NULL;
	g_auto(GStrv) package_ids = NULL;

	/* get the id's if the file exists */
	package_ids = pk_offline_get_prepared_ids (&error_local);
	if (package_ids == NULL) {
		if (g_error_matches (error_local,
				     PK_OFFLINE_ERROR,
				     PK_OFFLINE_ERROR_NO_DATA)) {
			return TRUE;
		}
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "Failed to get prepared IDs: %s",
			     error_local->message);
		return FALSE;
	}

	/* add them to the new array */
	for (i = 0; package_ids[i] != NULL; i++) {
		g_autoptr(GsApp) app = NULL;
		g_auto(GStrv) split = NULL;

		/* search in the cache */
		app = gs_plugin_cache_lookup (plugin, package_ids[i]);
		if (app != NULL) {
			gs_app_list_add (list, app);
			continue;
		}

		/* create new app */
		app = gs_app_new (NULL);
		gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);
		gs_app_set_management_plugin (app, "packagekit");
		gs_app_add_source_id (app, package_ids[i]);
		split = pk_package_id_split (package_ids[i]);
		gs_app_add_source (app, split[PK_PACKAGE_ID_NAME]);
		gs_app_set_update_version (app, split[PK_PACKAGE_ID_VERSION]);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
		gs_app_set_kind (app, AS_APP_KIND_GENERIC);
		gs_app_set_size_download (app, 0);
		gs_app_list_add (list, app);

		/* save in the cache */
		gs_plugin_cache_add (plugin, package_ids[i], app);
	}
	return TRUE;
}

static gboolean
_systemd_trigger_app (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* if we can process this online do not require a trigger */
	if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE)
		return TRUE;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
		return TRUE;

	/* already in correct state */
	if (priv->is_triggered)
		return TRUE;

	/* trigger offline update */
	if (!pk_offline_trigger (PK_OFFLINE_ACTION_REBOOT,
				 cancellable, error)) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}

	/* don't rely on the file monitor */
	gs_plugin_systemd_updates_refresh_is_triggered (plugin, cancellable);

	/* success */
	return TRUE;
}

gboolean
gs_plugin_update_app (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GPtrArray *related = gs_app_get_related (app);

	/* not a proxy, which is somewhat odd... */
	if (!gs_app_has_quirk (app, AS_APP_QUIRK_IS_PROXY))
		return _systemd_trigger_app (plugin, app, cancellable, error);

	/* try to trigger each related app */
	for (guint i = 0; i < related->len; i++) {
		GsApp *app_tmp = g_ptr_array_index (related, i);
		if (!_systemd_trigger_app (plugin, app_tmp, cancellable, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
gs_plugin_update_cancel (GsPlugin *plugin,
			 GsApp *app,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
		return TRUE;

	/* already in correct state */
	if (!priv->is_triggered)
		return TRUE;

	/* cancel offline update */
	if (!pk_offline_cancel (NULL, error))
		return FALSE;

	/* don't rely on the file monitor */
	gs_plugin_systemd_updates_refresh_is_triggered (plugin, cancellable);

	/* success! */
	return TRUE;
}

gboolean
gs_plugin_app_upgrade_trigger (GsPlugin *plugin,
                               GsApp *app,
                               GCancellable *cancellable,
                               GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
		return TRUE;
	return pk_offline_trigger_upgrade (PK_OFFLINE_ACTION_REBOOT, cancellable, error);
}
