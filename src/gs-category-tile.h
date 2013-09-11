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

#ifndef GS_CATEGORY_TILE_H
#define GS_CATEGORY_TILE_H

#include <gtk/gtk.h>

#include "gs-category.h"

#define GS_TYPE_CATEGORY_TILE		(gs_category_tile_get_type())
#define GS_CATEGORY_TILE(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_CATEGORY_TILE, GsCategoryTile))
#define GS_CATEGORY_TILE_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_CATEGORY_TILE, GsCategoryTileClass))
#define GS_IS_CATEGORY_TILE(obj)	(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_CATEGORY_TILE))
#define GS_IS_CATEGORY_TILE_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_CATEGORY_TILE))
#define GS_CATEGORY_TILE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_CATEGORY_TILE, GsCategoryTileClass))

G_BEGIN_DECLS

typedef struct _GsCategoryTile			GsCategoryTile;
typedef struct _GsCategoryTileClass		GsCategoryTileClass;
typedef struct _GsCategoryTilePrivate		GsCategoryTilePrivate;

struct _GsCategoryTile
{
	GtkBin		   parent;
	GsCategoryTilePrivate	*priv;
};

struct _GsCategoryTileClass
{
	GtkBinClass	 parent_class;

	void			(*clicked)	(GsCategoryTile	*tile);
};

GType		 gs_category_tile_get_type		(void);
GtkWidget	*gs_category_tile_new			(GsCategory *cat);
GsCategory      *gs_category_tile_get_category		(GsCategoryTile	*tile);
void		 gs_category_tile_set_category		(GsCategoryTile	*tile,
							 GsCategory     *cat);

G_END_DECLS

#endif /* GS_CATEGORY_TILE_H */

/* vim: set noexpandtab: */
