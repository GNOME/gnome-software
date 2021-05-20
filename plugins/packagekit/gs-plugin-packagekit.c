/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>

#include <gnome-software.h>

#include "packagekit-common.h"
#include "gs-markdown.h"
#include "gs-packagekit-helper.h"

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
	GMutex			 task_mutex;

	PkControl		*control_refine;
	PkClient		*client_refine;
	GMutex			 client_mutex_refine;
};

static void gs_plugin_packagekit_updates_changed_cb (PkControl *control, GsPlugin *plugin);
static void gs_plugin_packagekit_repo_list_changed_cb (PkControl *control, GsPlugin *plugin);

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* core */
	g_mutex_init (&priv->task_mutex);
	priv->task = pk_task_new ();
	pk_client_set_background (PK_CLIENT (priv->task), FALSE);
	pk_client_set_cache_age (PK_CLIENT (priv->task), G_MAXUINT);
	pk_client_set_interactive (PK_CLIENT (priv->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* refine */
	g_mutex_init (&priv->client_mutex_refine);
	priv->client_refine = pk_client_new ();
	priv->control_refine = pk_control_new ();
	g_signal_connect (priv->control_refine, "updates-changed",
			  G_CALLBACK (gs_plugin_packagekit_updates_changed_cb), plugin);
	g_signal_connect (priv->control_refine, "repo-list-changed",
			  G_CALLBACK (gs_plugin_packagekit_repo_list_changed_cb), plugin);
	pk_client_set_background (priv->client_refine, FALSE);
	pk_client_set_cache_age (priv->client_refine, G_MAXUINT);
	pk_client_set_interactive (priv->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* need pkgname and ID */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* core */
	g_mutex_clear (&priv->task_mutex);
	g_object_unref (priv->task);

	/* refine */
	g_mutex_clear (&priv->client_mutex_refine);
	g_object_unref (priv->client_refine);
	g_object_unref (priv->control_refine);
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
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	const gchar *id;
	gboolean ret = TRUE;
	g_autoptr(GsAppList) installed = gs_app_list_new ();
	g_autoptr(PkResults) results = NULL;

	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
					 PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_NOT_COLLECTIONS,
					 -1);
	g_mutex_lock (&priv->task_mutex);
	pk_client_set_interactive (PK_CLIENT (priv->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_packages (PK_CLIENT(priv->task),
					   filter,
					   cancellable,
					   gs_packagekit_helper_cb, helper,
					   error);
	g_mutex_unlock (&priv->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to get sources related: ");
		return FALSE;
	}
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
		if (split == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "invalid package-id: %s",
				     gs_app_get_source_id_default (app));
			return FALSE;
		}
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
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	const gchar *id;
	guint i;
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* ask PK for the repo details */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_SOURCE,
					 PK_FILTER_ENUM_NOT_DEVELOPMENT,
					 PK_FILTER_ENUM_NOT_SUPPORTED,
					 -1);
	g_mutex_lock (&priv->task_mutex);
	pk_client_set_interactive (PK_CLIENT (priv->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_repo_list (PK_CLIENT(priv->task),
					   filter,
					   cancellable,
					   gs_packagekit_helper_cb, helper,
					   error);
	g_mutex_unlock (&priv->task_mutex);
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
		gs_app_set_kind (app, AS_COMPONENT_KIND_REPOSITORY);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
		gs_app_set_state (app, pk_repo_detail_get_enabled (rd) ?
				  GS_APP_STATE_INSTALLED : GS_APP_STATE_AVAILABLE);
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
gs_plugin_app_origin_repo_enable (GsPlugin *plugin,
                                  GsApp *app,
                                  GCancellable *cancellable,
                                  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(GsApp) repo_app = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkError) error_code = NULL;
	const gchar *repo_id;

	repo_id = gs_app_get_origin (app);
	if (repo_id == NULL) {
		g_set_error_literal (error,
		                     GS_PLUGIN_ERROR,
		                     GS_PLUGIN_ERROR_NOT_SUPPORTED,
		                     "origin not set");
		return FALSE;
	}

	/* do sync call */
	gs_plugin_status_update (plugin, app, GS_PLUGIN_STATUS_WAITING);
	g_mutex_lock (&priv->task_mutex);
	pk_client_set_interactive (PK_CLIENT (priv->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_repo_enable (PK_CLIENT (priv->task),
	                                 repo_id,
	                                 TRUE,
	                                 cancellable,
	                                 gs_packagekit_helper_cb, helper,
	                                 error);
	g_mutex_unlock (&priv->task_mutex);

	/* pk_client_repo_enable() returns an error if the repo is already enabled. */
	if (results != NULL &&
	    (error_code = pk_results_get_error_code (results)) != NULL &&
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_REPO_ALREADY_SET) {
		g_clear_error (error);
	} else if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_utils_error_add_origin_id (error, app);
		return FALSE;
	}

	/* now that the repo is enabled, the app (not the repo!) moves from
	 * UNAVAILABLE state to AVAILABLE */
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE);

	/* Construct a simple fake GsApp for the repository, used only by the signal handler */
	repo_app = gs_app_new (repo_id);
	gs_app_set_state (repo_app, GS_APP_STATE_INSTALLED);
	gs_plugin_repository_changed (plugin, repo_app);

	return TRUE;
}

static gboolean
gs_plugin_repo_enable (GsPlugin *plugin,
                       GsApp *app,
                       GCancellable *cancellable,
                       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkError) error_code = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, app, GS_PLUGIN_STATUS_WAITING);
	gs_app_set_state (app, GS_APP_STATE_INSTALLING);
	gs_packagekit_helper_add_app (helper, app);
	g_mutex_lock (&priv->task_mutex);
	pk_client_set_interactive (PK_CLIENT (priv->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_repo_enable (PK_CLIENT (priv->task),
					 gs_app_get_id (app),
					 TRUE,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&priv->task_mutex);

	/* pk_client_repo_enable() returns an error if the repo is already enabled. */
	if (results != NULL &&
	    (error_code = pk_results_get_error_code (results)) != NULL &&
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_REPO_ALREADY_SET) {
		g_clear_error (error);
	} else if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_app_set_state_recover (app);
		gs_utils_error_add_origin_id (error, app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, GS_APP_STATE_INSTALLED);

	gs_plugin_repository_changed (plugin, app);

	return TRUE;
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsAppList *addons;
	GPtrArray *source_ids;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	const gchar *package_id;
	guint i, j;
	g_autofree gchar *local_filename = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GPtrArray) array_package_ids = NULL;
	g_autoptr(PkResults) results = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* enable repo */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_REPOSITORY)
		return gs_plugin_repo_enable (plugin, app, cancellable, error);

	/* queue for install if installation needs the network */
	if (!gs_plugin_get_network_available (plugin)) {
		gs_app_set_state (app, GS_APP_STATE_QUEUED_FOR_INSTALL);
		return TRUE;
	}

	if (gs_app_get_state (app) == GS_APP_STATE_UNAVAILABLE) {
		/* get everything up front we need */
		source_ids = gs_app_get_source_ids (app);
		if (source_ids->len == 0) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "installing not available");
			return FALSE;
		}
		package_ids = g_new0 (gchar *, 2);
		package_ids[0] = g_strdup (g_ptr_array_index (source_ids, 0));

		/* enable the repo where the unavailable app is coming from */
		if (!gs_plugin_app_origin_repo_enable (plugin, app, cancellable, error))
			return FALSE;

		gs_app_set_state (app, GS_APP_STATE_INSTALLING);

		/* FIXME: this is a hack, to allow PK time to re-initialize
		 * everything in order to match an actual result. The root cause
		 * is probably some kind of hard-to-debug race in the daemon. */
		g_usleep (G_USEC_PER_SEC * 3);

		/* actually install the package */
		gs_packagekit_helper_add_app (helper, app);
		g_mutex_lock (&priv->task_mutex);
		results = pk_task_install_packages_sync (priv->task,
							 package_ids,
							 cancellable,
							 gs_packagekit_helper_cb, helper,
							 error);
		g_mutex_unlock (&priv->task_mutex);
		if (!gs_plugin_packagekit_results_valid (results, error)) {
			gs_app_set_state_recover (app);
			return FALSE;
		}

		/* state is known */
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);

		/* if we remove the app again later, we should be able to
		 * cancel the installation if we'd never installed it */
		gs_app_set_allow_cancel (app, TRUE);

		/* no longer valid */
		gs_app_clear_source_ids (app);
		return TRUE;
	}

	/* get the list of available package ids to install */
	switch (gs_app_get_state (app)) {
	case GS_APP_STATE_AVAILABLE:
	case GS_APP_STATE_UPDATABLE:
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
		for (i = 0; i < gs_app_list_length (addons); i++) {
			GsApp *addon = gs_app_list_index (addons, i);

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

		gs_app_set_state (app, GS_APP_STATE_INSTALLING);
		addons = gs_app_get_addons (app);
		for (i = 0; i < gs_app_list_length (addons); i++) {
			GsApp *addon = gs_app_list_index (addons, i);
			if (gs_app_get_to_be_installed (addon))
				gs_app_set_state (addon, GS_APP_STATE_INSTALLING);
		}
		gs_packagekit_helper_add_app (helper, app);
		g_mutex_lock (&priv->task_mutex);
		results = pk_task_install_packages_sync (priv->task,
							 (gchar **) array_package_ids->pdata,
							 cancellable,
							 gs_packagekit_helper_cb, helper,
							 error);
		g_mutex_unlock (&priv->task_mutex);
		if (!gs_plugin_packagekit_results_valid (results, error)) {
			for (i = 0; i < gs_app_list_length (addons); i++) {
				GsApp *addon = gs_app_list_index (addons, i);
				if (gs_app_get_state (addon) == GS_APP_STATE_INSTALLING)
					gs_app_set_state_recover (addon);
			}
			gs_app_set_state_recover (app);
			return FALSE;
		}

		/* state is known */
		for (i = 0; i < gs_app_list_length (addons); i++) {
			GsApp *addon = gs_app_list_index (addons, i);
			if (gs_app_get_state (addon) == GS_APP_STATE_INSTALLING) {
				gs_app_set_state (addon, GS_APP_STATE_INSTALLED);
				gs_app_clear_source_ids (addon);
			}
		}
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);

		break;
	case GS_APP_STATE_AVAILABLE_LOCAL:
		if (gs_app_get_local_file (app) == NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "local package, but no filename");
			return FALSE;
		}
		local_filename = g_file_get_path (gs_app_get_local_file (app));
		package_ids = g_strsplit (local_filename, "\t", -1);

		gs_app_set_state (app, GS_APP_STATE_INSTALLING);
		gs_packagekit_helper_add_app (helper, app);
		g_mutex_lock (&priv->task_mutex);
		results = pk_task_install_files_sync (priv->task,
						      package_ids,
						      cancellable,
						      gs_packagekit_helper_cb, helper,
						      error);
		g_mutex_unlock (&priv->task_mutex);
		if (!gs_plugin_packagekit_results_valid (results, error)) {
			gs_app_set_state_recover (app);
			return FALSE;
		}

		/* state is known */
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);

		/* get the new icon from the package */
		gs_app_set_local_file (app, NULL);
		gs_app_remove_all_icons (app);
		break;
	default:
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "do not know how to install app in state %s",
			     gs_app_state_to_string (gs_app_get_state (app)));
		return FALSE;
	}

	/* no longer valid */
	gs_app_clear_source_ids (app);

	return TRUE;
}

static gboolean
gs_plugin_repo_disable (GsPlugin *plugin,
                        GsApp *app,
                        GCancellable *cancellable,
                        GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkError) error_code = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, app, GS_PLUGIN_STATUS_WAITING);
	gs_app_set_state (app, GS_APP_STATE_REMOVING);
	gs_packagekit_helper_add_app (helper, app);
	g_mutex_lock (&priv->task_mutex);
	pk_client_set_interactive (PK_CLIENT (priv->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_repo_enable (PK_CLIENT (priv->task),
					 gs_app_get_id (app),
					 FALSE,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&priv->task_mutex);

	/* pk_client_repo_enable() returns an error if the repo is already enabled. */
	if (results != NULL &&
	    (error_code = pk_results_get_error_code (results)) != NULL &&
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_REPO_ALREADY_SET) {
		g_clear_error (error);
	} else if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_app_set_state_recover (app);
		gs_utils_error_add_origin_id (error, app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE);

	gs_plugin_repository_changed (plugin, app);

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
	GsAppList *addons;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	guint i;
	guint cnt = 0;
	g_autoptr(PkResults) results = NULL;
	g_auto(GStrv) package_ids = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* disable repo */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_REPOSITORY)
		return gs_plugin_repo_disable (plugin, app, cancellable, error);

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
	gs_app_set_state (app, GS_APP_STATE_REMOVING);
	gs_packagekit_helper_add_app (helper, app);
	g_mutex_lock (&priv->task_mutex);
	results = pk_task_remove_packages_sync (priv->task,
						package_ids,
						TRUE, GS_PACKAGEKIT_AUTOREMOVE,
						cancellable,
						gs_packagekit_helper_cb, helper,
						error);
	g_mutex_unlock (&priv->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* Make sure addons' state is updated as well */
	addons = gs_app_get_addons (app);
	for (i = 0; i < gs_app_list_length (addons); i++) {
		GsApp *addon = gs_app_list_index (addons, i);
		if (gs_app_get_state (addon) == GS_APP_STATE_INSTALLED) {
			gs_app_set_state (addon, GS_APP_STATE_UNKNOWN);
			gs_app_clear_source_ids (addon);
		}
	}

	/* state is not known: we don't know if we can re-install this app */
	gs_app_set_state (app, GS_APP_STATE_UNKNOWN);

	/* no longer valid */
	gs_app_clear_source_ids (app);

	return TRUE;
}

static GsApp *
gs_plugin_packagekit_build_update_app (GsPlugin *plugin, PkPackage *package)
{
	GsApp *app = gs_plugin_cache_lookup (plugin, pk_package_get_id (package));
	if (app != NULL)
		return app;
	app = gs_app_new (NULL);
	gs_plugin_packagekit_set_packaging_format (plugin, app);
	gs_app_add_source (app, pk_package_get_name (package));
	gs_app_add_source_id (app, pk_package_get_id (package));
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST,
			 pk_package_get_name (package));
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
			    pk_package_get_summary (package));
	gs_app_set_metadata (app, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));
	gs_app_set_management_plugin (app, "packagekit");
	gs_app_set_update_version (app, pk_package_get_version (package));
	gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
	gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
	gs_plugin_cache_add (plugin, pk_package_get_id (package), app);
	return app;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	g_mutex_lock (&priv->task_mutex);
	pk_client_set_interactive (PK_CLIENT (priv->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_updates (PK_CLIENT (priv->task),
					 pk_bitfield_value (PK_FILTER_ENUM_NONE),
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&priv->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;

	/* add results */
	array = pk_results_get_package_array (results);
	for (guint i = 0; i < array->len; i++) {
		PkPackage *package = g_ptr_array_index (array, i);
		g_autoptr(GsApp) app = NULL;
		app = gs_plugin_packagekit_build_update_app (plugin, package);
		gs_app_list_add (list, app);
	}
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
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 -1);
	g_mutex_lock (&priv->task_mutex);
	pk_client_set_interactive (PK_CLIENT (priv->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_search_files (PK_CLIENT (priv->task),
	                                  filter,
	                                  search,
	                                  cancellable,
	                                  gs_packagekit_helper_cb, helper,
	                                  error);
	g_mutex_unlock (&priv->task_mutex);
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
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 -1);
	g_mutex_lock (&priv->task_mutex);
	pk_client_set_interactive (PK_CLIENT (priv->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_what_provides (PK_CLIENT (priv->task),
	                                   filter,
	                                   search,
	                                   cancellable,
	                                   gs_packagekit_helper_cb, helper,
	                                   error);
	g_mutex_unlock (&priv->task_mutex);
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

static void
gs_plugin_packagekit_updates_changed_cb (PkControl *control, GsPlugin *plugin)
{
	gs_plugin_updates_changed (plugin);
}

static void
gs_plugin_packagekit_repo_list_changed_cb (PkControl *control, GsPlugin *plugin)
{
	gs_plugin_reload (plugin);
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_PACKAGE &&
	    gs_app_get_scope (app) == AS_COMPONENT_SCOPE_SYSTEM) {
		gs_app_set_management_plugin (app, "packagekit");
		gs_plugin_packagekit_set_packaging_format (plugin, app);
		return;
	}
}

static gboolean
gs_plugin_packagekit_resolve_packages_with_filter (GsPlugin *plugin,
                                                   GsAppList *list,
                                                   PkBitfield filter,
                                                   GCancellable *cancellable,
                                                   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GPtrArray *sources;
	GsApp *app;
	const gchar *pkgname;
	guint i;
	guint j;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) package_ids = NULL;
	g_autoptr(GPtrArray) packages = NULL;

	package_ids = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		sources = gs_app_get_sources (app);
		for (j = 0; j < sources->len; j++) {
			pkgname = g_ptr_array_index (sources, j);
			if (pkgname == NULL || pkgname[0] == '\0') {
				g_warning ("invalid pkgname '%s' for %s",
					   pkgname,
					   gs_app_get_unique_id (app));
				continue;
			}
			g_ptr_array_add (package_ids, g_strdup (pkgname));
		}
	}
	if (package_ids->len == 0)
		return TRUE;
	g_ptr_array_add (package_ids, NULL);

	/* resolve them all at once */
	g_mutex_lock (&priv->client_mutex_refine);
	results = pk_client_resolve (priv->client_refine,
				     filter,
				     (gchar **) package_ids->pdata,
				     cancellable,
				     gs_packagekit_helper_cb, helper,
				     error);
	g_mutex_unlock (&priv->client_mutex_refine);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to resolve package_ids: ");
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);

	/* if the user types more characters we'll get cancelled - don't go on
	 * to mark apps as unavailable because packages->len = 0 */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		if (gs_app_get_local_file (app) != NULL)
			continue;
		gs_plugin_packagekit_resolve_packages_app (plugin, packages, app);
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_resolve_packages (GsPlugin *plugin,
                                       GsAppList *list,
                                       GCancellable *cancellable,
                                       GError **error)
{
	PkBitfield filter;
	g_autoptr(GsAppList) resolve2_list = NULL;

	/* first, try to resolve packages with ARCH filter */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
	                                 PK_FILTER_ENUM_ARCH,
	                                 -1);
	if (!gs_plugin_packagekit_resolve_packages_with_filter (plugin,
	                                                        list,
	                                                        filter,
	                                                        cancellable,
	                                                        error)) {
		return FALSE;
	}

	/* if any packages remaining in UNKNOWN state, try to resolve them again,
	 * but this time without ARCH filter */
	resolve2_list = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
			gs_app_list_add (resolve2_list, app);
	}
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
	                                 PK_FILTER_ENUM_NOT_ARCH,
	                                 PK_FILTER_ENUM_NOT_SOURCE,
	                                 -1);
	if (!gs_plugin_packagekit_resolve_packages_with_filter (plugin,
	                                                        resolve2_list,
	                                                        filter,
	                                                        cancellable,
	                                                        error)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_from_desktop (GsPlugin *plugin,
					  GsApp *app,
					  const gchar *filename,
					  GCancellable *cancellable,
					  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *to_array[] = { NULL, NULL };
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) packages = NULL;

	to_array[0] = filename;
	gs_packagekit_helper_add_app (helper, app);
	g_mutex_lock (&priv->client_mutex_refine);
	results = pk_client_search_files (priv->client_refine,
					  pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
					  (gchar **) to_array,
					  cancellable,
					  gs_packagekit_helper_cb, helper,
					  error);
	g_mutex_unlock (&priv->client_mutex_refine);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to search file %s: ", filename);
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (packages->len == 1) {
		PkPackage *package;
		package = g_ptr_array_index (packages, 0);
		gs_plugin_packagekit_set_metadata_from_package (plugin, app, package);
	} else {
		g_warning ("Failed to find one package for %s, %s, [%u]",
			   gs_app_get_id (app), filename, packages->len);
	}
	return TRUE;
}

/*
 * gs_plugin_packagekit_fixup_update_description:
 *
 * Lets assume Fedora is sending us valid markdown, but fall back to
 * plain text if this fails.
 */
static gchar *
gs_plugin_packagekit_fixup_update_description (const gchar *text)
{
	gchar *tmp;
	g_autoptr(GsMarkdown) markdown = NULL;

	/* nothing to do */
	if (text == NULL)
		return NULL;

	/* try to parse */
	markdown = gs_markdown_new (GS_MARKDOWN_OUTPUT_TEXT);
	gs_markdown_set_smart_quoting (markdown, FALSE);
	gs_markdown_set_autocode (markdown, FALSE);
	gs_markdown_set_autolinkify (markdown, FALSE);
	tmp = gs_markdown_parse (markdown, text);
	if (tmp != NULL)
		return tmp;
	return g_strdup (text);
}

static gboolean
gs_plugin_packagekit_refine_updatedetails (GsPlugin *plugin,
					   GsAppList *list,
					   GCancellable *cancellable,
					   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *package_id;
	guint j;
	GsApp *app;
	guint cnt = 0;
	PkUpdateDetail *update_detail;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autofree const gchar **package_ids = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	package_ids = g_new0 (const gchar *, gs_app_list_length (list) + 1);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		package_id = gs_app_get_source_id_default (app);
		if (package_id != NULL)
			package_ids[cnt++] = package_id;
	}

	/* nothing to do */
	if (cnt == 0)
		return TRUE;

	/* get any update details */
	g_mutex_lock (&priv->client_mutex_refine);
	results = pk_client_get_update_detail (priv->client_refine,
					       (gchar **) package_ids,
					       cancellable,
					       gs_packagekit_helper_cb, helper,
					       error);
	g_mutex_unlock (&priv->client_mutex_refine);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to get update details for %s: ",
				package_ids[0]);
		return FALSE;
	}

	/* set the update details for the update */
	array = pk_results_get_update_detail_array (results);
	for (j = 0; j < gs_app_list_length (list); j++) {
		app = gs_app_list_index (list, j);
		package_id = gs_app_get_source_id_default (app);
		for (guint i = 0; i < array->len; i++) {
			const gchar *tmp;
			g_autofree gchar *desc = NULL;
			/* right package? */
			update_detail = g_ptr_array_index (array, i);
			if (g_strcmp0 (package_id, pk_update_detail_get_package_id (update_detail)) != 0)
				continue;
			tmp = pk_update_detail_get_update_text (update_detail);
			desc = gs_plugin_packagekit_fixup_update_description (tmp);
			if (desc != NULL)
				gs_app_set_update_details (app, desc);
			break;
		}
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_details2 (GsPlugin *plugin,
				      GsAppList *list,
				      GCancellable *cancellable,
				      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GPtrArray *source_ids;
	GsApp *app;
	const gchar *package_id;
	guint i, j;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GPtrArray) package_ids = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GHashTable) details_collection = NULL;

	package_ids = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		source_ids = gs_app_get_source_ids (app);
		for (j = 0; j < source_ids->len; j++) {
			package_id = g_ptr_array_index (source_ids, j);
			g_ptr_array_add (package_ids, g_strdup (package_id));
		}
	}
	if (package_ids->len == 0)
		return TRUE;
	g_ptr_array_add (package_ids, NULL);

	/* get any details */
	g_mutex_lock (&priv->client_mutex_refine);
	results = pk_client_get_details (priv->client_refine,
					 (gchar **) package_ids->pdata,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&priv->client_mutex_refine);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_autofree gchar *package_ids_str = g_strjoinv (",", (gchar **) package_ids->pdata);
		g_prefix_error (error, "failed to get details for %s: ",
		                package_ids_str);
		return FALSE;
	}

	/* get the results and copy them into a hash table for fast lookups:
	 * there are typically 400 to 700 elements in @array, and 100 to 200
	 * elements in @list, each with 1 or 2 source IDs to look up (but
	 * sometimes 200) */
	array = pk_results_get_details_array (results);
	details_collection = gs_plugin_packagekit_details_array_to_hash (array);

	/* set the update details for the update */
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		gs_plugin_packagekit_refine_details_app (plugin, details_collection, app);
	}

	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_update_urgency (GsPlugin *plugin,
					    GsAppList *list,
					    GsPluginRefineFlags flags,
					    GCancellable *cancellable,
					    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	guint i;
	GsApp *app;
	const gchar *package_id;
	PkBitfield filter;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkPackageSack) sack = NULL;
	g_autoptr(PkResults) results = NULL;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_SEVERITY) == 0)
		return TRUE;

	/* get the list of updates */
	filter = pk_bitfield_value (PK_FILTER_ENUM_NONE);
	g_mutex_lock (&priv->client_mutex_refine);
	results = pk_client_get_updates (priv->client_refine,
					 filter,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&priv->client_mutex_refine);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to get updates for urgency: ");
		return FALSE;
	}

	/* set the update severity for the app */
	sack = pk_results_get_package_sack (results);
	for (i = 0; i < gs_app_list_length (list); i++) {
		g_autoptr (PkPackage) pkg = NULL;
		app = gs_app_list_index (list, i);
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		package_id = gs_app_get_source_id_default (app);
		if (package_id == NULL)
			continue;
		pkg = pk_package_sack_find_by_id (sack, package_id);
		if (pkg == NULL)
			continue;
		#ifdef HAVE_PK_PACKAGE_GET_UPDATE_SEVERITY
		switch (pk_package_get_update_severity (pkg)) {
		case PK_INFO_ENUM_LOW:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_LOW);
			break;
		case PK_INFO_ENUM_NORMAL:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_MEDIUM);
			break;
		case PK_INFO_ENUM_IMPORTANT:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_HIGH);
			break;
		case PK_INFO_ENUM_CRITICAL:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_CRITICAL);
			break;
		default:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_UNKNOWN);
			break;
		}
		#else
		switch (pk_package_get_info (pkg)) {
		case PK_INFO_ENUM_AVAILABLE:
		case PK_INFO_ENUM_NORMAL:
		case PK_INFO_ENUM_LOW:
		case PK_INFO_ENUM_ENHANCEMENT:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_LOW);
			break;
		case PK_INFO_ENUM_BUGFIX:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_MEDIUM);
			break;
		case PK_INFO_ENUM_SECURITY:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_CRITICAL);
			break;
		case PK_INFO_ENUM_IMPORTANT:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_HIGH);
			break;
		default:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_UNKNOWN);
			g_warning ("unhandled info state %s",
				   pk_info_enum_to_string (pk_package_get_info (pkg)));
			break;
		}
		#endif
	}
	return TRUE;
}

static gboolean
gs_plugin_refine_app_needs_details (GsPlugin *plugin, GsPluginRefineFlags flags, GsApp *app)
{
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) > 0 &&
	    gs_app_get_license (app) == NULL)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL) > 0 &&
	    gs_app_get_url (app, AS_URL_KIND_HOMEPAGE) == NULL)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) > 0 &&
	    gs_app_get_size_installed (app) == 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) > 0 &&
	    gs_app_get_size_download (app) == 0)
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_packagekit_refine_details (GsPlugin *plugin,
				     GsAppList *list,
				     GsPluginRefineFlags flags,
				     GCancellable *cancellable,
				     GError **error)
{
	gboolean ret = TRUE;
	g_autoptr(GsAppList) list_tmp = NULL;

	list_tmp = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
			continue;
		if (gs_app_get_source_id_default (app) == NULL)
			continue;
		if (!gs_plugin_refine_app_needs_details (plugin, flags, app))
			continue;
		gs_app_list_add (list_tmp, app);
	}
	if (gs_app_list_length (list_tmp) == 0)
		return TRUE;
	ret = gs_plugin_packagekit_refine_details2 (plugin,
						    list_tmp,
						    cancellable,
						    error);
	if (!ret)
		return FALSE;
	return TRUE;
}

static gboolean
gs_plugin_refine_requires_version (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_version (app);
	if (tmp != NULL)
		return FALSE;
	return (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) > 0;
}

static gboolean
gs_plugin_refine_requires_update_details (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_update_details (app);
	if (tmp != NULL)
		return FALSE;
	return (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS) > 0;
}

static gboolean
gs_plugin_refine_requires_origin (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_origin (app);
	if (tmp != NULL)
		return FALSE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) > 0)
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_refine_requires_package_id (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_source_id_default (app);
	if (tmp != NULL)
		return FALSE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION) > 0)
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_packagekit_refine_distro_upgrade (GsPlugin *plugin,
					    GsApp *app,
					    GCancellable *cancellable,
					    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	guint i;
	GsApp *app2;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GsAppList) list = NULL;
	guint cache_age_save;

	gs_packagekit_helper_add_app (helper, app);

	/* ask PK to simulate upgrading the system */
	g_mutex_lock (&priv->client_mutex_refine);
	cache_age_save = pk_client_get_cache_age (priv->client_refine);
	pk_client_set_cache_age (priv->client_refine, 60 * 60 * 24 * 7); /* once per week */
	pk_client_set_interactive (priv->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_upgrade_system (priv->client_refine,
					    pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_SIMULATE, -1),
					    gs_app_get_version (app),
					    PK_UPGRADE_KIND_ENUM_COMPLETE,
					    cancellable,
					    gs_packagekit_helper_cb, helper,
					    error);
	pk_client_set_cache_age (priv->client_refine, cache_age_save);
	g_mutex_unlock (&priv->client_mutex_refine);

	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to refine distro upgrade: ");
		return FALSE;
	}
	list = gs_app_list_new ();
	if (!gs_plugin_packagekit_add_results (plugin, list, results, error))
		return FALSE;

	/* add each of these as related applications */
	for (i = 0; i < gs_app_list_length (list); i++) {
		app2 = gs_app_list_index (list, i);
		if (gs_app_get_state (app2) != GS_APP_STATE_UNAVAILABLE)
			continue;
		gs_app_add_related (app, app2);
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_valid_package_name (const gchar *source)
{
	if (g_strstr_len (source, -1, "/") != NULL)
		return FALSE;
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_name_to_id (GsPlugin *plugin,
					GsAppList *list,
					GsPluginRefineFlags flags,
					GCancellable *cancellable,
					GError **error)
{
	g_autoptr(GsAppList) resolve_all = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GPtrArray *sources;
		GsApp *app = gs_app_list_index (list, i);
		const gchar *tmp;
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		tmp = gs_app_get_management_plugin (app);
		if (tmp != NULL && g_strcmp0 (tmp, "packagekit") != 0)
			continue;
		sources = gs_app_get_sources (app);
		if (sources->len == 0)
			continue;
		tmp = g_ptr_array_index (sources, 0);
		if (!gs_plugin_packagekit_refine_valid_package_name (tmp))
			continue;
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN ||
		    gs_plugin_refine_requires_package_id (app, flags) ||
		    gs_plugin_refine_requires_origin (app, flags) ||
		    gs_plugin_refine_requires_version (app, flags)) {
			gs_app_list_add (resolve_all, app);
		}
	}
	if (gs_app_list_length (resolve_all) > 0) {
		if (!gs_plugin_packagekit_resolve_packages (plugin,
							    resolve_all,
							    cancellable,
							    error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_filename_to_id (GsPlugin *plugin,
					    GsAppList *list,
					    GsPluginRefineFlags flags,
					    GCancellable *cancellable,
					    GError **error)
{
	/* not now */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION) == 0)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		g_autofree gchar *fn = NULL;
		GsApp *app = gs_app_list_index (list, i);
		const gchar *tmp;
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		if (gs_app_get_source_id_default (app) != NULL)
			continue;
		tmp = gs_app_get_management_plugin (app);
		if (tmp != NULL && g_strcmp0 (tmp, "packagekit") != 0)
			continue;
		tmp = gs_app_get_id (app);
		if (tmp == NULL)
			continue;
		switch (gs_app_get_kind (app)) {
		case AS_COMPONENT_KIND_DESKTOP_APP:
			fn = g_strdup_printf ("/usr/share/applications/%s", tmp);
			break;
		case AS_COMPONENT_KIND_ADDON:
			fn = g_strdup_printf ("/usr/share/metainfo/%s.metainfo.xml", tmp);
			if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
				g_free (fn);
				fn = g_strdup_printf ("/usr/share/appdata/%s.metainfo.xml", tmp);
			}
			break;
		default:
			break;
		}
		if (fn == NULL)
			continue;
		if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
			g_debug ("ignoring %s as does not exist", fn);
			continue;
		}
		if (!gs_plugin_packagekit_refine_from_desktop (plugin,
								app,
								fn,
								cancellable,
								error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_update_details (GsPlugin *plugin,
					    GsAppList *list,
					    GsPluginRefineFlags flags,
					    GCancellable *cancellable,
					    GError **error)
{
	g_autoptr(GsAppList) updatedetails_all = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		const gchar *tmp;
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		if (gs_app_get_state (app) != GS_APP_STATE_UPDATABLE)
			continue;
		if (gs_app_get_source_id_default (app) == NULL)
			continue;
		tmp = gs_app_get_management_plugin (app);
		if (tmp != NULL && g_strcmp0 (tmp, "packagekit") != 0)
			continue;
		if (gs_plugin_refine_requires_update_details (app, flags))
			gs_app_list_add (updatedetails_all, app);
	}
	if (gs_app_list_length (updatedetails_all) > 0) {
		if (!gs_plugin_packagekit_refine_updatedetails (plugin,
								updatedetails_all,
								cancellable,
								error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GsAppList *list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	/* when we need the cannot-be-upgraded applications, we implement this
	 * by doing a UpgradeSystem(SIMULATE) which adds the removed packages
	 * to the related-apps list with a state of %GS_APP_STATE_UNAVAILABLE */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED) {
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			if (gs_app_get_kind (app) != AS_COMPONENT_KIND_OPERATING_SYSTEM)
				continue;
			if (!gs_plugin_packagekit_refine_distro_upgrade (plugin,
									 app,
									 cancellable,
									 error))
				return FALSE;
		}
	}

	/* can we resolve in one go? */
	if (!gs_plugin_packagekit_refine_name_to_id (plugin, list, flags, cancellable, error))
		return FALSE;

	/* set the package-id for an installed desktop file */
	if (!gs_plugin_packagekit_refine_filename_to_id (plugin, list, flags, cancellable, error))
		return FALSE;

	/* any update details missing? */
	if (!gs_plugin_packagekit_refine_update_details (plugin, list, flags, cancellable, error))
		return FALSE;

	/* any package details missing? */
	if (!gs_plugin_packagekit_refine_details (plugin, list, flags, cancellable, error))
		return FALSE;

	/* get the update severity */
	if (!gs_plugin_packagekit_refine_update_urgency (plugin, list, flags, cancellable, error))
		return FALSE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		/* only process this app if was created by this plugin */
		if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
			continue;

		/* the scope is always system-wide */
		if (gs_app_get_scope (app) == AS_COMPONENT_SCOPE_UNKNOWN)
			gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
		if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_UNKNOWN)
			gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	}

	/* success */
	return TRUE;
}
