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

#include "config.h"

#include "gs-app-tile.h"
#include "gs-star-widget.h"
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

/* vim: set noexpandtab: */
