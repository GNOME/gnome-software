/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

#include <gnome-software.h>

G_BEGIN_DECLS

typedef struct {
	GsAppList *list;  /* (owned) (not nullable) */
	GsPluginRefineFlags flags;
} GsPluginRefineData;

GsPluginRefineData *gs_plugin_refine_data_new (GsAppList           *list,
                                               GsPluginRefineFlags  flags);
GTask *gs_plugin_refine_data_new_task (gpointer             source_object,
                                       GsAppList           *list,
                                       GsPluginRefineFlags  flags,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data);
void gs_plugin_refine_data_free (GsPluginRefineData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginRefineData, gs_plugin_refine_data_free)

typedef struct {
	guint64 cache_age_secs;
	GsPluginRefreshMetadataFlags flags;
} GsPluginRefreshMetadataData;

GsPluginRefreshMetadataData *gs_plugin_refresh_metadata_data_new (guint64                      cache_age_secs,
                                                                  GsPluginRefreshMetadataFlags flags);
void gs_plugin_refresh_metadata_data_free (GsPluginRefreshMetadataData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginRefreshMetadataData, gs_plugin_refresh_metadata_data_free)

typedef struct {
	GsAppQuery *query;  /* (owned) (nullable) */
	GsPluginListAppsFlags flags;
} GsPluginListAppsData;

GsPluginListAppsData *gs_plugin_list_apps_data_new (GsAppQuery            *query,
                                                    GsPluginListAppsFlags  flags);
GTask *gs_plugin_list_apps_data_new_task (gpointer               source_object,
                                          GsAppQuery            *query,
                                          GsPluginListAppsFlags  flags,
                                          GCancellable          *cancellable,
                                          GAsyncReadyCallback    callback,
                                          gpointer               user_data);
void gs_plugin_list_apps_data_free (GsPluginListAppsData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginListAppsData, gs_plugin_list_apps_data_free)

typedef struct {
	GsApp *repository;  /* (owned) (nullable) */
	GsPluginManageRepositoryFlags flags;
} GsPluginManageRepositoryData;

GsPluginManageRepositoryData *
		gs_plugin_manage_repository_data_new		(GsApp				*repository,
								 GsPluginManageRepositoryFlags   flags);
GTask *		gs_plugin_manage_repository_data_new_task	(gpointer			 source_object,
								 GsApp				*repository,
								 GsPluginManageRepositoryFlags	 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
void		gs_plugin_manage_repository_data_free		(GsPluginManageRepositoryData	*data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginManageRepositoryData, gs_plugin_manage_repository_data_free)

typedef struct {
	GPtrArray *list;  /* (element-type GsCategory) (owned) (not nullable) */
	GsPluginRefineCategoriesFlags flags;
} GsPluginRefineCategoriesData;

GsPluginRefineCategoriesData *gs_plugin_refine_categories_data_new (GPtrArray                     *list,
                                                                    GsPluginRefineCategoriesFlags  flags);
GTask *gs_plugin_refine_categories_data_new_task (gpointer                       source_object,
                                                  GPtrArray                     *list,
                                                  GsPluginRefineCategoriesFlags  flags,
                                                  GCancellable                  *cancellable,
                                                  GAsyncReadyCallback            callback,
                                                  gpointer                       user_data);
void gs_plugin_refine_categories_data_free (GsPluginRefineCategoriesData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginRefineCategoriesData, gs_plugin_refine_categories_data_free)

typedef struct {
	GsAppList *apps;  /* (owned) (not nullable) */
	GsPluginUpdateAppsFlags flags;
	GsPluginProgressCallback progress_callback;
	gpointer progress_user_data;
	GsPluginAppNeedsUserActionCallback app_needs_user_action_callback;
	gpointer app_needs_user_action_data;
} GsPluginUpdateAppsData;

GsPluginUpdateAppsData *gs_plugin_update_apps_data_new (GsAppList                          *apps,
                                                        GsPluginUpdateAppsFlags             flags,
                                                        GsPluginProgressCallback            progress_callback,
                                                        gpointer                            progress_user_data,
                                                        GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                                        gpointer                            app_needs_user_action_data);
GTask *gs_plugin_update_apps_data_new_task (gpointer                            source_object,
                                            GsAppList                          *apps,
                                            GsPluginUpdateAppsFlags             flags,
                                            GsPluginProgressCallback            progress_callback,
                                            gpointer                            progress_user_data,
                                            GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                            gpointer                            app_needs_user_action_data,
                                            GCancellable                       *cancellable,
                                            GAsyncReadyCallback                 callback,
                                            gpointer                            user_data);
void gs_plugin_update_apps_data_free (GsPluginUpdateAppsData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginUpdateAppsData, gs_plugin_update_apps_data_free)

typedef struct {
	GsApp *app;  /* (owned) (nullable) */
	GsPluginManageAppFlags flags;
} GsPluginManageAppData;

GsPluginManageAppData *
		gs_plugin_manage_app_data_new		(GsApp			*app,
							 GsPluginManageAppFlags  flags);
GTask *		gs_plugin_manage_app_data_new_task	(gpointer		 source_object,
							 GsApp			*app,
							 GsPluginManageAppFlags	 flags,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
void		gs_plugin_manage_app_data_free		(GsPluginManageAppData	*data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginManageAppData, gs_plugin_manage_app_data_free)

typedef struct {
	GsApp *app;  /* (owned) (nullable) */
	GsPluginUpdateCancelFlags flags;
} GsPluginUpdateCancelData;

GsPluginUpdateCancelData *
		gs_plugin_update_cancel_data_new	(GsApp			  *app,
							 GsPluginUpdateCancelFlags flags);
GTask *		gs_plugin_update_cancel_data_new_task	(gpointer		   source_object,
							 GsApp			  *app,
							 GsPluginUpdateCancelFlags flags,
							 GCancellable		  *cancellable,
							 GAsyncReadyCallback	   callback,
							 gpointer		   user_data);
void		gs_plugin_update_cancel_data_free	(GsPluginUpdateCancelData	*data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginUpdateCancelData, gs_plugin_update_cancel_data_free)

typedef struct {
	GsApp *app;  /* (owned) (nullable) */
	GsPluginUpgradeDownloadFlags flags;
} GsPluginUpgradeDownloadData;

GsPluginUpgradeDownloadData *
		gs_plugin_upgrade_download_data_new	(GsApp			     *app,
							 GsPluginUpgradeDownloadFlags flags);
GTask *		gs_plugin_upgrade_download_data_new_task(gpointer		      source_object,
							 GsApp			     *app,
							 GsPluginUpgradeDownloadFlags flags,
							 GCancellable		     *cancellable,
							 GAsyncReadyCallback	      callback,
							 gpointer		      user_data);
void		gs_plugin_upgrade_download_data_free	(GsPluginUpgradeDownloadData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginUpgradeDownloadData, gs_plugin_upgrade_download_data_free)

typedef struct {
	GsApp *app;  /* (owned) (nullable) */
	GsPluginUpgradeTriggerFlags flags;
} GsPluginUpgradeTriggerData;

GsPluginUpgradeTriggerData *
		gs_plugin_upgrade_trigger_data_new	(GsApp			    *app,
							 GsPluginUpgradeTriggerFlags flags);
GTask *		gs_plugin_upgrade_trigger_data_new_task	(gpointer		     source_object,
							 GsApp			    *app,
							 GsPluginUpgradeTriggerFlags flags,
							 GCancellable		    *cancellable,
							 GAsyncReadyCallback	     callback,
							 gpointer		     user_data);
void		gs_plugin_upgrade_trigger_data_free	(GsPluginUpgradeTriggerData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginUpgradeTriggerData, gs_plugin_upgrade_trigger_data_free)

typedef struct {
	GsApp *app;  /* (owned) (nullable) */
	GsPluginLaunchFlags flags;
} GsPluginLaunchData;

GsPluginLaunchData *
		gs_plugin_launch_data_new		(GsApp			    *app,
							 GsPluginLaunchFlags	     flags);
GTask *		gs_plugin_launch_data_new_task		(gpointer		     source_object,
							 GsApp			    *app,
							 GsPluginLaunchFlags	     flags,
							 GCancellable		    *cancellable,
							 GAsyncReadyCallback	     callback,
							 gpointer		     user_data);
void		gs_plugin_launch_data_free		(GsPluginLaunchData	    *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginLaunchData, gs_plugin_launch_data_free)

typedef struct {
	GFile *file;  /* (owned) */
	GsPluginFileToAppFlags flags;
} GsPluginFileToAppData;

GsPluginFileToAppData *
		gs_plugin_file_to_app_data_new		(GFile			    *file,
							 GsPluginFileToAppFlags	     flags);
GTask *		gs_plugin_file_to_app_data_new_task	(gpointer		     source_object,
							 GFile			    *file,
							 GsPluginFileToAppFlags	     flags,
							 GCancellable		    *cancellable,
							 GAsyncReadyCallback	     callback,
							 gpointer		     user_data);
void		gs_plugin_file_to_app_data_free		(GsPluginFileToAppData	    *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginFileToAppData, gs_plugin_file_to_app_data_free)

typedef struct {
	gchar *url;  /* (owned) */
	GsPluginUrlToAppFlags flags;
} GsPluginUrlToAppData;

GsPluginUrlToAppData *
		gs_plugin_url_to_app_data_new		(const gchar		    *url,
							 GsPluginUrlToAppFlags	     flags);
GTask *		gs_plugin_url_to_app_data_new_task	(gpointer		     source_object,
							 const gchar		    *url,
							 GsPluginUrlToAppFlags	     flags,
							 GCancellable		    *cancellable,
							 GAsyncReadyCallback	     callback,
							 gpointer		     user_data);
void		gs_plugin_url_to_app_data_free		(GsPluginUrlToAppData	    *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginUrlToAppData, gs_plugin_url_to_app_data_free)

typedef struct {
	gchar *locale;  /* (owned) */
	GsPluginGetLangpacksFlags flags;
} GsPluginGetLangpacksData;

GsPluginGetLangpacksData *
		gs_plugin_get_langpacks_data_new	(const gchar		    *locale,
							 GsPluginGetLangpacksFlags   flags);
GTask *		gs_plugin_get_langpacks_data_new_task	(gpointer		     source_object,
							 const gchar		    *locale,
							 GsPluginGetLangpacksFlags   flags,
							 GCancellable		    *cancellable,
							 GAsyncReadyCallback	     callback,
							 gpointer		     user_data);
void		gs_plugin_get_langpacks_data_free	(GsPluginGetLangpacksData   *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginGetLangpacksData, gs_plugin_get_langpacks_data_free)

G_END_DECLS
