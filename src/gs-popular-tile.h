/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#ifndef GS_POPULAR_TILE_H
#define GS_POPULAR_TILE_H

#include <gtk/gtk.h>

#include "gs-app.h"

#define GS_TYPE_POPULAR_TILE		(gs_popular_tile_get_type())
#define GS_POPULAR_TILE(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_POPULAR_TILE, GsPopularTile))
#define GS_POPULAR_TILE_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_POPULAR_TILE, GsPopularTileClass))
#define GS_IS_POPULAR_TILE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_POPULAR_TILE))
#define GS_IS_POPULAR_TILE_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_POPULAR_TILE))
#define GS_POPULAR_TILE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_POPULAR_TILE, GsPopularTileClass))

G_BEGIN_DECLS

typedef struct _GsPopularTile			GsPopularTile;
typedef struct _GsPopularTileClass		GsPopularTileClass;
typedef struct _GsPopularTilePrivate		GsPopularTilePrivate;

struct _GsPopularTile
{
	GtkButton		 parent;
	GsPopularTilePrivate	*priv;
};

struct _GsPopularTileClass
{
	GtkButtonClass		 parent_class;
};

GType		 gs_popular_tile_get_type		(void);
GtkWidget	*gs_popular_tile_new			(GsApp		*app);
GsApp		*gs_popular_tile_get_app		(GsPopularTile	*tile);
void		 gs_popular_tile_set_app		(GsPopularTile	*tile,
							 GsApp		*app);

G_END_DECLS

#endif /* GS_POPULAR_TILE_H */

/* vim: set noexpandtab: */
