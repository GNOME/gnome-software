/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>

#include <gnome-software.h>

#include "gs-packagekit-helper.h"
#include "packagekit-common.h"

struct GsPluginData {
	PkTask			*task;
	GMutex			 task_mutex;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	g_mutex_init (&priv->task_mutex);
	priv->task = pk_task_new ();
	pk_task_set_only_download (priv->task, TRUE);
	pk_client_set_background (PK_CLIENT (priv->task), TRUE);
	pk_client_set_cache_age (PK_CLIENT (priv->task), 60 * 60 * 24);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_mutex_clear (&priv->task_mutex);
	g_object_unref (priv->task);
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_kind (app) == AS_APP_KIND_OS_UPGRADE)
		gs_app_set_management_plugin (app, "packagekit");
}

gboolean
gs_plugin_app_upgrade_download (GsPlugin *plugin,
				GsApp *app,
				GCancellable *cancellable,
				GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
		return TRUE;

	/* check is distro-upgrade */
	if (gs_app_get_kind (app) != AS_APP_KIND_OS_UPGRADE)
		return TRUE;

	/* ask PK to download enough packages to upgrade the system */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	gs_packagekit_helper_set_progress_app (helper, app);
	g_mutex_lock (&priv->task_mutex);
	results = pk_task_upgrade_system_sync (priv->task,
					       gs_app_get_version (app),
					       PK_UPGRADE_KIND_ENUM_COMPLETE,
					       cancellable,
					       gs_packagekit_helper_cb, helper,
					       error);
	g_mutex_unlock (&priv->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	return TRUE;
}
