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
#include "gs-packagekit-task.h"

#include "gs-plugin-packagekit.h"

/*
 * SECTION:
 * Uses the system PackageKit instance to return installed packages,
 * sources and the ability to add and remove packages. Supports package history
 * and converting URIs to apps.
 *
 * Supports setting the session proxy on the system PackageKit instance.
 *
 * Also supports doing a PackageKit UpdatePackages(ONLY_DOWNLOAD) method on
 * refresh and also converts any package files to applications the best we can.
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

	PkTask			*task_refresh;
	GMutex			 task_mutex_refresh;

	GCancellable		*proxy_settings_cancellable;  /* (nullable) (owned) */
};

G_DEFINE_TYPE (GsPluginPackagekit, gs_plugin_packagekit, GS_TYPE_PLUGIN)

static void gs_plugin_packagekit_updates_changed_cb (PkControl *control, GsPlugin *plugin);
static void gs_plugin_packagekit_repo_list_changed_cb (PkControl *control, GsPlugin *plugin);
static void gs_plugin_packagekit_refine_history_async (GsPluginPackagekit  *self,
                                                       GsAppList           *list,
                                                       GCancellable        *cancellable,
                                                       GAsyncReadyCallback  callback,
                                                       gpointer             user_data);
static gboolean gs_plugin_packagekit_refine_history_finish (GsPluginPackagekit  *self,
                                                            GAsyncResult        *result,
                                                            GError             **error);
static void gs_plugin_packagekit_proxy_changed_cb (GSettings   *settings,
                                                   const gchar *key,
                                                   gpointer     user_data);
static void reload_proxy_settings_async (GsPluginPackagekit  *self,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data);
static gboolean reload_proxy_settings_finish (GsPluginPackagekit  *self,
                                              GAsyncResult        *result,
                                              GError             **error);

static void
gs_plugin_packagekit_init (GsPluginPackagekit *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	/* core */
	g_mutex_init (&self->task_mutex);
	self->task = gs_packagekit_task_new (plugin);
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
	self->task_local = gs_packagekit_task_new (plugin);
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
	self->task_upgrade = gs_packagekit_task_new (plugin);
	pk_task_set_only_download (self->task_upgrade, TRUE);
	pk_client_set_background (PK_CLIENT (self->task_upgrade), TRUE);
	pk_client_set_cache_age (PK_CLIENT (self->task_upgrade), 60 * 60 * 24);
	pk_client_set_interactive (PK_CLIENT (self->task_upgrade), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* refresh */
	g_mutex_init (&self->task_mutex_refresh);
	self->task_refresh = gs_packagekit_task_new (plugin);
	pk_task_set_only_download (self->task_refresh, TRUE);
	pk_client_set_background (PK_CLIENT (self->task_refresh), TRUE);
	pk_client_set_interactive (PK_CLIENT (self->task_refresh), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* need pkgname and ID */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");

	/* we can return better results than dpkg directly */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "dpkg");
}

static void
gs_plugin_packagekit_dispose (GObject *object)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (object);

	g_cancellable_cancel (self->proxy_settings_cancellable);
	g_clear_object (&self->proxy_settings_cancellable);

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

	/* refresh */
	g_clear_object (&self->task_refresh);

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
	g_mutex_clear (&self->task_mutex_refresh);

	G_OBJECT_CLASS (gs_plugin_packagekit_parent_class)->finalize (object);
}

typedef gboolean (*GsAppFilterFunc) (GsApp *app);

/* The elements in the returned #GPtrArray reference memory from within the
 * @apps list, so the array is only valid as long as @apps is not modified or
 * freed. */
static GPtrArray *
app_list_get_package_ids (GsAppList       *apps,
                          GsAppFilterFunc  app_filter,
                          gboolean         ignore_installed)
{
	g_autoptr(GPtrArray) list_package_ids = g_ptr_array_new_with_free_func (NULL);

	for (guint i = 0; i < gs_app_list_length (apps); i++) {
		GsApp *app = gs_app_list_index (apps, i);
		GPtrArray *app_source_ids;

		if (app_filter != NULL && !app_filter (app))
			continue;

		app_source_ids = gs_app_get_source_ids (app);
		for (guint j = 0; j < app_source_ids->len; j++) {
			const gchar *package_id = g_ptr_array_index (app_source_ids, j);

			if (ignore_installed &&
			    g_strstr_len (package_id, -1, ";installed") != NULL)
				continue;

			g_ptr_array_add (list_package_ids, (gchar *) package_id);
		}
	}

	if (list_package_ids->len > 0)
		g_ptr_array_add (list_package_ids, NULL);

	return g_steal_pointer (&list_package_ids);
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
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_GET_SOURCES, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_GET_SOURCES, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
		gs_app_set_management_plugin (app, plugin);
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
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_INSTALL, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
	guint i;
	g_autofree gchar *local_filename = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GPtrArray) array_package_ids = NULL;
	g_autoptr(PkResults) results = NULL;

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin))
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
		gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_INSTALL, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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

		addons = gs_app_get_addons (app);
		array_package_ids = app_list_get_package_ids (addons,
							      gs_app_get_to_be_installed,
							      TRUE);

		for (i = 0; i < source_ids->len; i++) {
			package_id = g_ptr_array_index (source_ids, i);
			if (g_strstr_len (package_id, -1, ";installed") != NULL)
				continue;
			g_ptr_array_add (array_package_ids, (gpointer) package_id);
		}

		if (array_package_ids->len == 0) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "no packages to install");
			return FALSE;
		}

		gs_app_set_state (app, GS_APP_STATE_INSTALLING);

		for (i = 0; i < gs_app_list_length (addons); i++) {
			GsApp *addon = gs_app_list_index (addons, i);
			if (gs_app_get_to_be_installed (addon))
				gs_app_set_state (addon, GS_APP_STATE_INSTALLING);
		}
		gs_packagekit_helper_add_app (helper, app);
		g_mutex_lock (&self->task_mutex);
		gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_INSTALL, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
		gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_INSTALL, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
	if (!gs_app_has_management_plugin (app, plugin))
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
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_REMOVE, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
	gs_app_set_management_plugin (app, plugin);
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
	g_autoptr(GsApp) first_app = NULL;
	gboolean all_downloaded = TRUE;

	/* do sync call */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	g_mutex_lock (&self->task_mutex);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_GET_UPDATES, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
		all_downloaded = all_downloaded && !gs_app_get_size_download (app);
		if (all_downloaded && first_app == NULL)
			first_app = g_object_ref (app);
		gs_app_list_add (list, app);
	}
	/* Having all packages downloaded doesn't mean the update is also prepared,
	   because the 'prepared-update' file can be missing, thus verify it and
	   if not found, then set one application as needed download, to have
	   the update properly prepared. */
	if (all_downloaded && first_app != NULL) {
		g_auto(GStrv) prepared_ids = NULL;
		/* It's an overhead to get all the package IDs, but there's no easier
		   way to verify the prepared-update file exists. */
		prepared_ids = pk_offline_get_prepared_ids (NULL);
		if (prepared_ids == NULL || prepared_ids[0] == NULL)
			gs_app_set_size_download (first_app, 1);
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
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_SEARCH_FILES, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_SEARCH_PROVIDES, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
	if (!gs_app_has_management_plugin (app, plugin))
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
		gs_app_set_management_plugin (app, plugin);
		gs_plugin_packagekit_set_packaging_format (plugin, app);
		return;
	} else if (gs_app_get_kind (app) == AS_COMPONENT_KIND_OPERATING_SYSTEM) {
		gs_app_set_management_plugin (app, plugin);
	}
}

typedef struct
{
	GsAppList *list;  /* (owned) (not nullable) */
	GsPackagekitHelper *progress_data;  /* (owned) (not nullable) */
} ResolvePackagesWithFilterData;

static void
resolve_packages_with_filter_data_free (ResolvePackagesWithFilterData *data)
{
	g_clear_object (&data->list);
	g_clear_object (&data->progress_data);

	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ResolvePackagesWithFilterData, resolve_packages_with_filter_data_free)

static void resolve_packages_with_filter_cb (GObject      *source_object,
                                             GAsyncResult *result,
                                             gpointer      user_data);

static void
gs_plugin_packagekit_resolve_packages_with_filter_async (GsPluginPackagekit  *self,
                                                         GsAppList           *list,
                                                         PkBitfield           filter,
                                                         GCancellable        *cancellable,
                                                         GAsyncReadyCallback  callback,
                                                         gpointer             user_data)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	GPtrArray *sources;
	GsApp *app;
	const gchar *pkgname;
	guint i;
	guint j;
	g_autoptr(GPtrArray) package_ids = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(ResolvePackagesWithFilterData) data = NULL;
	ResolvePackagesWithFilterData *data_unowned;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_resolve_packages_with_filter_async);
	data_unowned = data = g_new0 (ResolvePackagesWithFilterData, 1);
	data->list = g_object_ref (list);
	data->progress_data = gs_packagekit_helper_new (plugin);
	g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) resolve_packages_with_filter_data_free);

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

	if (package_ids->len == 0) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	g_ptr_array_add (package_ids, NULL);

	/* resolve them all at once */
	g_mutex_lock (&self->client_mutex_refine);
	pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	pk_client_resolve_async (self->client_refine,
				 filter,
				 (gchar **) package_ids->pdata,
				 cancellable,
				 gs_packagekit_helper_cb, data_unowned->progress_data,
				 resolve_packages_with_filter_cb,
				 g_steal_pointer (&task));
	g_mutex_unlock (&self->client_mutex_refine);
}

static void
resolve_packages_with_filter_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	ResolvePackagesWithFilterData *data = g_task_get_task_data (task);
	GsAppList *list = data->list;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) packages = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, &local_error)) {
		g_prefix_error (&local_error, "failed to resolve package_ids: ");
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* get results */
	packages = pk_results_get_package_array (results);

	/* if the user types more characters we'll get cancelled - don't go on
	 * to mark apps as unavailable because packages->len = 0 */
	if (g_cancellable_set_error_if_cancelled (cancellable, &local_error)) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (gs_app_get_local_file (app) != NULL)
			continue;
		gs_plugin_packagekit_resolve_packages_app (GS_PLUGIN (self), packages, app);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_packagekit_resolve_packages_with_filter_finish (GsPluginPackagekit  *self,
                                                          GAsyncResult        *result,
                                                          GError             **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
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
	markdown = gs_markdown_new (GS_MARKDOWN_OUTPUT_PANGO);
	gs_markdown_set_smart_quoting (markdown, FALSE);
	gs_markdown_set_autocode (markdown, FALSE);
	gs_markdown_set_autolinkify (markdown, FALSE);
	tmp = gs_markdown_parse (markdown, text);
	if (tmp != NULL)
		return tmp;
	return g_strdup (text);
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
	tmp = gs_app_get_update_details_markup (app);
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
gs_plugin_packagekit_refine_valid_package_name (const gchar *source)
{
	if (g_strstr_len (source, -1, "/") != NULL)
		return FALSE;
	return TRUE;
}

typedef struct {
	/* Track pending operations. */
	guint n_pending_operations;
	gboolean completed;
	GError *error;  /* (nullable) (owned) */
	GPtrArray *progress_datas;  /* (element-type GsPackagekitHelper) (owned) (not nullable) */

	/* Input data for operations. */
	GsAppList *full_list;  /* (nullable) (owned) */
	GsAppList *resolve_list;  /* (nullable) (owned) */
	GsApp *app_operating_system;  /* (nullable) (owned) */
	GsAppList *update_details_list;  /* (nullable) (owned) */
	GsAppList *details_list;  /* (nullable) (owned) */
} RefineData;

static void
refine_data_free (RefineData *data)
{
	g_assert (data->n_pending_operations == 0);
	g_assert (data->completed);

	g_clear_error (&data->error);
	g_clear_pointer (&data->progress_datas, g_ptr_array_unref);
	g_clear_object (&data->full_list);
	g_clear_object (&data->resolve_list);
	g_clear_object (&data->app_operating_system);
	g_clear_object (&data->update_details_list);
	g_clear_object (&data->details_list);

	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RefineData, refine_data_free)

/* Add @helper to the list of progress data closures to free when the
 * #RefineData is freed. This means it can be reliably used, 0 or more times,
 * by the async operation up until the operation is finished. */
static GsPackagekitHelper *
refine_task_add_progress_data (GTask              *refine_task,
                               GsPackagekitHelper *helper)
{
	RefineData *data = g_task_get_task_data (refine_task);

	g_ptr_array_add (data->progress_datas, g_object_ref (helper));

	return helper;
}

static GTask *
refine_task_add_operation (GTask *refine_task)
{
	RefineData *data = g_task_get_task_data (refine_task);

	g_assert (!data->completed);
	data->n_pending_operations++;

	return g_object_ref (refine_task);
}

static void
refine_task_complete_operation (GTask *refine_task)
{
	RefineData *data = g_task_get_task_data (refine_task);

	g_assert (data->n_pending_operations > 0);
	data->n_pending_operations--;

	/* Have all operations completed? */
	if (data->n_pending_operations == 0) {
		g_assert (!data->completed);
		data->completed = TRUE;

		if (data->error != NULL)
			g_task_return_error (refine_task, g_steal_pointer (&data->error));
		else
			g_task_return_boolean (refine_task, TRUE);
	}
}

static void
refine_task_complete_operation_with_error (GTask  *refine_task,
					   GError *error  /* (transfer full) */)
{
	RefineData *data = g_task_get_task_data (refine_task);
	g_autoptr(GError) owned_error = g_steal_pointer (&error);

	/* Multiple operations might fail. Just take the first error. */
	if (data->error == NULL)
		data->error = g_steal_pointer (&owned_error);

	refine_task_complete_operation (refine_task);
}

typedef struct {
	GTask *refine_task;  /* (owned) (not nullable) */
	GsApp *app;  /* (owned) (not nullable) */
	gchar *filename;  /* (owned) (not nullable) */
} SearchFilesData;

static void
search_files_data_free (SearchFilesData *data)
{
	g_free (data->filename);
	g_clear_object (&data->app);
	g_clear_object (&data->refine_task);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SearchFilesData, search_files_data_free)

static SearchFilesData *
search_files_data_new_operation (GTask       *refine_task,
                                 GsApp       *app,
                                 const gchar *filename)
{
	g_autoptr(SearchFilesData) data = g_new0 (SearchFilesData, 1);
	data->refine_task = refine_task_add_operation (refine_task);
	data->app = g_object_ref (app);
	data->filename = g_strdup (filename);

	return g_steal_pointer (&data);
}

static void upgrade_system_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data);
static void resolve_all_packages_with_filter_cb (GObject      *source_object,
                                                 GAsyncResult *result,
                                                 gpointer      user_data);
static void search_files_cb (GObject      *source_object,
                             GAsyncResult *result,
                             gpointer      user_data);
static void get_update_detail_cb (GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data);
static void get_details_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data);
static void get_updates_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data);
static void refine_all_history_cb (GObject      *source_object,
                                   GAsyncResult *result,
                                   gpointer      user_data);

static void
gs_plugin_packagekit_refine_async (GsPlugin            *plugin,
                                   GsAppList           *list,
                                   GsPluginRefineFlags  flags,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GsAppList) resolve_list = gs_app_list_new ();
	g_autoptr(GsAppList) update_details_list = gs_app_list_new ();
	g_autoptr(GsAppList) details_list = gs_app_list_new ();
	g_autoptr(GsAppList) history_list = gs_app_list_new ();
	g_autoptr(GTask) task = NULL;
	g_autoptr(RefineData) data = NULL;
	RefineData *data_unowned = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_refine_async);
	data_unowned = data = g_new0 (RefineData, 1);
	data->full_list = g_object_ref (list);
	data->n_pending_operations = 1;  /* to prevent the task being completed before all operations have been started */
	data->progress_datas = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) refine_data_free);

	/* Process the @list and work out what information is needed for each
	 * app. */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GPtrArray *sources;

		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;

		if (!gs_app_has_management_plugin (app, NULL) &&
		    !gs_app_has_management_plugin (app, GS_PLUGIN (self)))
			continue;

		sources = gs_app_get_sources (app);

		if (sources->len > 0 &&
		    gs_plugin_packagekit_refine_valid_package_name (g_ptr_array_index (sources, 0)) &&
		    (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN ||
		     gs_plugin_refine_requires_package_id (app, flags) ||
		     gs_plugin_refine_requires_origin (app, flags) ||
		     gs_plugin_refine_requires_version (app, flags))) {
			gs_app_list_add (resolve_list, app);
		}

		if ((gs_app_get_state (app) == GS_APP_STATE_UPDATABLE ||
		     gs_app_get_state (app) == GS_APP_STATE_UNKNOWN) &&
		    gs_app_get_source_id_default (app) != NULL &&
		    gs_plugin_refine_requires_update_details (app, flags)) {
			gs_app_list_add (update_details_list, app);
		}

		if (gs_app_get_source_id_default (app) != NULL &&
		    gs_plugin_refine_app_needs_details (flags, app)) {
			gs_app_list_add (details_list, app);
		}

		if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY) != 0 &&
		    sources->len > 0 &&
		    gs_app_get_install_date (app) == 0) {
			gs_app_list_add (history_list, app);
		}
	}

	/* when we need the cannot-be-upgraded applications, we implement this
	 * by doing a UpgradeSystem(SIMULATE) which adds the removed packages
	 * to the related-apps list with a state of %GS_APP_STATE_UNAVAILABLE */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED) {
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
			guint cache_age_save;

			if (gs_app_get_kind (app) != AS_COMPONENT_KIND_OPERATING_SYSTEM)
				continue;

			gs_packagekit_helper_add_app (helper, app);

			/* Expose the @app to the callback functions so that
			 * upgrade packages can be added as related. This only
			 * supports one OS. */
			g_assert (data_unowned->app_operating_system == NULL);
			data_unowned->app_operating_system = g_object_ref (app);

			/* ask PK to simulate upgrading the system */
			g_mutex_lock (&self->client_mutex_refine);
			cache_age_save = pk_client_get_cache_age (self->client_refine);
			pk_client_set_cache_age (self->client_refine, 60 * 60 * 24 * 7); /* once per week */
			pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
			pk_client_upgrade_system_async (self->client_refine,
							pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_SIMULATE, -1),
							gs_app_get_version (app),
							PK_UPGRADE_KIND_ENUM_COMPLETE,
							cancellable,
							gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
							upgrade_system_cb,
							refine_task_add_operation (task));
			pk_client_set_cache_age (self->client_refine, cache_age_save);
			g_mutex_unlock (&self->client_mutex_refine);

			/* Only support one operating system. */
			break;
		}
	}

	/* can we resolve in one go? */
	if (gs_app_list_length (resolve_list) > 0) {
		PkBitfield filter;

		/* Expose the @resolve_list to the callback functions in case a
		 * second attempt is needed. */
		g_assert (data_unowned->resolve_list == NULL);
		data_unowned->resolve_list = g_object_ref (resolve_list);

		/* first, try to resolve packages with ARCH filter */
		filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
			                         PK_FILTER_ENUM_ARCH,
			                         -1);

		gs_plugin_packagekit_resolve_packages_with_filter_async (self,
									 resolve_list,
									 filter,
									 cancellable,
									 resolve_all_packages_with_filter_cb,
									 refine_task_add_operation (task));
	}

	/* set the package-id for an installed desktop file */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION) != 0) {
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			g_autofree gchar *fn = NULL;
			GsApp *app = gs_app_list_index (list, i);
			const gchar *tmp;
			const gchar *to_array[] = { NULL, NULL };
			g_autoptr(GsPackagekitHelper) helper = NULL;

			if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
				continue;
			if (gs_app_get_source_id_default (app) != NULL)
				continue;
			if (!gs_app_has_management_plugin (app, NULL) &&
			    !gs_app_has_management_plugin (app, GS_PLUGIN (self)))
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

			helper = gs_packagekit_helper_new (plugin);
			to_array[0] = fn;
			gs_packagekit_helper_add_app (helper, app);
			g_mutex_lock (&self->client_mutex_refine);
			pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
			pk_client_search_files_async (self->client_refine,
						      pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
						      (gchar **) to_array,
						      cancellable,
						      gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
						      search_files_cb,
						      search_files_data_new_operation (task, app, fn));
			g_mutex_unlock (&self->client_mutex_refine);
		}
	}

	/* any update details missing? */
	if (gs_app_list_length (update_details_list) > 0) {
		GsApp *app;
		g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
		g_autofree const gchar **package_ids = NULL;

		/* Expose the @update_details_list to the callback functions so
		 * its apps can be updated. */
		g_assert (data_unowned->update_details_list == NULL);
		data_unowned->update_details_list = g_object_ref (update_details_list);

		package_ids = g_new0 (const gchar *, gs_app_list_length (update_details_list) + 1);
		for (guint i = 0; i < gs_app_list_length (update_details_list); i++) {
			app = gs_app_list_index (update_details_list, i);
			package_ids[i] = gs_app_get_source_id_default (app);
			g_assert (package_ids[i] != NULL);  /* checked when update_details_list is built */
		}

		/* get any update details */
		g_mutex_lock (&self->client_mutex_refine);
		pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
		pk_client_get_update_detail_async (self->client_refine,
						   (gchar **) package_ids,
						   cancellable,
						   gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
						   get_update_detail_cb,
						   refine_task_add_operation (task));
		g_mutex_unlock (&self->client_mutex_refine);
	}

	/* any package details missing? */
	if (gs_app_list_length (details_list) > 0) {
		g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
		g_autoptr(GPtrArray) package_ids = NULL;

		/* Expose the @details_list to the callback functions so
		 * its apps can be updated. */
		g_assert (data_unowned->details_list == NULL);
		data_unowned->details_list = g_object_ref (details_list);

		package_ids = app_list_get_package_ids (details_list, NULL, FALSE);

		if (package_ids->len > 0) {
			/* get any details */
			g_mutex_lock (&self->client_mutex_refine);
			pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
			pk_client_get_details_async (self->client_refine,
						     (gchar **) package_ids->pdata,
						     cancellable,
						     gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
						     get_details_cb,
						     refine_task_add_operation (task));
			g_mutex_unlock (&self->client_mutex_refine);
		}
	}

	/* get the update severity */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_SEVERITY) != 0) {
		PkBitfield filter;
		g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);

		/* get the list of updates */
		filter = pk_bitfield_value (PK_FILTER_ENUM_NONE);
		g_mutex_lock (&self->client_mutex_refine);
		pk_client_set_interactive (self->client_refine, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
		pk_client_get_updates_async (self->client_refine,
					     filter,
					     cancellable,
					     gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
					     get_updates_cb,
					     refine_task_add_operation (task));
		g_mutex_unlock (&self->client_mutex_refine);
	}

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		/* only process this app if was created by this plugin */
		if (!gs_app_has_management_plugin (app, plugin))
			continue;

		/* the scope is always system-wide */
		if (gs_app_get_scope (app) == AS_COMPONENT_SCOPE_UNKNOWN)
			gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
		if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_UNKNOWN)
			gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	}

	/* add any missing history data */
	if (gs_app_list_length (history_list) > 0) {
		gs_plugin_packagekit_refine_history_async (self,
							   history_list,
							   cancellable,
							   refine_all_history_cb,
							   refine_task_add_operation (task));
	}

	/* Mark the operation to set up all the other operations as completed.
	 * The @refine_task will now be completed once all the async operations
	 * have completed, and the task callback invoked. */
	refine_task_complete_operation (task);
}

static void
upgrade_system_cb (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(GTask) refine_task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (g_task_get_source_object (refine_task));
	RefineData *data = g_task_get_task_data (refine_task);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GsAppList) results_list = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);
	if (!gs_plugin_packagekit_results_valid (results, &local_error)) {
		g_prefix_error (&local_error, "failed to refine distro upgrade: ");
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	results_list = gs_app_list_new ();
	if (!gs_plugin_packagekit_add_results (GS_PLUGIN (self), results_list, results, &local_error)) {
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	/* add each of these as related applications */
	for (guint j = 0; j < gs_app_list_length (results_list); j++) {
		GsApp *app2 = gs_app_list_index (results_list, j);
		if (gs_app_get_state (app2) != GS_APP_STATE_UNAVAILABLE)
			continue;
		gs_app_add_related (data->app_operating_system, app2);
	}

	refine_task_complete_operation (refine_task);
}

static gboolean
gs_plugin_packagekit_refine_finish (GsPlugin      *plugin,
                                    GAsyncResult  *result,
                                    GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void resolve_all_packages_with_filter_cb2 (GObject      *source_object,
                                                  GAsyncResult *result,
                                                  gpointer      user_data);

static void
resolve_all_packages_with_filter_cb (GObject      *source_object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (source_object);
	g_autoptr(GTask) refine_task = g_steal_pointer (&user_data);
	RefineData *data = g_task_get_task_data (refine_task);
	GCancellable *cancellable = g_task_get_cancellable (refine_task);
	GsAppList *resolve_list = data->resolve_list;
	g_autoptr(GsAppList) resolve2_list = NULL;
	PkBitfield filter;
	g_autoptr(GError) local_error = NULL;

	if (!gs_plugin_packagekit_resolve_packages_with_filter_finish (self,
								       result,
								       &local_error)) {
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	/* if any packages remaining in UNKNOWN state, try to resolve them again,
	 * but this time without ARCH filter */
	resolve2_list = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (resolve_list); i++) {
		GsApp *app = gs_app_list_index (resolve_list, i);
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
			gs_app_list_add (resolve2_list, app);
	}
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
		                         PK_FILTER_ENUM_NOT_ARCH,
		                         PK_FILTER_ENUM_NOT_SOURCE,
		                         -1);

	gs_plugin_packagekit_resolve_packages_with_filter_async (self,
								 resolve2_list,
								 filter,
								 cancellable,
								 resolve_all_packages_with_filter_cb2,
								 g_steal_pointer (&refine_task));
}

static void
resolve_all_packages_with_filter_cb2 (GObject      *source_object,
                                      GAsyncResult *result,
                                      gpointer      user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (source_object);
	g_autoptr(GTask) refine_task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;

	if (!gs_plugin_packagekit_resolve_packages_with_filter_finish (self,
								       result,
								       &local_error)) {
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	refine_task_complete_operation (refine_task);
}

static void
search_files_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(SearchFilesData) search_files_data = g_steal_pointer (&user_data);
	GTask *refine_task = search_files_data->refine_task;
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (g_task_get_source_object (refine_task));
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) packages = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, &local_error)) {
		g_prefix_error (&local_error, "failed to search file %s: ", search_files_data->filename);
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (packages->len == 1) {
		PkPackage *package;
		package = g_ptr_array_index (packages, 0);
		gs_plugin_packagekit_set_metadata_from_package (GS_PLUGIN (self), search_files_data->app, package);
	} else {
		g_warning ("Failed to find one package for %s, %s, [%u]",
			   gs_app_get_id (search_files_data->app), search_files_data->filename, packages->len);
	}

	refine_task_complete_operation (refine_task);
}

static void
get_update_detail_cb (GObject      *source_object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(GTask) refine_task = g_steal_pointer (&user_data);
	RefineData *data = g_task_get_task_data (refine_task);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);
	if (!gs_plugin_packagekit_results_valid (results, &local_error)) {
		g_prefix_error (&local_error, "failed to get update details: ");
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	/* set the update details for the update */
	array = pk_results_get_update_detail_array (results);
	for (guint j = 0; j < gs_app_list_length (data->update_details_list); j++) {
		GsApp *app = gs_app_list_index (data->update_details_list, j);
		const gchar *package_id = gs_app_get_source_id_default (app);

		for (guint i = 0; i < array->len; i++) {
			const gchar *tmp;
			g_autofree gchar *desc = NULL;
			PkUpdateDetail *update_detail;

			/* right package? */
			update_detail = g_ptr_array_index (array, i);
			if (g_strcmp0 (package_id, pk_update_detail_get_package_id (update_detail)) != 0)
				continue;
			tmp = pk_update_detail_get_update_text (update_detail);
			desc = gs_plugin_packagekit_fixup_update_description (tmp);
			if (desc != NULL)
				gs_app_set_update_details_markup (app, desc);
			break;
		}
	}

	refine_task_complete_operation (refine_task);
}

static void
get_details_cb (GObject      *source_object,
                GAsyncResult *result,
                gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(GTask) refine_task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (g_task_get_source_object (refine_task));
	RefineData *data = g_task_get_task_data (refine_task);
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GHashTable) details_collection = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, &local_error)) {
		g_autoptr(GPtrArray) package_ids = app_list_get_package_ids (data->details_list, NULL, FALSE);
		g_autofree gchar *package_ids_str = g_strjoinv (",", (gchar **) package_ids->pdata);
		g_prefix_error (&local_error, "failed to get details for %s: ",
				package_ids_str);
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	/* get the results and copy them into a hash table for fast lookups:
	 * there are typically 400 to 700 elements in @array, and 100 to 200
	 * elements in @list, each with 1 or 2 source IDs to look up (but
	 * sometimes 200) */
	array = pk_results_get_details_array (results);
	details_collection = gs_plugin_packagekit_details_array_to_hash (array);

	/* set the update details for the update */
	for (guint i = 0; i < gs_app_list_length (data->details_list); i++) {
		GsApp *app = gs_app_list_index (data->details_list, i);
		gs_plugin_packagekit_refine_details_app (GS_PLUGIN (self), details_collection, app);
	}

	refine_task_complete_operation (refine_task);
}

static void
get_updates_cb (GObject      *source_object,
                GAsyncResult *result,
                gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(GTask) refine_task = g_steal_pointer (&user_data);
	RefineData *data = g_task_get_task_data (refine_task);
	g_autoptr(PkPackageSack) sack = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, &local_error)) {
		g_prefix_error (&local_error, "failed to get updates for urgency: ");
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	/* set the update severity for the app */
	sack = pk_results_get_package_sack (results);
	for (guint i = 0; i < gs_app_list_length (data->full_list); i++) {
		g_autoptr(PkPackage) pkg = NULL;
		const gchar *package_id;
		GsApp *app = gs_app_list_index (data->full_list, i);

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

	refine_task_complete_operation (refine_task);
}

static void
refine_all_history_cb (GObject      *source_object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (source_object);
	g_autoptr(GTask) refine_task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;

	if (!gs_plugin_packagekit_refine_history_finish (self, result, &local_error)) {
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	refine_task_complete_operation (refine_task);
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

static void setup_cb (GObject      *source_object,
                      GAsyncResult *result,
                      gpointer      user_data);
static void setup_proxy_settings_cb (GObject      *source_object,
                                     GAsyncResult *result,
                                     gpointer      user_data);

static void
gs_plugin_packagekit_setup_async (GsPlugin            *plugin,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_setup_async);

	g_bus_get (G_BUS_TYPE_SYSTEM, cancellable, setup_cb, g_steal_pointer (&task));
}

static void
setup_cb (GObject      *source_object,
          GAsyncResult *result,
          gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) local_error = NULL;

	self->connection_history = g_bus_get_finish (result, &local_error);
	if (self->connection_history == NULL) {
		gs_plugin_packagekit_error_convert (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	reload_proxy_settings_async (self, cancellable, setup_proxy_settings_cb, g_steal_pointer (&task));
}

static void
setup_proxy_settings_cb (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	g_autoptr(GError) local_error = NULL;

	if (!reload_proxy_settings_finish (self, result, &local_error))
		g_warning ("Failed to load proxy settings: %s", local_error->message);
	g_clear_error (&local_error);

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_packagekit_setup_finish (GsPlugin      *plugin,
                                   GAsyncResult  *result,
                                   GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_packagekit_shutdown_async (GsPlugin            *plugin,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_shutdown_async);

	/* Cancel any ongoing proxy settings loading operation. */
	g_cancellable_cancel (self->proxy_settings_cancellable);

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_packagekit_shutdown_finish (GsPlugin      *plugin,
                                      GAsyncResult  *result,
                                      GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void refine_history_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data);

static void
gs_plugin_packagekit_refine_history_async (GsPluginPackagekit  *self,
                                           GsAppList           *list,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
	guint i = 0, j;
	GsApp *app;
	g_autofree const gchar **package_names = NULL;
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_refine_history_async);
	g_task_set_task_data (task, g_object_ref (list), (GDestroyNotify) g_object_unref);

	/* get an array of package names */
	package_names = g_new0 (const gchar *, gs_app_list_length (list) + 1);
	for (j = 0; j < gs_app_list_length (list); j++) {
		app = gs_app_list_index (list, j);
		package_names[i++] = gs_app_get_source_default (app);
	}

	g_debug ("getting history for %u packages", gs_app_list_length (list));
	g_dbus_connection_call (self->connection_history,
				"org.freedesktop.PackageKit",
				"/org/freedesktop/PackageKit",
				"org.freedesktop.PackageKit",
				"GetPackageHistory",
				g_variant_new ("(^asu)", package_names, 0),
				NULL,
				G_DBUS_CALL_FLAGS_NONE,
				GS_PLUGIN_PACKAGEKIT_HISTORY_TIMEOUT,
				cancellable,
				refine_history_cb,
				g_steal_pointer (&task));
}

static void
refine_history_cb (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
	GDBusConnection *connection = G_DBUS_CONNECTION (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GsPlugin *plugin = GS_PLUGIN (self);
	GsAppList *list = g_task_get_task_data (task);
	gboolean ret;
	guint i = 0;
	GVariantIter iter;
	GVariant *value;
	g_autoptr(GVariant) result_variant = NULL;
	g_autoptr(GVariant) tuple = NULL;
	g_autoptr(GError) error_local = NULL;

	result_variant = g_dbus_connection_call_finish (connection, result, &error_local);

	if (result_variant == NULL) {
		g_dbus_error_strip_remote_error (error_local);
		if (g_error_matches (error_local,
				     G_DBUS_ERROR,
				     G_DBUS_ERROR_UNKNOWN_METHOD)) {
			g_debug ("No history available as PackageKit is too old: %s",
				 error_local->message);

			/* just set this to something non-zero so we don't keep
			 * trying to call GetPackageHistory */
			for (i = 0; i < gs_app_list_length (list); i++) {
				GsApp *app = gs_app_list_index (list, i);
				gs_app_set_install_date (app, GS_APP_INSTALL_DATE_UNKNOWN);
			}
		} else if (g_error_matches (error_local,
					    G_IO_ERROR,
					    G_IO_ERROR_CANCELLED)) {
			g_task_return_new_error (task,
						 GS_PLUGIN_ERROR,
						 GS_PLUGIN_ERROR_CANCELLED,
						 "Failed to get history: %s",
						 error_local->message);
			return;
		} else if (g_error_matches (error_local,
					    G_IO_ERROR,
					    G_IO_ERROR_TIMED_OUT)) {
			g_debug ("No history as PackageKit took too long: %s",
				 error_local->message);
			for (i = 0; i < gs_app_list_length (list); i++) {
				GsApp *app = gs_app_list_index (list, i);
				gs_app_set_install_date (app, GS_APP_INSTALL_DATE_UNKNOWN);
			}
		}

		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "Failed to get history: %s",
					 error_local->message);
		return;
	}

	/* get any results */
	tuple = g_variant_get_child_value (result_variant, 0);
	for (i = 0; i < gs_app_list_length (list); i++) {
		g_autoptr(GVariant) entries = NULL;
		GsApp *app = gs_app_list_index (list, i);
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

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_packagekit_refine_history_finish (GsPluginPackagekit  *self,
                                            GAsyncResult        *result,
                                            GError             **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
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
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task_local), GS_PLUGIN_ACTION_FILE_TO_APP, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task_local), GS_PLUGIN_ACTION_FILE_TO_APP, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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
	gs_app_set_management_plugin (app, plugin);
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
		gs_app_set_management_plugin (app, plugin);
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
		gs_app_set_management_plugin (app, plugin);
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

		gs_plugin_packagekit_resolve_packages_app (GS_PLUGIN (self), packages, app);
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

static void get_permission_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data);
static void set_proxy_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data);

static void
reload_proxy_settings_async (GsPluginPackagekit  *self,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, reload_proxy_settings_async);

	/* only if we can achieve the action *without* an auth dialog */
	gs_utils_get_permission_async ("org.freedesktop.packagekit."
				       "system-network-proxy-configure",
				       cancellable, get_permission_cb,
				       g_steal_pointer (&task));
}

static void
get_permission_cb (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autofree gchar *proxy_http = NULL;
	g_autofree gchar *proxy_https = NULL;
	g_autofree gchar *proxy_ftp = NULL;
	g_autofree gchar *proxy_socks = NULL;
	g_autofree gchar *no_proxy = NULL;
	g_autofree gchar *pac = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPermission) permission = NULL;
	g_autoptr(GError) local_error = NULL;

	permission = gs_utils_get_permission_finish (result, &local_error);
	if (permission == NULL) {
		g_debug ("not setting proxy as no permission: %s", local_error->message);
		g_task_return_boolean (task, TRUE);
		return;
	}
	if (!g_permission_get_allowed (permission)) {
		g_debug ("not setting proxy as no auth requested");
		g_task_return_boolean (task, TRUE);
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
				     g_steal_pointer (&task));
}

static void
set_proxy_cb (GObject      *source_object,
              GAsyncResult *result,
              gpointer      user_data)
{
	PkControl *control = PK_CONTROL (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;

	if (!pk_control_set_proxy_finish (control, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
reload_proxy_settings_finish (GsPluginPackagekit  *self,
                              GAsyncResult        *result,
                              GError             **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void proxy_changed_reload_proxy_settings_cb (GObject      *source_object,
                                                    GAsyncResult *result,
                                                    gpointer      user_data);

static void
gs_plugin_packagekit_proxy_changed_cb (GSettings   *settings,
                                       const gchar *key,
                                       gpointer     user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (user_data);

	if (!gs_plugin_get_enabled (GS_PLUGIN (self)))
		return;

	g_cancellable_cancel (self->proxy_settings_cancellable);
	g_clear_object (&self->proxy_settings_cancellable);
	self->proxy_settings_cancellable = g_cancellable_new ();

	reload_proxy_settings_async (self, self->proxy_settings_cancellable,
				     proxy_changed_reload_proxy_settings_cb, self);
}

static void
proxy_changed_reload_proxy_settings_cb (GObject      *source_object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (user_data);
	g_autoptr(GError) local_error = NULL;

	if (!reload_proxy_settings_finish (self, result, &local_error) &&
	    !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		g_warning ("Failed to set proxies: %s", local_error->message);
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
	if (!gs_app_has_management_plugin (app, plugin))
		return TRUE;

	/* check is distro-upgrade */
	if (gs_app_get_kind (app) != AS_COMPONENT_KIND_OPERATING_SYSTEM)
		return TRUE;

	/* ask PK to download enough packages to upgrade the system */
	gs_app_set_state (app, GS_APP_STATE_INSTALLING);
	gs_packagekit_helper_set_progress_app (helper, app);
	g_mutex_lock (&self->task_mutex_upgrade);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task_upgrade), GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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

static gboolean
gs_plugin_packagekit_refresh (GsPlugin *plugin,
			      GsApp *progress_app,
			      guint cache_age,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;

	gs_packagekit_helper_set_progress_app (helper, progress_app);

	g_mutex_lock (&self->task_mutex);
	/* cache age of 1 is user-initiated */
	pk_client_set_background (PK_CLIENT (self->task), cache_age > 1);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	pk_client_set_cache_age (PK_CLIENT (self->task), cache_age);
	/* refresh the metadata */
	results = pk_client_refresh_cache (PK_CLIENT (self->task),
	                                   FALSE /* force */,
	                                   cancellable,
	                                   gs_packagekit_helper_cb, helper,
	                                   error);
	g_mutex_unlock (&self->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		return FALSE;
	}

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
	if (!gs_app_has_management_plugin (repo, plugin))
		return TRUE;

	/* is repo */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	/* do sync call */
	gs_plugin_status_update (plugin, repo, GS_PLUGIN_STATUS_WAITING);
	gs_app_set_state (repo, GS_APP_STATE_INSTALLING);
	gs_packagekit_helper_add_app (helper, repo);
	g_mutex_lock (&self->task_mutex);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_ENABLE_REPO, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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

	/* This can fail silently, it's only to update necessary caches, to provide
	 * up-to-date information after the successful repository enable/install. */
	gs_plugin_packagekit_refresh (plugin, repo, 1, cancellable, NULL);

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
	if (!gs_app_has_management_plugin (repo, plugin))
		return TRUE;

	/* is repo */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	/* do sync call */
	gs_plugin_status_update (plugin, repo, GS_PLUGIN_STATUS_WAITING);
	gs_app_set_state (repo, GS_APP_STATE_REMOVING);
	gs_packagekit_helper_add_app (helper, repo);
	g_mutex_lock (&self->task_mutex);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task), GS_PLUGIN_ACTION_DISABLE_REPO, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
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

static gboolean
_download_only (GsPluginPackagekit  *self,
                GsAppList           *list,
                GsAppList           *progress_list,
                GCancellable        *cancellable,
                GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkPackageSack) sack = NULL;
	g_autoptr(PkResults) results2 = NULL;
	g_autoptr(PkResults) results = NULL;

	/* get the list of packages to update */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	g_mutex_lock (&self->task_mutex_refresh);
	/* never refresh the metadata here as this can surprise the frontend if
	 * we end up downloading a different set of packages than what was
	 * shown to the user */
	pk_client_set_cache_age (PK_CLIENT (self->task_refresh), G_MAXUINT);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task_refresh), GS_PLUGIN_ACTION_DOWNLOAD, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_updates (PK_CLIENT (self->task_refresh),
					 pk_bitfield_value (PK_FILTER_ENUM_NONE),
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&self->task_mutex_refresh);
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
	gs_packagekit_helper_set_progress_list (helper, progress_list);
	g_mutex_lock (&self->task_mutex_refresh);
	/* never refresh the metadata here as this can surprise the frontend if
	 * we end up downloading a different set of packages than what was
	 * shown to the user */
	pk_client_set_cache_age (PK_CLIENT (self->task_refresh), G_MAXUINT);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task_refresh), GS_PLUGIN_ACTION_DOWNLOAD, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results2 = pk_task_update_packages_sync (self->task_refresh,
						 package_ids,
						 cancellable,
						 gs_packagekit_helper_cb, helper,
						 error);
	g_mutex_unlock (&self->task_mutex_refresh);
	gs_app_list_override_progress (progress_list, GS_APP_PROGRESS_UNKNOWN);
	if (results2 == NULL) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}
	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		/* To indicate the app is already downloaded */
		gs_app_set_size_download (app, 0);
	}
	return TRUE;
}

gboolean
gs_plugin_download (GsPlugin *plugin,
                    GsAppList *list,
                    GCancellable *cancellable,
                    GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GsAppList) list_tmp = gs_app_list_new ();
	g_autoptr(GError) error_local = NULL;
	gboolean retval;
	gpointer schedule_entry_handle = NULL;

	/* add any packages */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsAppList *related = gs_app_get_related (app);

		/* add this app */
		if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_PROXY)) {
			if (gs_app_has_management_plugin (app, plugin))
				gs_app_list_add (list_tmp, app);
			continue;
		}

		/* add each related app */
		for (guint j = 0; j < gs_app_list_length (related); j++) {
			GsApp *app_tmp = gs_app_list_index (related, j);
			if (gs_app_has_management_plugin (app_tmp, plugin))
				gs_app_list_add (list_tmp, app_tmp);
		}
	}

	if (gs_app_list_length (list_tmp) == 0)
		return TRUE;

	if (!gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE)) {
		if (!gs_metered_block_app_list_on_download_scheduler (list_tmp, &schedule_entry_handle, cancellable, &error_local)) {
			g_warning ("Failed to block on download scheduler: %s",
				   error_local->message);
			g_clear_error (&error_local);
		}
	}

	retval = _download_only (self, list_tmp, list, cancellable, error);

	if (!gs_metered_remove_from_download_scheduler (schedule_entry_handle, NULL, &error_local))
		g_warning ("Failed to remove schedule entry: %s", error_local->message);

	return retval;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));
	g_autoptr(PkResults) results = NULL;

	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	gs_packagekit_helper_set_progress_app (helper, app_dl);

	g_mutex_lock (&self->task_mutex_refresh);
	/* cache age of 1 is user-initiated */
	pk_client_set_background (PK_CLIENT (self->task_refresh), cache_age > 1);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (self->task_refresh), GS_PLUGIN_ACTION_REFRESH, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	pk_client_set_cache_age (PK_CLIENT (self->task_refresh), cache_age);
	/* refresh the metadata */
	results = pk_client_refresh_cache (PK_CLIENT (self->task_refresh),
	                                   FALSE /* force */,
	                                   cancellable,
	                                   gs_packagekit_helper_cb, helper,
	                                   error);
	g_mutex_unlock (&self->task_mutex_refresh);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		return FALSE;
	}

	return TRUE;
}

static void
gs_plugin_packagekit_class_init (GsPluginPackagekitClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_packagekit_dispose;
	object_class->finalize = gs_plugin_packagekit_finalize;

	plugin_class->setup_async = gs_plugin_packagekit_setup_async;
	plugin_class->setup_finish = gs_plugin_packagekit_setup_finish;
	plugin_class->shutdown_async = gs_plugin_packagekit_shutdown_async;
	plugin_class->shutdown_finish = gs_plugin_packagekit_shutdown_finish;
	plugin_class->refine_async = gs_plugin_packagekit_refine_async;
	plugin_class->refine_finish = gs_plugin_packagekit_refine_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_PACKAGEKIT;
}
