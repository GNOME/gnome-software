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

#include "gs-app-tile.h"

struct _GsAppTilePrivate
{
	GsApp		*app;
	GtkWidget	*button;
	GtkWidget	*image;
	GtkWidget	*name;
	GtkWidget	*summary;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsAppTile, gs_app_tile, GTK_TYPE_BIN)

enum {
	SIGNAL_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

GsApp *
gs_app_tile_get_app (GsAppTile *tile)
{
	GsAppTilePrivate *priv;

	g_return_val_if_fail (GS_IS_APP_TILE (tile), NULL);

	priv = gs_app_tile_get_instance_private (tile);
	return priv->app;
}

void
gs_app_tile_set_app (GsAppTile *tile, GsApp *app)
{
	GsAppTilePrivate *priv;
	const gchar *summary;

	g_return_if_fail (GS_IS_APP_TILE (tile));
	g_return_if_fail (GS_IS_APP (app));

	priv = gs_app_tile_get_instance_private (tile);

	g_clear_object (&priv->app);
	priv->app = g_object_ref (app);

	gtk_image_set_from_pixbuf (GTK_IMAGE (priv->image), gs_app_get_pixbuf (app));
	gtk_label_set_label (GTK_LABEL (priv->name), gs_app_get_name (app));
	summary = gs_app_get_summary (app);
	gtk_label_set_label (GTK_LABEL (priv->summary), summary);
	gtk_widget_set_visible (priv->summary, summary && summary[0]);
}

static void
gs_app_tile_destroy (GtkWidget *widget)
{
	GsAppTile *tile = GS_APP_TILE (widget);
	GsAppTilePrivate *priv;

	priv = gs_app_tile_get_instance_private (tile);

	g_clear_object (&priv->app);

	GTK_WIDGET_CLASS (gs_app_tile_parent_class)->destroy (widget);
}

static void
button_clicked (GsAppTile *tile)
{
	g_signal_emit (tile, signals[SIGNAL_CLICKED], 0);
}

static void
gs_app_tile_init (GsAppTile *tile)
{
	GsAppTilePrivate *priv;

	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	gtk_widget_init_template (GTK_WIDGET (tile));
	priv = gs_app_tile_get_instance_private (tile);
	g_signal_connect_swapped (priv->button, "clicked",
				  G_CALLBACK (button_clicked), tile);
}

static void
gs_app_tile_class_init (GsAppTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_app_tile_destroy;

	signals [SIGNAL_CLICKED] =
		g_signal_new ("clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsAppTileClass, clicked),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/software/app-tile.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsAppTile, button);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppTile, image);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppTile, name);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppTile, summary);
}

GtkWidget *
gs_app_tile_new (GsApp *cat)
{
	GsAppTile *tile;

	tile = g_object_new (GS_TYPE_APP_TILE, NULL);
	gs_app_tile_set_app (tile, cat);

	return GTK_WIDGET (tile);
}

/* vim: set noexpandtab: */
