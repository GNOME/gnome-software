/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-app-tile.h"
#include "gs-common.h"

G_DEFINE_ABSTRACT_TYPE (GsAppTile, gs_app_tile, GTK_TYPE_BUTTON)

GsApp *
gs_app_tile_get_app (GsAppTile *tile)
{
	GsAppTileClass *klass;

	g_return_val_if_fail (GS_IS_APP_TILE (tile), NULL);

	klass = GS_APP_TILE_GET_CLASS (tile);
	g_assert (klass->get_app);

	return klass->get_app(tile);
}

void
gs_app_tile_set_app (GsAppTile *tile, GsApp *app)
{
	GsAppTileClass *klass;

	g_return_if_fail (GS_IS_APP_TILE (tile));
	g_return_if_fail (!app || GS_IS_APP (app));

	klass = GS_APP_TILE_GET_CLASS (tile);
	g_assert (klass->get_app);

	klass->set_app(tile, app);
}

void
gs_app_tile_class_init (GsAppTileClass *klass)
{}

void
gs_app_tile_init (GsAppTile *tile)
{}

GtkWidget *
gs_app_tile_new (GsApp *app)
{
	GsAppTile *tile;

	tile = g_object_new (GS_TYPE_APP_TILE, NULL);
	gs_app_tile_set_app (tile, app);

	return GTK_WIDGET (tile);
}
