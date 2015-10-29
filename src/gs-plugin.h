/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2014 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_PLUGIN_H
#define __GS_PLUGIN_H

#include <appstream-glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-category.h"

G_BEGIN_DECLS

typedef struct	GsPluginPrivate	GsPluginPrivate;
typedef struct	GsPlugin	GsPlugin;

typedef enum {
	GS_PLUGIN_STATUS_UNKNOWN,
	GS_PLUGIN_STATUS_WAITING,
	GS_PLUGIN_STATUS_FINISHED,
	GS_PLUGIN_STATUS_SETUP,
	GS_PLUGIN_STATUS_DOWNLOADING,
	GS_PLUGIN_STATUS_QUERYING,
	GS_PLUGIN_STATUS_INSTALLING,
	GS_PLUGIN_STATUS_REMOVING,
	GS_PLUGIN_STATUS_LAST
} GsPluginStatus;

typedef GList GsAppList;

typedef void (*GsPluginStatusUpdate)	(GsPlugin	*plugin,
					 GsApp		*app,
					 GsPluginStatus	 status,
					 gpointer	 user_data);
typedef void (*GsPluginUpdatesChanged)	(GsPlugin	*plugin,
					 gpointer	 user_data);

typedef gboolean (*GsPluginListFilter)	(GsApp		*app,
					 gpointer	 user_data);

struct GsPlugin {
	GModule			*module;
	gdouble			 priority;	/* largest number gets run first */
	const gchar		**deps;		/* allow-none */
	gboolean		 enabled;
	gboolean		 use_pkg_descriptions;
	gchar			*name;
	GsPluginPrivate		*priv;
	guint			 pixbuf_size;
	gint			 scale;
	GsPluginStatusUpdate	 status_update_fn;
	gpointer		 status_update_user_data;
	GsPluginUpdatesChanged	 updates_changed_fn;
	gpointer		 updates_changed_user_data;
	AsProfile		*profile;
};

typedef enum {
	GS_PLUGIN_ERROR_FAILED,
	GS_PLUGIN_ERROR_NOT_SUPPORTED,
	GS_PLUGIN_ERROR_LAST
} GsPluginError;

typedef enum {
	GS_PLUGIN_REFINE_FLAGS_DEFAULT			= 0,
	GS_PLUGIN_REFINE_FLAGS_USE_HISTORY		= 1 << 0,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENCE		= 1 << 1,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL		= 1 << 2,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION	= 1 << 3,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE		= 1 << 4,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING		= 1 << 5,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION		= 1 << 6,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY		= 1 << 7,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION	= 1 << 8,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS	= 1 << 9,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN		= 1 << 10,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED		= 1 << 11,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH	= 1 << 12,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS		= 1 << 13,
	GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES		= 1 << 14,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_SEVERITY	= 1 << 15,
	GS_PLUGIN_REFINE_FLAGS_LAST
} GsPluginRefineFlags;

typedef enum {
	GS_PLUGIN_REFRESH_FLAGS_UPDATES			= 1 << 0,
	GS_PLUGIN_REFRESH_FLAGS_LAST
} GsPluginRefreshFlags;

/* helpers */
#define	GS_PLUGIN_ERROR					1
#define	GS_PLUGIN_GET_PRIVATE(x)			g_new0 (x,1)
#define	GS_PLUGIN(x)					((GsPlugin *) x);

typedef const gchar	*(*GsPluginGetNameFunc)		(void);
typedef const gchar	**(*GsPluginGetDepsFunc)	(GsPlugin	*plugin);
typedef void		 (*GsPluginFunc)		(GsPlugin	*plugin);
typedef gboolean	 (*GsPluginSearchFunc)		(GsPlugin	*plugin,
							 gchar		**value,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginCategoryFunc)	(GsPlugin	*plugin,
							 GsCategory	*category,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginResultsFunc)		(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginActionFunc)		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginRefineFunc)		(GsPlugin	*plugin,
							 GList		**list,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginRefreshFunc	)	(GsPlugin	*plugin,
							 guint		 cache_age,
							 GsPluginRefreshFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginFilenameToAppFunc)	(GsPlugin	*plugin,
							 GList		**list,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginOfflineUpdateFunc)	(GsPlugin	*plugin,
							 GList		*apps,
							 GCancellable	*cancellable,
							 GError		**error);

const gchar	*gs_plugin_get_name			(void);
void		 gs_plugin_initialize			(GsPlugin	*plugin);
void		 gs_plugin_destroy			(GsPlugin	*plugin);
void		 gs_plugin_set_enabled			(GsPlugin	*plugin,
							 gboolean	 enabled);
gboolean	 gs_plugin_check_distro_id		(GsPlugin	*plugin,
							 const gchar	*distro_id);
void		 gs_plugin_add_app			(GList		**list,
							 GsApp		*app);
void		 gs_plugin_list_free			(GList		*list);
GList		*gs_plugin_list_copy			(GList		*list);
void		 gs_plugin_list_filter			(GList		**list,
							 GsPluginListFilter func,
							 gpointer	 user_data);
void		 gs_plugin_list_filter_duplicates	(GList		**list);
void		 gs_plugin_list_randomize		(GList		**list);

void		 gs_plugin_status_update		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginStatus	 status);
void		 gs_plugin_progress_update		(GsPlugin	*plugin,
							 GsApp		*app,
							 guint		 percentage);
void		 gs_plugin_updates_changed		(GsPlugin	*plugin);
const gchar	*gs_plugin_status_to_string		(GsPluginStatus	 status);
gboolean	 gs_plugin_add_search			(GsPlugin	*plugin,
							 gchar		**values,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_search_files		(GsPlugin	*plugin,
							 gchar		**values,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_search_what_provides	(GsPlugin	*plugin,
							 gchar		**values,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
const gchar	**gs_plugin_get_deps			(GsPlugin	*plugin);
gboolean	 gs_plugin_add_installed		(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_updates			(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_distro_upgrades		(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_sources			(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_updates_historical	(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_categories		(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_category_apps		(GsPlugin	*plugin,
							 GsCategory	*category,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_popular			(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_featured			(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_refine			(GsPlugin	*plugin,
							 GList		**list,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_install			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_remove			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_set_rating		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_refresh			(GsPlugin	*plugin,
							 guint		 cache_age,
							 GsPluginRefreshFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_filename_to_app		(GsPlugin	*plugin,
							 GList		**list,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_offline_update		(GsPlugin	*plugin,
							 GList		*apps,
							 GCancellable	*cancellable,
							 GError		**error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsAppList, gs_plugin_list_free)

G_END_DECLS

#endif /* __GS_PLUGIN_H */

/* vim: set noexpandtab: */
