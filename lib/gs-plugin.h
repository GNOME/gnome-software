/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2012-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
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
 *   apps to refine are provided in a list, and the flags specify what data to
 *   look up and add. Refining certain kinds of data can be very expensive (for
 *   example, requiring network requests), which is why it’s not all loaded by
 *   default. By refining multiple applications at once, data requests can be
 *   batched by the plugin where possible.
 * @refine_finish: (nullable): Finish method for @refine_async. Must be
 *   implemented if @refine_async is implemented.
 * @list_apps_async: (nullable): List apps matching a given query. (Since: 43)
 * @list_apps_finish: (nullable): Finish method for @list_apps_async. Must be
 *   implemented if @list_apps_async is implemented. (Since: 43)
 * @list_installed_apps_async: (nullable): Get the list of installed apps
 *   belonging to this plugin.
 * @list_installed_apps_finish: (nullable): Finish method for
 *   @list_installed_apps_async. Must be implemented if
 *   @list_installed_apps_async is implemented.
 * @refresh_metadata_async: (nullable): Refresh plugin metadata.
 * @refresh_metadata_finish: (nullable): Finish method for
 *   @refresh_metadata_async. Must be implemented if @refresh_metadata_async is
 *   implemented.
 * @list_distro_upgrades_async: (nullable): List available distro upgrades.
 * @list_distro_upgrades_finish: (nullable): Finish method for
 *   @list_distro_upgrades_async. Must be implemented if
 *   @list_distro_upgrades_async is implemented.
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
							 GsPluginRefineFlags	 flags,
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

	void			(*list_installed_apps_async)	(GsPlugin		*plugin,
								 GsPluginListInstalledAppsFlags flags,
								 GCancellable		*cancellable,
								 GAsyncReadyCallback	 callback,
								 gpointer		 user_data);
	GsAppList *		(*list_installed_apps_finish)	(GsPlugin		*plugin,
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

	gpointer		 padding[23];
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
gboolean	 gs_plugin_has_flags			(GsPlugin	*plugin,
							 GsPluginFlags	 flags);
void		 gs_plugin_add_flags			(GsPlugin	*plugin,
							 GsPluginFlags	 flags);
void		 gs_plugin_remove_flags			(GsPlugin	*plugin,
							 GsPluginFlags	 flags);
guint		 gs_plugin_get_scale			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_language			(GsPlugin	*plugin);
void		 gs_plugin_add_rule			(GsPlugin	*plugin,
							 GsPluginRule	 rule,
							 const gchar	*name);

/* helpers */
gboolean	 gs_plugin_download_file		(GsPlugin	*plugin,
							 GsApp		*app,
							 const gchar	*uri,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
gchar		*gs_plugin_download_rewrite_resource	(GsPlugin	*plugin,
							 GsApp		*app,
							 const gchar	*resource,
							 GCancellable	*cancellable,
							 GError		**error);

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
void		 gs_plugin_status_update		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginStatus	 status);
gboolean	 gs_plugin_app_launch			(GsPlugin	*plugin,
							 GsApp		*app,
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
gboolean	gs_plugin_get_action_supported		(GsPlugin	*plugin,
							 GsPluginAction	 action);
gboolean	gs_plugin_ask_untrusted			(GsPlugin	*plugin,
							 const gchar	*title,
							 const gchar	*msg,
							 const gchar	*details,
							 const gchar	*accept_label);

G_END_DECLS
