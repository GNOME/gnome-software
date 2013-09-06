/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#ifndef __APPSTREAM_CACHE_H
#define __APPSTREAM_CACHE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "appstream-app.h"

G_BEGIN_DECLS

#define APPSTREAM_TYPE_CACHE		(appstream_cache_get_type ())
#define APPSTREAM_CACHE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), APPSTREAM_TYPE_CACHE, AppstreamCache))
#define APPSTREAM_CACHE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), APPSTREAM_TYPE_CACHE, AppstreamCacheClass))
#define APPSTREAM_IS_CACHE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), APPSTREAM_TYPE_CACHE))
#define APPSTREAM_IS_CACHE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), APPSTREAM_TYPE_CACHE))
#define APPSTREAM_CACHE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), APPSTREAM_TYPE_CACHE, AppstreamCacheClass))
#define APPSTREAM_CACHE_ERROR		(appstream_cache_error_quark ())

typedef struct AppstreamCachePrivate AppstreamCachePrivate;

typedef struct
{
	 GObject			 parent;
	 AppstreamCachePrivate		*priv;
} AppstreamCache;

typedef struct
{
	GObjectClass			 parent_class;
} AppstreamCacheClass;

typedef enum {
	APPSTREAM_CACHE_ERROR_FAILED,
	APPSTREAM_CACHE_ERROR_LAST
} AppstreamCacheError;

GQuark		 appstream_cache_error_quark		(void);
GType		 appstream_cache_get_type		(void);

AppstreamCache	*appstream_cache_new			(void);
gboolean	 appstream_cache_parse_file		(AppstreamCache	*cache,
							 GFile		*file,
							 const gchar	*path_icons,
							 GCancellable	*cancellable,
							 GError		**error);
guint		 appstream_cache_get_size		(AppstreamCache	*cache);
GPtrArray	*appstream_cache_get_items		(AppstreamCache	*cache);
AppstreamApp	*appstream_cache_get_item_by_id		(AppstreamCache	*cache,
							 const gchar	*id);
AppstreamApp	*appstream_cache_get_item_by_pkgname	(AppstreamCache	*cache,
							 const gchar	*pkgname);

G_END_DECLS

#endif /* __APPSTREAM_CACHE_H */
