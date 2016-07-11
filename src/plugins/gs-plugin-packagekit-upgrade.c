/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include <gs-plugin.h>

#include "packagekit-common.h"

struct GsPluginPrivate {
	PkTask			*task;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "packagekit-upgrade";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->task = pk_task_new ();
	pk_task_set_only_download (plugin->priv->task, TRUE);
	pk_client_set_background (PK_CLIENT (plugin->priv->task), TRUE);
	pk_client_set_cache_age (PK_CLIENT (plugin->priv->task), 60 * 60 * 24);
	pk_client_set_interactive (PK_CLIENT (plugin->priv->task), FALSE);
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->task);
}

typedef struct {
	GsApp		*app;
	GsPlugin	*plugin;
} ProgressData;

/**
 * gs_plugin_packagekit_progress_cb:
 **/
static void
gs_plugin_packagekit_progress_cb (PkProgress *progress,
				  PkProgressType type,
				  gpointer user_data)
{
	ProgressData *data = (ProgressData *) user_data;
	GsPlugin *plugin = data->plugin;
	if (type == PK_PROGRESS_TYPE_STATUS) {
		GsPluginStatus plugin_status;
		PkStatusEnum status = pk_progress_get_status (progress);
		plugin_status = packagekit_status_enum_to_plugin_status (status);
		if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
			gs_plugin_status_update (plugin, NULL, plugin_status);
	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		gint percentage = pk_progress_get_percentage (progress);
		if (percentage >= 0 && percentage <= 100)
			gs_plugin_progress_update (plugin, data->app, percentage);
	}
}

/**
 * gs_plugin_app_upgrade_download:
 */
gboolean
gs_plugin_app_upgrade_download (GsPlugin *plugin,
				GsApp *app,
				GCancellable *cancellable,
				GError **error)
{
	ProgressData data;
	g_autoptr(PkResults) results = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
		return TRUE;

	data.app = app;
	data.plugin = plugin;

	/* check is distro-upgrade */
	if (gs_app_get_kind (app) != AS_APP_KIND_OS_UPGRADE) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "app %s is not a distro upgrade",
			     gs_app_get_id (app));
		return FALSE;
	}

	/* ask PK to download enough packages to upgrade the system */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	results = pk_task_upgrade_system_sync (plugin->priv->task,
					       gs_app_get_version (app),
					       PK_UPGRADE_KIND_ENUM_COMPLETE,
					       cancellable,
					       gs_plugin_packagekit_progress_cb, &data,
					       error);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	return TRUE;
}
