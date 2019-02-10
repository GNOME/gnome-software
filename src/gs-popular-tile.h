/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include "gs-app-tile.h"

G_BEGIN_DECLS

#define GS_TYPE_POPULAR_TILE (gs_popular_tile_get_type ())

G_DECLARE_FINAL_TYPE (GsPopularTile, gs_popular_tile, GS, POPULAR_TILE, GsAppTile)

GtkWidget	*gs_popular_tile_new			(GsApp		*app);

G_END_DECLS
