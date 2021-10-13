/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2017 Canonical Ltd
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <glib/gi18n-lib.h>
#include <gnome-software.h>
#include <gsettings-desktop-schemas/gdesktop-enums.h>
#include <packagekit-glib2/packagekit.h>
#include <string.h>

#include "packagekit-common.h"
#include "gs-markdown.h"
#include "gs-packagekit-helper.h"

#include "gs-plugin-packagekit.h"

/*
 * SECTION:
 * Uses the system PackageKit instance to return installed packages,
 * sources and the ability to add and remove packages. Supports package history
 * and converting URIs to apps.
 *
 * Supports setting the session proxy on the system PackageKit instance.
 *
 * Requires:    | [source-id]
 * Refines:     | [source-id], [source], [update-details], [management-plugin]
 */

#define GS_PLUGIN_PACKAGEKIT_HISTORY_TIMEOUT	5000 /* ms */

struct _GsPluginPackagekit {
	GsPlugin		 parent;

	PkTask			*task;
	GMutex			 task_mutex;

	PkControl		*control_refine;
	PkClient		*client_refine;
	GMutex			 client_mutex_refine;

	GDBusConnection		*connection_history;

	PkTask			*task_local;
	GMutex			 task_mutex_local;

	PkClient		*client_url_to_app;
	GMutex			 client_mutex_url_to_app;

	PkControl		*control_proxy;
	GSettings		*settings_proxy;
	GSettings		*settings_http;
	GSettings		*settings_https;
	GSettings		*settings_ftp;
	GSettings		*settings_socks;

	PkTask			*task_upgrade;
	GMutex			 task_mutex_upgrade;
};

G_DEFINE_TYPE (GsPluginPackagekit, gs_plugin_packagekit, GS_TYPE_PLUGIN)

static void gs_plugin_packagekit_updates_changed_cb (PkControl *control, GsPlugin *plugin);
static void gs_plugin_packagekit_repo_list_changed_cb (PkControl *control, GsPlugin *plugin);
static gboolean gs_plugin_packagekit_refine_history (GsPluginPackagekit  *self,
                                                     GsAppList           *list,
                                                     GCancellable        *cancellable,
                                                     GError             **error);
static void gs_plugin_packagekit_proxy_changed_cb (GSettings   *settings,
                                                   const gchar *key,
                                                   gpointer     user_data);
static void reload_proxy_settings (GsPluginPackagekit *self,
                                   GCancellable       *cancellable);

static void
gs_plugin_packagekit_init (GsPluginPackagekit *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	/* core */
	g_mutex_init (&self->task_mutex);
	self->task = pk_task_new ();
	pk_client_set_background (PK_CLIENT (self->task), FALSE);
	pk_client_set_cache_age (PK_CLIENT (self->task), G_MAXUINT);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* refine */
	g_mutex_init (&self->client_mutex_refine);
	self->client_refine = pk_client_new ();
	self->control_refine = pk_control_new ();
	g_signal_connect (self->control_refine, "updates-changed",
			  G_CALLBACK (gs_plugin_packagekit_updates_changed_cb), plugin);
	g_signal_connect (self->control_refine, "repo-list-changed",
			  G_CALLBACK (gs_plugin_packagekit_repo_list_changed_cb), plugin);
	pk_client_set_background (self->client_refine, FALSE);
	pk_client_set_cache_age (self->client_refine, G_MAXUINT);
	pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* local */
	g_mutex_init (&self->task_mutex_local);
	self->task_local = pk_task_new ();
	pk_client_set_background (PK_CLIENT (self->task_local), FALSE);
	pk_client_set_interactive (PK_CLIENT (self->task_local), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* url-to-app */
	g_mutex_init (&self->client_mutex_url_to_app);
	self->client_url_to_app = pk_client_new ();

	pk_client_set_background (self->client_url_to_app, FALSE);
	pk_client_set_cache_age (self->client_url_to_app, G_MAXUINT);
	pk_client_set_interactive (self->client_url_to_app, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* proxy */
	self->control_proxy = pk_control_new ();
	self->settings_proxy = g_settings_new ("org.gnome.system.proxy");
	g_signal_connect (self->settings_proxy, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), self);

	self->settings_http = g_settings_new ("org.gnome.system.proxy.http");
	self->settings_https = g_settings_new ("org.gnome.system.proxy.https");
	self->settings_ftp = g_settings_new ("org.gnome.system.proxy.ftp");
	self->settings_socks = g_settings_new ("org.gnome.system.proxy.socks");
	g_signal_connect (self->settings_http, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), self);
	g_signal_connect (self->settings_https, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), self);
	g_signal_connect (self->settings_ftp, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), self);
	g_signal_connect (self->settings_socks, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), self);

	/* upgrade */
	g_mutex_init (&self->task_mutex_upgrade);
	self->task_upgrade = pk_task_new ();
	pk_task_set_only_download (self->task_upgrade, TRUE);
	pk_client_set_background (PK_CLIENT (self->task_upgrade), TRUE);
	pk_client_set_cache_age (PK_CLIENT (self->task_upgrade), 60 * 60 * 24);
	pk_client_set_interactive (PK_CLIENT (self->task_upgrade), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* need pkgname and ID */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

static void
gs_plugin_packagekit_dispose (GObject *object)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (object);

	/* core */
	g_clear_object (&self->task);

	/* refine */
	g_clear_object (&self->client_refine);
	g_clear_object (&self->control_refine);

	/* history */
	g_clear_object (&self->connection_history);

	/* local */
	g_clear_object (&self->task_local);

	/* url-to-app */
	g_clear_object (&self->client_url_to_app);

	/* proxy */
	g_clear_object (&self->control_proxy);
	g_clear_object (&self->settings_proxy);
	g_clear_object (&self->settings_http);
	g_clear_object (&self->settings_https);
	g_clear_object (&self->settings_ftp);
	g_clear_object (&self->settings_socks);

	/* upgrade */
	g_clear_object (&self->task_upgrade);

	G_OBJECT_CLASS (gs_plugin_packagekit_parent_class)->dispose (object);
}

static void
gs_plugin_packagekit_finalize (GObject *object)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (object);

	g_mutex_clear (&self->task_mutex);
	g_mutex_clear (&self->client_mutex_refine);
	g_mutex_clear (&self->task_mutex_local);
	g_mutex_clear (&self->client_mutex_url_to_app);
	g_mutex_clear (&self->task_mutex_upgrade);

	G_OBJECT_CLASS (gs_plugin_packagekit_parent_class)->finalize (object);
}

static gboolean
gs_plugin_add_sources_related (GsPlugin *plugin,
			       GHashTable *hash,
			       GCancellable *cancellable,
			       GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
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
	g_mutex_lock (&self->task_mutex);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_packages (PK_CLIENT(self->task),
					   filter,
					   cancellable,
					   gs_packagekit_helper_cb, helper,
					   error);
	g_mutex_unlock (&self->task_mutex);
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
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
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
					 -1);
	g_mutex_lock (&self->task_mutex);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_repo_list (PK_CLIENT(self->task),
					   filter,
					   cancellable,
					   gs_packagekit_helper_cb, helper,
					   error);
	g_mutex_unlock (&self->task_mutex);
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
		gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
		gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
		gs_app_set_state (app, pk_repo_detail_get_enabled (rd) ?
				  GS_APP_STATE_INSTALLED : GS_APP_STATE_AVAILABLE);
		gs_app_set_name (app,
				 GS_APP_QUALITY_LOWEST,
				 pk_repo_detail_get_description (rd));
		gs_app_set_summary (app,
				    GS_APP_QUALITY_LOWEST,
				    pk_repo_detail_get_description (rd));
		gs_plugin_packagekit_set_packaging_format (plugin, app);
		gs_app_set_metadata (app, "GnomeSoftware::SortKey", "300");
		gs_app_set_origin_ui (app, _("Packages"));
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
gs_plugin_app_origin_repo_enable (GsPluginPackagekit  *self,
                                  GsApp               *app,
                                  GCancellable        *cancellable,
                                  GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
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
	g_mutex_lock (&self->task_mutex);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_repo_enable (PK_CLIENT (self->task),
	                                 repo_id,
	                                 TRUE,
	                                 cancellable,
	                                 gs_packagekit_helper_cb, helper,
	                                 error);
	g_mutex_unlock (&self->task_mutex);

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

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
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

	/* enable repo, handled by dedicated function */
	g_return_val_if_fail (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY, FALSE);

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
		if (!gs_plugin_app_origin_repo_enable (self, app, cancellable, error))
			return FALSE;

		gs_app_set_state (app, GS_APP_STATE_INSTALLING);

		/* FIXME: this is a hack, to allow PK time to re-initialize
		 * everything in order to match an actual result. The root cause
		 * is probably some kind of hard-to-debug race in the daemon. */
		g_usleep (G_USEC_PER_SEC * 3);

		/* actually install the package */
		gs_packagekit_helper_add_app (helper, app);
		g_mutex_lock (&self->task_mutex);
		pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
		results = pk_task_install_packages_sync (self->task,
							 package_ids,
							 cancellable,
							 gs_packagekit_helper_cb, helper,
							 error);
		g_mutex_unlock (&self->task_mutex);
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
		g_mutex_lock (&self->task_mutex);
		pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
		results = pk_task_install_packages_sync (self->task,
							 (gchar **) array_package_ids->pdata,
							 cancellable,
							 gs_packagekit_helper_cb, helper,
							 error);
		g_mutex_unlock (&self->task_mutex);
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
		g_mutex_lock (&self->task_mutex);
		pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
		results = pk_task_install_files_sync (self->task,
						      package_ids,
						      cancellable,
						      gs_packagekit_helper_cb, helper,
						      error);
		g_mutex_unlock (&self->task_mutex);
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

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
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

	/* disable repo, handled by dedicated function */
	g_return_val_if_fail (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY, FALSE);

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
	g_mutex_lock (&self->task_mutex);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_task_remove_packages_sync (self->task,
						package_ids,
						TRUE, GS_PACKAGEKIT_AUTOREMOVE,
						cancellable,
						gs_packagekit_helper_cb, helper,
						error);
	g_mutex_unlock (&self->task_mutex);
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
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	g_mutex_lock (&self->task_mutex);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_updates (PK_CLIENT (self->task),
					 pk_bitfield_value (PK_FILTER_ENUM_NONE),
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&self->task_mutex);
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
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	PkBitfield filter;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 -1);
	g_mutex_lock (&self->task_mutex);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_search_files (PK_CLIENT (self->task),
	                                  filter,
	                                  search,
	                                  cancellable,
	                                  gs_packagekit_helper_cb, helper,
	                                  error);
	g_mutex_unlock (&self->task_mutex);
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
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	PkBitfield filter;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 -1);
	g_mutex_lock (&self->task_mutex);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_what_provides (PK_CLIENT (self->task),
	                                   filter,
	                                   search,
	                                   cancellable,
	                                   gs_packagekit_helper_cb, helper,
	                                   error);
	g_mutex_unlock (&self->task_mutex);
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
	} else if (gs_app_get_kind (app) == AS_COMPONENT_KIND_OPERATING_SYSTEM) {
		gs_app_set_management_plugin (app, "packagekit");
	}
}

static gboolean
gs_plugin_packagekit_resolve_packages_with_filter (GsPluginPackagekit  *self,
                                                   GsAppList           *list,
                                                   PkBitfield           filter,
                                                   GCancellable        *cancellable,
                                                   GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
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
	g_mutex_lock (&self->client_mutex_refine);
	pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_resolve (self->client_refine,
				     filter,
				     (gchar **) package_ids->pdata,
				     cancellable,
				     gs_packagekit_helper_cb, helper,
				     error);
	g_mutex_unlock (&self->client_mutex_refine);
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
gs_plugin_packagekit_resolve_packages (GsPluginPackagekit  *self,
                                       GsAppList           *list,
                                       GCancellable        *cancellable,
                                       GError             **error)
{
	PkBitfield filter;
	g_autoptr(GsAppList) resolve2_list = NULL;

	/* first, try to resolve packages with ARCH filter */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
	                                 PK_FILTER_ENUM_ARCH,
	                                 -1);
	if (!gs_plugin_packagekit_resolve_packages_with_filter (self,
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
	if (!gs_plugin_packagekit_resolve_packages_with_filter (self,
	                                                        resolve2_list,
	                                                        filter,
	                                                        cancellable,
	                                                        error)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_from_desktop (GsPluginPackagekit  *self,
                                          GsApp               *app,
                                          const gchar         *filename,
                                          GCancellable        *cancellable,
                                          GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	const gchar *to_array[] = { NULL, NULL };
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) packages = NULL;

	to_array[0] = filename;
	gs_packagekit_helper_add_app (helper, app);
	g_mutex_lock (&self->client_mutex_refine);
	pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_search_files (self->client_refine,
					  pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
					  (gchar **) to_array,
					  cancellable,
					  gs_packagekit_helper_cb, helper,
					  error);
	g_mutex_unlock (&self->client_mutex_refine);
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
gs_plugin_packagekit_refine_updatedetails (GsPluginPackagekit  *self,
                                           GsAppList           *list,
                                           GCancellable        *cancellable,
                                           GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
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
	g_mutex_lock (&self->client_mutex_refine);
	pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_update_detail (self->client_refine,
					       (gchar **) package_ids,
					       cancellable,
					       gs_packagekit_helper_cb, helper,
					       error);
	g_mutex_unlock (&self->client_mutex_refine);
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
gs_plugin_packagekit_refine_details2 (GsPluginPackagekit  *self,
                                      GsAppList           *list,
                                      GCancellable        *cancellable,
                                      GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
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
	g_mutex_lock (&self->client_mutex_refine);
	pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_details (self->client_refine,
					 (gchar **) package_ids->pdata,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&self->client_mutex_refine);
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
gs_plugin_packagekit_refine_update_urgency (GsPluginPackagekit   *self,
                                            GsAppList            *list,
                                            GsPluginRefineFlags   flags,
                                            GCancellable         *cancellable,
                                            GError              **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
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
	g_mutex_lock (&self->client_mutex_refine);
	pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_updates (self->client_refine,
					 filter,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&self->client_mutex_refine);
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
gs_plugin_refine_app_needs_details (GsPluginRefineFlags  flags,
                                    GsApp               *app)
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
gs_plugin_packagekit_refine_details (GsPluginPackagekit   *self,
                                     GsAppList            *list,
                                     GsPluginRefineFlags   flags,
                                     GCancellable         *cancellable,
                                     GError              **error)
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
		if (!gs_plugin_refine_app_needs_details (flags, app))
			continue;
		gs_app_list_add (list_tmp, app);
	}
	if (gs_app_list_length (list_tmp) == 0)
		return TRUE;
	ret = gs_plugin_packagekit_refine_details2 (self,
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
gs_plugin_packagekit_refine_distro_upgrade (GsPluginPackagekit  *self,
                                            GsApp               *app,
                                            GCancellable        *cancellable,
                                            GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	guint i;
	GsApp *app2;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GsAppList) list = NULL;
	guint cache_age_save;

	gs_packagekit_helper_add_app (helper, app);

	/* ask PK to simulate upgrading the system */
	g_mutex_lock (&self->client_mutex_refine);
	cache_age_save = pk_client_get_cache_age (self->client_refine);
	pk_client_set_cache_age (self->client_refine, 60 * 60 * 24 * 7); /* once per week */
	pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_upgrade_system (self->client_refine,
					    pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_SIMULATE, -1),
					    gs_app_get_version (app),
					    PK_UPGRADE_KIND_ENUM_COMPLETE,
					    cancellable,
					    gs_packagekit_helper_cb, helper,
					    error);
	pk_client_set_cache_age (self->client_refine, cache_age_save);
	g_mutex_unlock (&self->client_mutex_refine);

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
gs_plugin_packagekit_refine_name_to_id (GsPluginPackagekit   *self,
                                        GsAppList            *list,
                                        GsPluginRefineFlags   flags,
                                        GCancellable         *cancellable,
                                        GError              **error)
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
		if (!gs_plugin_packagekit_resolve_packages (self,
							    resolve_all,
							    cancellable,
							    error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_filename_to_id (GsPluginPackagekit   *self,
                                            GsAppList            *list,
                                            GsPluginRefineFlags   flags,
                                            GCancellable         *cancellable,
                                            GError              **error)
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
		if (!gs_plugin_packagekit_refine_from_desktop (self,
								app,
								fn,
								cancellable,
								error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_update_details (GsPluginPackagekit   *self,
                                            GsAppList            *list,
                                            GsPluginRefineFlags   flags,
                                            GCancellable         *cancellable,
                                            GError              **error)
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
		if (!gs_plugin_packagekit_refine_updatedetails (self,
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
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);

	/* when we need the cannot-be-upgraded applications, we implement this
	 * by doing a UpgradeSystem(SIMULATE) which adds the removed packages
	 * to the related-apps list with a state of %GS_APP_STATE_UNAVAILABLE */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED) {
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			if (gs_app_get_kind (app) != AS_COMPONENT_KIND_OPERATING_SYSTEM)
				continue;
			if (!gs_plugin_packagekit_refine_distro_upgrade (self,
									 app,
									 cancellable,
									 error))
				return FALSE;
		}
	}

	/* can we resolve in one go? */
	if (!gs_plugin_packagekit_refine_name_to_id (self, list, flags, cancellable, error))
		return FALSE;

	/* set the package-id for an installed desktop file */
	if (!gs_plugin_packagekit_refine_filename_to_id (self, list, flags, cancellable, error))
		return FALSE;

	/* any update details missing? */
	if (!gs_plugin_packagekit_refine_update_details (self, list, flags, cancellable, error))
		return FALSE;

	/* any package details missing? */
	if (!gs_plugin_packagekit_refine_details (self, list, flags, cancellable, error))
		return FALSE;

	/* get the update severity */
	if (!gs_plugin_packagekit_refine_update_urgency (self, list, flags, cancellable, error))
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

	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY) != 0) {
		gboolean ret;
		guint i;
		GsApp *app;
		GPtrArray *sources;
		g_autoptr(GsAppList) packages = NULL;

		/* add any missing history data */
		packages = gs_app_list_new ();
		for (i = 0; i < gs_app_list_length (list); i++) {
			app = gs_app_list_index (list, i);
			if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
				continue;
			sources = gs_app_get_sources (app);
			if (sources->len == 0)
				continue;
			if (gs_app_get_install_date (app) != 0)
				continue;
			gs_app_list_add (packages, app);
		}
		if (gs_app_list_length (packages) > 0) {
			ret = gs_plugin_packagekit_refine_history (self,
								   packages,
								   cancellable,
								   error);
			if (!ret)
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
gs_plugin_packagekit_refine_add_history (GsApp *app, GVariant *dict)
{
	const gchar *version;
	gboolean ret;
	guint64 timestamp;
	PkInfoEnum info_enum;
	g_autoptr(GsApp) history = NULL;

	/* create new history item with same ID as parent */
	history = gs_app_new (gs_app_get_id (app));
	gs_app_set_kind (history, AS_COMPONENT_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_name (history, GS_APP_QUALITY_NORMAL, gs_app_get_name (app));

	/* get the installed state */
	ret = g_variant_lookup (dict, "info", "u", &info_enum);
	g_assert (ret);
	switch (info_enum) {
	case PK_INFO_ENUM_INSTALLING:
		gs_app_set_state (history, GS_APP_STATE_INSTALLED);
		break;
	case PK_INFO_ENUM_REMOVING:
		gs_app_set_state (history, GS_APP_STATE_AVAILABLE);
		break;
	case PK_INFO_ENUM_UPDATING:
		gs_app_set_state (history, GS_APP_STATE_UPDATABLE);
		break;
	default:
		g_debug ("ignoring history kind: %s",
			 pk_info_enum_to_string (info_enum));
		return;
	}

	/* set the history time and date */
	ret = g_variant_lookup (dict, "timestamp", "t", &timestamp);
	g_assert (ret);
	gs_app_set_install_date (history, timestamp);

	/* set the history version number */
	ret = g_variant_lookup (dict, "version", "&s", &version);
	g_assert (ret);
	gs_app_set_version (history, version);

	/* add the package to the main application */
	gs_app_add_history (app, history);

	/* use the last event as approximation of the package timestamp */
	gs_app_set_install_date (app, timestamp);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);

	self->connection_history = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
						   cancellable,
						   error);
	if (self->connection_history == NULL) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}

	reload_proxy_settings (self, cancellable);

	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_history (GsPluginPackagekit  *self,
                                     GsAppList           *list,
                                     GCancellable        *cancellable,
                                     GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	gboolean ret;
	guint j;
	GsApp *app;
	guint i = 0;
	GVariantIter iter;
	GVariant *value;
	g_autofree const gchar **package_names = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariant) result = NULL;
	g_autoptr(GVariant) tuple = NULL;

	/* get an array of package names */
	package_names = g_new0 (const gchar *, gs_app_list_length (list) + 1);
	for (j = 0; j < gs_app_list_length (list); j++) {
		app = gs_app_list_index (list, j);
		package_names[i++] = gs_app_get_source_default (app);
	}

	g_debug ("getting history for %u packages", gs_app_list_length (list));
	result = g_dbus_connection_call_sync (self->connection_history,
					      "org.freedesktop.PackageKit",
					      "/org/freedesktop/PackageKit",
					      "org.freedesktop.PackageKit",
					      "GetPackageHistory",
					      g_variant_new ("(^asu)", package_names, 0),
					      NULL,
					      G_DBUS_CALL_FLAGS_NONE,
					      GS_PLUGIN_PACKAGEKIT_HISTORY_TIMEOUT,
					      cancellable,
					      &error_local);
	if (result == NULL) {
		g_dbus_error_strip_remote_error (error_local);
		if (g_error_matches (error_local,
				     G_DBUS_ERROR,
				     G_DBUS_ERROR_UNKNOWN_METHOD)) {
			g_debug ("No history available as PackageKit is too old: %s",
				 error_local->message);

			/* just set this to something non-zero so we don't keep
			 * trying to call GetPackageHistory */
			for (i = 0; i < gs_app_list_length (list); i++) {
				app = gs_app_list_index (list, i);
				gs_app_set_install_date (app, GS_APP_INSTALL_DATE_UNKNOWN);
			}
		} else if (g_error_matches (error_local,
					    G_IO_ERROR,
					    G_IO_ERROR_CANCELLED)) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_CANCELLED,
				     "Failed to get history: %s",
				     error_local->message);
			return FALSE;
		} else if (g_error_matches (error_local,
					    G_IO_ERROR,
					    G_IO_ERROR_TIMED_OUT)) {
			g_debug ("No history as PackageKit took too long: %s",
				 error_local->message);
			for (i = 0; i < gs_app_list_length (list); i++) {
				app = gs_app_list_index (list, i);
				gs_app_set_install_date (app, GS_APP_INSTALL_DATE_UNKNOWN);
			}
		}
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "Failed to get history: %s",
			     error_local->message);
		return FALSE;
	}

	/* get any results */
	tuple = g_variant_get_child_value (result, 0);
	for (i = 0; i < gs_app_list_length (list); i++) {
		g_autoptr(GVariant) entries = NULL;
		app = gs_app_list_index (list, i);
		ret = g_variant_lookup (tuple,
					gs_app_get_source_default (app),
					"@aa{sv}",
					&entries);
		if (!ret) {
			/* make up a fake entry as we know this package was at
			 * least installed at some point in time */
			if (gs_app_get_state (app) == GS_APP_STATE_INSTALLED) {
				g_autoptr(GsApp) app_dummy = NULL;
				app_dummy = gs_app_new (gs_app_get_id (app));
				gs_plugin_packagekit_set_packaging_format (plugin, app);
				gs_app_set_metadata (app_dummy, "GnomeSoftware::Creator",
						     gs_plugin_get_name (plugin));
				gs_app_set_install_date (app_dummy, GS_APP_INSTALL_DATE_UNKNOWN);
				gs_app_set_kind (app_dummy, AS_COMPONENT_KIND_GENERIC);
				gs_app_set_state (app_dummy, GS_APP_STATE_INSTALLED);
				gs_app_set_version (app_dummy, gs_app_get_version (app));
				gs_app_add_history (app, app_dummy);
			}
			gs_app_set_install_date (app, GS_APP_INSTALL_DATE_UNKNOWN);
			continue;
		}

		/* add history for application */
		g_variant_iter_init (&iter, entries);
		while ((value = g_variant_iter_next_value (&iter))) {
			gs_plugin_packagekit_refine_add_history (app, value);
			g_variant_unref (value);
		}
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refresh_guess_app_id (GsPluginPackagekit  *self,
                                           GsApp               *app,
                                           const gchar         *filename,
                                           GCancellable        *cancellable,
                                           GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_auto(GStrv) files = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GString) basename_best = g_string_new (NULL);

	/* get file list so we can work out ID */
	files = g_strsplit (filename, "\t", -1);
	gs_packagekit_helper_add_app (helper, app);
	g_mutex_lock (&self->task_mutex_local);
	pk_client_set_interactive (PK_CLIENT (self->task_local), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_files_local (PK_CLIENT (self->task_local),
					     files,
					     cancellable,
					     gs_packagekit_helper_cb, helper,
					     error);
	g_mutex_unlock (&self->task_mutex_local);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_utils_error_add_origin_id (error, app);
		return FALSE;
	}
	array = pk_results_get_files_array (results);
	if (array->len == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "no files for %s", filename);
		return FALSE;
	}

	/* find the smallest length desktop file, on the logic that
	 * ${app}.desktop is going to be better than ${app}-${action}.desktop */
	for (guint i = 0; i < array->len; i++) {
		PkFiles *item = g_ptr_array_index (array, i);
		gchar **fns = pk_files_get_files (item);
		for (guint j = 0; fns[j] != NULL; j++) {
			if (g_str_has_prefix (fns[j], "/etc/yum.repos.d/") &&
			    g_str_has_suffix (fns[j], ".repo")) {
				gs_app_add_quirk (app, GS_APP_QUIRK_HAS_SOURCE);
			}
			if (g_str_has_prefix (fns[j], "/usr/share/applications/") &&
			    g_str_has_suffix (fns[j], ".desktop")) {
				g_autofree gchar *basename = g_path_get_basename (fns[j]);
				if (basename_best->len == 0 ||
				    strlen (basename) < basename_best->len)
					g_string_assign (basename_best, basename);
			}
		}
	}
	if (basename_best->len > 0) {
		gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_set_id (app, basename_best->str);
	}

	return TRUE;
}

static void
add_quirks_from_package_name (GsApp *app, const gchar *package_name)
{
	/* these packages don't have a .repo file in their file lists, but
	 * instead install one through rpm scripts / cron job */
	const gchar *packages_with_repos[] = {
		"google-chrome-stable",
		"google-earth-pro-stable",
		"google-talkplugin",
		NULL };

	if (g_strv_contains (packages_with_repos, package_name))
		gs_app_add_quirk (app, GS_APP_QUIRK_HAS_SOURCE);
}

static gboolean
gs_plugin_packagekit_local_check_installed (GsPluginPackagekit  *self,
                                            GsApp               *app,
                                            GCancellable        *cancellable,
                                            GError             **error)
{
	PkBitfield filter;
	const gchar *names[] = { gs_app_get_source_default (app), NULL };
	g_autoptr(GPtrArray) packages = NULL;
	g_autoptr(PkResults) results = NULL;

	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_INSTALLED,
					 -1);
	results = pk_client_resolve (PK_CLIENT (self->task_local), filter, (gchar **) names,
				     cancellable, NULL, NULL, error);
	if (results == NULL) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}
	packages = pk_results_get_package_array (results);
	if (packages->len > 0) {
		gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		for (guint i = 0; i < packages->len; i++){
			PkPackage *pkg = g_ptr_array_index (packages, i);
			gs_app_add_source_id (app, pk_package_get_id (pkg));
		}
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
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	const gchar *package_id;
	PkDetails *item;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *license_spdx = NULL;
	g_auto(GStrv) files = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GsApp) app = NULL;
	const gchar *mimetypes[] = {
		"application/x-app-package",
		"application/x-deb",
		"application/vnd.debian.binary-package",
		"application/x-redhat-package-manager",
		"application/x-rpm",
		NULL };

	/* does this match any of the mimetypes we support */
	content_type = gs_utils_get_content_type (file, cancellable, error);
	if (content_type == NULL)
		return FALSE;
	if (!g_strv_contains (mimetypes, content_type))
		return TRUE;

	/* get details */
	filename = g_file_get_path (file);
	files = g_strsplit (filename, "\t", -1);
	g_mutex_lock (&self->task_mutex_local);
	pk_client_set_cache_age (PK_CLIENT (self->task_local), G_MAXUINT);
	pk_client_set_interactive (PK_CLIENT (self->task_local), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_details_local (PK_CLIENT (self->task_local),
					       files,
					       cancellable,
					       gs_packagekit_helper_cb, helper,
					       error);
	g_mutex_unlock (&self->task_mutex_local);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;

	/* get results */
	array = pk_results_get_details_array (results);
	if (array->len == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "no details for %s", filename);
		return FALSE;
	}
	if (array->len > 1) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "too many details [%u] for %s",
			     array->len, filename);
		return FALSE;
	}

	/* create application */
	item = g_ptr_array_index (array, 0);
	app = gs_app_new (NULL);
	gs_plugin_packagekit_set_packaging_format (plugin, app);
	gs_app_set_metadata (app, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));
	package_id = pk_details_get_package_id (item);
	split = pk_package_id_split (package_id);
	if (split == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "invalid package-id: %s", package_id);
		return FALSE;
	}
	gs_app_set_management_plugin (app, "packagekit");
	gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, split[PK_PACKAGE_ID_NAME]);
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
			    pk_details_get_summary (item));
	gs_app_set_version (app, split[PK_PACKAGE_ID_VERSION]);
	gs_app_add_source (app, split[PK_PACKAGE_ID_NAME]);
	gs_app_add_source_id (app, package_id);
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST,
				pk_details_get_description (item));
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, pk_details_get_url (item));
	gs_app_set_size_installed (app, pk_details_get_size (item));
	gs_app_set_size_download (app, 0);
	license_spdx = as_license_to_spdx_id (pk_details_get_license (item));
	gs_app_set_license (app, GS_APP_QUALITY_LOWEST, license_spdx);
	add_quirks_from_package_name (app, split[PK_PACKAGE_ID_NAME]);

	/* is already installed? */
	if (!gs_plugin_packagekit_local_check_installed (self,
							 app,
							 cancellable,
							 error))
		return FALSE;

	/* look for a desktop file so we can use a valid application id */
	if (!gs_plugin_packagekit_refresh_guess_app_id (self,
							app,
							filename,
							cancellable,
							error))
		return FALSE;

	gs_app_list_add (list, app);
	return TRUE;
}

static gboolean
gs_plugin_packagekit_convert_error (GError **error,
				    PkErrorEnum error_enum,
				    const gchar *details)
{
	switch (error_enum) {
	case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
	case PK_ERROR_ENUM_NO_CACHE:
	case PK_ERROR_ENUM_NO_NETWORK:
	case PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY:
	case PK_ERROR_ENUM_CANNOT_FETCH_SOURCES:
	case PK_ERROR_ENUM_UNFINISHED_TRANSACTION:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NO_NETWORK,
				     details);
		break;
	case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
	case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
	case PK_ERROR_ENUM_GPG_FAILURE:
	case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
	case PK_ERROR_ENUM_PACKAGE_CORRUPT:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NO_SECURITY,
				     details);
		break;
	case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_CANCELLED,
				     details);
		break;
	case PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE:
	case PK_ERROR_ENUM_UPDATE_NOT_FOUND:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     details);
		break;
	case PK_ERROR_ENUM_NO_SPACE_ON_DEVICE:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NO_SPACE,
				     details);
		break;
	default:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     details);
		break;
	}
	return FALSE;
}

gboolean
gs_plugin_add_updates_historical (GsPlugin *plugin,
				  GsAppList *list,
				  GCancellable *cancellable,
				  GError **error)
{
	guint64 mtime;
	guint i;
	g_autoptr(GPtrArray) package_array = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(PkResults) results = NULL;
	PkExitEnum exit_code;

	/* get the results */
	results = pk_offline_get_results (&error_local);
	if (results == NULL) {
		/* was any offline update attempted */
		if (g_error_matches (error_local,
		                     PK_OFFLINE_ERROR,
		                     PK_OFFLINE_ERROR_NO_DATA)) {
			return TRUE;
		}

		gs_plugin_packagekit_error_convert (&error_local);

		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_INVALID_FORMAT,
		             "Failed to get offline update results: %s",
		             error_local->message);
		return FALSE;
	}

	/* get the mtime of the results */
	mtime = pk_offline_get_results_mtime (error);
	if (mtime == 0) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}

	/* only return results if successful */
	exit_code = pk_results_get_exit_code (results);
	if (exit_code != PK_EXIT_ENUM_SUCCESS) {
		g_autoptr(PkError) error_code = NULL;

		error_code = pk_results_get_error_code (results);
		if (error_code == NULL) {
			g_set_error (error,
			             GS_PLUGIN_ERROR,
			             GS_PLUGIN_ERROR_FAILED,
			             "Offline update failed without error_code set");
			return FALSE;
		}

		return gs_plugin_packagekit_convert_error (error,
		                                           pk_error_get_code (error_code),
		                                           pk_error_get_details (error_code));
	}

	/* distro upgrade? */
	if (pk_results_get_role (results) == PK_ROLE_ENUM_UPGRADE_SYSTEM) {
		g_autoptr(GsApp) app = NULL;

		app = gs_app_new (NULL);
		gs_app_set_from_unique_id (app, "*/*/*/system/*", AS_COMPONENT_KIND_GENERIC);
		gs_app_set_management_plugin (app, "packagekit");
		gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
		gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
		gs_app_set_kind (app, AS_COMPONENT_KIND_OPERATING_SYSTEM);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_set_install_date (app, mtime);
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_list_add (list, app);

		return TRUE;
	}

	/* get list of package-ids */
	package_array = pk_results_get_package_array (results);
	for (i = 0; i < package_array->len; i++) {
		PkPackage *pkg = g_ptr_array_index (package_array, i);
		const gchar *package_id;
		g_autoptr(GsApp) app = NULL;
		g_auto(GStrv) split = NULL;

		app = gs_app_new (NULL);
		package_id = pk_package_get_id (pkg);
		split = g_strsplit (package_id, ";", 4);
		gs_plugin_packagekit_set_packaging_format (plugin, app);
		gs_app_add_source (app, split[0]);
		gs_app_set_update_version (app, split[1]);
		gs_app_set_management_plugin (app, "packagekit");
		gs_app_add_source_id (app, package_id);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
		gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_set_install_date (app, mtime);
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_list_add (list, app);
	}
	return TRUE;
}

gboolean
gs_plugin_url_to_app (GsPlugin *plugin,
		      GsAppList *list,
		      const gchar *url,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autofree gchar *scheme = NULL;
	g_autofree gchar *path = NULL;
	const gchar *id = NULL;
	const gchar * const *id_like = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;
	g_autoptr(GPtrArray) packages = NULL;
	g_autoptr(GPtrArray) details = NULL;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);

	path = gs_utils_get_url_path (url);

	/* only do this for apt:// on debian or debian-like distros */
	os_release = gs_os_release_new (error);
	if (os_release == NULL) {
		g_prefix_error (error, "failed to determine OS information:");
		return FALSE;
	} else  {
		id = gs_os_release_get_id (os_release);
		id_like = gs_os_release_get_id_like (os_release);
		scheme = gs_utils_get_url_scheme (url);
		if (!(g_strcmp0 (scheme, "apt") == 0 &&
		     (g_strcmp0 (id, "debian") == 0 ||
		      g_strv_contains (id_like, "debian")))) {
			return TRUE;
		}
	}

	app = gs_app_new (NULL);
	gs_plugin_packagekit_set_packaging_format (plugin, app);
	gs_app_add_source (app, path);
	gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);

	package_ids = g_new0 (gchar *, 2);
	package_ids[0] = g_strdup (path);

	g_mutex_lock (&self->client_mutex_url_to_app);
	pk_client_set_interactive (self->client_url_to_app, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_resolve (self->client_url_to_app,
				     pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_ARCH, -1),
				     package_ids,
				     cancellable,
				     gs_packagekit_helper_cb, helper,
				     error);
	g_mutex_unlock (&self->client_mutex_url_to_app);

	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to resolve package_ids: ");
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	details = pk_results_get_details_array (results);

	if (packages->len >= 1) {
		g_autoptr(GHashTable) details_collection = NULL;

		if (gs_app_get_local_file (app) != NULL)
			return TRUE;

		details_collection = gs_plugin_packagekit_details_array_to_hash (details);

		gs_plugin_packagekit_resolve_packages_app (plugin, packages, app);
		gs_plugin_packagekit_refine_details_app (plugin, details_collection, app);

		gs_app_list_add (list, app);
	} else {
		g_warning ("no results returned");
	}

	return TRUE;
}

static gchar *
get_proxy_http (GsPluginPackagekit *self)
{
	gboolean ret;
	GString *string = NULL;
	gint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;
	g_autofree gchar *password = NULL;
	g_autofree gchar *username = NULL;

	proxy_mode = g_settings_get_enum (self->settings_proxy, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (self->settings_http, "host");
	if (host == NULL || host[0] == '\0')
		return NULL;

	port = g_settings_get_int (self->settings_http, "port");

	ret = g_settings_get_boolean (self->settings_http,
				      "use-authentication");
	if (ret) {
		username = g_settings_get_string (self->settings_http,
						  "authentication-user");
		password = g_settings_get_string (self->settings_http,
						  "authentication-password");
	}

	/* make PackageKit proxy string */
	string = g_string_new ("");
	if (username != NULL || password != NULL) {
		if (username != NULL)
			g_string_append_printf (string, "%s", username);
		if (password != NULL)
			g_string_append_printf (string, ":%s", password);
		g_string_append (string, "@");
	}
	g_string_append (string, host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	return g_string_free (string, FALSE);
}

static gchar *
get_proxy_https (GsPluginPackagekit *self)
{
	GString *string = NULL;
	gint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;

	proxy_mode = g_settings_get_enum (self->settings_proxy, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (self->settings_https, "host");
	if (host == NULL || host[0] == '\0')
		return NULL;
	port = g_settings_get_int (self->settings_https, "port");
	if (port == 0)
		return NULL;

	/* make PackageKit proxy string */
	string = g_string_new (host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	return g_string_free (string, FALSE);
}

static gchar *
get_proxy_ftp (GsPluginPackagekit *self)
{
	GString *string = NULL;
	gint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;

	proxy_mode = g_settings_get_enum (self->settings_proxy, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (self->settings_ftp, "host");
	if (host == NULL || host[0] == '\0')
		return NULL;
	port = g_settings_get_int (self->settings_ftp, "port");
	if (port == 0)
		return NULL;

	/* make PackageKit proxy string */
	string = g_string_new (host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	return g_string_free (string, FALSE);
}

static gchar *
get_proxy_socks (GsPluginPackagekit *self)
{
	GString *string = NULL;
	gint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;

	proxy_mode = g_settings_get_enum (self->settings_proxy, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (self->settings_socks, "host");
	if (host == NULL || host[0] == '\0')
		return NULL;
	port = g_settings_get_int (self->settings_socks, "port");
	if (port == 0)
		return NULL;

	/* make PackageKit proxy string */
	string = g_string_new (host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	return g_string_free (string, FALSE);
}

static gchar *
get_no_proxy (GsPluginPackagekit *self)
{
	GString *string = NULL;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar **hosts = NULL;
	guint i;

	proxy_mode = g_settings_get_enum (self->settings_proxy, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	hosts = g_settings_get_strv (self->settings_proxy, "ignore-hosts");
	if (hosts == NULL)
		return NULL;

	/* make PackageKit proxy string */
	string = g_string_new ("");
	for (i = 0; hosts[i] != NULL; i++) {
		if (i == 0)
			g_string_assign (string, hosts[i]);
		else
			g_string_append_printf (string, ",%s", hosts[i]);
		g_free (hosts[i]);
	}

	return g_string_free (string, FALSE);
}

static gchar *
get_pac (GsPluginPackagekit *self)
{
	GDesktopProxyMode proxy_mode;
	gchar *url = NULL;

	proxy_mode = g_settings_get_enum (self->settings_proxy, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_AUTO)
		return NULL;

	url = g_settings_get_string (self->settings_proxy, "autoconfig-url");
	if (url == NULL)
		return NULL;

	return url;
}

static void
set_proxy_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	if (!pk_control_set_proxy_finish (PK_CONTROL (object), res, &error)) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to set proxies: %s", error->message);
	}
}

static void
reload_proxy_settings (GsPluginPackagekit *self,
                       GCancellable       *cancellable)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	g_autofree gchar *proxy_http = NULL;
	g_autofree gchar *proxy_https = NULL;
	g_autofree gchar *proxy_ftp = NULL;
	g_autofree gchar *proxy_socks = NULL;
	g_autofree gchar *no_proxy = NULL;
	g_autofree gchar *pac = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPermission) permission = NULL;

	/* only if we can achieve the action *without* an auth dialog */
	permission = gs_utils_get_permission ("org.freedesktop.packagekit."
					      "system-network-proxy-configure",
					      cancellable, &error);
	if (permission == NULL) {
		g_debug ("not setting proxy as no permission: %s", error->message);
		return;
	}
	if (!g_permission_get_allowed (permission)) {
		g_debug ("not setting proxy as no auth requested");
		return;
	}

	proxy_http = get_proxy_http (self);
	proxy_https = get_proxy_https (self);
	proxy_ftp = get_proxy_ftp (self);
	proxy_socks = get_proxy_socks (self);
	no_proxy = get_no_proxy (self);
	pac = get_pac (self);

	g_debug ("Setting proxies (http: %s, https: %s, ftp: %s, socks: %s, "
	         "no_proxy: %s, pac: %s)",
	         proxy_http, proxy_https, proxy_ftp, proxy_socks,
	         no_proxy, pac);

	pk_control_set_proxy2_async (self->control_proxy,
				     proxy_http,
				     proxy_https,
				     proxy_ftp,
				     proxy_socks,
				     no_proxy,
				     pac,
				     cancellable,
				     set_proxy_cb,
				     plugin);
}

static void
gs_plugin_packagekit_proxy_changed_cb (GSettings   *settings,
                                       const gchar *key,
                                       gpointer     user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (user_data);

	if (!gs_plugin_get_enabled (GS_PLUGIN (self)))
		return;

	reload_proxy_settings (self, NULL);
}

gboolean
gs_plugin_app_upgrade_download (GsPlugin *plugin,
				GsApp *app,
				GCancellable *cancellable,
				GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
		return TRUE;

	/* check is distro-upgrade */
	if (gs_app_get_kind (app) != AS_COMPONENT_KIND_OPERATING_SYSTEM)
		return TRUE;

	/* ask PK to download enough packages to upgrade the system */
	gs_app_set_state (app, GS_APP_STATE_INSTALLING);
	gs_packagekit_helper_set_progress_app (helper, app);
	g_mutex_lock (&self->task_mutex_upgrade);
	pk_client_set_interactive (PK_CLIENT (self->task_upgrade), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_task_upgrade_system_sync (self->task_upgrade,
					       gs_app_get_version (app),
					       PK_UPGRADE_KIND_ENUM_COMPLETE,
					       cancellable,
					       gs_packagekit_helper_cb, helper,
					       error);
	g_mutex_unlock (&self->task_mutex_upgrade);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
	return TRUE;
}

gboolean
gs_plugin_enable_repo (GsPlugin *plugin,
		       GsApp *repo,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkError) error_code = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (repo),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* is repo */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	/* do sync call */
	gs_plugin_status_update (plugin, repo, GS_PLUGIN_STATUS_WAITING);
	gs_app_set_state (repo, GS_APP_STATE_INSTALLING);
	gs_packagekit_helper_add_app (helper, repo);
	g_mutex_lock (&self->task_mutex);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_repo_enable (PK_CLIENT (self->task),
					 gs_app_get_id (repo),
					 TRUE,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&self->task_mutex);

	/* pk_client_repo_enable() returns an error if the repo is already enabled. */
	if (results != NULL &&
	    (error_code = pk_results_get_error_code (results)) != NULL &&
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_REPO_ALREADY_SET) {
		g_clear_error (error);
	} else if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_app_set_state_recover (repo);
		gs_utils_error_add_origin_id (error, repo);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (repo, GS_APP_STATE_INSTALLED);

	gs_plugin_repository_changed (plugin, repo);

	return TRUE;
}

gboolean
gs_plugin_disable_repo (GsPlugin *plugin,
			GsApp *repo,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkError) error_code = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (repo),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* is repo */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	/* do sync call */
	gs_plugin_status_update (plugin, repo, GS_PLUGIN_STATUS_WAITING);
	gs_app_set_state (repo, GS_APP_STATE_REMOVING);
	gs_packagekit_helper_add_app (helper, repo);
	g_mutex_lock (&self->task_mutex);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_repo_enable (PK_CLIENT (self->task),
					 gs_app_get_id (repo),
					 FALSE,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&self->task_mutex);

	/* pk_client_repo_enable() returns an error if the repo is already enabled. */
	if (results != NULL &&
	    (error_code = pk_results_get_error_code (results)) != NULL &&
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_REPO_ALREADY_SET) {
		g_clear_error (error);
	} else if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_app_set_state_recover (repo);
		gs_utils_error_add_origin_id (error, repo);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (repo, GS_APP_STATE_AVAILABLE);

	gs_plugin_repository_changed (plugin, repo);

	return TRUE;
}

static void
gs_plugin_packagekit_class_init (GsPluginPackagekitClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_plugin_packagekit_dispose;
	object_class->finalize = gs_plugin_packagekit_finalize;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_PACKAGEKIT;
}
