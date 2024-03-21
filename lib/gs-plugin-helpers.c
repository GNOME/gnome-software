/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-plugin-helpers
 * @short_description: Helpers for storing call closures for #GsPlugin vfuncs
 *
 * The helpers in this file each create a context structure to store the
 * arguments passed to a standard #GsPlugin vfunc.
 *
 * These are intended to be used by plugin implementations to easily create
 * #GTasks for handling #GsPlugin vfunc calls, without all having to write the
 * same code to create a structure to wrap the vfunc arguments.
 *
 * Since: 42
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>

#include "gs-plugin-helpers.h"

/**
 * gs_plugin_refine_data_new:
 * @list: list of #GsApps to refine
 * @job_flags: job flags
 * @refine_flags: refine flags
 *
 * Context data for a call to #GsPluginClass.refine_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 42
 */
GsPluginRefineData *
gs_plugin_refine_data_new (GsAppList              *list,
                           GsPluginRefineJobFlags  job_flags,
                           GsPluginRefineFlags     refine_flags)
{
	g_autoptr(GsPluginRefineData) data = g_new0 (GsPluginRefineData, 1);
	data->list = g_object_ref (list);
	data->job_flags = job_flags;
	data->refine_flags = refine_flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_refine_data_new_task:
 * @source_object: task source object
 * @list: list of #GsApps to refine
 * @job_flags: job flags
 * @refine_flags: refine flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a refine operation with the given arguments. The task
 * data will be set to a #GsPluginRefineData containing the given context.
 *
 * This is essentially a combination of gs_plugin_refine_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 42
 */
GTask *
gs_plugin_refine_data_new_task (gpointer                source_object,
                                GsAppList              *list,
                                GsPluginRefineJobFlags  job_flags,
                                GsPluginRefineFlags     refine_flags,
                                GCancellable           *cancellable,
                                GAsyncReadyCallback     callback,
                                gpointer                user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_refine_data_new (list, job_flags, refine_flags), (GDestroyNotify) gs_plugin_refine_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_refine_data_free:
 * @data: (transfer full): a #GsPluginRefineData
 *
 * Free the given @data.
 *
 * Since: 42
 */
void
gs_plugin_refine_data_free (GsPluginRefineData *data)
{
	g_clear_object (&data->list);
	g_free (data);
}

/**
 * gs_plugin_refresh_metadata_data_new:
 * @cache_age_secs: maximum allowed age of the cache in order for it to remain valid, in seconds
 * @flags: refresh metadata flags
 *
 * Context data for a call to #GsPluginClass.refresh_metadata_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 42
 */
GsPluginRefreshMetadataData *
gs_plugin_refresh_metadata_data_new (guint64                      cache_age_secs,
                                     GsPluginRefreshMetadataFlags flags)
{
	g_autoptr(GsPluginRefreshMetadataData) data = g_new0 (GsPluginRefreshMetadataData, 1);
	data->cache_age_secs = cache_age_secs;
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_refresh_metadata_data_free:
 * @data: (transfer full): a #GsPluginRefreshMetadataData
 *
 * Free the given @data.
 *
 * Since: 42
 */
void
gs_plugin_refresh_metadata_data_free (GsPluginRefreshMetadataData *data)
{
	g_free (data);
}

/**
 * gs_plugin_list_apps_data_new:
 * @query: (nullable) (transfer none): a query to filter apps, or %NULL for
 *   no filtering
 * @flags: list apps flags
 *
 * Context data for a call to #GsPluginClass.list_apps_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 43
 */
GsPluginListAppsData *
gs_plugin_list_apps_data_new (GsAppQuery            *query,
                              GsPluginListAppsFlags  flags)
{
	g_autoptr(GsPluginListAppsData) data = g_new0 (GsPluginListAppsData, 1);
	data->query = (query != NULL) ? g_object_ref (query) : NULL;
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_list_apps_data_new_task:
 * @source_object: task source object
 * @query: (nullable) (transfer none): a query to filter apps, or %NULL for
 *   no filtering
 * @flags: list apps flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a list apps operation with the given arguments. The task
 * data will be set to a #GsPluginListAppsData containing the given context.
 *
 * This is essentially a combination of gs_plugin_list_apps_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 43
 */
GTask *
gs_plugin_list_apps_data_new_task (gpointer               source_object,
                                   GsAppQuery            *query,
                                   GsPluginListAppsFlags  flags,
                                   GCancellable          *cancellable,
                                   GAsyncReadyCallback    callback,
                                   gpointer               user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_list_apps_data_new (query, flags), (GDestroyNotify) gs_plugin_list_apps_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_list_apps_data_free:
 * @data: (transfer full): a #GsPluginListAppsData
 *
 * Free the given @data.
 *
 * Since: 43
 */
void
gs_plugin_list_apps_data_free (GsPluginListAppsData *data)
{
	g_clear_object (&data->query);
	g_free (data);
}

/**
 * gs_plugin_manage_repository_data_new:
 * @repository: (not-nullable) (transfer none): a repository to manage
 * @flags: manage repository flags
 *
 * Common context data for a call to #GsPluginClass.install_repository_async,
 * #GsPluginClass.remove_repository_async, #GsPluginClass.enable_repository_async
 * and #GsPluginClass.disable_repository_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 43
 */
GsPluginManageRepositoryData *
gs_plugin_manage_repository_data_new (GsApp			   *repository,
				      GsPluginManageRepositoryFlags flags)
{
	g_autoptr(GsPluginManageRepositoryData) data = g_new0 (GsPluginManageRepositoryData, 1);
	data->repository = g_object_ref (repository);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_manage_repository_data_new_task:
 * @source_object: task source object
 * @repository: (not-nullable) (transfer none): a repository to manage
 * @flags: manage repository flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a manage repository operation with the given arguments. The task
 * data will be set to a #GsPluginManageRepositoryData containing the given context.
 *
 * This is essentially a combination of gs_plugin_manage_repository_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 43
 */
GTask *
gs_plugin_manage_repository_data_new_task (gpointer			 source_object,
					   GsApp			*repository,
					   GsPluginManageRepositoryFlags flags,
					   GCancellable			*cancellable,
					   GAsyncReadyCallback		 callback,
					   gpointer			 user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_manage_repository_data_new (repository, flags), (GDestroyNotify) gs_plugin_manage_repository_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_manage_repository_data_free:
 * @data: (transfer full): a #GsPluginManageRepositoryData
 *
 * Free the given @data.
 *
 * Since: 43
 */
void
gs_plugin_manage_repository_data_free (GsPluginManageRepositoryData *data)
{
	g_clear_object (&data->repository);
	g_free (data);
}

/**
 * gs_plugin_refine_categories_data_new:
 * @list: (element-type GsCategory): list of #GsCategory objects to refine
 * @flags: refine flags
 *
 * Context data for a call to #GsPluginClass.refine_categories_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 43
 */
GsPluginRefineCategoriesData *
gs_plugin_refine_categories_data_new (GPtrArray                     *list,
                                      GsPluginRefineCategoriesFlags  flags)
{
	g_autoptr(GsPluginRefineCategoriesData) data = g_new0 (GsPluginRefineCategoriesData, 1);
	data->list = g_ptr_array_ref (list);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_refine_categories_data_new_task:
 * @source_object: task source object
 * @list: (element-type GsCategory): list of #GsCategory objects to refine
 * @flags: refine flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a refine categories operation with the given arguments.
 * The task data will be set to a #GsPluginRefineCategoriesData containing the
 * given context.
 *
 * This is essentially a combination of gs_plugin_refine_categories_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 43
 */
GTask *
gs_plugin_refine_categories_data_new_task (gpointer                       source_object,
                                           GPtrArray                     *list,
                                           GsPluginRefineCategoriesFlags  flags,
                                           GCancellable                  *cancellable,
                                           GAsyncReadyCallback            callback,
                                           gpointer                       user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_refine_categories_data_new (list, flags), (GDestroyNotify) gs_plugin_refine_categories_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_refine_categories_data_free:
 * @data: (transfer full): a #GsPluginRefineCategoriesData
 *
 * Free the given @data.
 *
 * Since: 43
 */
void
gs_plugin_refine_categories_data_free (GsPluginRefineCategoriesData *data)
{
	g_clear_pointer (&data->list, g_ptr_array_unref);
	g_free (data);
}

/**
 * gs_plugin_update_apps_data_new:
 * @apps: list of apps to update
 * @flags: update flags
 *
 * Context data for a call to #GsPluginClass.update_apps_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 44
 */
GsPluginUpdateAppsData *
gs_plugin_update_apps_data_new (GsAppList                          *apps,
                                GsPluginUpdateAppsFlags             flags,
                                GsPluginProgressCallback            progress_callback,
                                gpointer                            progress_user_data,
                                GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                gpointer                            app_needs_user_action_data)
{
	g_autoptr(GsPluginUpdateAppsData) data = g_new0 (GsPluginUpdateAppsData, 1);
	data->apps = g_object_ref (apps);
	data->flags = flags;
	data->progress_callback = progress_callback;
	data->progress_user_data = progress_user_data;
	data->app_needs_user_action_callback = app_needs_user_action_callback;
	data->app_needs_user_action_data = app_needs_user_action_data;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_update_apps_data_new_task:
 * @source_object: task source object
 * @apps: list of apps to update
 * @flags: update flags
 * @progress_callback: (nullable): function to call to notify of progress
 * @progress_user_data: data to pass to @progress_callback
 * @app_needs_user_action_callback: (nullable): function to call to ask the
 *   user for a decision
 * @app_needs_user_action_data: data to pass to @app_needs_user_action_callback
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for an update apps operation with the given arguments.
 * The task data will be set to a #GsPluginUpdateAppsData containing the
 * given context.
 *
 * This is essentially a combination of gs_plugin_update_apps_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 44
 */
GTask *
gs_plugin_update_apps_data_new_task (gpointer                            source_object,
                                     GsAppList                          *apps,
                                     GsPluginUpdateAppsFlags             flags,
                                     GsPluginProgressCallback            progress_callback,
                                     gpointer                            progress_user_data,
                                     GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                     gpointer                            app_needs_user_action_data,
                                     GCancellable                       *cancellable,
                                     GAsyncReadyCallback                 callback,
                                     gpointer                            user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task,
			      gs_plugin_update_apps_data_new (apps,
							      flags,
							      progress_callback,
							      progress_user_data,
							      app_needs_user_action_callback,
							      app_needs_user_action_data),
			      (GDestroyNotify) gs_plugin_update_apps_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_update_apps_data_free:
 * @data: (transfer full): a #GsPluginUpdateAppsData
 *
 * Free the given @data.
 *
 * Since: 44
 */
void
gs_plugin_update_apps_data_free (GsPluginUpdateAppsData *data)
{
	g_clear_object (&data->apps);
	g_free (data);
}

/**
 * gs_plugin_manage_app_data_new:
 * @app: (not-nullable) (transfer none): an app to manage
 * @flags: manage app flags
 *
 * Common context data for a call to #GsPluginClass.install_app_async,
 * #GsPluginClass.remove_app_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 47
 */
GsPluginManageAppData *
gs_plugin_manage_app_data_new (GsApp		      *app,
			       GsPluginManageAppFlags  flags)
{
	g_autoptr(GsPluginManageAppData) data = g_new0 (GsPluginManageAppData, 1);
	data->app = g_object_ref (app);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_manage_app_data_new_task:
 * @source_object: task source object
 * @app: (not-nullable) (transfer none): an app to manage
 * @flags: manage app flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a manage app operation with the given arguments. The task
 * data will be set to a #GsPluginManageAppData containing the given context.
 *
 * This is essentially a combination of gs_plugin_manage_app_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 47
 */
GTask *
gs_plugin_manage_app_data_new_task (gpointer		   source_object,
				    GsApp		  *app,
				    GsPluginManageAppFlags flags,
				    GCancellable	  *cancellable,
				    GAsyncReadyCallback	   callback,
				    gpointer		   user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_manage_app_data_new (app, flags), (GDestroyNotify) gs_plugin_manage_app_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_manage_app_data_free:
 * @data: (transfer full): a #GsPluginManageAppData
 *
 * Free the given @data.
 *
 * Since: 47
 */
void
gs_plugin_manage_app_data_free (GsPluginManageAppData *data)
{
	g_clear_object (&data->app);
	g_free (data);
}

/**
 * gs_plugin_update_cancel_data_new:
 * @app: (nullable) (transfer none): an app to cancel offline update on, or %NULL for all plugins
 * @flags: operation flags
 *
 * Common context data for a call to #GsPluginClass.update_cancel_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 47
 */
GsPluginUpdateCancelData *
gs_plugin_update_cancel_data_new (GsApp                     *app,
				  GsPluginUpdateCancelFlags  flags)
{
	g_autoptr(GsPluginUpdateCancelData) data = g_new0 (GsPluginUpdateCancelData, 1);
	data->app = app == NULL ? NULL : g_object_ref (app);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_update_cancel_data_new_task:
 * @source_object: task source object
 * @app: (nullable) (transfer none): an app to cancel offline update on, or %NULL for all plugins
 * @flags: operation flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for an update-cancel operation with the given arguments. The task
 * data will be set to a #GsPluginUpdateCancelData containing the given context.
 *
 * This is essentially a combination of gs_plugin_update_cancel_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 47
 */
GTask *
gs_plugin_update_cancel_data_new_task (gpointer                  source_object,
				       GsApp                    *app,
				       GsPluginUpdateCancelFlags flags,
				       GCancellable             *cancellable,
				       GAsyncReadyCallback       callback,
				       gpointer                  user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_update_cancel_data_new (app, flags), (GDestroyNotify) gs_plugin_update_cancel_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_update_cancel_data_free:
 * @data: (transfer full): a #GsPluginUpdateCancelData
 *
 * Free the given @data.
 *
 * Since: 47
 */
void
gs_plugin_update_cancel_data_free (GsPluginUpdateCancelData *data)
{
	g_clear_object (&data->app);
	g_free (data);
}

/**
 * gs_plugin_upgrade_download_data_new:
 * @app: (not-nullable) (transfer none): a #GsApp, with kind %AS_COMPONENT_KIND_OPERATING_SYSTEM
 * @flags: operation flags
 *
 * Common context data for a call to #GsPluginClass.upgrade_download_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 47
 */
GsPluginUpgradeDownloadData *
gs_plugin_upgrade_download_data_new (GsApp                        *app,
				     GsPluginUpgradeDownloadFlags  flags)
{
	g_autoptr(GsPluginUpgradeDownloadData) data = g_new0 (GsPluginUpgradeDownloadData, 1);
	data->app = g_object_ref (app);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_upgrade_download_data_new_task:
 * @source_object: task source object
 * @app: (not-nullable) (transfer none): a #GsApp, with kind %AS_COMPONENT_KIND_OPERATING_SYSTEM
 * @flags: operation flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for an upgrade-download operation with the given arguments. The task
 * data will be set to a #GsPluginUpgradeDownloadData containing the given context.
 *
 * This is essentially a combination of gs_plugin_upgrade_download_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 47
 */
GTask *
gs_plugin_upgrade_download_data_new_task (gpointer                     source_object,
					  GsApp                       *app,
					  GsPluginUpgradeDownloadFlags flags,
					  GCancellable                *cancellable,
					  GAsyncReadyCallback          callback,
					  gpointer                     user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_upgrade_download_data_new (app, flags), (GDestroyNotify) gs_plugin_upgrade_download_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_upgrade_download_data_free:
 * @data: (transfer full): a #GsPluginUpgradeDownloadData
 *
 * Free the given @data.
 *
 * Since: 47
 */
void
gs_plugin_upgrade_download_data_free (GsPluginUpgradeDownloadData *data)
{
	g_clear_object (&data->app);
	g_free (data);
}

/**
 * gs_plugin_upgrade_trigger_data_new:
 * @app: (not-nullable) (transfer none): a #GsApp, with kind %AS_COMPONENT_KIND_OPERATING_SYSTEM
 * @flags: operation flags
 *
 * Common context data for a call to #GsPluginClass.upgrade_trigger_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 47
 */
GsPluginUpgradeTriggerData *
gs_plugin_upgrade_trigger_data_new (GsApp                      *app,
				    GsPluginUpgradeTriggerFlags flags)
{
	g_autoptr(GsPluginUpgradeTriggerData) data = g_new0 (GsPluginUpgradeTriggerData, 1);
	data->app = g_object_ref (app);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_upgrade_trigger_data_new_task:
 * @source_object: task source object
 * @app: (not-nullable) (transfer none): a #GsApp, with kind %AS_COMPONENT_KIND_OPERATING_SYSTEM
 * @flags: operation flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for an upgrade-trigger operation with the given arguments. The task
 * data will be set to a #GsPluginUpgradeTriggerData containing the given context.
 *
 * This is essentially a combination of gs_plugin_upgrade_trigger_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 47
 */
GTask *
gs_plugin_upgrade_trigger_data_new_task (gpointer                    source_object,
					 GsApp                      *app,
					 GsPluginUpgradeTriggerFlags flags,
					 GCancellable               *cancellable,
					 GAsyncReadyCallback         callback,
					 gpointer                    user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_upgrade_trigger_data_new (app, flags), (GDestroyNotify) gs_plugin_upgrade_trigger_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_upgrade_trigger_data_free:
 * @data: (transfer full): a #GsPluginUpgradeTriggerData
 *
 * Free the given @data.
 *
 * Since: 47
 */
void
gs_plugin_upgrade_trigger_data_free (GsPluginUpgradeTriggerData *data)
{
	g_clear_object (&data->app);
	g_free (data);
}

/**
 * gs_plugin_launch_data_new:
 * @app: (not-nullable) (transfer none): a #GsApp
 * @flags: operation flags
 *
 * Common context data for a call to #GsPluginClass.launch_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 47
 */
GsPluginLaunchData *
gs_plugin_launch_data_new (GsApp              *app,
			   GsPluginLaunchFlags flags)
{
	g_autoptr(GsPluginLaunchData) data = g_new0 (GsPluginLaunchData, 1);
	data->app = g_object_ref (app);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_launch_data_new_task:
 * @source_object: task source object
 * @app: (not-nullable) (transfer none): a #GsApp
 * @flags: operation flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a launch operation with the given arguments. The task
 * data will be set to a #GsPluginLaunchData containing the given context.
 *
 * This is essentially a combination of gs_plugin_launch_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 47
 */
GTask *
gs_plugin_launch_data_new_task (gpointer            source_object,
				GsApp              *app,
				GsPluginLaunchFlags flags,
				GCancellable        *cancellable,
				GAsyncReadyCallback  callback,
				gpointer             user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_launch_data_new (app, flags), (GDestroyNotify) gs_plugin_launch_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_launch_data_free:
 * @data: (transfer full): a #GsPluginLaunchData
 *
 * Free the given @data.
 *
 * Since: 47
 */
void
gs_plugin_launch_data_free (GsPluginLaunchData *data)
{
	g_clear_object (&data->app);
	g_free (data);
}

/**
 * gs_plugin_file_to_app_data_new:
 * @file: (not-nullable) (transfer none): a #GFile
 * @flags: operation flags
 *
 * Common context data for a call to #GsPluginClass.file_to_app_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 47
 */
GsPluginFileToAppData *
gs_plugin_file_to_app_data_new (GFile                 *file,
				GsPluginFileToAppFlags flags)
{
	g_autoptr(GsPluginFileToAppData) data = g_new0 (GsPluginFileToAppData, 1);
	data->file = g_object_ref (file);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_file_to_app_data_new_task:
 * @source_object: task source object
 * @file: (not-nullable) (transfer none): a #GFile
 * @flags: operation flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a file-to-app operation with the given arguments. The task
 * data will be set to a #GsPluginFileToAppData containing the given context.
 *
 * This is essentially a combination of gs_plugin_file_to_app_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 47
 */
GTask *
gs_plugin_file_to_app_data_new_task (gpointer               source_object,
				     GFile                 *file,
				     GsPluginFileToAppFlags flags,
				     GCancellable          *cancellable,
				     GAsyncReadyCallback    callback,
				     gpointer               user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_file_to_app_data_new (file, flags), (GDestroyNotify) gs_plugin_file_to_app_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_file_to_app_data_free:
 * @data: (transfer full): a #GsPluginFileToAppData
 *
 * Free the given @data.
 *
 * Since: 47
 */
void
gs_plugin_file_to_app_data_free (GsPluginFileToAppData *data)
{
	g_clear_object (&data->file);
	g_free (data);
}

/**
 * gs_plugin_url_to_app_data_new:
 * @url: (not-nullable): a URL
 * @flags: operation flags
 *
 * Common context data for a call to #GsPluginClass.url_to_app_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 47
 */
GsPluginUrlToAppData *
gs_plugin_url_to_app_data_new (const gchar          *url,
			       GsPluginUrlToAppFlags flags)
{
	g_autoptr(GsPluginUrlToAppData) data = g_new0 (GsPluginUrlToAppData, 1);
	data->url = g_strdup (url);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_url_to_app_data_new_task:
 * @source_object: task source object
 * @url: (not-nullable): a URL
 * @flags: operation flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a url-to-app operation with the given arguments. The task
 * data will be set to a #GsPluginUrlToAppData containing the given context.
 *
 * This is essentially a combination of gs_plugin_url_to_app_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 47
 */
GTask *
gs_plugin_url_to_app_data_new_task (gpointer              source_object,
				    const gchar          *url,
				    GsPluginUrlToAppFlags flags,
				    GCancellable         *cancellable,
				    GAsyncReadyCallback   callback,
				    gpointer              user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_url_to_app_data_new (url, flags), (GDestroyNotify) gs_plugin_url_to_app_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_url_to_app_data_free:
 * @data: (transfer full): a #GsPluginUrlToAppData
 *
 * Free the given @data.
 *
 * Since: 47
 */
void
gs_plugin_url_to_app_data_free (GsPluginUrlToAppData *data)
{
	g_free (data->url);
	g_free (data);
}

/**
 * gs_plugin_get_langpacks_data_new:
 * @locale: (not-nullable): a #LANGUAGE_CODE or #LOCALE, e.g. "ja" or "ja_JP"
 * @flags: operation flags
 *
 * Common context data for a call to #GsPluginClass.get_langpacks_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 47
 */
GsPluginGetLangpacksData *
gs_plugin_get_langpacks_data_new (const gchar              *locale,
				  GsPluginGetLangpacksFlags flags)
{
	g_autoptr(GsPluginGetLangpacksData) data = g_new0 (GsPluginGetLangpacksData, 1);
	data->locale = g_strdup (locale);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_get_langpacks_data_new_task:
 * @source_object: task source object
 * @locale: (not-nullable): a #LANGUAGE_CODE or #LOCALE, e.g. "ja" or "ja_JP"
 * @flags: operation flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a get-langpacks operation with the given arguments. The task
 * data will be set to a #GsPluginGetLangpacksData containing the given context.
 *
 * This is essentially a combination of gs_plugin_get_langpacks_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 47
 */
GTask *
gs_plugin_get_langpacks_data_new_task (gpointer                  source_object,
				       const gchar              *locale,
				       GsPluginGetLangpacksFlags flags,
				       GCancellable             *cancellable,
				       GAsyncReadyCallback       callback,
				       gpointer                  user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_get_langpacks_data_new (locale, flags), (GDestroyNotify) gs_plugin_get_langpacks_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_get_langpacks_data_free:
 * @data: (transfer full): a #GsPluginGetLangpacksData
 *
 * Free the given @data.
 *
 * Since: 47
 */
void
gs_plugin_get_langpacks_data_free (GsPluginGetLangpacksData *data)
{
	g_free (data->locale);
	g_free (data);
}
