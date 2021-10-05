/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_SUMMARY_TILE_LAYOUT (gs_summary_tile_layout_get_type ())
G_DECLARE_FINAL_TYPE (GsSummaryTileLayout, gs_summary_tile_layout, GS, SUMMARY_TILE_LAYOUT, GtkLayoutManager)

void	gs_summary_tile_layout_set_preferred_width	(GsSummaryTileLayout *self,
							 gint                 preferred_width);

G_END_DECLS
