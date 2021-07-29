/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

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

typedef enum {
	PROP_CATEGORY = 1,
} GsCategoryTileProperty;

static GParamSpec *obj_props[PROP_CATEGORY + 1] = { NULL, };

static void
gs_category_tile_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsCategoryTile *self = GS_CATEGORY_TILE (object);

	switch ((GsCategoryTileProperty) prop_id) {
	case PROP_CATEGORY:
		g_value_set_object (value, gs_category_tile_get_category (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_category_tile_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsCategoryTile *self = GS_CATEGORY_TILE (object);

	switch ((GsCategoryTileProperty) prop_id) {
	case PROP_CATEGORY:
		gs_category_tile_set_category (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GsCategory *
gs_category_tile_get_category (GsCategoryTile *tile)
{
	g_return_val_if_fail (GS_IS_CATEGORY_TILE (tile), NULL);

	return tile->cat;
}

static void
gs_category_tile_refresh (GsCategoryTile *tile)
{
	/* set labels */
	gtk_label_set_label (GTK_LABEL (tile->label),
			     gs_category_get_name (tile->cat));
	gtk_image_set_from_icon_name (GTK_IMAGE (tile->image),
				      gs_category_get_icon_name (tile->cat),
				      GTK_ICON_SIZE_MENU);
}

void
gs_category_tile_set_category (GsCategoryTile *tile, GsCategory *cat)
{
	g_return_if_fail (GS_IS_CATEGORY_TILE (tile));
	g_return_if_fail (GS_IS_CATEGORY (cat));

	if (g_set_object (&tile->cat, cat)) {
		gs_category_tile_refresh (tile);
		g_object_notify_by_pspec (G_OBJECT (tile), obj_props[PROP_CATEGORY]);
	}
}

static void
gs_category_tile_dispose (GObject *object)
{
	GsCategoryTile *tile = GS_CATEGORY_TILE (object);

	g_clear_object (&tile->cat);

	G_OBJECT_CLASS (gs_category_tile_parent_class)->dispose (object);
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
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_category_tile_get_property;
	object_class->set_property = gs_category_tile_set_property;
	object_class->dispose = gs_category_tile_dispose;

	/**
	 * GsCategoryTile:category: (not nullable)
	 *
	 * The category to display in this tile.
	 *
	 * This must not be %NULL.
	 *
	 * Since: 41
	 */
	obj_props[PROP_CATEGORY] =
		g_param_spec_object ("category", NULL, NULL,
				     GS_TYPE_CATEGORY,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-category-tile.ui");

	gtk_widget_class_bind_template_child (widget_class, GsCategoryTile, label);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryTile, image);
}

GtkWidget *
gs_category_tile_new (GsCategory *cat)
{
	return g_object_new (GS_TYPE_CATEGORY_TILE,
			     "category", cat,
			     NULL);
}
