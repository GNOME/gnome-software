/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2015 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_PLUGIN_LOADER_H
#define __GS_PLUGIN_LOADER_H

#include <glib-object.h>

#include "gs-app.h"
#include "gs-category.h"
#include "gs-plugin.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_LOADER		(gs_plugin_loader_get_type ())
#define GS_PLUGIN_LOADER_ERROR		(gs_plugin_loader_error_quark ())

G_DECLARE_DERIVABLE_TYPE (GsPluginLoader, gs_plugin_loader, GS, PLUGIN_LOADER, GObject)

struct _GsPluginLoaderClass
{
	GObjectClass		 parent_class;
	void			(*status_changed)	(GsPluginLoader	*plugin_loader,
							 GsApp		*app,
							 GsPluginStatus	 status);
	void			(*pending_apps_changed)	(GsPluginLoader	*plugin_loader);
	void			(*updates_changed)	(GsPluginLoader	*plugin_loader);
};

typedef enum
{
	GS_PLUGIN_LOADER_ERROR_FAILED,
	GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
	GS_PLUGIN_LOADER_ERROR_LAST
} GsPluginLoaderError;

typedef enum {
	GS_PLUGIN_LOADER_ACTION_INSTALL,
	GS_PLUGIN_LOADER_ACTION_REMOVE,
	GS_PLUGIN_LOADER_ACTION_UPDATE,
	GS_PLUGIN_LOADER_ACTION_SET_RATING,
	GS_PLUGIN_LOADER_ACTION_UPGRADE_DOWNLOAD,
	GS_PLUGIN_LOADER_ACTION_UPGRADE_TRIGGER,
	GS_PLUGIN_LOADER_ACTION_LAUNCH,
	GS_PLUGIN_LOADER_ACTION_OFFLINE_UPDATE_CANCEL,
	GS_PLUGIN_LOADER_ACTION_LAST
} GsPluginLoaderAction;

typedef void	 (*GsPluginLoaderFinishedFunc)		(GsPluginLoader	*plugin_loader,
							 GsApp		*app,
							 gpointer	 user_data);

GQuark		 gs_plugin_loader_error_quark		(void);

GsPluginLoader	*gs_plugin_loader_new			(void);
void		 gs_plugin_loader_get_installed_async	(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_get_installed_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_get_updates_async	(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_get_updates_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_get_distro_upgrades_async (GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_get_distro_upgrades_finish (GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_get_unvoted_reviews_async (GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_get_unvoted_reviews_finish (GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_get_sources_async	(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_get_sources_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_get_popular_async	(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_get_popular_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_get_featured_async	(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_get_featured_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_get_categories_async	(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_get_categories_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_get_category_apps_async (GsPluginLoader	*plugin_loader,
							 GsCategory	*category,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_get_category_apps_finish (GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_search_async		(GsPluginLoader	*plugin_loader,
							 const gchar	*value,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_search_finish		(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_search_files_async	(GsPluginLoader	*plugin_loader,
							 const gchar	*value,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_search_files_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_search_what_provides_async (GsPluginLoader	*plugin_loader,
							 const gchar	*value,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GList		*gs_plugin_loader_search_what_provides_finish (GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_filename_to_app_async	(GsPluginLoader	*plugin_loader,
							 const gchar	*filename,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GsApp		*gs_plugin_loader_filename_to_app_finish(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_offline_update_async	(GsPluginLoader	*plugin_loader,
							 GList		*apps,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
gboolean	 gs_plugin_loader_offline_update_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
gboolean	 gs_plugin_loader_setup			(GsPluginLoader	*plugin_loader,
							 gchar		**whitelist,
							 GError		**error);
void		 gs_plugin_loader_dump_state		(GsPluginLoader	*plugin_loader);
gboolean	 gs_plugin_loader_get_enabled		(GsPluginLoader	*plugin_loader,
							 const gchar	*plugin_name);
void		 gs_plugin_loader_set_location		(GsPluginLoader	*plugin_loader,
							 const gchar	*location);
gint		 gs_plugin_loader_get_scale		(GsPluginLoader	*plugin_loader);
void		 gs_plugin_loader_set_scale		(GsPluginLoader	*plugin_loader,
							 gint		 scale);
void		 gs_plugin_loader_app_refine_async	(GsPluginLoader	*plugin_loader,
							 GsApp		*app,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
gboolean	 gs_plugin_loader_app_refine_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_app_action_async	(GsPluginLoader	*plugin_loader,
							 GsApp		*app,
							 GsPluginLoaderAction a,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
gboolean	 gs_plugin_loader_app_action_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_review_action_async	(GsPluginLoader	*plugin_loader,
							 GsApp		*app,
							 GsReview	*review,
							 GsReviewAction	 action,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
gboolean	 gs_plugin_loader_review_action_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
gboolean	 gs_plugin_loader_refresh_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_refresh_async		(GsPluginLoader	*plugin_loader,
							 guint		 cache_age,
							 GsPluginRefreshFlags flags,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GPtrArray	*gs_plugin_loader_get_pending		(GsPluginLoader	*plugin_loader);
void		 gs_plugin_loader_set_network_status    (GsPluginLoader *plugin_loader,
							 gboolean        online);
gboolean	 gs_plugin_loader_get_plugin_supported	(GsPluginLoader	*plugin_loader,
							 const gchar	*plugin_func);

G_END_DECLS

#endif /* __GS_PLUGIN_LOADER_H */

/* vim: set noexpandtab: */
