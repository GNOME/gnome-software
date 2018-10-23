/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2016 Kalev Lember <klember@redhat.com>
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
 * Mark previously downloaded packages as zero size, and also allow
 * scheduling the offline update.
 */

struct GsPluginData {
	GFileMonitor		*monitor;
	GFileMonitor		*monitor_trigger;
	GPermission		*permission;
	gboolean		 is_triggered;
	GHashTable		*hash_prepared;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit-refresh");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit-refine");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "generic-updates");
	priv->hash_prepared = g_hash_table_new_full (g_str_hash, g_str_equal,
						     g_free, NULL);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_hash_table_unref (priv->hash_prepared);
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

static gboolean
gs_plugin_systemd_update_cache (GsPlugin *plugin, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GError) error_local = NULL;
	g_auto(GStrv) package_ids = NULL;

	/* invalidate */
	g_hash_table_remove_all (priv->hash_prepared);

	/* get new list of package-ids */
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
	for (guint i = 0; package_ids[i] != NULL; i++) {
		g_hash_table_insert (priv->hash_prepared,
				     g_strdup (package_ids[i]),
				     GUINT_TO_POINTER (1));
	}
	return TRUE;
}

static void
gs_plugin_systemd_updates_changed_cb (GFileMonitor *monitor,
				      GFile *file, GFile *other_file,
				      GFileMonitorEvent event_type,
				      gpointer user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);

	/* update UI */
	gs_plugin_systemd_update_cache (plugin, NULL);
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

static void
gs_plugin_systemd_refine_app (GsPlugin *plugin, GsApp *app)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *package_id;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
		return;

	/* the package is already downloaded */
	package_id = gs_app_get_source_id_default (app);
	if (package_id == NULL)
		return;
	if (g_hash_table_lookup (priv->hash_prepared, package_id) != NULL)
		gs_app_set_size_download (app, 0);
}

gboolean
gs_plugin_refine (GsPlugin *plugin,
                  GsAppList *list,
                  GsPluginRefineFlags flags,
                  GCancellable *cancellable,
                  GError **error)
{
	/* not now */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) == 0)
		return TRUE;

	/* re-read /var/lib/PackageKit/prepared-update */
	if (!gs_plugin_systemd_update_cache (plugin, error))
		return FALSE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsAppList *related = gs_app_get_related (app);
		/* refine the app itself */
		gs_plugin_systemd_refine_app (plugin, app);
		/* and anything related for proxy apps */
		for (guint j = 0; j < gs_app_list_length (related); j++) {
			GsApp *app_related = gs_app_list_index (related, j);
			gs_plugin_systemd_refine_app (plugin, app_related);
		}
	}

	return TRUE;
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
		"org.freedesktop.packagekit.trigger-offline-update",
		NULL, NULL);
	if (priv->permission != NULL) {
		g_signal_connect (priv->permission, "notify",
				  G_CALLBACK (gs_plugin_systemd_updates_permission_cb),
				  plugin);
	}

	/* get the list of currently downloaded packages */
	return gs_plugin_systemd_update_cache (plugin, error);
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
gs_plugin_update (GsPlugin *plugin,
		  GsAppList *list,
		  GCancellable *cancellable,
		  GError **error)
{
	/* any are us? */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsAppList *related = gs_app_get_related (app);

		/* not a proxy, which is somewhat odd... */
		if (!gs_app_has_quirk (app, AS_APP_QUIRK_IS_PROXY))
			return _systemd_trigger_app (plugin, app, cancellable, error);

		/* try to trigger each related app */
		for (guint j = 0; j < gs_app_list_length (related); j++) {
			GsApp *app_tmp = gs_app_list_index (related, j);
			if (!_systemd_trigger_app (plugin, app_tmp, cancellable, error))
				return FALSE;
		}
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
