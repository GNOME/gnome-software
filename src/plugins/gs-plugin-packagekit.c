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

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>
#include <glib/gi18n.h>

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
	return "packagekit";
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
	pk_client_set_background (PK_CLIENT (plugin->priv->task), FALSE);
	pk_client_set_interactive (PK_CLIENT (plugin->priv->task), FALSE);
	pk_client_set_cache_age (PK_CLIENT (plugin->priv->task), G_MAXUINT);
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->task);
}

/**
 * gs_plugin_packagekit_progress_cb:
 **/
static void
gs_plugin_packagekit_progress_cb (PkProgress *progress,
				  PkProgressType type,
				  gpointer user_data)
{
	GsPluginStatus plugin_status;
	PkStatusEnum status;
	GsPlugin *plugin = GS_PLUGIN (user_data);

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;
	g_object_get (progress,
		      "status", &status,
		      NULL);

	/* profile */
	if (status == PK_STATUS_ENUM_SETUP) {
		gs_profile_start (plugin->profile,
				  "packagekit-refine::transaction");
	} else if (status == PK_STATUS_ENUM_FINISHED) {
		gs_profile_stop (plugin->profile,
				 "packagekit-refine::transaction");
	}

	plugin_status = packagekit_status_enum_to_plugin_status (status);
	if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
		gs_plugin_status_update (plugin, NULL, plugin_status);
}

/**
 * gs_plugin_add_installed:
 */
gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GList **list,
			 GCancellable *cancellable,
			 GError **error)
{
	gboolean ret = TRUE;
	PkBitfield filter;
	PkResults *results;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* do sync call */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
					 PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 PK_FILTER_ENUM_NOT_COLLECTIONS,
					 -1);
	results = pk_client_get_packages (PK_CLIENT(plugin->priv->task),
					  filter,
					  cancellable,
					  gs_plugin_packagekit_progress_cb, plugin,
					  error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* add results */
	ret = gs_plugin_packagekit_add_results (plugin, list, results, error);
	if (!ret)
		goto out;
out:
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_add_sources_related:
 */
static gboolean
gs_plugin_add_sources_related (GsPlugin *plugin,
			       GHashTable *hash,
			       GCancellable *cancellable,
			       GError **error)
{
	GList *installed = NULL;
	GList *l;
	GsApp *app;
	GsApp *app_tmp;
	PkBitfield filter;
	PkResults *results = NULL;
	const gchar *id;
	gboolean ret = TRUE;
	gchar **split;

	gs_profile_start (plugin->profile, "packagekit::add-sources-related");
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
					 PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_NOT_COLLECTIONS,
					 -1);
	results = pk_client_get_packages (PK_CLIENT(plugin->priv->task),
					   filter,
					   cancellable,
					   gs_plugin_packagekit_progress_cb, plugin,
					   error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}
	ret = gs_plugin_packagekit_add_results (plugin,
						&installed,
						results,
						error);
	if (!ret)
		goto out;
	for (l = installed; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		split = pk_package_id_split (gs_app_get_source_id_default (app));
		if (g_str_has_prefix (split[PK_PACKAGE_ID_DATA], "installed:")) {
			id = split[PK_PACKAGE_ID_DATA] + 10;
			app_tmp = g_hash_table_lookup (hash, id);
			if (app_tmp != NULL) {
				g_debug ("found package %s from %s",
					 gs_app_get_source_default (app), id);
				gs_app_add_related (app_tmp, app);
			}
		}
		g_strfreev (split);
	}
out:
	gs_profile_stop (plugin->profile, "packagekit::add-sources-related");
	gs_plugin_list_free (installed);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_add_sources:
 */
gboolean
gs_plugin_add_sources (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	GPtrArray *array = NULL;
	GsApp *app;
	PkBitfield filter;
	PkRepoDetail *rd;
	PkResults *results;
	const gchar *id;
	gboolean ret = TRUE;
	guint i;
	GHashTable *hash = NULL;

	/* ask PK for the repo details */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_SOURCE,
					 PK_FILTER_ENUM_NOT_SUPPORTED,
					 PK_FILTER_ENUM_INSTALLED,
					 -1);
	results = pk_client_get_repo_list (PK_CLIENT(plugin->priv->task),
					   filter,
					   cancellable,
					   gs_plugin_packagekit_progress_cb, plugin,
					   error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	array = pk_results_get_repo_detail_array (results);
	for (i = 0; i < array->len; i++) {
		rd = g_ptr_array_index (array, i);
#if PK_CHECK_VERSION(0,9,1)
		id = pk_repo_detail_get_id (rd);
		app = gs_app_new (id);
		gs_app_set_management_plugin (app, "PackageKit");
		gs_app_set_kind (app, GS_APP_KIND_SOURCE);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_set_name (app,
				 GS_APP_QUALITY_LOWEST,
				 pk_repo_detail_get_description (rd));
		gs_app_set_summary (app,
				    GS_APP_QUALITY_LOWEST,
				    pk_repo_detail_get_description (rd));
		gs_plugin_add_app (list, app);
		g_hash_table_insert (hash,
				     g_strdup (id),
				     (gpointer) app);
		g_object_unref (app);
#endif
	}

	/* get every application on the system and add it as a related package
	 * if it matches */
	ret = gs_plugin_add_sources_related (plugin, hash, cancellable, error);
	if (!ret)
		goto out;
out:
	if (hash != NULL)
		g_hash_table_unref (hash);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return ret;
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
	GPtrArray *addons;
	GPtrArray *array_package_ids = NULL;
	GPtrArray *source_ids;
	PkError *error_code = NULL;
	PkResults *results = NULL;
	const gchar *package_id;
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	guint i, j;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "PackageKit") != 0)
		goto out;

	/* get the list of available package ids to install */
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_UPDATABLE:
		source_ids = gs_app_get_source_ids (app);
		if (source_ids->len == 0) {
			ret = FALSE;
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "installing not available");
			goto out;
		}
		array_package_ids = g_ptr_array_new_with_free_func (g_free);
		for (i = 0; i < source_ids->len; i++) {
			package_id = g_ptr_array_index (source_ids, i);
			if (g_strstr_len (package_id, -1, ";installed") != NULL)
				continue;
			g_ptr_array_add (array_package_ids, g_strdup (package_id));
		}

		addons = gs_app_get_addons (app);
		for (i = 0; i < addons->len; i++) {
			GsApp *addon = g_ptr_array_index (addons, i);

			if (!gs_app_get_to_be_installed (addon))
				continue;

			source_ids = gs_app_get_source_ids (addon);
			for (j = 0; j < source_ids->len; j++) {
				package_id = g_ptr_array_index (source_ids, j);
				if (g_strstr_len (package_id, -1, ";installed") != NULL)
					continue;
				g_ptr_array_add (array_package_ids, g_strdup (package_id));
			}
		}
		g_ptr_array_add (array_package_ids, NULL);

		if (array_package_ids->len == 0) {
			ret = FALSE;
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "no packages to install");
			goto out;
		}
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		addons = gs_app_get_addons (app);
		for (i = 0; i < addons->len; i++) {
			GsApp *addon = g_ptr_array_index (addons, i);
			if (gs_app_get_to_be_installed (addon))
				gs_app_set_state (addon, AS_APP_STATE_INSTALLING);
		}
		results = pk_task_install_packages_sync (plugin->priv->task,
							 (gchar **) array_package_ids->pdata,
							 cancellable,
							 gs_plugin_packagekit_progress_cb, plugin,
							 error);
		if (results == NULL) {
			ret = FALSE;
			goto out;
		}
		break;
	case AS_APP_STATE_AVAILABLE_LOCAL:
		package_id = gs_app_get_metadata_item (app, "PackageKit::local-filename");
		if (package_id == NULL) {
			ret = FALSE;
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "local package, but no filename");
			goto out;
		}
		package_ids = g_strsplit (package_id, "\t", -1);
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		results = pk_task_install_files_sync (plugin->priv->task,
						      package_ids,
						      cancellable,
						      gs_plugin_packagekit_progress_cb, plugin,
						      error);
		if (results == NULL) {
			ret = FALSE;
			goto out;
		}
		break;
	default:
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "do not know how to install app in state %s",
			     as_app_state_to_string (gs_app_get_state (app)));
		goto out;
	}

	/* no longer valid */
	gs_app_clear_source_ids (app);

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to install package: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}
out:
	g_strfreev (package_ids);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array_package_ids != NULL)
		g_ptr_array_unref (array_package_ids);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_app_source_disable:
 */
static gboolean
gs_plugin_app_source_disable (GsPlugin *plugin,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	gboolean ret = TRUE;
	PkResults *results;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	results = pk_client_repo_enable (PK_CLIENT (plugin->priv->task),
					 gs_app_get_id (app),
					 FALSE,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, plugin,
					 error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}
out:
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_app_source_remove:
 */
static gboolean
gs_plugin_app_source_remove (GsPlugin *plugin,
			     GsApp *app,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean ret = TRUE;
#if PK_CHECK_VERSION(0,9,1)
	PkResults *results;
	GError *error_local = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	results = pk_client_repo_remove (PK_CLIENT (plugin->priv->task),
					 pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_NONE, -1),
					 gs_app_get_id (app),
					 TRUE,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, plugin,
					 &error_local);
	if (results == NULL) {
		/* fall back to disabling it */
		g_warning ("ignoring source remove error, trying disable: %s",
			   error_local->message);
		g_error_free (error_local);
		ret = gs_plugin_app_source_disable (plugin,
						    app,
						    cancellable,
						    error);
		goto out;
	}
out:
	if (results != NULL)
		g_object_unref (results);
#else
	ret = gs_plugin_app_source_disable (plugin, app, cancellable, error);
#endif
	return ret;
}

/**
 * gs_plugin_app_remove:
 */
gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *package_id;
	gchar **package_ids = NULL;
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	PkResults *results = NULL;
	GPtrArray *source_ids;
	guint i;
	guint cnt = 0;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "PackageKit") != 0)
		goto out;

	/* remove repo and all apps in it */
	if (gs_app_get_kind (app) == GS_APP_KIND_SOURCE) {
		ret = gs_plugin_app_source_remove (plugin,
						   app,
						   cancellable,
						   error);
		goto out;
	}

	/* get the list of available package ids to install */
	source_ids = gs_app_get_source_ids (app);
	if (source_ids->len == 0) {
		ret = FALSE;
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "removing not available");
		goto out;
	}
	package_ids = g_new0 (gchar *, source_ids->len + 1);
	for (i = 0; i < source_ids->len; i++) {
		package_id = g_ptr_array_index (source_ids, i);
		if (g_strstr_len (package_id, -1, ";installed") == NULL)
			continue;
		package_ids[cnt++] = g_strdup (package_id);
	}
	if (cnt == 0) {
		ret = FALSE;
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no packages to remove");
		goto out;
	}

	/* do the action */
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	results = pk_task_remove_packages_sync (plugin->priv->task,
						package_ids,
						TRUE, FALSE,
						cancellable,
						gs_plugin_packagekit_progress_cb, plugin,
						error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* no longer valid */
	gs_app_clear_source_ids (app);

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to remove package: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}
out:
	g_strfreev (package_ids);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}
