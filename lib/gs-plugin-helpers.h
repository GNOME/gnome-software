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
	GsPluginRefineFlags job_flags;
	GsPluginRefineRequireFlags require_flags;
	GsPluginEventCallback event_callback;
	void *event_user_data;
} GsPluginRefineData;

GsPluginRefineData *gs_plugin_refine_data_new (GsAppList                  *list,
                                               GsPluginRefineFlags         job_flags,
                                               GsPluginRefineRequireFlags  require_flags,
                                               GsPluginEventCallback       event_callback,
                                               void                       *event_user_data);
GTask *gs_plugin_refine_data_new_task (gpointer                    source_object,
                                       GsAppList                  *list,
                                       GsPluginRefineFlags         job_flags,
                                       GsPluginRefineRequireFlags  refine_flags,
                                       GsPluginEventCallback       event_callback,
                                       void                       *event_user_data,
                                       GCancellable               *cancellable,
                                       GAsyncReadyCallback         callback,
                                       gpointer                    user_data);
void gs_plugin_refine_data_free (GsPluginRefineData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginRefineData, gs_plugin_refine_data_free)

typedef struct {
	guint64 cache_age_secs;
	GsPluginRefreshMetadataFlags flags;
	GsPluginEventCallback event_callback;
	void *event_user_data;
} GsPluginRefreshMetadataData;

GsPluginRefreshMetadataData *gs_plugin_refresh_metadata_data_new (guint64                       cache_age_secs,
                                                                  GsPluginRefreshMetadataFlags  flags,
                                                                  GsPluginEventCallback         event_callback,
                                                                  void                         *event_user_data);
void gs_plugin_refresh_metadata_data_free (GsPluginRefreshMetadataData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginRefreshMetadataData, gs_plugin_refresh_metadata_data_free)

typedef struct {
	GsAppQuery *query;  /* (owned) (nullable) */
	GsPluginListAppsFlags flags;
	GsPluginEventCallback event_callback;
	void *event_user_data;
} GsPluginListAppsData;

GsPluginListAppsData *gs_plugin_list_apps_data_new (GsAppQuery            *query,
                                                    GsPluginListAppsFlags  flags,
                                                    GsPluginEventCallback  event_callback,
                                                    void                  *event_user_data);
GTask *gs_plugin_list_apps_data_new_task (gpointer               source_object,
                                          GsAppQuery            *query,
                                          GsPluginListAppsFlags  flags,
                                          GsPluginEventCallback  event_callback,
                                          void                  *event_user_data,
                                          GCancellable          *cancellable,
                                          GAsyncReadyCallback    callback,
                                          gpointer               user_data);
void gs_plugin_list_apps_data_free (GsPluginListAppsData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginListAppsData, gs_plugin_list_apps_data_free)

typedef struct {
	GsApp *repository;  /* (owned) (nullable) */
	GsPluginManageRepositoryFlags flags;
	GsPluginEventCallback event_callback;
	void *event_user_data;
} GsPluginManageRepositoryData;

GsPluginManageRepositoryData *
		gs_plugin_manage_repository_data_new		(GsApp				*repository,
								 GsPluginManageRepositoryFlags   flags,
								 GsPluginEventCallback		 event_callback,
								 void				*event_user_data);
GTask *		gs_plugin_manage_repository_data_new_task	(gpointer			 source_object,
								 GsApp				*repository,
								 GsPluginManageRepositoryFlags	 flags,
								 GsPluginEventCallback		 event_callback,
								 void				*event_user_data,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
void		gs_plugin_manage_repository_data_free		(GsPluginManageRepositoryData	*data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginManageRepositoryData, gs_plugin_manage_repository_data_free)

typedef struct {
	GPtrArray *list;  /* (element-type GsCategory) (owned) (not nullable) */
	GsPluginRefineCategoriesFlags flags;
	GsPluginEventCallback event_callback;
	void *event_user_data;
} GsPluginRefineCategoriesData;

GsPluginRefineCategoriesData *gs_plugin_refine_categories_data_new (GPtrArray                     *list,
                                                                    GsPluginRefineCategoriesFlags  flags,
                                                                    GsPluginEventCallback          event_callback,
                                                                    void                          *event_user_data);
GTask *gs_plugin_refine_categories_data_new_task (gpointer                       source_object,
                                                  GPtrArray                     *list,
                                                  GsPluginRefineCategoriesFlags  flags,
                                                  GsPluginEventCallback          event_callback,
                                                  void                          *event_user_data,
                                                  GCancellable                  *cancellable,
                                                  GAsyncReadyCallback            callback,
                                                  gpointer                       user_data);
void gs_plugin_refine_categories_data_free (GsPluginRefineCategoriesData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginRefineCategoriesData, gs_plugin_refine_categories_data_free)

typedef struct {
	GsAppList *apps;  /* (owned) (not nullable) */
	GsPluginInstallAppsFlags flags;
	GsPluginProgressCallback progress_callback;
	gpointer progress_user_data;
	GsPluginEventCallback event_callback;
	void *event_user_data;
	GsPluginAppNeedsUserActionCallback app_needs_user_action_callback;
	gpointer app_needs_user_action_data;
} GsPluginInstallAppsData;

GsPluginInstallAppsData *gs_plugin_install_apps_data_new (GsAppList                          *apps,
                                                          GsPluginInstallAppsFlags            flags,
                                                          GsPluginProgressCallback            progress_callback,
                                                          gpointer                            progress_user_data,
                                                          GsPluginEventCallback               event_callback,
                                                          void                               *event_user_data,
                                                          GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                                          gpointer                            app_needs_user_action_data);
GTask *gs_plugin_install_apps_data_new_task (gpointer                            source_object,
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
                                             gpointer                            user_data);
void gs_plugin_install_apps_data_free (GsPluginInstallAppsData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginInstallAppsData, gs_plugin_install_apps_data_free)

typedef struct {
	GsAppList *apps;  /* (owned) (not nullable) */
	GsPluginUninstallAppsFlags flags;
	GsPluginProgressCallback progress_callback;
	gpointer progress_user_data;
	GsPluginEventCallback event_callback;
	gpointer event_user_data;
	GsPluginAppNeedsUserActionCallback app_needs_user_action_callback;
	gpointer app_needs_user_action_data;
} GsPluginUninstallAppsData;

GsPluginUninstallAppsData *gs_plugin_uninstall_apps_data_new (GsAppList                          *apps,
                                                              GsPluginUninstallAppsFlags          flags,
                                                              GsPluginProgressCallback            progress_callback,
                                                              gpointer                            progress_user_data,
                                                              GsPluginEventCallback               event_callback,
                                                              gpointer                            event_user_data,
                                                              GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                                              gpointer                            app_needs_user_action_data);
GTask *gs_plugin_uninstall_apps_data_new_task (gpointer                            source_object,
                                               GsAppList                          *apps,
                                               GsPluginUninstallAppsFlags          flags,
                                               GsPluginProgressCallback            progress_callback,
                                               gpointer                            progress_user_data,
                                               GsPluginEventCallback               event_callback,
                                               gpointer                            event_user_data,
                                               GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                               gpointer                            app_needs_user_action_data,
                                               GCancellable                       *cancellable,
                                               GAsyncReadyCallback                 callback,
                                               gpointer                            user_data);
void gs_plugin_uninstall_apps_data_free (GsPluginUninstallAppsData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginUninstallAppsData, gs_plugin_uninstall_apps_data_free)

typedef struct {
	GsAppList *apps;  /* (owned) (not nullable) */
	GsPluginUpdateAppsFlags flags;
	GsPluginProgressCallback progress_callback;
	gpointer progress_user_data;
	GsPluginEventCallback event_callback;
	void *event_user_data;
	GsPluginAppNeedsUserActionCallback app_needs_user_action_callback;
	gpointer app_needs_user_action_data;
} GsPluginUpdateAppsData;

GsPluginUpdateAppsData *gs_plugin_update_apps_data_new (GsAppList                          *apps,
                                                        GsPluginUpdateAppsFlags             flags,
                                                        GsPluginProgressCallback            progress_callback,
                                                        gpointer                            progress_user_data,
                                                        GsPluginEventCallback               event_callback,
                                                        void                               *event_user_data,
                                                        GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                                        gpointer                            app_needs_user_action_data);
GTask *gs_plugin_update_apps_data_new_task (gpointer                            source_object,
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
                                            gpointer                            user_data);
void gs_plugin_update_apps_data_free (GsPluginUpdateAppsData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginUpdateAppsData, gs_plugin_update_apps_data_free)

typedef struct {
	GsPluginCancelOfflineUpdateFlags flags;
} GsPluginCancelOfflineUpdateData;

GsPluginCancelOfflineUpdateData *
		gs_plugin_cancel_offline_update_data_new	(GsPluginCancelOfflineUpdateFlags  flags);
GTask *		gs_plugin_cancel_offline_update_data_new_task	(gpointer			   source_object,
								 GsPluginCancelOfflineUpdateFlags  flags,
								 GCancellable			  *cancellable,
								 GAsyncReadyCallback		   callback,
								 gpointer			   user_data);
void		gs_plugin_cancel_offline_update_data_free	(GsPluginCancelOfflineUpdateData  *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginCancelOfflineUpdateData, gs_plugin_cancel_offline_update_data_free)

typedef struct {
	GsApp *app;  /* (owned) (not nullable) */
	GsPluginDownloadUpgradeFlags flags;
	GsPluginEventCallback event_callback;
	void *event_user_data;
} GsPluginDownloadUpgradeData;

GsPluginDownloadUpgradeData *
		gs_plugin_download_upgrade_data_new	(GsApp			     *app,
							 GsPluginDownloadUpgradeFlags flags,
							 GsPluginEventCallback	      event_callback,
							 void			     *event_user_data);
GTask *		gs_plugin_download_upgrade_data_new_task(gpointer		      source_object,
							 GsApp			     *app,
							 GsPluginDownloadUpgradeFlags flags,
							 GsPluginEventCallback	      event_callback,
							 void			     *event_user_data,
							 GCancellable		     *cancellable,
							 GAsyncReadyCallback	      callback,
							 gpointer		      user_data);
void		gs_plugin_download_upgrade_data_free	(GsPluginDownloadUpgradeData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginDownloadUpgradeData, gs_plugin_download_upgrade_data_free)

typedef struct {
	GsApp *app;  /* (owned) (not nullable) */
	GsPluginTriggerUpgradeFlags flags;
} GsPluginTriggerUpgradeData;

GsPluginTriggerUpgradeData *
		gs_plugin_trigger_upgrade_data_new	(GsApp			    *app,
							 GsPluginTriggerUpgradeFlags flags);
GTask *		gs_plugin_trigger_upgrade_data_new_task	(gpointer		     source_object,
							 GsApp			    *app,
							 GsPluginTriggerUpgradeFlags flags,
							 GCancellable		    *cancellable,
							 GAsyncReadyCallback	     callback,
							 gpointer		     user_data);
void		gs_plugin_trigger_upgrade_data_free	(GsPluginTriggerUpgradeData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginTriggerUpgradeData, gs_plugin_trigger_upgrade_data_free)

typedef struct {
	GsApp *app;  /* (owned) (not nullable) */
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
	GsPluginEventCallback event_callback;
	void *event_user_data;
} GsPluginFileToAppData;

GsPluginFileToAppData *
		gs_plugin_file_to_app_data_new		(GFile			    *file,
							 GsPluginFileToAppFlags	     flags,
							 GsPluginEventCallback	     event_callback,
							 void			    *event_user_data);
GTask *		gs_plugin_file_to_app_data_new_task	(gpointer		     source_object,
							 GFile			    *file,
							 GsPluginFileToAppFlags	     flags,
							 GsPluginEventCallback	     event_callback,
							 void			    *event_user_data,
							 GCancellable		    *cancellable,
							 GAsyncReadyCallback	     callback,
							 gpointer		     user_data);
void		gs_plugin_file_to_app_data_free		(GsPluginFileToAppData	    *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginFileToAppData, gs_plugin_file_to_app_data_free)

typedef struct {
	gchar *url;  /* (owned) */
	GsPluginUrlToAppFlags flags;
	GsPluginEventCallback event_callback;
	void *event_user_data;
} GsPluginUrlToAppData;

GsPluginUrlToAppData *
		gs_plugin_url_to_app_data_new		(const gchar		    *url,
							 GsPluginUrlToAppFlags	     flags,
							 GsPluginEventCallback	     event_callback,
							 void			    *event_user_data);
GTask *		gs_plugin_url_to_app_data_new_task	(gpointer		     source_object,
							 const gchar		    *url,
							 GsPluginUrlToAppFlags	     flags,
							 GsPluginEventCallback	     event_callback,
							 void			    *event_user_data,
							 GCancellable		    *cancellable,
							 GAsyncReadyCallback	     callback,
							 gpointer		     user_data);
void		gs_plugin_url_to_app_data_free		(GsPluginUrlToAppData	    *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginUrlToAppData, gs_plugin_url_to_app_data_free)

G_END_DECLS
