/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>
#include <gnome-software.h>

#include "gs-packagekit-helper.h"
#include "packagekit-common.h"

/*
 * SECTION:
 * Do a PackageKit UpdatePackages(ONLY_DOWNLOAD) method on refresh and
 * also convert any package files to applications the best we can.
 */

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

	/* we can return better results than dpkg directly */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "dpkg");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_mutex_clear (&priv->task_mutex);
	g_object_unref (priv->task);
}

static gboolean
_download_only (GsPlugin *plugin, GsAppList *list,
		GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkPackageSack) sack = NULL;
	g_autoptr(PkResults) results2 = NULL;
	g_autoptr(PkResults) results = NULL;

	/* get the list of packages to update */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	g_mutex_lock (&priv->task_mutex);
	/* never refresh the metadata here as this can surprise the frontend if
	 * we end up downloading a different set of packages than what was
	 * shown to the user */
	pk_client_set_cache_age (PK_CLIENT (priv->task), G_MAXUINT);
	results = pk_client_get_updates (PK_CLIENT (priv->task),
					 pk_bitfield_value (PK_FILTER_ENUM_NONE),
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&priv->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		return FALSE;
	}

	/* download all the packages */
	sack = pk_results_get_package_sack (results);
	if (pk_package_sack_get_size (sack) == 0)
		return TRUE;
	package_ids = pk_package_sack_get_ids (sack);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		gs_packagekit_helper_add_app (helper, app);
	}
	g_mutex_lock (&priv->task_mutex);
	/* never refresh the metadata here as this can surprise the frontend if
	 * we end up downloading a different set of packages than what was
	 * shown to the user */
	pk_client_set_cache_age (PK_CLIENT (priv->task), G_MAXUINT);
	results2 = pk_task_update_packages_sync (priv->task,
						 package_ids,
						 cancellable,
						 gs_packagekit_helper_cb, helper,
						 error);
	g_mutex_unlock (&priv->task_mutex);
	if (results2 == NULL) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_download (GsPlugin *plugin,
                    GsAppList *list,
                    GCancellable *cancellable,
                    GError **error)
{
	g_autoptr(GsAppList) list_tmp = gs_app_list_new ();

	/* TODO: Should this use Mogwai? */
	/* add any packages */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsAppList *related = gs_app_get_related (app);

		/* add this app */
		if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_PROXY))
			if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") == 0) {
				gs_app_list_add (list_tmp, app);
			continue;
		}

		/* add each related app */
		for (guint j = 0; j < gs_app_list_length (related); j++) {
			GsApp *app_tmp = gs_app_list_index (related, j);
			if (g_strcmp0 (gs_app_get_management_plugin (app_tmp), "packagekit") == 0)
				gs_app_list_add (list_tmp, app_tmp);
		}
	}
	if (gs_app_list_length (list_tmp) > 0)
		return _download_only (plugin, list_tmp, cancellable, error);

	return TRUE;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));
	g_autoptr(PkResults) results = NULL;

	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	gs_packagekit_helper_add_app (helper, app_dl);

	g_mutex_lock (&priv->task_mutex);
	/* cache age of 1 is user-initiated */
	pk_client_set_background (PK_CLIENT (priv->task), cache_age > 1);
	pk_client_set_cache_age (PK_CLIENT (priv->task), cache_age);
	/* refresh the metadata */
	results = pk_client_refresh_cache (PK_CLIENT (priv->task),
	                                   FALSE /* force */,
	                                   cancellable,
	                                   gs_packagekit_helper_cb, helper,
	                                   error);
	g_mutex_unlock (&priv->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		return FALSE;
	}

	return TRUE;
}
