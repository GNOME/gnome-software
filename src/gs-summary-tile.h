/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef GS_SUMMARY_TILE_H
#define GS_SUMMARY_TILE_H

#include "gs-app-tile.h"

G_BEGIN_DECLS

#define GS_TYPE_SUMMARY_TILE (gs_summary_tile_get_type ())

G_DECLARE_FINAL_TYPE (GsSummaryTile, gs_summary_tile, GS, SUMMARY_TILE, GsAppTile)

GtkWidget	*gs_summary_tile_new	(GsApp		*app);

G_END_DECLS

#endif /* GS_SUMMARY_TILE_H */

/* vim: set noexpandtab: */
