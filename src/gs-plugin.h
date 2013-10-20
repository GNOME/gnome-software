/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2013 Richard Hughes <richard@hughsie.com>
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

#include <glib-object.h>
#include <gmodule.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-category.h"
#include "gs-profile.h"

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

typedef void (*GsPluginStatusUpdate)	(GsPlugin	*plugin,
					 GsApp		*app,
					 GsPluginStatus	 status,
					 gpointer	 user_data);

typedef gboolean (*GsPluginListFilter)	(GsApp		*app,
					 gpointer	 user_data);

struct GsPlugin {
	GModule			*module;
	gdouble			 priority;	/* largest number gets run first */
	gboolean		 enabled;
	gchar			*name;
	GsPluginPrivate		*priv;
	guint			 pixbuf_size;
	GsPluginStatusUpdate	 status_update_fn;
	gpointer		 status_update_user_data;
	GsProfile		*profile;
	GHashTable		*icon_cache;
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
	GS_PLUGIN_REFINE_FLAGS_LAST
} GsPluginRefineFlags;

/* helpers */
#define	GS_PLUGIN_ERROR					1
#define	GS_PLUGIN_GET_PRIVATE(x)			g_new0 (x,1)
#define	GS_PLUGIN(x)					((GsPlugin *) x);

typedef const gchar	*(*GsPluginGetNameFunc)		(void);
typedef gdouble		 (*GsPluginGetPriorityFunc)	(GsPlugin	*plugin);
typedef void		 (*GsPluginFunc)		(GsPlugin	*plugin);
typedef gboolean	 (*GsPluginSearchFunc)		(GsPlugin	*plugin,
							 const gchar	*value,
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
							 GList		*list,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);

const gchar	*gs_plugin_get_name			(void);
void		 gs_plugin_initialize			(GsPlugin	*plugin);
void		 gs_plugin_destroy			(GsPlugin	*plugin);
void		 gs_plugin_set_enabled			(GsPlugin	*plugin,
							 gboolean	 enabled);
void		 gs_plugin_add_app			(GList		**list,
							 GsApp		*app);
void		 gs_plugin_list_free			(GList		*list);
GList		*gs_plugin_list_copy			(GList		*list);
void		 gs_plugin_list_filter			(GList		**list,
							 GsPluginListFilter func,
							 gpointer	 user_data);
void		 gs_plugin_list_filter_duplicates	(GList		**list);

void		 gs_plugin_status_update		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginStatus	 status);
const gchar	*gs_plugin_status_to_string		(GsPluginStatus	 status);
gboolean	 gs_plugin_add_search			(GsPlugin	*plugin,
							 const gchar	*value,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gdouble		 gs_plugin_get_priority			(GsPlugin	*plugin);
gboolean	 gs_plugin_add_installed		(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_updates			(GsPlugin	*plugin,
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
							 GList		*list,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_install			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_update			(GsPlugin	*plugin,
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

G_END_DECLS

#endif /* __GS_PLUGIN_H */

/* vim: set noexpandtab: */
