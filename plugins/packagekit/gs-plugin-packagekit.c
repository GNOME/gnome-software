/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2017 Canonical Ltd
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2024 GNOME Foundation, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>

#include <glib/gi18n-lib.h>
#include <gdesktop-enums.h>
#include <gnome-software.h>
#include <packagekit-glib2/packagekit.h>
#include <string.h>

#include "packagekit-common.h"
#include "gs-markdown.h"
#include "gs-packagekit-helper.h"
#include "gs-packagekit-task.h"
#include "gs-plugin-private.h"

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
 * Also supports converting repo filenames to package-ids.
 *
 * Also supports marking previously downloaded packages as zero size, and allows
 * scheduling an offline update. An offline update is when packages are
 * downloaded in advance, but are then deployed on reboot, when the system is in
 * a minimally started-up state. This reduces the risk of things crashing as
 * files are updated.
 *
 * See https://github.com/PackageKit/PackageKit/blob/main/docs/offline-updates.txt
 * and https://www.freedesktop.org/software/systemd/man/latest/systemd.offline-updates.html
 * for details of how offline updates work.
 *
 * As PackageKit provides a D-Bus API, this plugin is a wrapper around that and
 * runs almost entirely asynchronously in the main thread. A few PackageKit
 * operations don’t yet have an asynchronous API provided in libpackagekit-glib2
 * so have to be run in a #GTask thread pool thread. These operations happen
 * infrequently, so it’s not worth keeping a #GsWorkerThread around for them.
 * The few fields which are used for them in `GsPluginPackagekit` are locked
 * individually; most fields in `GsPluginPackagekit` do not require a lock to
 * access.
 *
 * Requires:    | [source-id], [repos::repo-filename]
 * Refines:     | [source-id], [source], [update-details], [management-plugin]
 */

#define GS_PLUGIN_PACKAGEKIT_HISTORY_TIMEOUT	5000 /* ms */

/* Timeout to trigger auto-prepare update after the prepared update had been invalidated */
#define PREPARE_UPDATE_TIMEOUT_SECS 30

struct _GsPluginPackagekit {
	GsPlugin		 parent;

	PkControl		*control_refine;

	PkControl		*control_proxy;
	GSettings		*settings_proxy;
	GSettings		*settings_http;
	GSettings		*settings_https;
	GSettings		*settings_ftp;
	GSettings		*settings_socks;

	GFileMonitor		*monitor;
	GFileMonitor		*monitor_trigger;
	GPermission		*permission;
	gboolean		 is_triggered;
	GHashTable		*prepared_updates;  /* (element-type utf8); set of package IDs for updates which are already prepared */
	guint			 prepare_update_timeout_id;

	GCancellable		*proxy_settings_cancellable;  /* (nullable) (owned) */

	GHashTable		*cached_sources; /* (nullable) (owned) (element-type utf8 GsApp); sources by id, each value is weak reffed */
};

G_DEFINE_TYPE (GsPluginPackagekit, gs_plugin_packagekit, GS_TYPE_PLUGIN)

static void gs_plugin_packagekit_installed_changed_cb (PkControl *control, GsPlugin *plugin);
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
static void gs_plugin_packagekit_enable_repository_async (GsPlugin                      *plugin,
                                                          GsApp                         *repository,
                                                          GsPluginManageRepositoryFlags  flags,
                                                          GsPluginEventCallback          event_callback,
                                                          void                          *event_user_data,
                                                          GCancellable                  *cancellable,
                                                          GAsyncReadyCallback            callback,
                                                          gpointer                       user_data);
static gboolean gs_plugin_packagekit_enable_repository_finish (GsPlugin      *plugin,
                                                               GAsyncResult  *result,
                                                               GError       **error);
static void gs_plugin_packagekit_proxy_changed_cb (GSettings   *settings,
                                                   const gchar *key,
                                                   gpointer     user_data);
static void reload_proxy_settings_async (GsPluginPackagekit  *self,
                                         gboolean             force_set,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data);
static gboolean reload_proxy_settings_finish (GsPluginPackagekit  *self,
                                              GAsyncResult        *result,
                                              GError             **error);
static void gs_plugin_packagekit_refine_async (GsPlugin                   *plugin,
                                               GsAppList                  *list,
                                               GsPluginRefineFlags         job_flags,
                                               GsPluginRefineRequireFlags  require_flags,
                                               GsPluginEventCallback       event_callback,
                                               void                       *event_user_data,
                                               GCancellable               *cancellable,
                                               GAsyncReadyCallback         callback,
                                               gpointer                    user_data);
static gboolean gs_plugin_packagekit_refine_finish (GsPlugin      *plugin,
                                                    GAsyncResult  *result,
                                                    GError       **error);

static void
cached_sources_weak_ref_cb (gpointer user_data,
			    GObject *object)
{
	GsPluginPackagekit *self = user_data;
	GHashTableIter iter;
	gpointer key, value;

	g_assert (self->cached_sources != NULL);

	g_hash_table_iter_init (&iter, self->cached_sources);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GObject *repo_object = value;
		if (repo_object == object) {
			g_hash_table_iter_remove (&iter);
			if (!g_hash_table_size (self->cached_sources))
				g_clear_pointer (&self->cached_sources, g_hash_table_unref);
			break;
		}
	}
}

static void
gs_plugin_packagekit_init (GsPluginPackagekit *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	/* refine */
	self->control_refine = pk_control_new ();
	g_signal_connect (self->control_refine, "updates-changed",
			  G_CALLBACK (gs_plugin_packagekit_updates_changed_cb), plugin);
	g_signal_connect (self->control_refine, "repo-list-changed",
			  G_CALLBACK (gs_plugin_packagekit_repo_list_changed_cb), plugin);
	if (g_signal_lookup ("installed-changed", PK_TYPE_CONTROL) != 0) {
		g_debug ("Connecting to PkControl::installed-changed signal");
		g_signal_connect_object (self->control_refine, "installed-changed",
					 G_CALLBACK (gs_plugin_packagekit_installed_changed_cb), plugin, 0);
	}

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

	/* offline updates */
	self->prepared_updates = g_hash_table_new_full (g_str_hash, g_str_equal,
							g_free, NULL);

	/* need pkgname and ID */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");

	/* we can return better results than dpkg directly */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "dpkg");

	/* need repos::repo-filename */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "repos");

	/* generic updates happen after PackageKit offline updates */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "generic-updates");
}

static void
gs_plugin_packagekit_dispose (GObject *object)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (object);

	if (self->prepare_update_timeout_id) {
		g_source_remove (self->prepare_update_timeout_id);
		self->prepare_update_timeout_id = 0;
	}

	g_cancellable_cancel (self->proxy_settings_cancellable);
	g_clear_object (&self->proxy_settings_cancellable);

	/* refine */
	g_clear_object (&self->control_refine);

	/* proxy */
	g_clear_object (&self->control_proxy);
	g_clear_object (&self->settings_proxy);
	g_clear_object (&self->settings_http);
	g_clear_object (&self->settings_https);
	g_clear_object (&self->settings_ftp);
	g_clear_object (&self->settings_socks);

	/* offline updates */
	g_clear_pointer (&self->prepared_updates, g_hash_table_unref);
	g_clear_object (&self->monitor);
	g_clear_object (&self->monitor_trigger);

	if (self->cached_sources != NULL) {
		GHashTableIter iter;
		gpointer value;

		g_hash_table_iter_init (&iter, self->cached_sources);
		while (g_hash_table_iter_next (&iter, NULL, &value)) {
			GObject *app_repo = value;
			g_object_weak_unref (app_repo, cached_sources_weak_ref_cb, self);
		}

		g_clear_pointer (&self->cached_sources, g_hash_table_unref);
	}

	G_OBJECT_CLASS (gs_plugin_packagekit_parent_class)->dispose (object);
}

static gboolean
gs_plugin_packagekit_convert_error (GError **error,
				    PkErrorEnum error_enum,
				    const gchar *details,
				    const gchar *prefix)
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
	if (prefix != NULL)
		g_prefix_error_literal (error, prefix);
	return FALSE;
}

typedef gboolean (*GsAppFilterFunc) (GsApp *app);

static gboolean
package_is_installed (const gchar *package_id)
{
	g_auto(GStrv) split = NULL;
	const gchar *data;

	split = pk_package_id_split (package_id);
	if (split == NULL) {
		return FALSE;
	}

	data = split[PK_PACKAGE_ID_DATA];
	if (g_str_has_prefix (data, "installed") ||
            g_str_has_prefix (data, "manual:") ||
            g_str_has_prefix (data, "auto:")) {
		return TRUE;
	}

	return FALSE;
}

/* The elements in the returned #GPtrArray reference memory from within the
 * @apps list, so the array is only valid as long as @apps is not modified or
 * freed. The array is not NULL-terminated.
 *
 * If @apps is %NULL, that’s considered equivalent to an empty list. */
static GPtrArray *
app_list_get_package_ids (GsAppList       *apps,
                          GsAppFilterFunc  app_filter,
                          gboolean         ignore_installed)
{
	g_autoptr(GPtrArray) list_package_ids = g_ptr_array_new_with_free_func (NULL);

	for (guint i = 0; apps != NULL && i < gs_app_list_length (apps); i++) {
		GsApp *app = gs_app_list_index (apps, i);
		GPtrArray *app_source_ids;

		if (app_filter != NULL && !app_filter (app))
			continue;

		app_source_ids = gs_app_get_source_ids (app);
		for (guint j = 0; j < app_source_ids->len; j++) {
			const gchar *package_id = g_ptr_array_index (app_source_ids, j);

			if (ignore_installed && package_is_installed (package_id))
				continue;

			g_ptr_array_add (list_package_ids, (gchar *) package_id);
		}
	}

	return g_steal_pointer (&list_package_ids);
}

static GsApp *
gs_plugin_packagekit_dup_app_origin_repo (GsPluginPackagekit  *self,
                                          GsApp               *app,
                                          GError             **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GsApp) repo_app = NULL;
	const gchar *repo_id;

	repo_id = gs_app_get_origin (app);
	if (repo_id == NULL) {
		g_set_error_literal (error,
		                     GS_PLUGIN_ERROR,
		                     GS_PLUGIN_ERROR_NOT_SUPPORTED,
		                     "origin not set");
		return NULL;
	}

	repo_app = g_hash_table_lookup (self->cached_sources, repo_id);
	if (repo_app != NULL) {
		g_object_ref (repo_app);
	} else {
		repo_app = gs_app_new (repo_id);
		gs_app_set_management_plugin (repo_app, plugin);
		gs_app_set_kind (repo_app, AS_COMPONENT_KIND_REPOSITORY);
		gs_app_set_bundle_kind (repo_app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_set_scope (repo_app, AS_COMPONENT_SCOPE_SYSTEM);
		gs_app_add_quirk (repo_app, GS_APP_QUIRK_NOT_LAUNCHABLE);
		gs_plugin_packagekit_set_packaging_format (plugin, repo_app);
	}

	return g_steal_pointer (&repo_app);
}

typedef struct {
	/* Input data. */
	GsAppList *apps;  /* (owned) (not nullable) */
	GsPluginInstallAppsFlags flags;
	GsPluginProgressCallback progress_callback;
	gpointer progress_user_data;
	GsPluginEventCallback event_callback;
	void *event_user_data;

	/* In-progress data. */
	guint n_pending_enable_repo_ops;
	guint n_pending_install_ops;
	GError *saved_enable_repo_error;  /* (owned) (nullable) */
	GError *saved_install_error;  /* (owned) (nullable) */
	GsAppList *remote_apps_to_install;  /* (owned) (nullable) */
	GsAppList *local_apps_to_install;  /* (owned) (nullable) */
	GsPackagekitHelper *progress_data;  /* (owned) (nullable) */
} InstallAppsData;

static void
install_apps_data_free (InstallAppsData *data)
{
	g_clear_object (&data->apps);
	g_clear_object (&data->remote_apps_to_install);
	g_clear_object (&data->local_apps_to_install);
	g_clear_object (&data->progress_data);

	/* Error should have been propagated by now, and all pending ops completed. */
	g_assert (data->saved_enable_repo_error == NULL);
	g_assert (data->saved_install_error == NULL);
	g_assert (data->n_pending_enable_repo_ops == 0);
	g_assert (data->n_pending_install_ops == 0);

	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (InstallAppsData, install_apps_data_free)

static void finish_install_apps_enable_repo_op (GTask  *task,
                                                GError *error);
static void install_apps_enable_repo_cb (GObject      *source_object,
                                         GAsyncResult *result,
                                         gpointer      user_data);
static void install_apps_remote_cb (GObject      *source_object,
                                    GAsyncResult *result,
                                    gpointer      user_data);
static void install_apps_local_cb (GObject      *source_object,
                                   GAsyncResult *result,
                                   gpointer      user_data);
static void finish_install_apps_install_op (GTask  *task,
                                            GError *error);

static void
gs_plugin_packagekit_install_apps_async (GsPlugin                           *plugin,
                                         GsAppList                          *apps,
                                         GsPluginInstallAppsFlags            flags,
                                         GsPluginProgressCallback            progress_callback,
                                         gpointer                            progress_user_data,
                                         GsPluginEventCallback               event_callback,
                                         void                               *event_user_data,
                                         GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                         gpointer                            app_needs_user_action_data,
                                         GCancellable                       *cancellable,
                                         GAsyncReadyCallback                 callback,
                                         gpointer                            user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GTask) task = NULL;
	InstallAppsData *data;
	g_autoptr(InstallAppsData) data_owned = NULL;
	gboolean interactive = (flags & GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE);
	g_autoptr(GHashTable) repos = NULL;
	GHashTableIter iter;
	gpointer value;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_install_apps_async);

	data = data_owned = g_new0 (InstallAppsData, 1);
	data->flags = flags;
	data->progress_callback = progress_callback;
	data->progress_user_data = progress_user_data;
	data->event_callback = event_callback;
	data->event_user_data = event_user_data;
	data->apps = g_object_ref (apps);
	g_task_set_task_data (task, g_steal_pointer (&data_owned), (GDestroyNotify) install_apps_data_free);

	/* Start a load of operations in parallel to install the apps, in the
	 * following structure:
	 *
	 *          gs_plugin_packagekit_install_apps_async
	 *                             |
	 *                    /--------+------------------------------------+--------------------------\
	 *                    v                                             v                          v
	 * gs_plugin_packagekit_enable_repository_async  gs_plugin_packagekit_enable_repository_async  …
	 *                    |                                             |                          |
	 *                    \--------+------------------------------------+--------------------------/
	 *                             |
	 *              /--------------+----------------\
	 *              |                               |
	 *              v                               v
	 * pk_task_install_packages_async  pk_task_install_files_async
	 *              |                               |
	 *              \--------------+----------------/
	 *                             |
	 *                             v
	 *                finish_install_apps_install_op
	 *
	 * When all installs are finished for all apps,
	 * finish_install_apps_install_op() will return success/error for the
	 * overall #GTask.
	 *
	 * FIXME: Tie @progress_callback to number of completed operations. */
	data->n_pending_enable_repo_ops = 1;

	/* Firstly, find all the apps which need their origin repo to be enabled
	 * first, deduplicate the repos and enable them. */
	repos = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);

	for (guint i = 0; i < gs_app_list_length (apps); i++) {
		GsApp *app = gs_app_list_index (apps, i);

		/* only process this app if was created by this plugin */
		if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
			continue;

		/* enable repo, handled by dedicated function */
		g_assert (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY);

		if (gs_app_get_state (app) == GS_APP_STATE_UNAVAILABLE) {
			g_autoptr(GsApp) repo_app = NULL;
			const gchar *repo_app_id;

			repo_app = gs_plugin_packagekit_dup_app_origin_repo (self, app, &local_error);
			if (repo_app == NULL) {
				finish_install_apps_enable_repo_op (task, g_steal_pointer (&local_error));
				return;
			}

			repo_app_id = gs_app_get_id (repo_app);
			g_hash_table_replace (repos, (gpointer) repo_app_id, g_steal_pointer (&repo_app));
		}
	}

	/* Enable the repos. */
	g_hash_table_iter_init (&iter, repos);

	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GsApp *repo_app = value;

		data->n_pending_enable_repo_ops++;
		gs_plugin_packagekit_enable_repository_async (plugin, repo_app,
							      interactive ? GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE : GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_NONE,
							      data->event_callback, data->event_user_data,
							      cancellable, install_apps_enable_repo_cb, g_object_ref (task));
	}

	finish_install_apps_enable_repo_op (task, NULL);
}

static void
install_apps_enable_repo_cb (GObject      *source_object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;

	gs_plugin_packagekit_enable_repository_finish (GS_PLUGIN (self), result, &local_error);
	finish_install_apps_enable_repo_op (task, g_steal_pointer (&local_error));
}

/* @error is (transfer full) if non-%NULL */
static void
finish_install_apps_enable_repo_op (GTask  *task,
                                    GError *error)
{
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	InstallAppsData *data = g_task_get_task_data (task);
	gboolean interactive = (data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);
	g_autoptr(GPtrArray) overall_remote_package_ids = NULL;
	g_autoptr(GPtrArray) overall_local_package_ids = NULL;
	g_autoptr(PkTask) task_install = NULL;
	g_autoptr(GError) local_error = NULL;

	if (error_owned != NULL && data->saved_enable_repo_error == NULL)
		data->saved_enable_repo_error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while enabling repos to install apps: %s", error_owned->message);

	g_assert (data->n_pending_enable_repo_ops > 0);
	data->n_pending_enable_repo_ops--;

	if (data->n_pending_enable_repo_ops > 0)
		return;

	/* If enabling any repos failed, abandon the entire operation.
	 * Otherwise, carry on to installing apps. */
	if (data->saved_enable_repo_error != NULL) {
		g_autoptr(GsPluginEvent) event = NULL;

		event = gs_plugin_event_new ("error", data->saved_enable_repo_error,
					     NULL);
		if (interactive)
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
		if (data->event_callback != NULL)
			data->event_callback (GS_PLUGIN (self), event, data->event_user_data);

		g_task_return_boolean (task, TRUE);
		return;
	}

	overall_remote_package_ids = g_ptr_array_new_with_free_func (NULL);
	overall_local_package_ids = g_ptr_array_new_with_free_func (g_free);
	data->remote_apps_to_install = gs_app_list_new ();
	data->local_apps_to_install = gs_app_list_new ();

	/* Mark all the unavailable apps as available, now that their repos
	 * are enabled. */
	for (guint i = 0; i < gs_app_list_length (data->apps); i++) {
		GsApp *app = gs_app_list_index (data->apps, i);

		/* only process this app if was created by this plugin */
		if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
			continue;

		if (gs_app_get_state (app) == GS_APP_STATE_UNAVAILABLE)
			gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	}

	/* Next, group the apps into those which need internet to install,
	 * and those which can be installed locally, and grab their package
	 * IDs ready to pass to PackageKit. */
	for (guint i = 0; i < gs_app_list_length (data->apps); i++) {
		GsApp *app = gs_app_list_index (data->apps, i);

		/* only process this app if was created by this plugin */
		if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
			continue;

		/* queue for install if installation needs the network */
		if (!gs_plugin_get_network_available (GS_PLUGIN (self)) &&
		    gs_app_get_state (app) != GS_APP_STATE_AVAILABLE_LOCAL) {
			gs_app_set_state (app, GS_APP_STATE_QUEUED_FOR_INSTALL);
			continue;
		}

		switch (gs_app_get_state (app)) {
		case GS_APP_STATE_AVAILABLE:
		case GS_APP_STATE_UPDATABLE:
		case GS_APP_STATE_QUEUED_FOR_INSTALL: {
			GPtrArray *source_ids;
			g_autoptr(GsAppList) addons = NULL;
			g_autoptr(GPtrArray) array_package_ids = NULL;

			source_ids = gs_app_get_source_ids (app);
			if (source_ids->len == 0) {
				g_autoptr(GsPluginEvent) event = NULL;

				g_set_error_literal (&local_error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_NOT_SUPPORTED,
						     "installing not available");

				event = gs_plugin_event_new ("error", local_error,
							     "app", app,
							     NULL);
				if (interactive)
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
				if (data->event_callback != NULL)
					data->event_callback (GS_PLUGIN (self), event, data->event_user_data);
				g_clear_error (&local_error);

				continue;
			}

			addons = gs_app_dup_addons (app);
			array_package_ids = app_list_get_package_ids (addons,
								      gs_app_get_to_be_installed,
								      TRUE);

			for (guint j = 0; j < source_ids->len; j++) {
				const gchar *package_id = g_ptr_array_index (source_ids, j);
				if (package_is_installed (package_id))
					continue;
				g_ptr_array_add (array_package_ids, (gpointer) package_id);
			}

			if (array_package_ids->len == 0) {
				g_autoptr(GsPluginEvent) event = NULL;

				g_set_error_literal (&local_error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_NOT_SUPPORTED,
						     "no packages to install");

				event = gs_plugin_event_new ("error", local_error,
							     NULL);
				if (interactive)
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
				if (data->event_callback != NULL)
					data->event_callback (GS_PLUGIN (self), event, data->event_user_data);
				g_clear_error (&local_error);

				continue;
			}

			/* Add to the big array. */
			g_ptr_array_extend_and_steal (overall_remote_package_ids,
						      g_steal_pointer (&array_package_ids));

			for (guint j = 0; addons != NULL && j < gs_app_list_length (addons); j++) {
				GsApp *addon = gs_app_list_index (addons, j);
				if (gs_app_get_to_be_installed (addon))
					gs_app_list_add (data->remote_apps_to_install, addon);
			}
			gs_app_list_add (data->remote_apps_to_install, app);

			break;
		}
		case GS_APP_STATE_AVAILABLE_LOCAL: {
			g_autofree gchar *local_filename = NULL;
			g_auto(GStrv) package_ids = NULL;

			if (gs_app_get_local_file (app) == NULL) {
				g_autoptr(GsPluginEvent) event = NULL;

				g_set_error_literal (&local_error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_NOT_SUPPORTED,
						     "local package, but no filename");

				event = gs_plugin_event_new ("error", local_error,
							     "app", app,
							     NULL);
				if (interactive)
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
				if (data->event_callback != NULL)
					data->event_callback (GS_PLUGIN (self), event, data->event_user_data);
				g_clear_error (&local_error);

				continue;
			}
			local_filename = g_file_get_path (gs_app_get_local_file (app));
			package_ids = g_strsplit (local_filename, "\t", -1);

			/* Add to the big array. */
			for (gsize j = 0; package_ids[j] != NULL; j++)
				g_ptr_array_add (overall_local_package_ids, g_steal_pointer (&package_ids[j]));
			gs_app_list_add (data->local_apps_to_install, app);

			break;
		}
		default: {
			g_autoptr(GsPluginEvent) event = NULL;

			g_set_error (&local_error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "do not know how to install app in state %s",
				     gs_app_state_to_string (gs_app_get_state (app)));

			event = gs_plugin_event_new ("error", local_error,
						     "app", app,
						     NULL);
			if (interactive)
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			if (data->event_callback != NULL)
				data->event_callback (GS_PLUGIN (self), event, data->event_user_data);
			g_clear_error (&local_error);

			continue;
		}
		}
	}

	/* Set up a #PkTask to handle the D-Bus calls to packagekitd. */
	data->progress_data = gs_packagekit_helper_new (GS_PLUGIN (self));
	task_install = gs_packagekit_task_new (GS_PLUGIN (self));
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_install), GS_PACKAGEKIT_TASK_QUESTION_TYPE_INSTALL, interactive);

	data->n_pending_install_ops = 1;  /* to track setup */

	/* Install the remote packages. */
	if (overall_remote_package_ids->len > 0 &&
	    !(data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_NO_DOWNLOAD) &&
	    !(data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_NO_APPLY)) {
		/* NULL-terminate the array. */
		g_ptr_array_add (overall_remote_package_ids, NULL);

		/* Update the app’s and its addons‘ states. */
		for (guint i = 0; i < gs_app_list_length (data->remote_apps_to_install); i++) {
			GsApp *app = gs_app_list_index (data->remote_apps_to_install, i);
			gs_app_set_state (app, GS_APP_STATE_INSTALLING);
			gs_packagekit_helper_add_app (data->progress_data, app);
		}

		data->n_pending_install_ops++;
		pk_task_install_packages_async (task_install,
		                                (gchar **) overall_remote_package_ids->pdata,
		                                cancellable,
		                                gs_packagekit_helper_cb, data->progress_data,
		                                install_apps_remote_cb,
		                                g_object_ref (task));
	}

	/* And, in parallel, install the local packages. */
	if (overall_local_package_ids->len > 0 &&
	    !(data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_NO_APPLY)) {
		/* NULL-terminate the array. */
		g_ptr_array_add (overall_local_package_ids, NULL);

		/* Update the apps’ states. */
		for (guint i = 0; i < gs_app_list_length (data->local_apps_to_install); i++) {
			GsApp *app = gs_app_list_index (data->local_apps_to_install, i);
			gs_app_set_state (app, GS_APP_STATE_INSTALLING);
			gs_packagekit_helper_add_app (data->progress_data, app);
		}

		data->n_pending_install_ops++;
		pk_task_install_files_async (task_install,
		                             (gchar **) overall_local_package_ids->pdata,
		                             cancellable,
		                             gs_packagekit_helper_cb, data->progress_data,
		                             install_apps_local_cb,
		                             g_object_ref (task));
	}

	finish_install_apps_install_op (task, NULL);
}

static void
install_apps_remote_cb (GObject      *source_object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
	PkTask *task_install = PK_TASK (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	InstallAppsData *data = g_task_get_task_data (task);
	gboolean interactive = (data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_task_generic_finish (task_install, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
		g_autoptr(GsPluginEvent) event = NULL;

		for (guint i = 0; i < gs_app_list_length (data->remote_apps_to_install); i++) {
			GsApp *app = gs_app_list_index (data->remote_apps_to_install, i);
			gs_app_set_state_recover (app);
		}

		gs_plugin_packagekit_error_convert (&local_error, cancellable);

		event = gs_plugin_event_new ("error", local_error,
					     NULL);
		if (interactive)
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
		if (data->event_callback != NULL)
			data->event_callback (GS_PLUGIN (self), event, data->event_user_data);
		g_clear_error (&local_error);

		finish_install_apps_install_op (task, g_steal_pointer (&local_error));
		return;
	}

	for (guint i = 0; i < gs_app_list_length (data->remote_apps_to_install); i++) {
		GsApp *app = gs_app_list_index (data->remote_apps_to_install, i);

		gs_app_set_state (app, GS_APP_STATE_INSTALLED);

		/* no longer valid */
		gs_app_clear_source_ids (app);
	}

	finish_install_apps_install_op (task, NULL);
}

static void
install_apps_local_cb (GObject      *source_object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
	PkTask *task_install = PK_TASK (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	InstallAppsData *data = g_task_get_task_data (task);
	gboolean interactive = (data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_task_generic_finish (task_install, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
		g_autoptr(GsPluginEvent) event = NULL;

		for (guint i = 0; i < gs_app_list_length (data->local_apps_to_install); i++) {
			GsApp *app = gs_app_list_index (data->local_apps_to_install, i);
			gs_app_set_state_recover (app);
		}

		gs_plugin_packagekit_error_convert (&local_error, cancellable);

		event = gs_plugin_event_new ("error", local_error,
					     NULL);
		if (interactive)
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
		if (data->event_callback != NULL)
			data->event_callback (GS_PLUGIN (self), event, data->event_user_data);
		g_clear_error (&local_error);

		finish_install_apps_install_op (task, g_steal_pointer (&local_error));
		return;
	}

	for (guint i = 0; i < gs_app_list_length (data->local_apps_to_install); i++) {
		GsApp *app = gs_app_list_index (data->local_apps_to_install, i);

		/* state is known */
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);

		/* get the new icon from the package */
		gs_app_set_local_file (app, NULL);
		gs_app_remove_all_icons (app);

		/* no longer valid */
		gs_app_clear_source_ids (app);
	}

	finish_install_apps_install_op (task, NULL);
}

/* @error is (transfer full) if non-%NULL */
static void
finish_install_apps_install_op (GTask  *task,
                                GError *error)
{
	InstallAppsData *data = g_task_get_task_data (task);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);

	if (error_owned != NULL && data->saved_install_error == NULL)
		data->saved_install_error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while installing apps: %s", error_owned->message);

	g_assert (data->n_pending_install_ops > 0);
	data->n_pending_install_ops--;

	if (data->n_pending_install_ops > 0)
		return;

	/* Get the results of the parallel ops. */
	if (data->saved_install_error != NULL)
		g_task_return_error (task, g_steal_pointer (&data->saved_install_error));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_packagekit_install_apps_finish (GsPlugin      *plugin,
                                          GAsyncResult  *result,
                                          GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

typedef struct {
	/* Input data. */
	GsAppList *apps;  /* (owned) (not nullable) */
	GsPluginUninstallAppsFlags flags;
	GsPluginProgressCallback progress_callback;
	gpointer progress_user_data;
	GsPluginEventCallback event_callback;
	void *event_user_data;

	/* In-progress data. */
	GsAppList *apps_to_uninstall;  /* (owned) (nullable) */
	GsPackagekitHelper *progress_data;  /* (owned) (nullable) */
} UninstallAppsData;

static void
uninstall_apps_data_free (UninstallAppsData *data)
{
	g_clear_object (&data->apps);
	g_clear_object (&data->apps_to_uninstall);
	g_clear_object (&data->progress_data);

	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (UninstallAppsData, uninstall_apps_data_free)

static void uninstall_apps_remove_cb (GObject      *source_object,
                                      GAsyncResult *result,
                                      gpointer      user_data);
static void uninstall_apps_refine_cb (GObject      *source_object,
                                      GAsyncResult *result,
                                      gpointer      user_data);

static void
gs_plugin_packagekit_uninstall_apps_async (GsPlugin                           *plugin,
                                           GsAppList                          *apps,
                                           GsPluginUninstallAppsFlags          flags,
                                           GsPluginProgressCallback            progress_callback,
                                           gpointer                            progress_user_data,
                                           GsPluginEventCallback               event_callback,
                                           void                               *event_user_data,
                                           GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                           gpointer                            app_needs_user_action_data,
                                           GCancellable                       *cancellable,
                                           GAsyncReadyCallback                 callback,
                                           gpointer                            user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GTask) task = NULL;
	UninstallAppsData *data;
	g_autoptr(UninstallAppsData) data_owned = NULL;
	gboolean interactive = (flags & GS_PLUGIN_UNINSTALL_APPS_FLAGS_INTERACTIVE);
	g_autoptr(GPtrArray) overall_package_ids = NULL;
	g_autoptr(PkTask) task_uninstall = NULL;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_uninstall_apps_async);

	data = data_owned = g_new0 (UninstallAppsData, 1);
	data->flags = flags;
	data->progress_callback = progress_callback;
	data->progress_user_data = progress_user_data;
	data->event_callback = event_callback;
	data->event_user_data = event_user_data;
	data->apps = g_object_ref (apps);
	g_task_set_task_data (task, g_steal_pointer (&data_owned), (GDestroyNotify) uninstall_apps_data_free);

	overall_package_ids = g_ptr_array_new_with_free_func (NULL);
	data->apps_to_uninstall = gs_app_list_new ();

	/* Grab the package IDs from the apps ready to pass to PackageKit. */
	for (guint i = 0; i < gs_app_list_length (data->apps); i++) {
		GsApp *app = gs_app_list_index (data->apps, i);
		GPtrArray *source_ids;
		g_autoptr(GPtrArray) array_package_ids = NULL;

		/* only process this app if was created by this plugin */
		if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
			continue;

		/* disable repo, handled by dedicated function */
		g_assert (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY);

		source_ids = gs_app_get_source_ids (app);
		if (source_ids->len == 0) {
			g_autoptr(GsPluginEvent) event = NULL;

			g_set_error_literal (&local_error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "uninstalling not available");

			event = gs_plugin_event_new ("error", local_error,
						     "app", app,
						     NULL);
			if (interactive)
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			if (event_callback != NULL)
				event_callback (GS_PLUGIN (self), event, event_user_data);
			g_clear_error (&local_error);

			continue;
		}

		array_package_ids = g_ptr_array_new_with_free_func (NULL);

		for (guint j = 0; j < source_ids->len; j++) {
			const gchar *package_id = g_ptr_array_index (source_ids, j);
			if (!package_is_installed (package_id))
				continue;
			g_ptr_array_add (array_package_ids, (gpointer) package_id);
		}

		if (array_package_ids->len == 0) {
			g_autoptr(GsPluginEvent) event = NULL;

			g_set_error_literal (&local_error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "no packages to uninstall");

			event = gs_plugin_event_new ("error", local_error,
						     "app", app,
						     NULL);
			if (interactive)
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			if (event_callback != NULL)
				event_callback (GS_PLUGIN (self), event, event_user_data);
			g_clear_error (&local_error);

			continue;
		}

		/* Add to the big array. */
		g_ptr_array_extend_and_steal (overall_package_ids,
					      g_steal_pointer (&array_package_ids));
		gs_app_list_add (data->apps_to_uninstall, app);
	}

	if (overall_package_ids->len == 0) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* NULL-terminate the array. */
	g_ptr_array_add (overall_package_ids, NULL);

	/* Set up a #PkTask to handle the D-Bus calls to packagekitd.
	 * FIXME: Tie @progress_callback to number of completed operations. */
	data->progress_data = gs_packagekit_helper_new (GS_PLUGIN (self));
	task_uninstall = gs_packagekit_task_new (GS_PLUGIN (self));
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_uninstall), GS_PACKAGEKIT_TASK_QUESTION_TYPE_NONE, interactive);

	/* Update the app’s and its addons‘ states. */
	for (guint i = 0; i < gs_app_list_length (data->apps_to_uninstall); i++) {
		GsApp *app = gs_app_list_index (data->apps_to_uninstall, i);
		gs_app_set_state (app, GS_APP_STATE_REMOVING);
		gs_packagekit_helper_add_app (data->progress_data, app);
	}

	/* Uninstall the packages. */
	pk_task_remove_packages_async (task_uninstall,
	                               (gchar **) overall_package_ids->pdata,
	                               TRUE  /* allow_deps */,
	                               GS_PACKAGEKIT_AUTOREMOVE,
	                               cancellable,
	                               gs_packagekit_helper_cb, data->progress_data,
	                               uninstall_apps_remove_cb,
	                               g_steal_pointer (&task));
}

static void
uninstall_apps_remove_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
	PkTask *task_uninstall = PK_TASK (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	UninstallAppsData *data = g_task_get_task_data (task);
	gboolean interactive = (data->flags & GS_PLUGIN_UNINSTALL_APPS_FLAGS_INTERACTIVE);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_task_generic_finish (task_uninstall, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
		g_autoptr(GsPluginEvent) event = NULL;

		for (guint i = 0; i < gs_app_list_length (data->apps_to_uninstall); i++) {
			GsApp *app = gs_app_list_index (data->apps_to_uninstall, i);
			gs_app_set_state_recover (app);
		}

		gs_plugin_packagekit_error_convert (&local_error, cancellable);

		event = gs_plugin_event_new ("error", local_error,
					     NULL);
		if (interactive)
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
		if (data->event_callback != NULL)
			data->event_callback (GS_PLUGIN (self), event, data->event_user_data);
		g_clear_error (&local_error);

		g_task_return_boolean (task, TRUE);
		return;
	}

	for (guint i = 0; i < gs_app_list_length (data->apps_to_uninstall); i++) {
		GsApp *app = gs_app_list_index (data->apps_to_uninstall, i);
		g_autoptr(GsAppList) addons = NULL;

		/* Make sure addons' state is updated as well */
		addons = gs_app_dup_addons (app);
		for (guint j = 0; addons != NULL && j < gs_app_list_length (addons); j++) {
			GsApp *addon = gs_app_list_index (addons, j);
			if (gs_app_get_state (addon) == GS_APP_STATE_INSTALLED) {
				gs_app_set_state (addon, GS_APP_STATE_UNKNOWN);
				gs_app_clear_source_ids (addon);
			}
		}

		/* state is not known: we don't know if we can re-install this app */
		gs_app_set_state (app, GS_APP_STATE_UNKNOWN);

		/* no longer valid */
		gs_app_clear_source_ids (app);
	}

	/* Refine the apps so their state is up to date again. */
	gs_plugin_packagekit_refine_async (GS_PLUGIN (self),
					   data->apps_to_uninstall,
					   GS_PLUGIN_REFINE_FLAGS_NONE,
					   GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN |
					   GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION,
					   data->event_callback,
					   data->event_user_data,
					   cancellable,
					   uninstall_apps_refine_cb,
					   g_steal_pointer (&task));
}

static void
uninstall_apps_refine_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) local_error = NULL;

	if (!gs_plugin_packagekit_refine_finish (GS_PLUGIN (self), result, &local_error)) {
		gs_plugin_packagekit_error_convert (&local_error, cancellable);
		g_debug ("Error refining apps after uninstall: %s", local_error->message);
		g_clear_error (&local_error);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_packagekit_uninstall_apps_finish (GsPlugin      *plugin,
                                            GAsyncResult  *result,
                                            GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_packagekit_set_update_app_state (GsApp *app,
					   PkPackage *package)
{
#if PK_CHECK_VERSION(1, 3, 0)
	if (pk_package_get_info (package) == PK_INFO_ENUM_REMOVE ||
	    pk_package_get_info (package) == PK_INFO_ENUM_REMOVING ||
	    pk_package_get_info (package) == PK_INFO_ENUM_OBSOLETE ||
	    pk_package_get_info (package) == PK_INFO_ENUM_OBSOLETING) {
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
	} else if (pk_package_get_info (package) == PK_INFO_ENUM_INSTALL ||
		   pk_package_get_info (package) == PK_INFO_ENUM_INSTALLING) {
		gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	} else {
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
	}
#else
	if (pk_package_get_info (package) == PK_INFO_ENUM_REMOVING ||
	    pk_package_get_info (package) == PK_INFO_ENUM_OBSOLETING) {
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
	} else if (pk_package_get_info (package) == PK_INFO_ENUM_INSTALLING) {
		gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	} else {
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
	}
#endif
}

static GsApp *
gs_plugin_packagekit_build_update_app (GsPlugin *plugin, PkPackage *package)
{
	GsApp *app = gs_plugin_cache_lookup (plugin, pk_package_get_id (package));
	if (app != NULL) {
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
			gs_plugin_packagekit_set_update_app_state (app, package);
		return app;
	}
	app = gs_app_new (NULL);
	gs_plugin_packagekit_set_packaging_format (plugin, app);
	gs_app_add_source (app, pk_package_get_name (package));
	gs_app_add_source_id (app, pk_package_get_id (package));
	gs_plugin_packagekit_set_package_name (app, package);
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
	gs_plugin_packagekit_set_update_app_state (app, package);
	gs_plugin_cache_add (plugin, pk_package_get_id (package), app);
	return app;
}

static gboolean
gs_plugin_package_list_updates_process_results (GsPlugin *plugin,
						PkResults *results,
						GsAppList *list,
						GCancellable *cancellable,
						GError **error)
{
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GsApp) first_app = NULL;
	gboolean all_downloaded = TRUE;

	if (!gs_plugin_packagekit_results_valid (results, cancellable, error))
		return FALSE;

	/* add results */
	array = pk_results_get_package_array (results);
	for (guint i = 0; i < array->len; i++) {
		PkPackage *package = g_ptr_array_index (array, i);
		g_autoptr(GsApp) app = NULL;
		guint64 size_download_bytes;

		if (pk_package_get_info (package) == PK_INFO_ENUM_BLOCKED) {
			g_debug ("Skipping blocked '%s' in list of packages to update", pk_package_get_id (package));
			continue;
		}

		app = gs_plugin_packagekit_build_update_app (plugin, package);
		all_downloaded = (all_downloaded &&
				  gs_app_get_size_download (app, &size_download_bytes) == GS_SIZE_TYPE_VALID &&
				  size_download_bytes == 0);
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
			gs_app_set_size_download (first_app, GS_SIZE_TYPE_VALID, 1);
	}

	return TRUE;
}

static void
gs_packagekit_list_updates_cb (GObject *source_object,
			       GAsyncResult *result,
			       gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (PK_CLIENT (source_object), result, &local_error);
	if (!gs_plugin_package_list_updates_process_results (GS_PLUGIN (g_task_get_source_object (task)), results, list,
							     g_task_get_cancellable (task), &local_error)) {
		g_debug ("Failed to get updates: %s", local_error->message);
	}

	/* only log about the errors, do not propagate them to the caller */
	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static gboolean
gs_packagekit_add_historical_updates_sync (GsPlugin *plugin,
					   GsAppList *list,
					   GCancellable *cancellable,
					   GError **error)
{
	guint64 mtime;
	guint i;
	g_autoptr(GPtrArray) package_array = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GSettings) settings = NULL;
	g_autoptr(PkResults) results = NULL;
	gboolean is_new_result;
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

		gs_plugin_packagekit_error_convert (&error_local, cancellable);

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
		gs_plugin_packagekit_error_convert (error, cancellable);
		return FALSE;
	}

	settings = g_settings_new ("org.gnome.software");
	/* Two seconds precision */
	is_new_result = mtime > g_settings_get_uint64 (settings, "packagekit-historical-updates-timestamp") + 2;
	if (is_new_result)
		g_settings_set_uint64 (settings, "packagekit-historical-updates-timestamp", mtime);

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

		/* Ignore previously shown errors */
		if (!is_new_result)
			return TRUE;

		return gs_plugin_packagekit_convert_error (error,
		                                           pk_error_get_code (error_code),
		                                           pk_error_get_details (error_code),
							   _("Failed to install updates: "));
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
		gs_plugin_packagekit_set_package_name (app, pkg);
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

static void
gs_packagekit_list_sources_cb (GObject *source_object,
			       GAsyncResult *result,
			       gpointer user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) local_error = NULL;
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (g_task_get_source_object (task));
	GsPlugin *plugin = GS_PLUGIN (self);

	results = pk_client_generic_finish (PK_CLIENT (source_object), result, &local_error);
	if (local_error != NULL || !gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (task), &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	if (self->cached_sources == NULL)
		self->cached_sources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	array = pk_results_get_repo_detail_array (results);
	for (guint i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		PkRepoDetail *rd = g_ptr_array_index (array, i);
		const gchar *id = pk_repo_detail_get_id (rd);
		app = g_hash_table_lookup (self->cached_sources, id);
		if (app == NULL) {
			app = gs_app_new (id);
			gs_app_set_management_plugin (app, plugin);
			gs_app_set_kind (app, AS_COMPONENT_KIND_REPOSITORY);
			gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
			gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
			gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
			gs_app_set_state (app, pk_repo_detail_get_enabled (rd) ?
					  GS_APP_STATE_INSTALLED : GS_APP_STATE_AVAILABLE);
			gs_app_set_name (app,
					 GS_APP_QUALITY_HIGHEST,
					 pk_repo_detail_get_description (rd));
			gs_app_set_summary (app,
					    GS_APP_QUALITY_HIGHEST,
					    pk_repo_detail_get_description (rd));
			gs_plugin_packagekit_set_packaging_format (plugin, app);
			gs_app_set_metadata (app, "GnomeSoftware::SortKey", "300");
			gs_app_set_origin_ui (app, _("Packages"));
			g_hash_table_insert (self->cached_sources, g_strdup (id), app);
			g_object_weak_ref (G_OBJECT (app), cached_sources_weak_ref_cb, self);
		} else {
			g_object_ref (app);
			/* The repo-related apps are those installed; due to re-using
			   cached app, make sure the list is populated from fresh data. */
			gs_app_list_remove_all (gs_app_get_related (app));
		}
		gs_app_list_add (list, app);
	}

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static void list_apps_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data);

static void
gs_plugin_packagekit_list_apps_async (GsPlugin              *plugin,
                                      GsAppQuery            *query,
                                      GsPluginListAppsFlags  flags,
                                      GsPluginEventCallback  event_callback,
                                      void                  *event_user_data,
                                      GCancellable          *cancellable,
                                      GAsyncReadyCallback    callback,
                                      gpointer               user_data)
{
	PkBitfield filter;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkTask) task_list_apps = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));
	const gchar *const *provides_files = NULL;
	const gchar *provides_tag = NULL;
	GsAppQueryProvidesType provides_type = GS_APP_QUERY_PROVIDES_UNKNOWN;
	GsAppQueryTristate is_for_update = GS_APP_QUERY_TRISTATE_UNSET;
	GsAppQueryTristate is_historical_update = GS_APP_QUERY_TRISTATE_UNSET;
	const AsComponentKind *component_kinds = NULL;
	gboolean interactive = (flags & GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_list_apps_async);
	g_task_set_task_data (task, g_object_ref (helper), g_object_unref);

	if (query != NULL) {
		provides_files = gs_app_query_get_provides_files (query);
		provides_type = gs_app_query_get_provides (query, &provides_tag);
		is_for_update = gs_app_query_get_is_for_update (query);
		is_historical_update = gs_app_query_get_is_historical_update (query);
		component_kinds = gs_app_query_get_component_kinds (query);
	}

	/* Currently only support a subset of query properties, and only one set at once. */
	if ((provides_files == NULL &&
	     provides_tag == NULL &&
	     is_for_update == GS_APP_QUERY_TRISTATE_UNSET &&
	     is_historical_update == GS_APP_QUERY_TRISTATE_UNSET &&
	     component_kinds == NULL) ||
	    is_for_update == GS_APP_QUERY_TRISTATE_FALSE ||
	    is_historical_update == GS_APP_QUERY_TRISTATE_FALSE ||
	    (component_kinds != NULL && !gs_component_kind_array_contains (component_kinds, AS_COMPONENT_KIND_REPOSITORY)) ||
	    gs_app_query_get_n_properties_set (query) != 1) {
		g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					 "Unsupported query");
		return;
	}

	gs_packagekit_helper_set_progress_app (helper, app_dl);

	task_list_apps = gs_packagekit_task_new (plugin);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_list_apps), GS_PACKAGEKIT_TASK_QUESTION_TYPE_NONE, interactive);

	if (provides_files != NULL) {
		filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
						 PK_FILTER_ENUM_ARCH,
						 -1);
		pk_client_search_files_async (PK_CLIENT (task_list_apps),
					      filter,
					      (gchar **) provides_files,
					      cancellable,
					      gs_packagekit_helper_cb, helper,
					      list_apps_cb, g_steal_pointer (&task));
	} else if (provides_type != GS_APP_QUERY_PROVIDES_UNKNOWN) {
		const gchar * const provides_tag_strv[2] = { provides_tag, NULL };

		filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
						 PK_FILTER_ENUM_ARCH,
						 -1);

		pk_client_what_provides_async (PK_CLIENT (task_list_apps),
					       filter,
					       (gchar **) provides_tag_strv,
					       cancellable,
					       gs_packagekit_helper_cb, helper,
					       list_apps_cb, g_steal_pointer (&task));
	} else if (is_for_update == GS_APP_QUERY_TRISTATE_TRUE) {
		gs_packagekit_helper_set_allow_emit_updates_changed (helper, FALSE);
		pk_client_get_updates_async (PK_CLIENT (task_list_apps),
					     pk_bitfield_value (PK_FILTER_ENUM_NONE),
					     cancellable,
					     gs_packagekit_helper_cb, helper,
					     gs_packagekit_list_updates_cb, g_steal_pointer (&task));
	} else if (is_historical_update == GS_APP_QUERY_TRISTATE_TRUE) {
		g_autoptr(GsAppList) list = gs_app_list_new ();
		g_autoptr(GError) local_error = NULL;
		if (gs_packagekit_add_historical_updates_sync (plugin, list, cancellable, &local_error))
			g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
		else
			g_task_return_error (task, g_steal_pointer (&local_error));
	} else if (gs_component_kind_array_contains (component_kinds, AS_COMPONENT_KIND_REPOSITORY)) {
		/* ask PK for the repo details */
		filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_SOURCE,
						 PK_FILTER_ENUM_NOT_DEVELOPMENT,
						 -1);
		pk_client_get_repo_list_async (PK_CLIENT (task_list_apps),
					       filter,
					       cancellable,
					       gs_packagekit_helper_cb, helper,
					       gs_packagekit_list_sources_cb, g_steal_pointer (&task));
	} else {
		g_assert_not_reached ();
	}
}

static void
list_apps_cb (GObject      *source_object,
              GAsyncResult *result,
              gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPlugin *plugin = g_task_get_source_object (task);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (task), &local_error) ||
	    !gs_plugin_packagekit_add_results (plugin, list, results, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
	} else {
		g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
	}
}

static GsAppList *
gs_plugin_packagekit_list_apps_finish (GsPlugin      *plugin,
                                       GAsyncResult  *result,
                                       GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static gboolean
plugin_packagekit_pick_rpm_desktop_file_cb (GsPlugin *plugin,
					    GsApp *app,
					    const gchar *filename,
					    GKeyFile *key_file,
					    gpointer user_data)
{
	return strstr (filename, "/snapd/") == NULL &&
	       strstr (filename, "/snap/") == NULL &&
	       strstr (filename, "/flatpak/") == NULL &&
	       g_key_file_has_group (key_file, "Desktop Entry") &&
	       !g_key_file_has_key (key_file, "Desktop Entry", "X-Flatpak", NULL) &&
	       !g_key_file_has_key (key_file, "Desktop Entry", "X-SnapInstanceName", NULL);
}

static void
gs_plugin_packagekit_launch_async (GsPlugin            *plugin,
				   GsApp               *app,
				   GsPluginLaunchFlags  flags,
				   GCancellable        *cancellable,
				   GAsyncReadyCallback  callback,
				   gpointer             user_data)
{
	gs_plugin_app_launch_filtered_async (plugin, app, flags,
					     plugin_packagekit_pick_rpm_desktop_file_cb, NULL,
					     cancellable,
					     callback, user_data);
}

static gboolean
gs_plugin_packagekit_launch_finish (GsPlugin      *plugin,
				    GAsyncResult  *result,
				    GError       **error)
{
	return gs_plugin_app_launch_filtered_finish (plugin, result, error);
}

static void
gs_plugin_packagekit_invoke_reload (GsPlugin *plugin)
{
	g_autoptr(GsAppList) list = gs_plugin_list_cached (plugin);
	guint sz = gs_app_list_length (list);
	for (guint i = 0; i < sz; i++) {
		GsApp *app = gs_app_list_index (list, i);
		/* to ensure the app states are refined */
		gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
	}
	gs_plugin_reload (plugin);
}

static void
gs_plugin_packagekit_installed_changed_cb (PkControl *control, GsPlugin *plugin)
{
	gs_plugin_packagekit_invoke_reload (plugin);
}

static void
gs_plugin_packagekit_updates_changed_cb (PkControl *control, GsPlugin *plugin)
{
	gs_plugin_updates_changed (plugin);
}

static void
gs_plugin_packagekit_repo_list_changed_cb (PkControl *control, GsPlugin *plugin)
{
	gs_plugin_packagekit_invoke_reload (plugin);
}

static void
gs_plugin_packagekit_adopt_app (GsPlugin *plugin,
				GsApp *app)
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
                                                         PkClient            *client_refine,
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
	pk_client_resolve_async (client_refine,
				 filter,
				 (gchar **) package_ids->pdata,
				 cancellable,
				 gs_packagekit_helper_cb, data_unowned->progress_data,
				 resolve_packages_with_filter_cb,
				 g_steal_pointer (&task));
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

	if (!gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
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
 * markdown_to_pango:
 *
 * Converts markdown text to pango markup which can be used in a
 * GtkLabel etc. This function assumes @text is valid markdown.
 *
 * Returns: pango markup, or %NULL on failure
 *
 */
static gchar *
markdown_to_pango (const gchar *text)
{
	g_autoptr(GsMarkdown) markdown = NULL;

	g_return_val_if_fail (text != NULL, NULL);

	/* try to parse */
	markdown = gs_markdown_new (GS_MARKDOWN_OUTPUT_PANGO);
	gs_markdown_set_smart_quoting (markdown, FALSE);
	gs_markdown_set_autocode (markdown, FALSE);
	gs_markdown_set_autolinkify (markdown, FALSE);

	return gs_markdown_parse (markdown, text);
}

static gboolean
gs_plugin_refine_app_needs_details (GsPluginRefineRequireFlags  flags,
                                    GsApp                      *app)
{
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_LICENSE) > 0 &&
	    gs_app_get_license (app) == NULL)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_URL) > 0 &&
	    gs_app_get_url (app, AS_URL_KIND_HOMEPAGE) == NULL)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE) > 0 &&
	    gs_app_get_size_installed (app, NULL) != GS_SIZE_TYPE_VALID)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE) > 0 &&
	    gs_app_get_size_download (app, NULL) != GS_SIZE_TYPE_VALID)
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_refine_requires_version (GsApp *app, GsPluginRefineRequireFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_version (app);
	if (tmp != NULL)
		return FALSE;
	return (flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_VERSION) > 0;
}

static gboolean
gs_plugin_refine_requires_update_details (GsApp *app, GsPluginRefineRequireFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_update_details_markup (app);
	if (tmp != NULL)
		return FALSE;
	return (flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_DETAILS) > 0;
}

static gboolean
gs_plugin_refine_requires_origin (GsApp *app, GsPluginRefineRequireFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_origin (app);
	if (tmp != NULL)
		return FALSE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN) > 0)
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_refine_requires_package_id (GsApp *app, GsPluginRefineRequireFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_default_source_id (app);
	if (tmp != NULL)
		return FALSE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_VERSION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_LICENSE) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_URL) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_DESCRIPTION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_VERSION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_DETAILS) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_SEVERITY) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_PROVENANCE) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION) > 0)
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

static gboolean
gs_plugin_systemd_update_cache (GsPluginPackagekit  *self,
				GCancellable	    *cancellable,
                                GError             **error)
{
	g_autoptr(GError) error_local = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GHashTable) new_prepared_updates = NULL;

	/* get new list of package-ids. This loads a local file, so should be
	 * just about fast enough to be sync. */
	new_prepared_updates = g_hash_table_new_full (g_str_hash, g_str_equal,
						      g_free, NULL);
	package_ids = pk_offline_get_prepared_ids (&error_local);
	if (package_ids == NULL) {
		if (g_error_matches (error_local,
				     PK_OFFLINE_ERROR,
				     PK_OFFLINE_ERROR_NO_DATA)) {
			return TRUE;
		}
		g_debug ("Failed to get prepared IDs: %s", error_local->message);
		/* Ignore errors returned here, they are not crucial, the plugin can work without it too */
		return TRUE;
	}

	/* Build the new table, stealing all the elements from @package_ids. */
	for (guint i = 0; package_ids[i] != NULL; i++) {
		g_hash_table_add (new_prepared_updates, g_steal_pointer (&package_ids[i]));
	}

	g_clear_pointer (&package_ids, g_free);

	/* Update the shared state. */
	g_clear_pointer (&self->prepared_updates, g_hash_table_unref);
	self->prepared_updates = g_steal_pointer (&new_prepared_updates);

	return TRUE;
}

typedef struct {
	/* Track pending operations. */
	guint n_pending_operations;
	gboolean completed;
	GError *error;  /* (nullable) (owned) */
	GPtrArray *progress_datas;  /* (element-type GsPackagekitHelper) (owned) (not nullable) */
	PkClient *client_refine;  /* (owned) */

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
	g_clear_object (&data->client_refine);
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
	GsApp *app; /* (owned) (nullable) for single file query */
	GHashTable *source_to_app; /* (owned) (nullable) for multifile query */
	guint n_expected_results;
} SearchFilesData;

static void
search_files_data_free (SearchFilesData *data)
{
	g_clear_object (&data->app);
	g_clear_object (&data->refine_task);
	g_clear_pointer (&data->source_to_app, g_hash_table_unref);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SearchFilesData, search_files_data_free)

static SearchFilesData *
search_files_data_new_operation (GTask *refine_task,
				 GsApp *app,
				 GHashTable *source_to_app,
				 guint n_expected_results)
{
	g_autoptr(SearchFilesData) data = g_new0 (SearchFilesData, 1);
	g_assert ((app != NULL && source_to_app == NULL) ||
		  (app == NULL && source_to_app != NULL));
	data->refine_task = refine_task_add_operation (refine_task);
	if (app) {
		data->app = g_object_ref (app);
	} else {
		data->source_to_app = g_hash_table_ref (source_to_app);
		data->n_expected_results = n_expected_results;
	}

	return g_steal_pointer (&data);
}

typedef struct {
	GTask *refine_task;  /* (owned) (not nullable) */
	GsAppList *sources;  /* (owned) (not nullable) */
} SourcesRelatedData;

static void
sources_related_data_free (SourcesRelatedData *data)
{
	g_clear_object (&data->sources);
	g_clear_object (&data->refine_task);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SourcesRelatedData, sources_related_data_free)

static SourcesRelatedData *
sources_related_data_new_operation (GTask       *refine_task,
                                    GsAppList   *sources)
{
	g_autoptr(SourcesRelatedData) data = g_new0 (SourcesRelatedData, 1);
	data->refine_task = refine_task_add_operation (refine_task);
	data->sources = g_object_ref (sources);

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
static void sources_related_got_installed_cb (GObject      *source_object,
					      GAsyncResult *result,
					      gpointer      user_data);
static void
gs_plugin_packagekit_refine_async (GsPlugin                   *plugin,
                                   GsAppList                  *list,
                                   GsPluginRefineFlags         job_flags,
                                   GsPluginRefineRequireFlags  require_flags,
                                   GsPluginEventCallback       event_callback,
                                   void                       *event_user_data,
                                   GCancellable               *cancellable,
                                   GAsyncReadyCallback         callback,
                                   gpointer                    user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GHashTable) resolve_list_apps = g_hash_table_new (NULL, NULL);
	g_autoptr(GsAppList) resolve_list = gs_app_list_new ();
	g_autoptr(GsAppList) update_details_list = gs_app_list_new ();
	g_autoptr(GsAppList) details_list = gs_app_list_new ();
	g_autoptr(GsAppList) history_list = gs_app_list_new ();
	g_autoptr(GsAppList) repos_list = gs_app_list_new ();
	g_autoptr(GTask) task = NULL;
	g_autoptr(RefineData) data = NULL;
	RefineData *data_unowned = NULL;
	g_autoptr(GError) local_error = NULL;
	guint n_considered = 0;

	/* Searches for multiple files are broken for PackageKit’s apt backend
	 * in 1.2.6 and earlier.
	 * See https://github.com/PackageKit/PackageKit/pull/649 */
#if PK_CHECK_VERSION(1, 2, 7)
	gboolean is_pk_apt_backend_broken = FALSE;
#else
	gboolean is_pk_apt_backend_broken = TRUE;
#endif

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_refine_async);
	data_unowned = data = g_new0 (RefineData, 1);
	data->full_list = g_object_ref (list);
	data->n_pending_operations = 1;  /* to prevent the task being completed before all operations have been started */
	data->progress_datas = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	data->client_refine = pk_client_new ();
	pk_client_set_interactive (data->client_refine, (job_flags & GS_PLUGIN_REFINE_FLAGS_INTERACTIVE) != 0);
	g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) refine_data_free);

	/* Process the @list and work out what information is needed for each
	 * app. */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GPtrArray *sources;
		const gchar *filename;

		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;

		if (!gs_app_has_management_plugin (app, NULL) &&
		    !gs_app_has_management_plugin (app, GS_PLUGIN (self)))
			continue;

		n_considered++;

		/* Repositories */
		filename = gs_app_get_metadata_item (app, "repos::repo-filename");

		if (gs_app_get_kind (app) == AS_COMPONENT_KIND_REPOSITORY &&
		    filename != NULL) {
			gs_app_list_add (repos_list, app);
		}

		/* Apps */
		sources = gs_app_get_sources (app);

		if (sources->len > 0 &&
		    gs_plugin_packagekit_refine_valid_package_name (g_ptr_array_index (sources, 0)) &&
		    (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN ||
		     gs_plugin_refine_requires_package_id (app, require_flags) ||
		     gs_plugin_refine_requires_origin (app, require_flags) ||
		     gs_plugin_refine_requires_version (app, require_flags))) {
			g_hash_table_add (resolve_list_apps, app);
			gs_app_list_add (resolve_list, app);
		}

		if ((gs_app_get_state (app) == GS_APP_STATE_UPDATABLE ||
		     gs_app_get_state (app) == GS_APP_STATE_UNKNOWN) &&
		    gs_app_get_default_source_id (app) != NULL &&
		    gs_plugin_refine_requires_update_details (app, require_flags)) {
			gs_app_list_add (update_details_list, app);
		}

		if (gs_app_get_default_source_id (app) != NULL &&
		    gs_plugin_refine_app_needs_details (require_flags, app)) {
			gs_app_list_add (details_list, app);
		}

		if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_HISTORY) != 0 &&
		    sources->len > 0 &&
		    gs_app_get_install_date (app) == 0) {
			gs_app_list_add (history_list, app);
		}
	}

	/* Add sources' related apps only when refining sources and nothing else */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_RELATED) != 0 &&
	    n_considered > 0 && gs_app_list_length (repos_list) == n_considered) {
		PkBitfield filter;
		g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);

		filter = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
						 PK_FILTER_ENUM_NEWEST,
						 PK_FILTER_ENUM_ARCH,
						 PK_FILTER_ENUM_NOT_COLLECTIONS,
						 -1);

		pk_client_get_packages_async (data_unowned->client_refine,
					      filter,
					      cancellable,
					      gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
					      sources_related_got_installed_cb,
					      sources_related_data_new_operation (task, repos_list));

	}

	/* re-read /var/lib/PackageKit/prepared-update so we know what packages
	 * to mark as already downloaded and prepared for offline updates */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE) &&
	    !gs_plugin_systemd_update_cache (self, cancellable, &local_error)) {
		refine_task_complete_operation_with_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* when we need the cannot-be-upgraded applications, we implement this
	 * by doing a UpgradeSystem(SIMULATE) which adds the removed packages
	 * to the related-apps list with a state of %GS_APP_STATE_UNAVAILABLE */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPGRADE_REMOVED) != 0) {
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
			cache_age_save = pk_client_get_cache_age (data_unowned->client_refine);
			pk_client_set_cache_age (data_unowned->client_refine, 60 * 60 * 24 * 7); /* once per week */
			pk_client_set_interactive (data_unowned->client_refine, (job_flags & GS_PLUGIN_REFINE_FLAGS_INTERACTIVE) != 0);
			pk_client_upgrade_system_async (data_unowned->client_refine,
							pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_SIMULATE, -1),
							gs_app_get_version (app),
							PK_UPGRADE_KIND_ENUM_COMPLETE,
							cancellable,
							gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
							upgrade_system_cb,
							refine_task_add_operation (task));
			pk_client_set_cache_age (data_unowned->client_refine, cache_age_save);

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
									 data_unowned->client_refine,
									 resolve_list,
									 filter,
									 cancellable,
									 resolve_all_packages_with_filter_cb,
									 refine_task_add_operation (task));
	}

	/* set the package-id for an installed desktop file */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION) != 0) {
		g_autoptr(GPtrArray) to_array = g_ptr_array_new_with_free_func (g_free);
		g_autoptr(GHashTable) source_to_app = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
		g_autoptr(GsPackagekitHelper) helper = NULL;
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			g_autofree gchar *fn = NULL;
			GsApp *app = gs_app_list_index (list, i);
			GPtrArray *sources;
			const gchar *tmp;

			if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
				continue;
			if (gs_app_get_default_source_id (app) != NULL)
				continue;
			if (!gs_app_has_management_plugin (app, NULL) &&
			    !gs_app_has_management_plugin (app, GS_PLUGIN (self)))
				continue;
			tmp = gs_app_get_id (app);
			if (tmp == NULL)
				continue;
			/* The information will be added within the resolve_list operation */
			if (g_hash_table_contains (resolve_list_apps, app))
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

			sources = gs_app_get_sources (app);
			if (!is_pk_apt_backend_broken && sources->len > 0) {
				/* do a batch query and match by the source (aka package name), if available */
				g_ptr_array_add (to_array, g_strdup (fn));
				if (helper == NULL)
					helper = gs_packagekit_helper_new (plugin);
				gs_packagekit_helper_add_app (helper, app);

				for (guint jj = 0; jj < sources->len; jj++) {
					const gchar *source = g_ptr_array_index (sources, jj);
					g_hash_table_insert (source_to_app, g_strdup (source), g_object_ref (app));
				}
			} else {
				/* otherwise do a query with a single file only */
				const gchar *single_array[] = { NULL, NULL };
				g_autoptr(GsPackagekitHelper) single_helper = NULL;
				single_array[0] = fn;
				single_helper = gs_packagekit_helper_new (plugin);
				gs_packagekit_helper_add_app (single_helper, app);
				pk_client_search_files_async (data_unowned->client_refine,
							      pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
							      (gchar **) single_array,
							      cancellable,
							      gs_packagekit_helper_cb, refine_task_add_progress_data (task, single_helper),
							      search_files_cb,
							      search_files_data_new_operation (task, app, NULL, 0));
			}
		}
		if (to_array->len > 0) {
			g_ptr_array_add (to_array, NULL);
			pk_client_search_files_async (data_unowned->client_refine,
						      pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
						      (gchar **) to_array->pdata,
						      cancellable,
						      gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
						      search_files_cb,
						      search_files_data_new_operation (task, NULL, source_to_app, to_array->len - 1));
		}
	}

	/* Refine repo package names */
	if (gs_app_list_length (repos_list) > 0) {
		g_autoptr(GPtrArray) to_array = g_ptr_array_new_full (gs_app_list_length (repos_list) + 1, g_free);
		g_autoptr(GHashTable) source_to_app = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
		g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
		for (guint i = 0; i < gs_app_list_length (repos_list); i++) {
			GsApp *app = gs_app_list_index (repos_list, i);
			GPtrArray *sources;
			const gchar *filename;

			/* The information will be added within the resolve_list operation */
			if (g_hash_table_contains (resolve_list_apps, app))
				continue;

			filename = gs_app_get_metadata_item (app, "repos::repo-filename");

			sources = gs_app_get_sources (app);
			if (!is_pk_apt_backend_broken && sources->len > 0) {
				/* do a batch query and match by the source (aka package name), if available */
				g_ptr_array_add (to_array, g_strdup (filename));
				gs_packagekit_helper_add_app (helper, app);

				for (guint jj = 0; jj < sources->len; jj++) {
					const gchar *source = g_ptr_array_index (sources, jj);
					g_hash_table_insert (source_to_app, g_strdup (source), g_object_ref (app));
				}
			} else {
				/* otherwise do a query with a single file only */
				const gchar *single_array[] = { NULL, NULL };
				g_autoptr(GsPackagekitHelper) single_helper = NULL;
				single_array[0] = filename;
				single_helper = gs_packagekit_helper_new (plugin);
				gs_packagekit_helper_add_app (single_helper, app);
				pk_client_search_files_async (data_unowned->client_refine,
							      pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
							      (gchar **) single_array,
							      cancellable,
							      gs_packagekit_helper_cb, refine_task_add_progress_data (task, single_helper),
							      search_files_cb,
							      search_files_data_new_operation (task, app, NULL, 0));
			}
		}

		if (to_array->len > 0) {
			g_ptr_array_add (to_array, NULL);

			pk_client_search_files_async (data_unowned->client_refine,
						      pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
						      (gchar **) to_array->pdata,
						      cancellable,
						      gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
						      search_files_cb,
						      search_files_data_new_operation (task, NULL, source_to_app, to_array->len - 1));
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
			package_ids[i] = gs_app_get_default_source_id (app);
			g_assert (package_ids[i] != NULL);  /* checked when update_details_list is built */
		}

		/* get any update details */
		pk_client_get_update_detail_async (data_unowned->client_refine,
						   (gchar **) package_ids,
						   cancellable,
						   gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
						   get_update_detail_cb,
						   refine_task_add_operation (task));
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
			/* NULL-terminate the array */
			g_ptr_array_add (package_ids, NULL);

			#if PK_CHECK_VERSION (1, 2, 7)
			if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE) != 0)
				pk_client_set_details_with_deps_size (data_unowned->client_refine, TRUE);
			#endif

			/* get any details */
			pk_client_get_details_async (data_unowned->client_refine,
						     (gchar **) package_ids->pdata,
						     cancellable,
						     gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
						     get_details_cb,
						     refine_task_add_operation (task));
		}
	}

	/* get the update severity */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_SEVERITY) != 0) {
		PkBitfield filter;
		g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);

		gs_packagekit_helper_set_allow_emit_updates_changed (helper, FALSE);

		/* get the list of updates */
		filter = pk_bitfield_value (PK_FILTER_ENUM_NONE);
		pk_client_get_updates_async (data_unowned->client_refine,
					     filter,
					     cancellable,
					     gs_packagekit_helper_cb, refine_task_add_progress_data (task, helper),
					     get_updates_cb,
					     refine_task_add_operation (task));
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
	if (!gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (refine_task), &local_error)) {
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

static void
sources_related_got_installed_cb (GObject      *source_object,
				  GAsyncResult *result,
				  gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(SourcesRelatedData) sources_related_data = g_steal_pointer (&user_data);
	GTask *refine_task = sources_related_data->refine_task;
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (g_task_get_source_object (refine_task));
	g_autoptr(GHashTable) sources_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GsAppList) installed = gs_app_list_new ();
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);
	if (!gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (refine_task), &local_error)) {
		g_prefix_error (&local_error, "failed to get sources related: ");
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	if (!gs_plugin_packagekit_add_results (GS_PLUGIN (self), installed, results, &local_error)) {
		g_prefix_error (&local_error, "failed to read results for sources related: ");
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	for (guint i = 0; i < gs_app_list_length (sources_related_data->sources); i++) {
		GsApp *app = gs_app_list_index (sources_related_data->sources, i);

		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD) ||
		    gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY ||
		    gs_app_get_id (app) == NULL)
			continue;

		if (!gs_app_has_management_plugin (app, NULL) &&
		    !gs_app_has_management_plugin (app, GS_PLUGIN (self)))
			continue;

		g_hash_table_insert (sources_hash, g_strdup (gs_app_get_id (app)), app);
	}

	for (guint i = 0; i < gs_app_list_length (installed); i++) {
		g_auto(GStrv) split = NULL;
		GsApp *app = gs_app_list_index (installed, i);
		split = pk_package_id_split (gs_app_get_default_source_id (app));
		if (split == NULL) {
			g_set_error (&local_error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "invalid package-id: %s",
				     gs_app_get_default_source_id (app));
			refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
			return;
		}
		if (g_str_has_prefix (split[PK_PACKAGE_ID_DATA], "installed:")) {
			const gchar *id = split[PK_PACKAGE_ID_DATA] + 10;
			GsApp *app_tmp = g_hash_table_lookup (sources_hash, id);
			if (app_tmp != NULL) {
				g_debug ("found package %s from %s",
					 gs_app_get_default_source (app), id);
				gs_app_add_related (app_tmp, app);
			}
		}
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
								 data->client_refine,
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

	if (!gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (refine_task), &local_error)) {
		g_prefix_error_literal (&local_error, "failed to search files: ");
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (search_files_data->app != NULL) {
		if (packages->len == 1) {
			PkPackage *package;
			package = g_ptr_array_index (packages, 0);
			gs_plugin_packagekit_set_metadata_from_package (GS_PLUGIN (self), search_files_data->app, package);
		} else {
			g_debug ("%s: Failed to find one package for %s, [%u]", G_STRFUNC,
				 gs_app_get_id (search_files_data->app), packages->len);

		}
	} else {
		for (guint ii = 0; ii < packages->len; ii++) {
			PkPackage *package = g_ptr_array_index (packages, ii);
			GsApp *app;
			if (pk_package_get_name (package) == NULL)
				continue;
			app = g_hash_table_lookup (search_files_data->source_to_app, pk_package_get_name (package));
			if (app != NULL)
				gs_plugin_packagekit_set_metadata_from_package (GS_PLUGIN (self), app, package);
			else
				g_debug ("%s: Failed to find app for package id '%s'", G_STRFUNC, pk_package_get_id (package));
		}

		if (packages->len != search_files_data->n_expected_results) {
			g_debug ("%s: Failed to find package data for each of %u apps, received %u packages instead",
				 G_STRFUNC, search_files_data->n_expected_results, packages->len);
		} else {
			g_debug ("%s: Received package data for all %u apps", G_STRFUNC, packages->len);
		}
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
	GsPlugin *plugin = GS_PLUGIN (g_task_get_source_object (refine_task));
	RefineData *data = g_task_get_task_data (refine_task);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) local_error = NULL;
	gboolean is_markdown_desc;

	results = pk_client_generic_finish (client, result, &local_error);
	if (!gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (refine_task), &local_error)) {
		g_prefix_error (&local_error, "failed to get update details: ");
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	/*
	 * Only Fedora and RHEL (PackageKit DNF backend) are known to
	 * provide update descriptions in markdown format. Other
	 * distros if any should be added below in future. For more
	 * details, refer:
	 *
	 * - https://gitlab.gnome.org/GNOME/gnome-software/-/issues/2621
	 * - https://github.com/PackageKit/PackageKit/issues/828
	 *
	 */
	is_markdown_desc = (gs_plugin_check_distro_id (plugin, "fedora") ||
			    gs_plugin_check_distro_id (plugin, "rhel"));

	/* set the update details for the update */
	array = pk_results_get_update_detail_array (results);
	for (guint j = 0; j < gs_app_list_length (data->update_details_list); j++) {
		GsApp *app = gs_app_list_index (data->update_details_list, j);
		const gchar *package_id = gs_app_get_default_source_id (app);

		for (guint i = 0; i < array->len; i++) {
			const gchar *tmp;
			PkUpdateDetail *update_detail;
			g_autofree gchar *pango_desc = NULL;

			/* right package? */
			update_detail = g_ptr_array_index (array, i);
			if (g_strcmp0 (package_id, pk_update_detail_get_package_id (update_detail)) != 0)
				continue;
			tmp = pk_update_detail_get_update_text (update_detail);
			if (tmp == NULL || *tmp == '\0')
				break;

			if (is_markdown_desc)
				pango_desc = markdown_to_pango (tmp);

			if (pango_desc != NULL && *pango_desc != '\0')
				gs_app_set_update_details_markup (app, pango_desc);
			else
				gs_app_set_update_details_text (app, tmp);
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
	g_autoptr(GHashTable) prepared_updates = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (refine_task), &local_error)) {
		g_autoptr(GPtrArray) package_ids = app_list_get_package_ids (data->details_list, NULL, FALSE);
		g_autofree gchar *package_ids_str = NULL;
		/* NULL-terminate the array */
		g_ptr_array_add (package_ids, NULL);
		package_ids_str = g_strjoinv (",", (gchar **) package_ids->pdata);
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
	prepared_updates = g_hash_table_ref (self->prepared_updates);

	for (guint i = 0; i < gs_app_list_length (data->details_list); i++) {
		GsApp *app = gs_app_list_index (data->details_list, i);
		gs_plugin_packagekit_refine_details_app (GS_PLUGIN (self), details_collection, prepared_updates, app);
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

	if (!gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (refine_task), &local_error)) {
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
		package_id = gs_app_get_default_source_id (app);
		if (package_id == NULL)
			continue;
		pkg = pk_package_sack_find_by_id (sack, package_id);
		if (pkg == NULL)
			continue;
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

/* Run in the main thread. */
static void
gs_plugin_packagekit_permission_cb (GPermission *permission,
                                    GParamSpec  *pspec,
                                    gpointer     data)
{
	GsPlugin *plugin = GS_PLUGIN (data);
	gboolean ret = g_permission_get_allowed (permission) ||
			g_permission_get_can_acquire (permission);
	gs_plugin_set_allow_updates (plugin, ret);
}

static void gs_plugin_packagekit_download_async (GsPluginPackagekit    *self,
                                                 GsAppList             *list,
                                                 gboolean               interactive,
                                                 GsPluginEventCallback  event_callback,
                                                 void                  *event_user_data,
                                                 GCancellable          *cancellable,
                                                 GAsyncReadyCallback    callback,
                                                 gpointer               user_data);
static gboolean gs_plugin_packagekit_download_finish (GsPluginPackagekit  *self,
                                                      GAsyncResult        *result,
                                                      GError             **error);

/* Any events from the auto-prepare-update job need to be reported via
 * `GsPlugin`’s event code (rather than, as is more typical, a
 * `GsPluginEventCallback` provided by a `GsPluginJob`) because this job is
 * started in response to an external system change, and is not tied to any
 * `GsPluginJob`.
 *
 * This callback could be called in a worker thread or the main thread.
 * gs_plugin_report_event() allows that. */
static void
prepare_update_event_cb (GsPlugin      *plugin,
                         GsPluginEvent *event,
                         void          *user_data)
{
	gs_plugin_report_event (plugin, event);
}

static void prepare_update_get_updates_cb (GObject      *source_object,
                                           GAsyncResult *result,
                                           void         *user_data);
static void prepare_update_download_cb (GObject      *source_object,
                                        GAsyncResult *result,
                                        void         *user_data);
static void prepare_update_finished_cb (GObject      *source_object,
                                        GAsyncResult *result,
                                        void         *user_data);

static gboolean
gs_plugin_packagekit_run_prepare_update_cb (gpointer user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (user_data);
	g_autoptr(GTask) task = NULL;
	GCancellable *cancellable = self->proxy_settings_cancellable;
	gboolean interactive = FALSE; /* this is done in the background, thus not interactive */
	g_autoptr(GsPackagekitHelper) helper = NULL;
	g_autoptr(PkTask) task_updates = NULL;

	self->prepare_update_timeout_id = 0;

	g_debug ("Going to auto-prepare update");
	task = g_task_new (self, cancellable, prepare_update_finished_cb, NULL);
	g_task_set_source_tag (task, gs_plugin_packagekit_run_prepare_update_cb);

	/* Get updates */
	task_updates = gs_packagekit_task_new (GS_PLUGIN (self));
	helper = gs_packagekit_helper_new (GS_PLUGIN (self));
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_updates), GS_PACKAGEKIT_TASK_QUESTION_TYPE_NONE, interactive);
	gs_packagekit_helper_set_allow_emit_updates_changed (helper, FALSE);
	gs_packagekit_task_take_helper (GS_PACKAGEKIT_TASK (task_updates), helper);

	pk_client_get_updates_async (PK_CLIENT (task_updates),
				     pk_bitfield_value (PK_FILTER_ENUM_NONE),
				     cancellable,
				     gs_packagekit_helper_cb, g_steal_pointer (&helper),
				     prepare_update_get_updates_cb, g_steal_pointer (&task));

	return G_SOURCE_REMOVE;
}

static void
prepare_update_get_updates_cb (GObject      *source_object,
                               GAsyncResult *result,
                               void         *user_data)
{
	PkTask *task_updates = PK_TASK (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	gboolean interactive = FALSE; /* this is done in the background, thus not interactive */
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autoptr(PkResults) results = NULL;

	list = gs_app_list_new ();
	results = pk_client_generic_finish (PK_CLIENT (task_updates), result, &local_error);
	if (!gs_plugin_package_list_updates_process_results (GS_PLUGIN (self), results, list, cancellable, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* It’s OK to call this with an empty list; it’ll return immediately */
	gs_plugin_packagekit_download_async (self, list, interactive,
					     prepare_update_event_cb, NULL,
					     cancellable,
					     prepare_update_download_cb, g_steal_pointer (&task));
}

static void
prepare_update_download_cb (GObject      *source_object,
                            GAsyncResult *result,
                            void         *user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) local_error = NULL;

	if (!gs_plugin_packagekit_download_finish (self, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* Ignore errors here */
	gs_plugin_systemd_update_cache (self, cancellable, NULL);

	g_task_return_boolean (task, TRUE);
}

static void
prepare_update_finished_cb (GObject      *source_object,
                            GAsyncResult *result,
                            void         *user_data)
{
	g_autoptr(GError) local_error = NULL;

	if (g_task_propagate_boolean (G_TASK (result), &local_error)) {
		g_debug ("Successfully auto-prepared update");
		gs_plugin_updates_changed (GS_PLUGIN (source_object));
	} else {
		g_debug ("Failed to auto-prepare update: %s", local_error->message);
	}
}

/* Run in the main thread. */
static void
gs_plugin_packagekit_prepared_update_changed_cb (GFileMonitor      *monitor,
						 GFile             *file,
						 GFile             *other_file,
						 GFileMonitorEvent  event_type,
						 gpointer           user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (user_data);

	/* Interested only in these events. */
	if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
	    event_type != G_FILE_MONITOR_EVENT_DELETED &&
	    event_type != G_FILE_MONITOR_EVENT_CREATED)
		return;

	/* This is going to break, if PackageKit renames the file, but it's unlikely to happen;
	   there is no API to get the file name from, sadly. */
	if (g_file_peek_path (file) == NULL ||
	    !g_str_has_suffix (g_file_peek_path (file), "prepared-update"))
		return;

	if (event_type == G_FILE_MONITOR_EVENT_DELETED) {
		g_autoptr(GSettings) settings = g_settings_new ("org.gnome.software");
		if (g_settings_get_boolean (settings, "download-updates")) {
			/* The prepared-update file had been removed, but the user has set
			   to have the updates downloaded, thus prepared, thus prepare
			   the update again. */
			if (self->prepare_update_timeout_id)
				g_source_remove (self->prepare_update_timeout_id);
			g_debug ("Scheduled to auto-prepare update in %d s", PREPARE_UPDATE_TIMEOUT_SECS);
			self->prepare_update_timeout_id = g_timeout_add_seconds (PREPARE_UPDATE_TIMEOUT_SECS,
				gs_plugin_packagekit_run_prepare_update_cb, self);
		} else {
			if (self->prepare_update_timeout_id) {
				g_source_remove (self->prepare_update_timeout_id);
				self->prepare_update_timeout_id = 0;
				g_debug ("Cancelled auto-prepare update");
			}
		}
	} else if (self->prepare_update_timeout_id) {
		g_source_remove (self->prepare_update_timeout_id);
		self->prepare_update_timeout_id = 0;
		g_debug ("Cancelled auto-prepare update");
	}

	/* update UI */
	gs_plugin_systemd_update_cache (self, NULL, NULL);
	gs_plugin_updates_changed (GS_PLUGIN (self));
}

static void
gs_plugin_packagekit_refresh_is_triggered (GsPluginPackagekit *self,
                                           GCancellable       *cancellable)
{
	g_autoptr(GFile) file_trigger = NULL;
	file_trigger = g_file_new_for_path ("/system-update");
	self->is_triggered = g_file_query_exists (file_trigger, NULL);
	g_debug ("offline trigger is now %s",
		 self->is_triggered ? "enabled" : "disabled");
}

/* Run in the main thread. */
static void
gs_plugin_systemd_trigger_changed_cb (GFileMonitor *monitor,
				      GFile *file, GFile *other_file,
				      GFileMonitorEvent event_type,
				      gpointer user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (user_data);

	gs_plugin_packagekit_refresh_is_triggered (self, NULL);
}

static void setup_proxy_settings_cb (GObject      *source_object,
                                     GAsyncResult *result,
                                     gpointer      user_data);
static void get_offline_update_permission_cb (GObject      *source_object,
                                              GAsyncResult *result,
                                              gpointer      user_data);

static void
gs_plugin_packagekit_get_properties_cb (GObject *source_object,
					GAsyncResult *result,
					gpointer user_data)
{
	PkControl *control = PK_CONTROL (source_object);
	g_autoptr(GError) error = NULL;

	if (pk_control_get_properties_finish (control, result, &error)) {
		guint32 major, minor, micro;
		g_autoptr(GString) string = g_string_new (NULL);

		g_object_get (control,
			      "version_major", &major,
			      "version_minor", &minor,
			      "version_micro", &micro,
			      NULL);

		g_string_append_printf (string, "PackageKit version: %u.%u.%u", major, minor, micro);

		if (major != PK_MAJOR_VERSION || minor != PK_MINOR_VERSION || micro != PK_MICRO_VERSION) {
			g_string_append_printf (string,
						" (build version: %d.%d.%d)",
						PK_MAJOR_VERSION,
						PK_MINOR_VERSION,
						PK_MICRO_VERSION);
		}

		g_debug ("%s", string->str);
	} else {
		g_debug ("Failed to get PackageKit properties: %s (build version: %d.%d.%d)",
			 (error ? error->message : "Unknown error"),
			 PK_MAJOR_VERSION,
			 PK_MINOR_VERSION,
			 PK_MICRO_VERSION);
	}
}

static void
gs_plugin_packagekit_setup_async (GsPlugin            *plugin,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GTask) task = NULL;

	/* print real packagekit version, no need to wait for it */
	pk_control_get_properties_async (self->control_proxy, cancellable, gs_plugin_packagekit_get_properties_cb, NULL);

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_setup_async);

	reload_proxy_settings_async (self, FALSE, cancellable, setup_proxy_settings_cb, g_steal_pointer (&task));
}

static void
setup_proxy_settings_cb (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GFile) file_trigger = NULL;
	g_autoptr(GError) local_error = NULL;

	if (!reload_proxy_settings_finish (self, result, &local_error))
		g_warning ("Failed to load proxy settings: %s", local_error->message);
	g_clear_error (&local_error);

	/* watch the prepared file */
	self->monitor = pk_offline_get_prepared_monitor (cancellable, &local_error);
	if (self->monitor == NULL) {
		g_debug ("Failed to get prepared update file monitor: %s", local_error->message);
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_signal_connect (self->monitor, "changed",
			  G_CALLBACK (gs_plugin_packagekit_prepared_update_changed_cb),
			  self);

	/* watch the trigger file */
	file_trigger = g_file_new_for_path ("/system-update");
	self->monitor_trigger = g_file_monitor_file (file_trigger,
						     G_FILE_MONITOR_NONE,
						     NULL,
						     &local_error);
	if (self->monitor_trigger == NULL) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_signal_connect (self->monitor_trigger, "changed",
			  G_CALLBACK (gs_plugin_systemd_trigger_changed_cb),
			  self);

	/* check if we have permission to trigger offline updates */
	gs_utils_get_permission_async ("org.freedesktop.packagekit.trigger-offline-update",
				       cancellable, get_offline_update_permission_cb, g_steal_pointer (&task));
}

static void
get_offline_update_permission_cb (GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	g_autoptr(GError) local_error = NULL;

	self->permission = gs_utils_get_permission_finish (result, &local_error);
	if (self->permission != NULL) {
		g_signal_connect (self->permission, "notify",
				  G_CALLBACK (gs_plugin_packagekit_permission_cb),
				  self);
	}

	/* get the list of currently downloaded packages */
	if (!gs_plugin_systemd_update_cache (self, g_task_get_cancellable (task), &local_error))
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
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
	GsApp *app;
	g_autofree const gchar **package_names = NULL;
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_refine_history_async);
	g_task_set_task_data (task, g_object_ref (list), (GDestroyNotify) g_object_unref);

	/* get an array of package names */
	package_names = g_new0 (const gchar *, gs_app_list_length (list) + 1);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		package_names[i] = gs_app_get_default_source (app);
	}

	g_debug ("getting history for %u packages", gs_app_list_length (list));
	g_dbus_connection_call (gs_plugin_get_system_bus_connection (GS_PLUGIN (self)),
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
					gs_app_get_default_source (app),
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
		gs_app_add_quirk (app, GS_APP_QUIRK_LOCAL_HAS_REPOSITORY);
}

typedef struct {
	GFile *file;  /* (not nullable) (owned) */
	GsPluginFileToAppFlags flags;

	GsApp *app;  /* (nullable) (owned) */
} FileToAppData;

static void
file_to_app_data_free (FileToAppData *data)
{
	g_clear_object (&data->file);
	g_clear_object (&data->app);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FileToAppData, file_to_app_data_free)

static void file_to_app_get_content_type_cb (GObject      *source_object,
                                             GAsyncResult *result,
                                             gpointer      user_data);
static void file_to_app_get_details_local_cb (GObject      *source_object,
                                              GAsyncResult *result,
                                              gpointer      user_data);
static void file_to_app_resolve_cb (GObject      *source_object,
                                    GAsyncResult *result,
                                    gpointer      user_data);
static void file_to_app_get_files_cb (GObject      *source_object,
                                      GAsyncResult *result,
                                      gpointer      user_data);

static void
gs_plugin_packagekit_file_to_app_async (GsPlugin *plugin,
					GFile *file,
					GsPluginFileToAppFlags flags,
					GsPluginEventCallback event_callback,
					void *event_user_data,
					GCancellable *cancellable,
					GAsyncReadyCallback callback,
					gpointer user_data)
{
	g_autoptr(GTask) task = NULL;
	g_autoptr(FileToAppData) data = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_file_to_app_async);

	data = g_new0 (FileToAppData, 1);
	data->file = g_object_ref (file);
	data->flags = flags;

	g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) file_to_app_data_free);

	/* does this match any of the mimetypes we support */
	gs_utils_get_content_type_async (file, cancellable,
					 file_to_app_get_content_type_cb,
					 g_steal_pointer (&task));
}

static void
file_to_app_get_content_type_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
	GFile *file = G_FILE (source_object);
	g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
	g_autoptr(GError) local_error = NULL;
	GsPlugin *plugin = g_task_get_source_object (task);
	FileToAppData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	gboolean interactive = (data->flags & GS_PLUGIN_FILE_TO_APP_FLAGS_INTERACTIVE);
	g_autoptr(GsPackagekitHelper) helper = NULL;
	g_autoptr(PkTask) task_local = NULL;
	g_autofree char *content_type = NULL;
	g_autofree char *filename = NULL;
	g_auto(GStrv) files = NULL;
	const gchar *mimetypes[] = {
		"application/x-app-package",
		"application/x-deb",
		"application/vnd.debian.binary-package",
		"application/x-redhat-package-manager",
		"application/x-rpm",
		NULL };

	/* does this match any of the mimetypes we support */
	content_type = gs_utils_get_content_type_finish (file, result, &local_error);
	if (content_type == NULL) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	} else if (!g_strv_contains (mimetypes, content_type)) {
		g_task_return_pointer (task, gs_app_list_new (), g_object_unref);
		return;
	}

	/* get details */
	filename = g_file_get_path (file);
	files = g_strsplit (filename, "\t", -1);

	task_local = gs_packagekit_task_new (plugin);
	helper = gs_packagekit_helper_new (plugin);
	pk_client_set_cache_age (PK_CLIENT (task_local), G_MAXUINT);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_local), GS_PACKAGEKIT_TASK_QUESTION_TYPE_NONE, interactive);
	gs_packagekit_task_take_helper (GS_PACKAGEKIT_TASK (task_local), helper);

	pk_client_get_details_local_async (PK_CLIENT (task_local),
					   files,
					   cancellable,
					   gs_packagekit_helper_cb, g_steal_pointer (&helper),
					   file_to_app_get_details_local_cb,
					   g_steal_pointer (&task));
}

static void
file_to_app_get_details_local_cb (GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
	PkTask *task_local = PK_TASK (source_object);
	g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
	g_autoptr(GError) local_error = NULL;
	GsPlugin *plugin = g_task_get_source_object (task);
	FileToAppData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	const gchar *package_id;
	PkDetails *item;
	g_autoptr(PkResults) results = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *packagename = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GsApp) app = NULL;
	PkBitfield filter;
	const gchar *names[2] = { NULL, };

	results = pk_client_generic_finish (PK_CLIENT (source_object), result, &local_error);
	if (local_error != NULL || !gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
		g_prefix_error (&local_error, "Failed to resolve package_ids: ");
		gs_plugin_packagekit_error_convert (&local_error, cancellable);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* get results */
	filename = g_file_get_path (data->file);
	array = pk_results_get_details_array (results);
	if (array->len == 0) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_INVALID_FORMAT,
					 "No details for %s", filename);
		return;
	} else if (array->len > 1) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_INVALID_FORMAT,
					 "Too many details [%u] for %s",
					 array->len, filename);
		return;
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
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_INVALID_FORMAT,
					 "Invalid package-id: %s", package_id);
		return;
	}

	gs_app_set_management_plugin (app, plugin);
	gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_set_local_file (app, data->file);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, split[PK_PACKAGE_ID_NAME]);
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
			    pk_details_get_summary (item));
	gs_app_set_version (app, split[PK_PACKAGE_ID_VERSION]);
	gs_app_add_source (app, split[PK_PACKAGE_ID_NAME]);
	gs_app_add_source_id (app, package_id);
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST,
				pk_details_get_description (item));
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, pk_details_get_url (item));
	gs_app_set_size_installed (app, GS_SIZE_TYPE_VALID, pk_details_get_size (item));
	gs_app_set_size_download (app, GS_SIZE_TYPE_VALID, 0);
	if (pk_details_get_license (item) != NULL &&
	    g_ascii_strcasecmp (pk_details_get_license (item), "unknown") != 0) {
		g_autofree gchar *license_spdx = NULL;
		license_spdx = as_license_to_spdx_id (pk_details_get_license (item));
		if (license_spdx != NULL && g_ascii_strcasecmp (license_spdx, "unknown") == 0) {
			g_clear_pointer (&license_spdx, g_free);
			license_spdx = g_strdup (pk_details_get_license (item));
			if (license_spdx != NULL)
				g_strstrip (license_spdx);
		}
		gs_app_set_license (app, GS_APP_QUALITY_LOWEST, license_spdx);
	}
	add_quirks_from_package_name (app, split[PK_PACKAGE_ID_NAME]);
	packagename = g_strdup_printf ("%s-%s.%s",
					split[PK_PACKAGE_ID_NAME],
					split[PK_PACKAGE_ID_VERSION],
					split[PK_PACKAGE_ID_ARCH]);
	gs_app_set_metadata (app, "GnomeSoftware::packagename-value", packagename);

	data->app = g_steal_pointer (&app);

	/* is already installed? */
	names[0] = gs_app_get_default_source (data->app);
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_INSTALLED,
					 -1);
	pk_client_resolve_async (PK_CLIENT (task_local),
				 filter, (gchar **) names,
				 cancellable, NULL, NULL,
				 file_to_app_resolve_cb,
				 g_steal_pointer (&task));
}

static void
file_to_app_resolve_cb (GObject      *source_object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
	PkTask *task_local = PK_TASK (source_object);
	g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
	g_autoptr(GError) local_error = NULL;
	FileToAppData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	GsPackagekitHelper *helper;
	g_autoptr(PkResults) results = NULL;
	g_autofree gchar *filename = NULL;
	g_auto(GStrv) files = NULL;
	g_autoptr(GPtrArray) packages = NULL;

	results = pk_client_generic_finish (PK_CLIENT (source_object), result, &local_error);
	if (local_error != NULL || !gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
		g_prefix_error (&local_error, "Failed to resolve whether package is installed: ");
		gs_plugin_packagekit_error_convert (&local_error, cancellable);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	packages = pk_results_get_package_array (results);
	if (packages->len > 0) {
		gboolean is_higher_version = FALSE;
		const gchar *app_version = gs_app_get_version (data->app);

		for (guint i = 0; i < packages->len; i++){
			PkPackage *pkg = g_ptr_array_index (packages, i);
			gs_app_add_source_id (data->app, pk_package_get_id (pkg));
			gs_plugin_packagekit_set_package_name (data->app, pkg);
			if (!is_higher_version &&
			    gs_utils_compare_versions (pk_package_get_version (pkg), app_version) < 0)
				is_higher_version = TRUE;
		}

		if (!is_higher_version) {
			gs_app_set_state (data->app, GS_APP_STATE_UNKNOWN);
			gs_app_set_state (data->app, GS_APP_STATE_INSTALLED);
		}
	}

	/* look for a desktop file so we can use a valid application id */
	filename = g_file_get_path (data->file);

	/* get file list so we can work out ID */
	files = g_strsplit (filename, "\t", -1);
	helper = gs_packagekit_task_get_helper (GS_PACKAGEKIT_TASK (task_local));
	gs_packagekit_helper_add_app (helper, data->app);

	pk_client_get_files_local_async (PK_CLIENT (task_local),
					 files,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 file_to_app_get_files_cb,
					 g_steal_pointer (&task));
}

static void
file_to_app_get_files_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
	g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GError) local_error = NULL;
	FileToAppData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(PkResults) results = NULL;
	g_autofree char *filename = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GString) basename_best = g_string_new (NULL);

	results = pk_client_generic_finish (PK_CLIENT (source_object), result, &local_error);
	if (local_error != NULL || !gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
		gs_utils_error_add_origin_id (&local_error, data->app);
		g_prefix_error (&local_error, "Failed to resolve files in local package: ");
		gs_plugin_packagekit_error_convert (&local_error, cancellable);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	filename = g_file_get_path (data->file);
	array = pk_results_get_files_array (results);
	if (array->len == 0) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_INVALID_FORMAT,
					 "No files for %s", filename);
		return;
	}

	/* find the smallest length desktop file, on the logic that
	 * ${app}.desktop is going to be better than ${app}-${action}.desktop */
	for (guint i = 0; i < array->len; i++) {
		PkFiles *item = g_ptr_array_index (array, i);
		const char * const *fns = (const char * const *) pk_files_get_files (item);

		for (guint j = 0; fns[j] != NULL; j++) {
			if (g_str_has_prefix (fns[j], "/etc/yum.repos.d/") &&
			    g_str_has_suffix (fns[j], ".repo")) {
				gs_app_add_quirk (data->app, GS_APP_QUIRK_LOCAL_HAS_REPOSITORY);
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
		gs_app_set_kind (data->app, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_set_id (data->app, basename_best->str);
	}

	/* Success */
	gs_app_list_add (list, data->app);

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_packagekit_file_to_app_finish (GsPlugin      *plugin,
					 GAsyncResult  *result,
					 GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void gs_plugin_packagekit_url_to_app_resolved_cb (GObject      *source_object,
                                                         GAsyncResult *result,
                                                         gpointer      user_data);

static void
gs_plugin_packagekit_url_to_app_async (GsPlugin *plugin,
				       const gchar *url,
				       GsPluginUrlToAppFlags flags,
				       GsPluginEventCallback event_callback,
				       void *event_user_data,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer user_data)
{
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;
	g_autoptr(GsPackagekitHelper) helper = NULL;
	g_autoptr(PkTask) task_resolve = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;

	task = gs_plugin_url_to_app_data_new_task (plugin, url, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_url_to_app_async);

	/* only do this for apt:// on debian or debian-like distros */
	os_release = gs_os_release_new (&local_error);
	if (os_release == NULL) {
		g_prefix_error_literal (&local_error, "Failed to determine OS information: ");
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	} else {
		const gchar *id = NULL;
		const gchar * const *id_like = NULL;
		g_autofree gchar *scheme = NULL;
		id = gs_os_release_get_id (os_release);
		id_like = gs_os_release_get_id_like (os_release);
		scheme = gs_utils_get_url_scheme (url);
		if (!(g_strcmp0 (scheme, "apt") == 0 &&
		     (g_strcmp0 (id, "debian") == 0 ||
		      (id_like != NULL && g_strv_contains (id_like, "debian"))))) {
			g_task_return_pointer (task, gs_app_list_new (), g_object_unref);
			return;
		}
	}

	package_ids = g_new0 (gchar *, 2);
	package_ids[0] = gs_utils_get_url_path (url);

	task_resolve = gs_packagekit_task_new (plugin);
	helper = gs_packagekit_helper_new (plugin);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_resolve), GS_PACKAGEKIT_TASK_QUESTION_TYPE_NONE,
				  flags & GS_PLUGIN_URL_TO_APP_FLAGS_INTERACTIVE);
	gs_packagekit_task_take_helper (GS_PACKAGEKIT_TASK (task_resolve), helper);

	pk_client_resolve_async (PK_CLIENT (task_resolve),
				 pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_ARCH, -1),
				 package_ids,
				 cancellable,
				 gs_packagekit_helper_cb, g_steal_pointer (&helper),
				 gs_plugin_packagekit_url_to_app_resolved_cb, g_steal_pointer (&task));
}

static void
gs_plugin_packagekit_url_to_app_resolved_cb (GObject *source_object,
					     GAsyncResult *result,
					     gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (g_task_get_source_object (task));
	GsPluginUrlToAppData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	GsPlugin *plugin = GS_PLUGIN (self);
	g_autofree gchar *path = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GPtrArray) packages = NULL;
	g_autoptr(GPtrArray) details = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (PK_CLIENT (source_object), result, &local_error);
	if (local_error != NULL || !gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
		g_prefix_error (&local_error, "Failed to resolve package_ids: ");
		gs_plugin_packagekit_error_convert (&local_error, cancellable);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	path = gs_utils_get_url_path (data->url);
	list = gs_app_list_new ();
	app = gs_app_new (NULL);
	gs_plugin_packagekit_set_packaging_format (plugin, app);
	gs_app_add_source (app, path);
	gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);

	/* get results */
	packages = pk_results_get_package_array (results);
	details = pk_results_get_details_array (results);

	if (packages->len >= 1) {
		g_autoptr(GHashTable) details_collection = NULL;
		g_autoptr(GHashTable) prepared_updates = NULL;

		if (gs_app_get_local_file (app) == NULL) {
			details_collection = gs_plugin_packagekit_details_array_to_hash (details);

			prepared_updates = g_hash_table_ref (self->prepared_updates);

			gs_plugin_packagekit_resolve_packages_app (plugin, packages, app);
			gs_plugin_packagekit_refine_details_app (plugin, details_collection, prepared_updates, app);
		}

		gs_app_list_add (list, app);
	} else {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_INVALID_FORMAT,
					 "No files for %s", data->url);
		return;
	}

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_packagekit_url_to_app_finish (GsPlugin      *plugin,
					GAsyncResult  *result,
					GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void
get_desktop_proxy_settings (GsPluginPackagekit  *self,
                            char               **out_http,
                            char               **out_https,
                            char               **out_ftp,
                            char               **out_socks,
                            char               **out_ignore_hosts,
                            char               **out_pac)
{
	GDesktopProxyMode proxy_mode;

	/* Clear all the outputs first. */
	*out_http = NULL;
	*out_https = NULL;
	*out_ftp = NULL;
	*out_socks = NULL;
	*out_ignore_hosts = NULL;
	*out_pac = NULL;

	proxy_mode = g_settings_get_enum (self->settings_proxy, "mode");

	if (proxy_mode == G_DESKTOP_PROXY_MODE_MANUAL) {
		g_autofree char *http_host = NULL;
		g_auto(GStrv) ignore_hosts = NULL;
		const struct {
			GSettings *settings;
			char **out;
		} similar_protocols[] = {
			{ self->settings_https, out_https },
			{ self->settings_ftp, out_ftp },
			{ self->settings_socks, out_socks },
		};

		/* HTTP */
		http_host = g_settings_get_string (self->settings_http, "host");
		if (http_host != NULL && *http_host != '\0') {
			GString *string = NULL;
			gint port;
			g_autofree gchar *password = NULL;
			g_autofree gchar *username = NULL;

			port = g_settings_get_int (self->settings_http, "port");

			if (g_settings_get_boolean (self->settings_http,
						    "use-authentication")) {
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
			g_string_append (string, http_host);
			if (port > 0)
				g_string_append_printf (string, ":%i", port);
			*out_http = g_string_free (string, FALSE);
		}

		/* HTTPS, FTP and SOCKS all follow the same pattern */
		for (size_t i = 0; i < G_N_ELEMENTS (similar_protocols); i++) {
			g_autofree char *host = g_settings_get_string (similar_protocols[i].settings, "host");
			int port = g_settings_get_int (similar_protocols[i].settings, "port");

			if (host != NULL && *host != '\0' && port != 0) {
				/* make PackageKit proxy string */
				if (port > 0)
					*(similar_protocols[i].out) = g_strdup_printf ("%s:%i", host, port);
				else
					*(similar_protocols[i].out) = g_steal_pointer (&host);
			}
		}

		/* ignore-hosts */
		ignore_hosts = g_settings_get_strv (self->settings_proxy, "ignore-hosts");

		if (ignore_hosts != NULL)
			*out_ignore_hosts = g_strjoinv (",", ignore_hosts);
	} else if (proxy_mode == G_DESKTOP_PROXY_MODE_AUTO) {
		/* PAC */
		*out_pac = g_settings_get_string (self->settings_proxy, "autoconfig-url");
	}
}

static void get_permission_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data);
static void set_proxy_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data);

static void
reload_proxy_settings_async (GsPluginPackagekit  *self,
                             gboolean             force_set,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, reload_proxy_settings_async);

	/* Check whether there are any proxy settings set. If not, we can save
	 * several D-Bus round-trips to query polkit and call SetProxy() on
	 * PackageKit just to set its defaults.
	 *
	 * We always want to set the proxy settings if they’ve changed, though,
	 * which is what @force_set is for. */
	if (!force_set) {
		g_autofree char *proxy_http = NULL;
		g_autofree char *proxy_https = NULL;
		g_autofree char *proxy_ftp = NULL;
		g_autofree char *proxy_socks = NULL;
		g_autofree char *proxy_ignore_hosts = NULL;
		g_autofree char *proxy_pac = NULL;

		get_desktop_proxy_settings (self, &proxy_http, &proxy_https,
					    &proxy_ftp, &proxy_socks,
					    &proxy_ignore_hosts, &proxy_pac);

		if (proxy_http == NULL && proxy_https == NULL && proxy_ftp == NULL &&
		    proxy_socks == NULL && proxy_ignore_hosts == NULL && proxy_pac == NULL) {
			g_debug ("Setting skipping proxies as they are all empty");
			g_task_return_boolean (task, TRUE);
			return;
		}
	}

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
	g_autofree gchar *proxy_ignore_hosts = NULL;
	g_autofree gchar *proxy_pac = NULL;
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

	get_desktop_proxy_settings (self, &proxy_http, &proxy_https, &proxy_ftp,
				    &proxy_socks, &proxy_ignore_hosts, &proxy_pac);

	g_debug ("Setting proxies (http: %s, https: %s, ftp: %s, socks: %s, "
		 "ignore-hosts: %s, pac: %s)",
		 proxy_http, proxy_https, proxy_ftp, proxy_socks,
		 proxy_ignore_hosts, proxy_pac);

	pk_control_set_proxy2_async (self->control_proxy,
				     proxy_http,
				     proxy_https,
				     proxy_ftp,
				     proxy_socks,
				     proxy_ignore_hosts,
				     proxy_pac,
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

	reload_proxy_settings_async (self, TRUE, self->proxy_settings_cancellable,
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

static void
gs_packagekit_upgrade_system_cb (GObject *source_object,
				 GAsyncResult *result,
				 gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) local_error = NULL;
	GsPluginDownloadUpgradeData *data = g_task_get_task_data (task);

	results = pk_task_generic_finish (PK_TASK (source_object), result, &local_error);

	if (local_error != NULL || !gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (task), &local_error)) {
		gs_app_set_state_recover (data->app);
		gs_plugin_packagekit_error_convert (&local_error, g_task_get_cancellable (task));
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* state is known */
	gs_app_set_state (data->app, GS_APP_STATE_UPDATABLE);

	g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_packagekit_download_upgrade_async (GsPlugin                     *plugin,
                                             GsApp                        *app,
                                             GsPluginDownloadUpgradeFlags  flags,
                                             GsPluginEventCallback         event_callback,
                                             void                         *event_user_data,
                                             GCancellable                 *cancellable,
                                             GAsyncReadyCallback           callback,
                                             gpointer                      user_data)
{
	g_autoptr(GsPackagekitHelper) helper = NULL;
	g_autoptr(PkTask) task_upgrade = NULL;
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_DOWNLOAD_UPGRADE_FLAGS_INTERACTIVE) != 0;

	task = gs_plugin_download_upgrade_data_new_task (plugin, app, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_download_upgrade_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* check is distro-upgrade */
	if (gs_app_get_kind (app) != AS_COMPONENT_KIND_OPERATING_SYSTEM) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	helper = gs_packagekit_helper_new (plugin);

	/* ask PK to download enough packages to upgrade the system */
	gs_app_set_state (app, GS_APP_STATE_DOWNLOADING);
	gs_packagekit_helper_set_progress_app (helper, app);

	task_upgrade = gs_packagekit_task_new (plugin);
	pk_task_set_only_download (task_upgrade, TRUE);
	pk_client_set_cache_age (PK_CLIENT (task_upgrade), 60 * 60 * 24);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_upgrade), GS_PACKAGEKIT_TASK_QUESTION_TYPE_DOWNLOAD, interactive);
	gs_packagekit_task_take_helper (GS_PACKAGEKIT_TASK (task_upgrade), helper);

	pk_task_upgrade_system_async (task_upgrade,
				      gs_app_get_version (app),
				      PK_UPGRADE_KIND_ENUM_COMPLETE,
				      cancellable,
				      gs_packagekit_helper_cb, g_steal_pointer (&helper),
				      gs_packagekit_upgrade_system_cb,
				      g_steal_pointer (&task));
}

static gboolean
gs_plugin_packagekit_download_upgrade_finish (GsPlugin      *plugin,
                                              GAsyncResult  *result,
                                              GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void gs_plugin_packagekit_refresh_metadata_async (GsPlugin                     *plugin,
                                                         guint64                       cache_age_secs,
                                                         GsPluginRefreshMetadataFlags  flags,
                                                         GsPluginEventCallback         event_callback,
                                                         void                         *event_user_data,
                                                         GCancellable                 *cancellable,
                                                         GAsyncReadyCallback           callback,
                                                         gpointer                      user_data);

static void
gs_plugin_packagekit_enable_repository_refresh_ready_cb (GObject *source_object,
							 GAsyncResult *result,
							 gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (g_task_get_source_object (task));
	GsPluginManageRepositoryData *data = g_task_get_task_data (task);

	gs_plugin_repository_changed (GS_PLUGIN (self), data->repository);

	/* Ignore refresh errors */
	g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_packagekit_enable_repository_ready_cb (GObject *source_object,
						 GAsyncResult *result,
						 gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(GError) local_error = NULL;
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (g_task_get_source_object (task));
	GsPluginManageRepositoryData *data = g_task_get_task_data (task);
	GsPluginRefreshMetadataFlags metadata_flags;
	GCancellable *cancellable = g_task_get_cancellable (task);

	results = pk_client_generic_finish (PK_CLIENT (source_object), result, &local_error);

	/* pk_client_repo_enable() returns an error if the repo is already enabled. */
	if (results != NULL &&
	    (error_code = pk_results_get_error_code (results)) != NULL &&
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_REPO_ALREADY_SET) {
		g_clear_error (&local_error);
	} else if (local_error != NULL || !gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
		gs_app_set_state_recover (data->repository);
		gs_utils_error_add_origin_id (&local_error, data->repository);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* state is known */
	gs_app_set_state (data->repository, GS_APP_STATE_INSTALLED);

	metadata_flags = (data->flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE) != 0 ?
			 GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE :
			 GS_PLUGIN_REFRESH_METADATA_FLAGS_NONE;

	gs_plugin_packagekit_refresh_metadata_async (GS_PLUGIN (self),
						     1,  /* cache age */
						     metadata_flags,
						     data->event_callback,
						     data->event_user_data,
						     cancellable,
						     gs_plugin_packagekit_enable_repository_refresh_ready_cb,
						     g_steal_pointer (&task));
}

static void
gs_plugin_packagekit_enable_repository_async (GsPlugin                     *plugin,
					      GsApp			   *repository,
                                              GsPluginManageRepositoryFlags flags,
                                              GsPluginEventCallback         event_callback,
                                              void                         *event_user_data,
                                              GCancellable		   *cancellable,
                                              GAsyncReadyCallback	    callback,
                                              gpointer			    user_data)
{
	g_autoptr(GsPackagekitHelper) helper = NULL;
	g_autoptr(PkTask) task_enable_repo = NULL;
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_manage_repository_data_new_task (plugin, repository, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_enable_repository_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (repository, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* is repo */
	g_assert (gs_app_get_kind (repository) == AS_COMPONENT_KIND_REPOSITORY);

	/* do the call */
	gs_app_set_state (repository, GS_APP_STATE_INSTALLING);

	helper = gs_packagekit_helper_new (plugin);
	gs_packagekit_helper_add_app (helper, repository);

	task_enable_repo = gs_packagekit_task_new (plugin);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_enable_repo), GS_PACKAGEKIT_TASK_QUESTION_TYPE_NONE,
				  (flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE) != 0);
	gs_packagekit_task_take_helper (GS_PACKAGEKIT_TASK (task_enable_repo), helper);

	pk_client_repo_enable_async (PK_CLIENT (task_enable_repo),
				     gs_app_get_id (repository),
				     TRUE,
				     cancellable,
				     gs_packagekit_helper_cb, g_steal_pointer (&helper),
				     gs_plugin_packagekit_enable_repository_ready_cb,
				     g_steal_pointer (&task));
}

static gboolean
gs_plugin_packagekit_enable_repository_finish (GsPlugin      *plugin,
					       GAsyncResult  *result,
					       GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_packagekit_disable_repository_ready_cb (GObject *source_object,
						  GAsyncResult *result,
						  gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(GError) local_error = NULL;
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (g_task_get_source_object (task));
	GsPluginManageRepositoryData *data = g_task_get_task_data (task);

	results = pk_client_generic_finish (PK_CLIENT (source_object), result, &local_error);

	/* pk_client_repo_enable() returns an error if the repo is already disabled. */
	if (results != NULL &&
	    (error_code = pk_results_get_error_code (results)) != NULL &&
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_REPO_ALREADY_SET) {
		g_clear_error (&local_error);
	} else if (local_error != NULL || !gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (task), &local_error)) {
		gs_app_set_state_recover (data->repository);
		gs_utils_error_add_origin_id (&local_error, data->repository);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* state is known */
	gs_app_set_state (data->repository, GS_APP_STATE_AVAILABLE);

	gs_plugin_repository_changed (GS_PLUGIN (self), data->repository);

	g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_packagekit_disable_repository_async (GsPlugin                     *plugin,
					       GsApp			    *repository,
                                               GsPluginManageRepositoryFlags flags,
                                               GsPluginEventCallback         event_callback,
                                               void                         *event_user_data,
                                               GCancellable		    *cancellable,
                                               GAsyncReadyCallback	     callback,
                                               gpointer			     user_data)
{
	g_autoptr(GsPackagekitHelper) helper = NULL;
	g_autoptr(PkTask) task_disable_repo = NULL;
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_manage_repository_data_new_task (plugin, repository, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_disable_repository_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (repository, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* is repo */
	g_assert (gs_app_get_kind (repository) == AS_COMPONENT_KIND_REPOSITORY);

	/* do the call */
	gs_app_set_state (repository, GS_APP_STATE_REMOVING);

	helper = gs_packagekit_helper_new (plugin);
	gs_packagekit_helper_add_app (helper, repository);

	task_disable_repo = gs_packagekit_task_new (plugin);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_disable_repo), GS_PACKAGEKIT_TASK_QUESTION_TYPE_NONE,
				  (flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE) != 0);
	gs_packagekit_task_take_helper (GS_PACKAGEKIT_TASK (task_disable_repo), helper);

	pk_client_repo_enable_async (PK_CLIENT (task_disable_repo),
				     gs_app_get_id (repository),
				     FALSE,
				     cancellable,
				     gs_packagekit_helper_cb, g_steal_pointer (&helper),
				     gs_plugin_packagekit_disable_repository_ready_cb,
				     g_steal_pointer (&task));
}

static gboolean
gs_plugin_packagekit_disable_repository_finish (GsPlugin      *plugin,
						GAsyncResult  *result,
						GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

typedef struct {
	gpointer schedule_entry_handle;  /* (nullable) (owned) */

	/* List of apps to download, and list of apps to notify of download
	 * progress on. @download_list is a superset of @progress_list, and
	 * may include extra dependencies. */
	GsAppList *download_list;  /* (owned) */
	GsAppList *progress_list;  /* (owned) */

	gboolean interactive;
	GsPluginEventCallback event_callback;
	void *event_user_data;

	GsPackagekitHelper *helper;  /* (owned) */
} DownloadData;

static void
download_data_free (DownloadData *data)
{
	/* Should have been explicitly removed from the scheduler by now. */
	g_assert (data->schedule_entry_handle == NULL);

	g_clear_object (&data->download_list);
	g_clear_object (&data->progress_list);
	g_clear_object (&data->helper);

	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DownloadData, download_data_free)

static void download_schedule_cb (GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data);
static void download_get_updates_cb (GObject      *source_object,
                                     GAsyncResult *result,
                                     gpointer      user_data);
static void download_update_packages_cb (GObject      *source_object,
                                         GAsyncResult *result,
                                         gpointer      user_data);
static void finish_download (GTask  *task,
                             GError *error);

static void
gs_plugin_packagekit_download_async (GsPluginPackagekit    *self,
                                     GsAppList             *list,
                                     gboolean               interactive,
                                     GsPluginEventCallback  event_callback,
                                     void                  *event_user_data,
                                     GCancellable          *cancellable,
                                     GAsyncReadyCallback    callback,
                                     gpointer               user_data)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	g_autoptr(GTask) task = NULL;
	g_autoptr(DownloadData) data_owned = NULL;
	DownloadData *data;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_download_async);

	data = data_owned = g_new0 (DownloadData, 1);
	data->download_list = gs_app_list_new ();
	data->progress_list = g_object_ref (list);
	data->interactive = interactive;
	data->event_callback = event_callback;
	data->event_user_data = event_user_data;
	data->helper = gs_packagekit_helper_new (plugin);
	gs_packagekit_helper_set_allow_emit_updates_changed (data->helper, FALSE);
	g_task_set_task_data (task, g_steal_pointer (&data_owned), (GDestroyNotify) download_data_free);

	/* add any packages */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsAppList *related = gs_app_get_related (app);

		/* add this app */
		if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_PROXY)) {
			if (gs_app_has_management_plugin (app, plugin))
				gs_app_list_add (data->download_list, app);
			continue;
		}

		/* add each related app */
		for (guint j = 0; j < gs_app_list_length (related); j++) {
			GsApp *app_tmp = gs_app_list_index (related, j);
			if (gs_app_has_management_plugin (app_tmp, plugin))
				gs_app_list_add (data->download_list, app_tmp);
		}
	}

	if (gs_app_list_length (data->download_list) == 0) {
		finish_download (task, NULL);
		return;
	}

	/* Wait for permission to download, if needed. */
	if (!data->interactive) {
		g_auto(GVariantDict) parameters_dict = G_VARIANT_DICT_INIT (NULL);

		g_variant_dict_insert (&parameters_dict, "resumable", "b", FALSE);

		gs_metered_block_on_download_scheduler_async (g_variant_dict_end (&parameters_dict),
							      cancellable,
							      download_schedule_cb,
							      g_steal_pointer (&task));
	} else {
		download_schedule_cb (NULL, NULL, g_steal_pointer (&task));
	}
}

static void
download_schedule_cb (GObject      *source_object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	DownloadData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(PkTask) task_update = NULL;
	g_autoptr(GError) local_error = NULL;

	if (result != NULL &&
	    !gs_metered_block_on_download_scheduler_finish (result, &data->schedule_entry_handle, &local_error)) {
		g_warning ("Failed to block on download scheduler: %s",
			   local_error->message);
		g_clear_error (&local_error);
	}

	/* get the list of packages to update */

	/* never refresh the metadata here as this can surprise the frontend if
	 * we end up downloading a different set of packages than what was
	 * shown to the user */
	task_update = gs_packagekit_task_new (GS_PLUGIN (self));
	pk_task_set_only_download (task_update, TRUE);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_update),
				  GS_PACKAGEKIT_TASK_QUESTION_TYPE_DOWNLOAD,
				  data->interactive);

	pk_client_get_updates_async (PK_CLIENT (task_update),
				     pk_bitfield_value (PK_FILTER_ENUM_NONE),
				     cancellable,
				     gs_packagekit_helper_cb, data->helper,
				     download_get_updates_cb,
				     g_steal_pointer (&task));
}

static gboolean
update_system_filter_cb (PkPackage *package,
			 gpointer user_data)
{
	PkInfoEnum info = pk_package_get_info (package);
#if PK_CHECK_VERSION(1, 3, 0)
	return info != PK_INFO_ENUM_OBSOLETE &&
	       info != PK_INFO_ENUM_OBSOLETING &&
	       info != PK_INFO_ENUM_REMOVE &&
	       info != PK_INFO_ENUM_REMOVING &&
	       info != PK_INFO_ENUM_BLOCKED;
#else
	return info != PK_INFO_ENUM_OBSOLETING && info != PK_INFO_ENUM_REMOVING && info != PK_INFO_ENUM_BLOCKED;
#endif
}

static void
download_get_updates_cb (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
	PkTask *task_update = PK_TASK (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	DownloadData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkPackageSack) sack = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (PK_CLIENT (task_update), result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, cancellable, &local_error)) {
		if (local_error->domain == PK_CLIENT_ERROR) {
			g_autoptr(GsPluginEvent) event = NULL;

			event = gs_plugin_event_new ("error", local_error,
						     NULL);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			if (data->interactive)
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
			if (data->event_callback != NULL)
				data->event_callback (g_task_get_source_object (task), event, data->event_user_data);
		}
		finish_download (task, g_steal_pointer (&local_error));
		return;
	}

	/* download all the packages */
	sack = pk_results_get_package_sack (results);
	if (pk_package_sack_get_size (sack) == 0) {
		finish_download (task, NULL);
		return;
	}

	/* Include only packages which are not to be obsoleted nor removed,
	   because these can cause failure due to unmet dependencies. */
	pk_package_sack_remove_by_filter (sack, update_system_filter_cb, NULL);

	package_ids = pk_package_sack_get_ids (sack);
	for (guint i = 0; i < gs_app_list_length (data->download_list); i++) {
		GsApp *app = gs_app_list_index (data->download_list, i);
		gs_packagekit_helper_add_app (data->helper, app);
	}
	gs_packagekit_helper_set_progress_list (data->helper, data->progress_list);

	/* make 'download' operation always low priority, so other interactive
	   operations get to run when a download is in progress */
	pk_client_set_background (PK_CLIENT (task_update), TRUE);

	/* never refresh the metadata here as this can surprise the frontend if
	 * we end up downloading a different set of packages than what was
	 * shown to the user */
	pk_task_update_packages_async (task_update,
				       package_ids,
				       cancellable,
				       gs_packagekit_helper_cb, data->helper,
				       download_update_packages_cb,
				       g_steal_pointer (&task));
}

static void
download_update_packages_cb (GObject      *source_object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
	PkTask *task_update = PK_TASK (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	DownloadData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_task_generic_finish (task_update, result, &local_error);

	gs_app_list_override_progress (data->progress_list, GS_APP_PROGRESS_UNKNOWN);
	if (results == NULL) {
		if (local_error->domain == PK_CLIENT_ERROR) {
			g_autoptr(GsPluginEvent) event = NULL;

			event = gs_plugin_event_new ("error", local_error,
						     NULL);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			if (data->interactive)
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
			if (data->event_callback != NULL)
				data->event_callback (g_task_get_source_object (task), event, data->event_user_data);
		}
		gs_plugin_packagekit_error_convert (&local_error, cancellable);
		finish_download (task, g_steal_pointer (&local_error));
		return;
	}

	if (g_cancellable_set_error_if_cancelled (cancellable, &local_error)) {
		finish_download (task, g_steal_pointer (&local_error));
		return;
	}

	for (guint i = 0; i < gs_app_list_length (data->download_list); i++) {
		GsApp *app = gs_app_list_index (data->download_list, i);
		/* To indicate the app is already downloaded */
		gs_app_set_size_download (app, GS_SIZE_TYPE_VALID, 0);
	}

	/* Success! */
	finish_download (task, NULL);
}

/* If non-%NULL, @error is (transfer full). */
static void
finish_download (GTask  *task,
                 GError *error)
{
	GsPluginPackagekit *self = g_task_get_source_object (task);
	DownloadData *data = g_task_get_task_data (task);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);

	/* Fire this call off into the void, it’s not worth tracking it.
	 * Don’t pass a cancellable in, as the download may have been cancelled. */
	if (data->schedule_entry_handle != NULL)
		gs_metered_remove_from_download_scheduler_async (data->schedule_entry_handle, NULL, NULL, NULL);

	if (error_owned == NULL)
		gs_plugin_updates_changed (GS_PLUGIN (self));

	if (error_owned != NULL)
		g_task_return_error (task, g_steal_pointer (&error_owned));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_packagekit_download_finish (GsPluginPackagekit  *self,
                                      GAsyncResult        *result,
                                      GError             **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void update_apps_download_cb (GObject      *source_object,
                                     GAsyncResult *result,
                                     gpointer      user_data);
static void update_apps_trigger_cb (GObject      *source_object,
                                    GAsyncResult *result,
                                    gpointer      user_data);

static void
gs_plugin_packagekit_update_apps_async (GsPlugin                           *plugin,
                                        GsAppList                          *apps,
                                        GsPluginUpdateAppsFlags             flags,
                                        GsPluginProgressCallback            progress_callback,
                                        gpointer                            progress_user_data,
                                        GsPluginEventCallback               event_callback,
                                        void                               *event_user_data,
                                        GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                        gpointer                            app_needs_user_action_data,
                                        GCancellable                       *cancellable,
                                        GAsyncReadyCallback                 callback,
                                        gpointer                            user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE);

	task = gs_plugin_update_apps_data_new_task (plugin, apps, flags,
						    progress_callback, progress_user_data,
						    event_callback, event_user_data,
						    app_needs_user_action_callback, app_needs_user_action_data,
						    cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_update_apps_async);

	if (!(flags & GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD)) {
		/* FIXME: Add progress reporting */
		gs_plugin_packagekit_download_async (self, apps, interactive,
						     event_callback, event_user_data,
						     cancellable, update_apps_download_cb, g_steal_pointer (&task));
	} else {
		update_apps_download_cb (G_OBJECT (self), NULL, g_steal_pointer (&task));
	}
}

static void
update_apps_download_cb (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginUpdateAppsData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	gboolean interactive = (data->flags & GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE);
	g_autoptr(GError) local_error = NULL;

	if (result != NULL &&
	    !gs_plugin_packagekit_download_finish (self, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	if (!(data->flags & GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY)) {
		gboolean trigger_update = FALSE;

		/* Are any of these apps from PackageKit, and suitable for offline
		 * updates? If any of them can be processed offline, trigger an offline
		 * update. If all of them are updatable online, don’t. */
		for (guint i = 0; i < gs_app_list_length (data->apps); i++) {
			GsApp *app = gs_app_list_index (data->apps, i);
			GsAppList *related = gs_app_get_related (app);

			/* try to trigger this app */
			if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_PROXY) &&
			    gs_app_get_state (app) == GS_APP_STATE_UPDATABLE &&
			    gs_app_has_management_plugin (app, GS_PLUGIN (self))) {
				trigger_update = TRUE;
				break;
			}

			/* try to trigger each related app */
			for (guint j = 0; j < gs_app_list_length (related); j++) {
				GsApp *app_tmp = gs_app_list_index (related, j);

				if (gs_app_get_state (app_tmp) == GS_APP_STATE_UPDATABLE &&
				    gs_app_has_management_plugin (app_tmp, GS_PLUGIN (self))) {
					trigger_update = TRUE;
					break;
				}
			}
		}

		if (trigger_update && !self->is_triggered) {
			GDBusConnection *connection;

			/* trigger offline update if it’s not already been triggered */

			/* Assume we can use the singleton system bus connection
			 * due to prior PackageKit calls having created it. This
			 * avoids an async callback. */
			connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
						     cancellable,
						     &local_error);
			if (connection == NULL) {
				g_task_return_error (task, g_steal_pointer (&local_error));
				return;
			}

			/* FIXME: This can be simplified down to a call to
			 * pk_offline_trigger_with_flags_async() when it exists.
			 * See https://github.com/PackageKit/PackageKit/issues/605 */
			g_dbus_connection_call (connection,
						"org.freedesktop.PackageKit",
						"/org/freedesktop/PackageKit",
						"org.freedesktop.PackageKit.Offline",
						"Trigger",
						g_variant_new ("(s)", pk_offline_action_to_string (PK_OFFLINE_ACTION_REBOOT)),
						NULL,
						interactive ? G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION : G_DBUS_CALL_FLAGS_NONE,
						-1,
						cancellable,
						update_apps_trigger_cb,
						g_steal_pointer (&task));
			return;
		}
	}

	g_task_return_boolean (task, TRUE);
}

static void
update_apps_trigger_cb (GObject      *source_object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
	GDBusConnection *connection = G_DBUS_CONNECTION (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginPackagekit *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) local_error = NULL;

	if (!g_dbus_connection_call_finish (connection, result, &local_error)) {
		gs_plugin_packagekit_error_convert (&local_error, cancellable);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* don't rely on the file monitor */
	gs_plugin_packagekit_refresh_is_triggered (self, cancellable);

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_packagekit_update_apps_finish (GsPlugin      *plugin,
                                         GAsyncResult  *result,
                                         GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void refresh_metadata_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);

static void
gs_plugin_packagekit_refresh_metadata_async (GsPlugin                     *plugin,
                                             guint64                       cache_age_secs,
                                             GsPluginRefreshMetadataFlags  flags,
                                             GsPluginEventCallback         event_callback,
                                             void                         *event_user_data,
                                             GCancellable                 *cancellable,
                                             GAsyncReadyCallback           callback,
                                             gpointer                      user_data)
{
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));
	gboolean interactive = (flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE);
	g_autoptr(GTask) task = NULL;
	g_autoptr(PkTask) task_refresh = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_refresh_metadata_async);
	g_task_set_task_data (task, g_object_ref (helper), g_object_unref);

	gs_packagekit_helper_set_progress_app (helper, app_dl);

	task_refresh = gs_packagekit_task_new (plugin);
	pk_task_set_only_download (task_refresh, TRUE);
	gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_refresh), GS_PACKAGEKIT_TASK_QUESTION_TYPE_NONE, interactive);
	pk_client_set_cache_age (PK_CLIENT (task_refresh), cache_age_secs);

	/* refresh the metadata */
	pk_client_refresh_cache_async (PK_CLIENT (task_refresh),
				       FALSE /* force */,
				       cancellable,
				       gs_packagekit_helper_cb, helper,
				       refresh_metadata_cb, g_steal_pointer (&task));
}

static void
refresh_metadata_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPlugin *plugin = g_task_get_source_object (task);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, g_task_get_cancellable (task), &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
	} else {
		gs_plugin_updates_changed (plugin);
		g_task_return_boolean (task, TRUE);
	}
}

static gboolean
gs_plugin_packagekit_refresh_metadata_finish (GsPlugin      *plugin,
                                              GAsyncResult  *result,
                                              GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void gs_packagekit_cancel_offline_update_thread (GTask        *task,
                                                        gpointer      source_object,
                                                        gpointer      task_data,
                                                        GCancellable *cancellable);

static void
gs_plugin_packagekit_cancel_offline_update_async (GsPlugin                         *plugin,
                                                  GsPluginCancelOfflineUpdateFlags  flags,
                                                  GCancellable                     *cancellable,
                                                  GAsyncReadyCallback               callback,
                                                  gpointer                          user_data)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (plugin);
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_cancel_offline_update_data_new_task (plugin, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_cancel_offline_update_async);

	/* already in correct state */
	if (!self->is_triggered) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* There is no async API in the pk-offline, thus run in a thread */
	g_task_run_in_thread (task, gs_packagekit_cancel_offline_update_thread);
}

static void
gs_packagekit_cancel_offline_update_thread (GTask        *task,
                                            gpointer      source_object,
                                            gpointer      task_data,
                                            GCancellable *cancellable)
{
	GsPluginPackagekit *self = GS_PLUGIN_PACKAGEKIT (source_object);
	GsPluginCancelOfflineUpdateData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_CANCEL_OFFLINE_UPDATE_FLAGS_INTERACTIVE) != 0;
	g_autoptr(GError) local_error = NULL;

	/* cancel offline update */
	if (!pk_offline_cancel_with_flags (interactive ? PK_OFFLINE_FLAGS_INTERACTIVE : PK_OFFLINE_FLAGS_NONE,
					   cancellable, &local_error)) {
		gs_plugin_packagekit_error_convert (&local_error, cancellable);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* don't rely on the file monitor */
	gs_plugin_packagekit_refresh_is_triggered (self, cancellable);

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_packagekit_cancel_offline_update_finish (GsPlugin      *plugin,
                                                   GAsyncResult  *result,
                                                   GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_packagekit_trigger_upgrade_thread (GTask *task,
				      gpointer source_object,
				      gpointer task_data,
				      GCancellable *cancellable)
{
	GsPluginTriggerUpgradeData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_TRIGGER_UPGRADE_FLAGS_INTERACTIVE) != 0;
	g_autoptr(GError) local_error = NULL;

	if (!pk_offline_trigger_upgrade_with_flags (PK_OFFLINE_ACTION_REBOOT,
						    interactive ? PK_OFFLINE_FLAGS_INTERACTIVE : PK_OFFLINE_FLAGS_NONE,
						    cancellable, &local_error)) {
		gs_app_set_state (data->app, GS_APP_STATE_UPDATABLE);
		gs_plugin_packagekit_error_convert (&local_error, cancellable);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	gs_app_set_state (data->app, GS_APP_STATE_UPDATABLE);

	g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_packagekit_trigger_upgrade_async (GsPlugin                    *plugin,
                                            GsApp                       *app,
                                            GsPluginTriggerUpgradeFlags  flags,
                                            GCancellable                *cancellable,
                                            GAsyncReadyCallback          callback,
                                            gpointer                     user_data)
{
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_trigger_upgrade_data_new_task (plugin, app, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_trigger_upgrade_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	gs_app_set_state (app, GS_APP_STATE_PENDING_INSTALL);

	/* There is no async API in the pk-offline, thus run in a thread */
	g_task_run_in_thread (task, gs_packagekit_trigger_upgrade_thread);
}

static gboolean
gs_plugin_packagekit_trigger_upgrade_finish (GsPlugin      *plugin,
                                             GAsyncResult  *result,
                                             GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_packagekit_class_init (GsPluginPackagekitClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_packagekit_dispose;

	plugin_class->adopt_app = gs_plugin_packagekit_adopt_app;
	plugin_class->setup_async = gs_plugin_packagekit_setup_async;
	plugin_class->setup_finish = gs_plugin_packagekit_setup_finish;
	plugin_class->shutdown_async = gs_plugin_packagekit_shutdown_async;
	plugin_class->shutdown_finish = gs_plugin_packagekit_shutdown_finish;
	plugin_class->refine_async = gs_plugin_packagekit_refine_async;
	plugin_class->refine_finish = gs_plugin_packagekit_refine_finish;
	plugin_class->refresh_metadata_async = gs_plugin_packagekit_refresh_metadata_async;
	plugin_class->refresh_metadata_finish = gs_plugin_packagekit_refresh_metadata_finish;
	plugin_class->list_apps_async = gs_plugin_packagekit_list_apps_async;
	plugin_class->list_apps_finish = gs_plugin_packagekit_list_apps_finish;
	plugin_class->enable_repository_async = gs_plugin_packagekit_enable_repository_async;
	plugin_class->enable_repository_finish = gs_plugin_packagekit_enable_repository_finish;
	plugin_class->disable_repository_async = gs_plugin_packagekit_disable_repository_async;
	plugin_class->disable_repository_finish = gs_plugin_packagekit_disable_repository_finish;
	plugin_class->install_apps_async = gs_plugin_packagekit_install_apps_async;
	plugin_class->install_apps_finish = gs_plugin_packagekit_install_apps_finish;
	plugin_class->uninstall_apps_async = gs_plugin_packagekit_uninstall_apps_async;
	plugin_class->uninstall_apps_finish = gs_plugin_packagekit_uninstall_apps_finish;
	plugin_class->update_apps_async = gs_plugin_packagekit_update_apps_async;
	plugin_class->update_apps_finish = gs_plugin_packagekit_update_apps_finish;
	plugin_class->cancel_offline_update_async = gs_plugin_packagekit_cancel_offline_update_async;
	plugin_class->cancel_offline_update_finish = gs_plugin_packagekit_cancel_offline_update_finish;
	plugin_class->download_upgrade_async = gs_plugin_packagekit_download_upgrade_async;
	plugin_class->download_upgrade_finish = gs_plugin_packagekit_download_upgrade_finish;
	plugin_class->trigger_upgrade_async = gs_plugin_packagekit_trigger_upgrade_async;
	plugin_class->trigger_upgrade_finish = gs_plugin_packagekit_trigger_upgrade_finish;
	plugin_class->launch_async = gs_plugin_packagekit_launch_async;
	plugin_class->launch_finish = gs_plugin_packagekit_launch_finish;
	plugin_class->file_to_app_async = gs_plugin_packagekit_file_to_app_async;
	plugin_class->file_to_app_finish = gs_plugin_packagekit_file_to_app_finish;
	plugin_class->url_to_app_async = gs_plugin_packagekit_url_to_app_async;
	plugin_class->url_to_app_finish = gs_plugin_packagekit_url_to_app_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_PACKAGEKIT;
}
