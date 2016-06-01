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
#include "gs-common.h"

struct _GsCategoryTile
{
	GtkButton	 parent_instance;

	GsCategory	*cat;
	GtkWidget	*label;
	GtkWidget	*image;
};

G_DEFINE_TYPE (GsCategoryTile, gs_category_tile, GTK_TYPE_BUTTON)

GsCategory *
gs_category_tile_get_category (GsCategoryTile *tile)
{
	g_return_val_if_fail (GS_IS_CATEGORY_TILE (tile), NULL);

	return tile->cat;
}

void
gs_category_tile_set_category (GsCategoryTile *tile, GsCategory *cat)
{
	GPtrArray *key_colors;

	g_return_if_fail (GS_IS_CATEGORY_TILE (tile));
	g_return_if_fail (GS_IS_CATEGORY (cat));

	g_clear_object (&tile->cat);
	tile->cat = g_object_ref (cat);

	gtk_label_set_label (GTK_LABEL (tile->label), gs_category_get_name (cat));
	gtk_image_set_from_icon_name (GTK_IMAGE (tile->image),
				      gs_category_get_icon (cat),
				      GTK_ICON_SIZE_MENU);

	/* set custom CSS for important tiles */
	key_colors = gs_category_get_key_colors (cat);
	if (gs_category_get_important (cat) && key_colors->len > 0) {
		GdkRGBA *tmp = g_ptr_array_index (key_colors, 0);
		g_autofree gchar *css = NULL;
		g_autofree gchar *color = gdk_rgba_to_string (tmp);;
		css = g_strdup_printf ("border-bottom: 3px solid %s", color);
		gs_utils_widget_set_css_simple (GTK_WIDGET (tile), css);
	}
}

static void
gs_category_tile_destroy (GtkWidget *widget)
{
	GsCategoryTile *tile = GS_CATEGORY_TILE (widget);

	g_clear_object (&tile->cat);

	GTK_WIDGET_CLASS (gs_category_tile_parent_class)->destroy (widget);
}

static void
gs_category_tile_init (GsCategoryTile *tile)
{
	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	gtk_widget_init_template (GTK_WIDGET (tile));
}

static void
gs_category_tile_class_init (GsCategoryTileClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_category_tile_destroy;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-category-tile.ui");

	gtk_widget_class_bind_template_child (widget_class, GsCategoryTile, label);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryTile, image);
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
