/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-category-tile.h"
#include "gs-common.h"

#define COLORFUL_TILE_CLASS "colorful"
#define COLORFUL_TILE_PROVIDER_KEY "GnomeSoftware::colorful-tile-provider"

struct _GsCategoryTile
{
	GtkButton	 parent_instance;

	GsCategory	*cat;
	GtkWidget	*label;
	GtkWidget	*image;
	gboolean	 colorful;
};

G_DEFINE_TYPE (GsCategoryTile, gs_category_tile, GTK_TYPE_BUTTON)

GsCategory *
gs_category_tile_get_category (GsCategoryTile *tile)
{
	g_return_val_if_fail (GS_IS_CATEGORY_TILE (tile), NULL);

	return tile->cat;
}

static void
gs_category_tile_refresh (GsCategoryTile *tile)
{
	GPtrArray *key_colors;
	GtkStyleContext *context;

	/* get the style context */
	context = gtk_widget_get_style_context (GTK_WIDGET (tile));

	/* set labels */
	gtk_label_set_label (GTK_LABEL (tile->label),
			     gs_category_get_name (tile->cat));
	gtk_image_set_from_icon_name (GTK_IMAGE (tile->image),
				      gs_category_get_icon (tile->cat),
				      GTK_ICON_SIZE_MENU);

	/* set custom CSS for colorful tiles */
	key_colors = gs_category_get_key_colors (tile->cat);
	if (tile->colorful && key_colors->len > 0) {
		GdkRGBA *tmp = g_ptr_array_index (key_colors, 0);
		g_autoptr(GtkCssProvider) provider = NULL;
		g_autofree gchar *css = NULL;
		g_autofree gchar *color = gdk_rgba_to_string (tmp);

		css = g_strdup_printf ("@define-color gs_tile_color %s;", color);

		provider = gtk_css_provider_new ();
		gtk_css_provider_load_from_data (provider, css, -1, NULL);

		gtk_style_context_add_class (context, COLORFUL_TILE_CLASS);
		gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (provider),
						GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

		g_object_set_data_full (G_OBJECT (tile),
					COLORFUL_TILE_PROVIDER_KEY,
					g_object_ref (provider),
					g_object_unref);
	} else {
		GtkStyleProvider *provider = g_object_get_data (G_OBJECT (tile),
							        COLORFUL_TILE_PROVIDER_KEY);
		if (provider) {
			gtk_style_context_remove_class (context, COLORFUL_TILE_CLASS);
			gtk_style_context_remove_provider (context, provider);
		}
	}
}

void
gs_category_tile_set_category (GsCategoryTile *tile, GsCategory *cat)
{
	g_return_if_fail (GS_IS_CATEGORY_TILE (tile));
	g_return_if_fail (GS_IS_CATEGORY (cat));

	g_set_object (&tile->cat, cat);
	gs_category_tile_refresh (tile);
}

gboolean
gs_category_tile_get_colorful (GsCategoryTile *tile)
{
	g_return_val_if_fail (GS_IS_CATEGORY_TILE (tile), FALSE);
	return tile->colorful;
}

void
gs_category_tile_set_colorful (GsCategoryTile *tile, gboolean colorful)
{
	g_return_if_fail (GS_IS_CATEGORY_TILE (tile));
	tile->colorful = colorful;
	gs_category_tile_refresh (tile);
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
