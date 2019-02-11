/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef GS_CATEGORY_TILE_H
#define GS_CATEGORY_TILE_H

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_CATEGORY_TILE (gs_category_tile_get_type ())

G_DECLARE_FINAL_TYPE (GsCategoryTile, gs_category_tile, GS, CATEGORY_TILE, GtkButton)

GtkWidget	*gs_category_tile_new			(GsCategory *cat);
GsCategory      *gs_category_tile_get_category		(GsCategoryTile	*tile);
void		 gs_category_tile_set_category		(GsCategoryTile	*tile,
							 GsCategory     *cat);
gboolean	 gs_category_tile_get_colorful		(GsCategoryTile	*tile);
void		 gs_category_tile_set_colorful		(GsCategoryTile	*tile,
							 gboolean	 colorful);

G_END_DECLS

#endif /* GS_CATEGORY_TILE_H */

/* vim: set noexpandtab: */
