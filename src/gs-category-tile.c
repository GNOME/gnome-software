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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-category-tile.h"

struct _GsCategoryTilePrivate
{
	GsCategory	*cat;
	GtkWidget	*button;
	GtkWidget	*label;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsCategoryTile, gs_category_tile, GTK_TYPE_BIN)

enum {
	SIGNAL_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

GsCategory *
gs_category_tile_get_category (GsCategoryTile *tile)
{
	GsCategoryTilePrivate *priv;

	g_return_val_if_fail (GS_IS_CATEGORY_TILE (tile), NULL);

	priv = gs_category_tile_get_instance_private (tile);
	return priv->cat;
}

void
gs_category_tile_set_category (GsCategoryTile *tile, GsCategory *cat)
{
	GsCategoryTilePrivate *priv;

	g_return_if_fail (GS_IS_CATEGORY_TILE (tile));
	g_return_if_fail (GS_IS_CATEGORY (cat));

	priv = gs_category_tile_get_instance_private (tile);

	g_clear_object (&priv->cat);
	priv->cat = g_object_ref (cat);

	gtk_label_set_label (GTK_LABEL (priv->label), gs_category_get_name (cat));
}

static void
gs_category_tile_destroy (GtkWidget *widget)
{
	GsCategoryTile *tile = GS_CATEGORY_TILE (widget);
	GsCategoryTilePrivate *priv;

	priv = gs_category_tile_get_instance_private (tile);

	g_clear_object (&priv->cat);

	GTK_WIDGET_CLASS (gs_category_tile_parent_class)->destroy (widget);
}

static void
button_clicked (GsCategoryTile *tile)
{
	g_signal_emit (tile, signals[SIGNAL_CLICKED], 0);
}

static void
gs_category_tile_init (GsCategoryTile *tile)
{
	GsCategoryTilePrivate *priv;

	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	gtk_widget_init_template (GTK_WIDGET (tile));
	priv = gs_category_tile_get_instance_private (tile);
	g_signal_connect_swapped (priv->button, "clicked",
				  G_CALLBACK (button_clicked), tile);
}

static void
gs_category_tile_class_init (GsCategoryTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_category_tile_destroy;

	signals [SIGNAL_CLICKED] =
		g_signal_new ("clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsCategoryTileClass, clicked),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/software/category-tile.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsCategoryTile, button);
	gtk_widget_class_bind_template_child_private (widget_class, GsCategoryTile, label);
}

GtkWidget *
gs_category_tile_new (GsCategory *cat)
{
	GsCategoryTile *tile;

	tile = g_object_new (GS_TYPE_CATEGORY_TILE, NULL);
	gs_category_tile_set_category (tile, cat);

	return GTK_WIDGET (tile);
}

/* vim: set noexpandtab: */
