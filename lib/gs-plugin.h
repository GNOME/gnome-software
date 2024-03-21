/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2012-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <gmodule.h>
#include <gio/gio.h>

#include "gs-app.h"
#include "gs-app-list.h"
#include "gs-app-query.h"
#include "gs-category.h"
#include "gs-plugin-event.h"
#include "gs-plugin-types.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN (gs_plugin_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsPlugin, gs_plugin, GS, PLUGIN, GObject)

/**
 * GsPluginClass:
 * @adopt_app: (nullable): Called when an app has not been claimed (i.e. a management
 *   plugin has not been set). (Since: 47)
 * @setup_async: (nullable): Setup method for the plugin. This is called after
 *   the #GsPlugin object is constructed, before it’s used for anything. It
 *   should do any long-running setup operations which the plugin needs, such as
 *   file or network access. It may be %NULL if the plugin doesn’t need to be
 *   explicitly shut down. It is not called if the plugin is disabled during
 *   construction.
 * @setup_finish: (nullable): Finish method for @setup_async. Must be
 *   implemented if @setup_async is implemented. If this returns an error, the
 *   plugin will be disabled.
 * @shutdown_async: (nullable): Shutdown method for the plugin. This is called
 *   by the #GsPluginLoader when the process is terminating or the
 *   #GsPluginLoader is being destroyed. It should be used to cancel or stop any
 *   ongoing operations or threads in the plugin. It may be %NULL if the plugin
 *   doesn’t need to be explicitly shut down.
 * @shutdown_finish: (nullable): Finish method for @shutdown_async. Must be
 *   implemented if @shutdown_async is implemented.
 * @refine_async: (nullable): Refining looks up and adds data to #GsApps. The
 *   apps to refine are provided in a list, and the refine_flags specify what data to
 *   look up and add. Refining certain kinds of data can be very expensive (for
 *   example, requiring network requests), which is why it’s not all loaded by
 *   default. By refining multiple apps at once, data requests can be
 *   batched by the plugin where possible. (Since: 43)
 * @refine_finish: (nullable): Finish method for @refine_async. Must be
 *   implemented if @refine_async is implemented. (Since: 43)
 * @list_apps_async: (nullable): List apps matching a given query. (Since: 43)
 * @list_apps_finish: (nullable): Finish method for @list_apps_async. Must be
 *   implemented if @list_apps_async is implemented. (Since: 43)
 * @refresh_metadata_async: (nullable): Refresh plugin metadata. (Since: 43)
 * @refresh_metadata_finish: (nullable): Finish method for
 *   @refresh_metadata_async. Must be implemented if @refresh_metadata_async is
 *   implemented. (Since: 43)
 * @list_distro_upgrades_async: (nullable): List available distro upgrades. (Since: 43)
 * @list_distro_upgrades_finish: (nullable): Finish method for
 *   @list_distro_upgrades_async. Must be implemented if
 *   @list_distro_upgrades_async is implemented. (Since: 43)
 * @install_repository_async: (nullable): Install repository. (Since: 43)
 * @install_repository_finish: (nullable): Finish method for
 *   @install_repository_async. Must be implemented if
 *   @install_repository_async is implemented. (Since: 43)
 * @remove_repository_async: (nullable): Remove repository. (Since: 43)
 * @remove_repository_finish: (nullable): Finish method for
 *   @remove_repository_async. Must be implemented if
 *   @remove_repository_async is implemented. (Since: 43)
 * @enable_repository_async: (nullable): Enable repository. (Since: 43)
 * @enable_repository_finish: (nullable): Finish method for
 *   @enable_repository_async. Must be implemented if
 *   @enable_repository_async is implemented. (Since: 43)
 * @disable_repository_async: (nullable): Disable repository. (Since: 43)
 * @disable_repository_finish: (nullable): Finish method for
 *   @disable_repository_async. Must be implemented if
 *   @disable_repository_async is implemented. (Since: 43)
 * @refine_categories_async: (nullable): Refining looks up and adds data to
 *   #GsCategorys. The categories to refine are provided in a list, and the
 *   flags specify what data to look up and add. Refining certain kinds of data
 *   can be very expensive (for example, requiring network requests), which is
 *   why it’s not all loaded by default. By refining multiple categories at
 *   once, data requests can be batched by the plugin where possible. (Since: 43)
 * @refine_categories_finish: (nullable): Finish method for
 *   @refine_categories_async. Must be implemented if @refine_categories_async
 *   is implemented. (Since: 43)
 * @update_apps_async: (nullable): Update apps or the OS, or download updates
 *   ready for installation. (Since: 44)
 * @update_apps_finish: (nullable): Finish method for @update_apps_async. Must
 *   be implemented if @update_apps_async is implemented. (Since: 44)
 * @install_app_async: (nullable): Install app. (Since: 47)
 * @install_app_finish: (nullable): Finish method for
 *   @install_app_async. Must be implemented if
 *   @install_app_async is implemented. (Since: 47)
 * @remove_app_async: (nullable): Remove app. (Since: 47)
 * @remove_app_finish: (nullable): Finish method for
 *   @remove_app_async. Must be implemented if
 *   @remove_app_async is implemented. (Since: 47)
 * @update_cancel_async: (nullable): Cancels the offline update of given @app. (Since: 47)
 * @update_cancel_finish: (nullable): Finish method for
 *   @update_cancel_async. Must be implemented if
 *   @update_cancel_async is implemented. (Since: 47)
 * @upgrade_download_async: (nullable): Start download of a distribution upgrade
 *   in the background. (Since: 47)
 * @upgrade_download_finish: (nullable): Finish method for
 *   @upgrade_download_async. Must be implemented if
 *   @upgrade_download_async is implemented. (Since: 47)
 * @upgrade_trigger_async: (nullable): Triggers the distribution upgrade to be
 *   installed on next boot. (Since: 47)
 * @upgrade_trigger_finish: (nullable): Finish method for
 *   @upgrade_trigger_async. Must be implemented if
 *   @upgrade_trigger_async is implemented. (Since: 47)
 * @launch_async: (nullable): Launch the specified app using a plugin-specific method. (Since: 47)
 * @launch_finish: (nullable): Finish method for
 *   @launch_async. Must be implemented if
 *   @launch_async is implemented. (Since: 47)
 * @file_to_app_async: (nullable): Converts a local file to a #GsApp. (Since: 47)
 * @file_to_app_finish: (nullable): Finish method for
 *   @file_to_app_async. Must be implemented if
 *   @file_to_app_async is implemented. (Since: 47)
 * @url_to_app_async: (nullable): Converts a URL to a #GsApp. (Since: 47)
 * @url_to_app_finish: (nullable): Finish method for
 *   @url_to_app_async. Must be implemented if
 *   @url_to_app_async is implemented. (Since: 47)
 * @get_langpacks_async: (nullable): Get a list of language packs, as given input language code
 *   or locale, e.g. "ja" or "ja_JP". (Since: 47)
 * @get_langpacks_finish: (nullable): Finish method for
 *   @get_langpacks_async. Must be implemented if
 *   @get_langpacks_async is implemented. (Since: 47)
 *
 * The class structure for a #GsPlugin. Virtual methods here should be
 * implemented by plugin implementations derived from #GsPlugin to provide their
 * plugin-specific behaviour.
 */
struct _GsPluginClass
{
	GObjectClass		 parent_class;
	void			(*updates_changed)	(GsPlugin	*plugin);
	void			(*status_changed)	(GsPlugin	*plugin,
							 GsApp		*app,
							 guint		 status);
	void			(*reload)		(GsPlugin	*plugin);
	void			(*report_event)		(GsPlugin	*plugin,
							 GsPluginEvent	*event);
	void			(*allow_updates)	(GsPlugin	*plugin,
							 gboolean	 allow_updates);
	void			(*basic_auth_start)	(GsPlugin	*plugin,
							 const gchar	*remote,
							 const gchar	*realm,
							 GCallback	 callback,
							 gpointer	 user_data);
	void			(*repository_changed)	(GsPlugin	*plugin,
							 GsApp		*repository);
	gboolean		(*ask_untrusted)	(GsPlugin	*plugin,
							 const gchar	*title,
							 const gchar	*msg,
							 const gchar	*details,
							 const gchar	*accept_label);
	void			(*adopt_app)		(GsPlugin	*plugin,
							 GsApp		*app);

	void			(*setup_async)		(GsPlugin		*plugin,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
	gboolean		(*setup_finish)		(GsPlugin		*plugin,
							 GAsyncResult		*result,
							 GError			**error);

	void			(*shutdown_async)	(GsPlugin		*plugin,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
	gboolean		(*shutdown_finish)	(GsPlugin		*plugin,
							 GAsyncResult		*result,
							 GError			**error);

	void			(*refine_async)		(GsPlugin		*plugin,
							 GsAppList		*list,
							 GsPluginRefineJobFlags	 job_flags,
							 GsPluginRefineFlags	 refine_flags,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
	gboolean		(*refine_finish)	(GsPlugin		*plugin,
							 GAsyncResult		*result,
							 GError			**error);

	void			(*list_apps_async)		(GsPlugin		*plugin,
								 GsAppQuery		*query,
								 GsPluginListAppsFlags	 flags,
								 GCancellable		*cancellable,
								 GAsyncReadyCallback	 callback,
								 gpointer		 user_data);
	GsAppList *		(*list_apps_finish)		(GsPlugin		*plugin,
								 GAsyncResult		*result,
								 GError			**error);

	void			(*refresh_metadata_async)	(GsPlugin		*plugin,
								 guint64		 cache_age_secs,
								 GsPluginRefreshMetadataFlags flags,
								 GCancellable		*cancellable,
								 GAsyncReadyCallback	 callback,
								 gpointer		 user_data);
	gboolean		(*refresh_metadata_finish)	(GsPlugin		*plugin,
								 GAsyncResult		*result,
								 GError			**error);

	void			(*list_distro_upgrades_async)	(GsPlugin		*plugin,
								 GsPluginListDistroUpgradesFlags flags,
								 GCancellable		*cancellable,
								 GAsyncReadyCallback	 callback,
								 gpointer		 user_data);
	GsAppList *		(*list_distro_upgrades_finish)	(GsPlugin		*plugin,
								 GAsyncResult		*result,
								 GError			**error);

	void			(*install_repository_async)	(GsPlugin		*plugin,
								 GsApp			*repository,
								 GsPluginManageRepositoryFlags flags,
								 GCancellable		*cancellable,
								 GAsyncReadyCallback	 callback,
								 gpointer		 user_data);
	gboolean		(*install_repository_finish)	(GsPlugin		*plugin,
								 GAsyncResult		*result,
								 GError			**error);
	void			(*remove_repository_async)	(GsPlugin		*plugin,
								 GsApp			*repository,
								 GsPluginManageRepositoryFlags flags,
								 GCancellable		*cancellable,
								 GAsyncReadyCallback	 callback,
								 gpointer		 user_data);
	gboolean		(*remove_repository_finish)	(GsPlugin		*plugin,
								 GAsyncResult		*result,
								 GError			**error);
	void			(*enable_repository_async)	(GsPlugin		*plugin,
								 GsApp			*repository,
								 GsPluginManageRepositoryFlags flags,
								 GCancellable		*cancellable,
								 GAsyncReadyCallback	 callback,
								 gpointer		 user_data);
	gboolean		(*enable_repository_finish)	(GsPlugin		*plugin,
								 GAsyncResult		*result,
								 GError			**error);
	void			(*disable_repository_async)	(GsPlugin		*plugin,
								 GsApp			*repository,
								 GsPluginManageRepositoryFlags flags,
								 GCancellable		*cancellable,
								 GAsyncReadyCallback	 callback,
								 gpointer		 user_data);
	gboolean		(*disable_repository_finish)	(GsPlugin		*plugin,
								 GAsyncResult		*result,
								 GError			**error);

	void			(*refine_categories_async)	(GsPlugin			*plugin,
								 GPtrArray			*list,
								 GsPluginRefineCategoriesFlags	 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	gboolean		(*refine_categories_finish)	(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);

	void			(*update_apps_async)		(GsPlugin			*plugin,
								 GsAppList			*apps,
								 GsPluginUpdateAppsFlags	 flags,
								 GsPluginProgressCallback	 progress_callback,
								 gpointer			 progress_user_data,
								 GsPluginAppNeedsUserActionCallback	app_needs_user_action_callback,
								 gpointer				app_needs_user_action_data,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	gboolean		(*update_apps_finish)		(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);
	void			(*install_app_async)		(GsPlugin			*plugin,
								 GsApp				*app,
								 GsPluginManageAppFlags		 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback	 	callback,
								 gpointer		 	user_data);
	gboolean		(*install_app_finish)		(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);
	void			(*remove_app_async)		(GsPlugin			*plugin,
								 GsApp				*app,
								 GsPluginManageAppFlags		 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	gboolean		(*remove_app_finish)		(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);
	void			(*update_cancel_async)		(GsPlugin			*plugin,
								 GsApp				*app,
								 GsPluginUpdateCancelFlags	 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	gboolean		(*update_cancel_finish)		(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);
	void			(*upgrade_download_async)	(GsPlugin			*plugin,
								 GsApp				*app,
								 GsPluginUpgradeDownloadFlags	 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	gboolean		(*upgrade_download_finish)	(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);
	void			(*upgrade_trigger_async)	(GsPlugin			*plugin,
								 GsApp				*app,
								 GsPluginUpgradeTriggerFlags	 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	gboolean		(*upgrade_trigger_finish)	(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);
	void			(*launch_async)			(GsPlugin			*plugin,
								 GsApp				*app,
								 GsPluginLaunchFlags		 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	gboolean		(*launch_finish)		(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);
	void			(*file_to_app_async)		(GsPlugin			*plugin,
								 GFile				*file,
								 GsPluginFileToAppFlags		 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	GsAppList *		(*file_to_app_finish)		(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);
	void			(*url_to_app_async)		(GsPlugin			*plugin,
								 const gchar			*url,
								 GsPluginUrlToAppFlags		 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	GsAppList *		(*url_to_app_finish)		(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);
	void			(*get_langpacks_async)		(GsPlugin			*plugin,
								 const gchar			*locale,
								 GsPluginGetLangpacksFlags	 flags,
								 GCancellable			*cancellable,
								 GAsyncReadyCallback		 callback,
								 gpointer			 user_data);
	GsAppList *		(*get_langpacks_finish)		(GsPlugin			*plugin,
								 GAsyncResult			*result,
								 GError				**error);

	gpointer		 padding[20];
};

/* helpers */
#define	GS_PLUGIN_ERROR					gs_plugin_error_quark ()

GQuark		 gs_plugin_error_quark			(void);

/* public getters and setters */
const gchar	*gs_plugin_get_name			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_appstream_id		(GsPlugin	*plugin);
void		 gs_plugin_set_appstream_id		(GsPlugin	*plugin,
							 const gchar	*appstream_id);
gboolean	 gs_plugin_get_enabled			(GsPlugin	*plugin);
void		 gs_plugin_set_enabled			(GsPlugin	*plugin,
							 gboolean	 enabled);
guint		 gs_plugin_get_scale			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_language			(GsPlugin	*plugin);
void		 gs_plugin_add_rule			(GsPlugin	*plugin,
							 GsPluginRule	 rule,
							 const gchar	*name);
void		 gs_plugin_adopt_app			(GsPlugin	*plugin,
							 GsApp		*app);

/* helpers */
gboolean	 gs_plugin_check_distro_id		(GsPlugin	*plugin,
							 const gchar	*distro_id);
GsApp		*gs_plugin_cache_lookup			(GsPlugin	*plugin,
							 const gchar	*key);
void		 gs_plugin_cache_lookup_by_state	(GsPlugin	*plugin,
							 GsAppList	*list,
							 GsAppState	 state);
void		 gs_plugin_cache_add			(GsPlugin	*plugin,
							 const gchar	*key,
							 GsApp		*app);
void		 gs_plugin_cache_remove			(GsPlugin	*plugin,
							 const gchar	*key);
void		 gs_plugin_cache_invalidate		(GsPlugin	*plugin);
GsAppList	*gs_plugin_list_cached			(GsPlugin	*plugin);
void		 gs_plugin_status_update		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginStatus	 status);
void		 gs_plugin_app_launch_async		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginLaunchFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	user_data);
gboolean	 gs_plugin_app_launch_finish		(GsPlugin	*plugin,
							 GAsyncResult	*result,
							 GError		**error);
typedef gboolean (* GsPluginPickDesktopFileCallback)	(GsPlugin	*plugin,
							 GsApp		*app,
							 const gchar	*filename,
							 GKeyFile	*key_file,
							 gpointer	 user_data);
/**
 * GsPluginPickDesktopFileCallback:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @filename: a .desktop file name
 * @key_file: a #GKeyFile with @filename loaded
 * @user_data: callback user data
 *
 * A callback used by gs_plugin_app_launch_filtered_async() to filter which
 * of the candidate .desktop files should be used to launch the @app.
 *
 * Returns: %TRUE, when the @key_file should be used, %FALSE to continue
 *    searching.
 *
 * Since: 43
 **/
void		 gs_plugin_app_launch_filtered_async	(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginLaunchFlags flags,
							 GsPluginPickDesktopFileCallback cb,
							 gpointer	cb_user_data,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback async_callback,
							 gpointer	async_user_data);
gboolean	 gs_plugin_app_launch_filtered_finish	(GsPlugin	*plugin,
							 GAsyncResult	*result,
							 GError		**error);
void		 gs_plugin_updates_changed		(GsPlugin	*plugin);
void		 gs_plugin_reload			(GsPlugin	*plugin);
const gchar	*gs_plugin_status_to_string		(GsPluginStatus	 status);
void		 gs_plugin_report_event			(GsPlugin	*plugin,
							 GsPluginEvent	*event);
void		 gs_plugin_set_allow_updates		(GsPlugin	*plugin,
							 gboolean	 allow_updates);
gboolean	 gs_plugin_get_network_available	(GsPlugin	*plugin);
void		 gs_plugin_basic_auth_start		(GsPlugin	*plugin,
							 const gchar	*remote,
							 const gchar	*realm,
							 GCallback	 callback,
							 gpointer	 user_data);
void		gs_plugin_repository_changed		(GsPlugin	*plugin,
							 GsApp		*repository);
void		gs_plugin_update_cache_state_for_repository
							(GsPlugin *plugin,
							 GsApp *repository);
gboolean	gs_plugin_ask_untrusted			(GsPlugin	*plugin,
							 const gchar	*title,
							 const gchar	*msg,
							 const gchar	*details,
							 const gchar	*accept_label);

GDBusConnection	*gs_plugin_get_session_bus_connection	(GsPlugin	*self);
GDBusConnection	*gs_plugin_get_system_bus_connection	(GsPlugin	*self);

G_END_DECLS
