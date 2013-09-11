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

#ifndef GS_FEATURE_TILE_H
#define GS_FEATURE_TILE_H

#include <gtk/gtk.h>

#include "gs-app.h"

#define GS_TYPE_FEATURE_TILE		(gs_feature_tile_get_type())
#define GS_FEATURE_TILE(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_FEATURE_TILE, GsFeatureTile))
#define GS_FEATURE_TILE_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_FEATURE_TILE, GsFeatureTileClass))
#define GS_IS_FEATURE_TILE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_FEATURE_TILE))
#define GS_IS_FEATURE_TILE_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_FEATURE_TILE))
#define GS_FEATURE_TILE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_FEATURE_TILE, GsFeatureTileClass))

G_BEGIN_DECLS

typedef struct _GsFeatureTile			GsFeatureTile;
typedef struct _GsFeatureTileClass		GsFeatureTileClass;
typedef struct _GsFeatureTilePrivate		GsFeatureTilePrivate;

struct _GsFeatureTile
{
	GtkBin		   parent;
	GsFeatureTilePrivate	*priv;
};

struct _GsFeatureTileClass
{
	GtkBinClass	 parent_class;

	void			(*clicked)		(GsFeatureTile	*tile);
};

GType		 gs_feature_tile_get_type		(void);
GtkWidget	*gs_feature_tile_new			(GsApp		*app);
GsApp		*gs_feature_tile_get_app		(GsFeatureTile	*tile);
void		 gs_feature_tile_set_app		(GsFeatureTile	*tile,
							 GsApp		*app);

G_END_DECLS

#endif /* GS_FEATURE_TILE_H */

/* vim: set noexpandtab: */
