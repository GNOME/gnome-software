/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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

#include <gnome-software.h>

#include "packagekit-common.h"

/*
 * SECTION:
 * Uses the system PackageKit instance to return installed packages,
 * sources and the ability to add and remove packages.
 *
 * Requires:    | [source-id]
 * Refines:     | [source-id], [source], [update-details], [management-plugin]
 */

struct GsPluginData {
	PkTask			*task;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->task = pk_task_new ();
	pk_client_set_background (PK_CLIENT (priv->task), FALSE);
	pk_client_set_interactive (PK_CLIENT (priv->task), FALSE);
	pk_client_set_cache_age (PK_CLIENT (priv->task), G_MAXUINT);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->task);
}

typedef struct {
	GsApp		*app;
	GsPlugin	*plugin;
	AsProfileTask	*ptask;
} ProgressData;

static void
gs_plugin_packagekit_progress_cb (PkProgress *progress,
				  PkProgressType type,
				  gpointer user_data)
{
	ProgressData *data = (ProgressData *) user_data;
	GsPlugin *plugin = data->plugin;

	if (type == PK_PROGRESS_TYPE_STATUS) {
		GsPluginStatus plugin_status;
		PkStatusEnum status;
		g_object_get (progress,
			      "status", &status,
			      NULL);

		/* profile */
		if (status == PK_STATUS_ENUM_SETUP) {
			data->ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
						"packagekit-refine::transaction");
		} else if (status == PK_STATUS_ENUM_FINISHED) {
			g_clear_pointer (&data->ptask, as_profile_task_free);
		}

		plugin_status = packagekit_status_enum_to_plugin_status (status);
		if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
			gs_plugin_status_update (plugin, data->app, plugin_status);

	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		gint percentage = pk_progress_get_percentage (progress);
		if (data->app != NULL && percentage >= 0 && percentage <= 100)
			gs_app_set_progress (data->app, (guint) percentage);
	}
}

static gboolean
gs_plugin_add_sources_related (GsPlugin *plugin,
			       GHashTable *hash,
			       GCancellable *cancellable,
			       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	guint i;
	GsApp *app;
	GsApp *app_tmp;
	PkBitfield filter;
	ProgressData data;
	const gchar *id;
	gboolean ret = TRUE;
	g_autoptr(GsAppList) installed = gs_app_list_new ();
	g_autoptr(PkResults) results = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	data.app = NULL;
	data.plugin = plugin;
	data.ptask = NULL;

	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "packagekit::add-sources-related");
	g_assert (ptask != NULL);
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
					 PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_NOT_COLLECTIONS,
					 -1);
	results = pk_client_get_packages (PK_CLIENT(priv->task),
					   filter,
					   cancellable,
					   gs_plugin_packagekit_progress_cb, &data,
					   error);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;
	ret = gs_plugin_packagekit_add_results (plugin,
						installed,
						results,
						error);
	if (!ret)
		return FALSE;
	for (i = 0; i < gs_app_list_length (installed); i++) {
		g_auto(GStrv) split = NULL;
		app = gs_app_list_index (installed, i);
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
	PkBitfield filter;
	PkRepoDetail *rd;
	ProgressData data;
	const gchar *id;
	guint i;
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	data.app = NULL;
	data.plugin = plugin;
	data.ptask = NULL;

	/* ask PK for the repo details */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_SOURCE,
					 PK_FILTER_ENUM_NOT_SUPPORTED,
					 -1);
	results = pk_client_get_repo_list (PK_CLIENT(priv->task),
					   filter,
					   cancellable,
					   gs_plugin_packagekit_progress_cb, &data,
					   error);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	array = pk_results_get_repo_detail_array (results);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		rd = g_ptr_array_index (array, i);
		id = pk_repo_detail_get_id (rd);
		app = gs_app_new (id);
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		gs_app_set_kind (app, AS_APP_KIND_SOURCE);
		gs_app_set_state (app, pk_repo_detail_get_enabled (rd) ?
				  AS_APP_STATE_INSTALLED : AS_APP_STATE_AVAILABLE);
		gs_app_set_name (app,
				 GS_APP_QUALITY_LOWEST,
				 pk_repo_detail_get_description (rd));
		gs_app_set_summary (app,
				    GS_APP_QUALITY_LOWEST,
				    pk_repo_detail_get_description (rd));
		gs_app_list_add (list, app);
		g_hash_table_insert (hash,
				     g_strdup (id),
				     (gpointer) app);
	}

	/* get every application on the system and add it as a related package
	 * if it matches */
	return gs_plugin_add_sources_related (plugin, hash, cancellable, error);
}

static gboolean
gs_plugin_app_source_enable (GsPlugin *plugin,
			     GsApp *app,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	ProgressData data;
	g_autoptr(PkResults) results = NULL;

	data.app = app;
	data.plugin = plugin;
	data.ptask = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, app, GS_PLUGIN_STATUS_WAITING);
	results = pk_client_repo_enable (PK_CLIENT (priv->task),
					 gs_app_get_origin (app),
					 TRUE,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, &data,
					 error);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_utils_error_add_unique_id (error, app);
		return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GPtrArray *addons;
	GPtrArray *source_ids;
	ProgressData data;
	const gchar *package_id;
	guint i, j;
	g_autofree gchar *local_filename = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GPtrArray) array_package_ids = NULL;
	g_autoptr(PkResults) results = NULL;

	data.app = app;
	data.plugin = plugin;
	data.ptask = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* we enable the repo */
	if (gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {

		/* get everything up front we need */
		source_ids = gs_app_get_source_ids (app);
		package_ids = g_new0 (gchar *, 2);
		package_ids[0] = g_strdup (g_ptr_array_index (source_ids, 0));

		/* enable the source */
		if (!gs_plugin_app_source_enable (plugin, app, cancellable, error))
			return FALSE;

		/* FIXME: this is a hack, to allow PK time to re-initialize
		 * everything in order to match an actual result. The root cause
		 * is probably some kind of hard-to-debug race in the daemon. */
		g_usleep (G_USEC_PER_SEC * 3);

		/* actually install the package */
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		results = pk_task_install_packages_sync (priv->task,
							 package_ids,
							 cancellable,
							 gs_plugin_packagekit_progress_cb, &data,
							 error);
		if (!gs_plugin_packagekit_results_valid (results, error)) {
			gs_app_set_state_recover (app);
			return FALSE;
		}

		/* state is known */
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);

		/* no longer valid */
		gs_app_clear_source_ids (app);
		return TRUE;
	}

	/* get the list of available package ids to install */
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_UPDATABLE:
		source_ids = gs_app_get_source_ids (app);
		if (source_ids->len == 0) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "installing not available");
			return FALSE;
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
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "no packages to install");
			return FALSE;
		}
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		addons = gs_app_get_addons (app);
		for (i = 0; i < addons->len; i++) {
			GsApp *addon = g_ptr_array_index (addons, i);
			if (gs_app_get_to_be_installed (addon))
				gs_app_set_state (addon, AS_APP_STATE_INSTALLING);
		}
		results = pk_task_install_packages_sync (priv->task,
							 (gchar **) array_package_ids->pdata,
							 cancellable,
							 gs_plugin_packagekit_progress_cb, &data,
							 error);
		if (!gs_plugin_packagekit_results_valid (results, error)) {
			gs_app_set_state_recover (app);
			return FALSE;
		}

		/* state is known */
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);

		break;
	case AS_APP_STATE_AVAILABLE_LOCAL:
		if (gs_app_get_local_file (app) == NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "local package, but no filename");
			return FALSE;
		}
		local_filename = g_file_get_path (gs_app_get_local_file (app));
		package_ids = g_strsplit (local_filename, "\t", -1);
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		results = pk_task_install_files_sync (priv->task,
						      package_ids,
						      cancellable,
						      gs_plugin_packagekit_progress_cb, &data,
						      error);
		if (!gs_plugin_packagekit_results_valid (results, error)) {
			gs_app_set_state_recover (app);
			return FALSE;
		}

		/* state is known */
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);

		/* get the new icon from the package */
		gs_app_set_local_file (app, NULL);
		gs_app_add_icon (app, NULL);
		gs_app_set_pixbuf (app, NULL);
		break;
	default:
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "do not know how to install app in state %s",
			     as_app_state_to_string (gs_app_get_state (app)));
		return FALSE;
	}

	/* no longer valid */
	gs_app_clear_source_ids (app);

	return TRUE;
}

static gboolean
gs_plugin_app_source_disable (GsPlugin *plugin,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	ProgressData data;
	g_autoptr(PkResults) results = NULL;

	data.app = app;
	data.plugin = plugin;
	data.ptask = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, app, GS_PLUGIN_STATUS_WAITING);
	results = pk_client_repo_enable (PK_CLIENT (priv->task),
					 gs_app_get_id (app),
					 FALSE,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, &data,
					 error);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_utils_error_add_unique_id (error, app);
		return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_app_source_remove (GsPlugin *plugin,
			     GsApp *app,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	ProgressData data;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(PkResults) results = NULL;

	data.app = NULL;
	data.plugin = plugin;
	data.ptask = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, app, GS_PLUGIN_STATUS_WAITING);
	results = pk_client_repo_remove (PK_CLIENT (priv->task),
					 pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_NONE, -1),
					 gs_app_get_id (app),
					 TRUE,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, &data,
					 &error_local);
	if (results == NULL) {
		/* fall back to disabling it */
		g_warning ("ignoring source remove error, trying disable: %s",
			   error_local->message);
		return gs_plugin_app_source_disable (plugin, app,
						     cancellable, error);
	}
	return TRUE;
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *package_id;
	GPtrArray *source_ids;
	ProgressData data;
	guint i;
	guint cnt = 0;
	g_autoptr(PkResults) results = NULL;
	g_auto(GStrv) package_ids = NULL;

	data.app = app;
	data.plugin = plugin;
	data.ptask = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* remove repo and all apps in it */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE) {
		return gs_plugin_app_source_remove (plugin, app,
						    cancellable, error);
	}

	/* get the list of available package ids to install */
	source_ids = gs_app_get_source_ids (app);
	if (source_ids->len == 0) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "removing not available");
		return FALSE;
	}
	package_ids = g_new0 (gchar *, source_ids->len + 1);
	for (i = 0; i < source_ids->len; i++) {
		package_id = g_ptr_array_index (source_ids, i);
		if (g_strstr_len (package_id, -1, ";installed") == NULL)
			continue;
		package_ids[cnt++] = g_strdup (package_id);
	}
	if (cnt == 0) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no packages to remove");
		return FALSE;
	}

	/* do the action */
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	results = pk_task_remove_packages_sync (priv->task,
						package_ids,
						TRUE, FALSE,
						cancellable,
						gs_plugin_packagekit_progress_cb, &data,
						error);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is not known: we don't know if we can re-install this app */
	gs_app_set_state (app, AS_APP_STATE_UNKNOWN);

	/* no longer valid */
	gs_app_clear_source_ids (app);

	return TRUE;
}

gboolean
gs_plugin_add_search_files (GsPlugin *plugin,
                            gchar **search,
                            GsAppList *list,
                            GCancellable *cancellable,
                            GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	PkBitfield filter;
	ProgressData data;
	g_autoptr(PkResults) results = NULL;

	data.app = NULL;
	data.plugin = plugin;
	data.ptask = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 -1);
	results = pk_client_search_files (PK_CLIENT (priv->task),
	                                  filter,
	                                  search,
	                                  cancellable,
	                                  gs_plugin_packagekit_progress_cb, &data,
	                                  error);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;

	/* add results */
	return gs_plugin_packagekit_add_results (plugin, list, results, error);
}

gboolean
gs_plugin_add_search_what_provides (GsPlugin *plugin,
                                    gchar **search,
                                    GsAppList *list,
                                    GCancellable *cancellable,
                                    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	PkBitfield filter;
	ProgressData data;
	g_autoptr(PkResults) results = NULL;

	data.app = NULL;
	data.plugin = plugin;
	data.ptask = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 -1);
	results = pk_client_what_provides (PK_CLIENT (priv->task),
	                                   filter,
	                                   search,
	                                   cancellable,
	                                   gs_plugin_packagekit_progress_cb, &data,
	                                   error);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;

	/* add results */
	return gs_plugin_packagekit_add_results (plugin, list, results, error);
}

gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;
	return gs_plugin_app_launch (plugin, app, error);
}
