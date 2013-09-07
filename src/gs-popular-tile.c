/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "gs-popular-tile.h"

struct _GsPopularTilePrivate
{
	GsApp		*app;
	GtkWidget	*button;
	GtkWidget	*label;
	GtkWidget	*image;
	GtkWidget	*eventbox;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsPopularTile, gs_popular_tile, GTK_TYPE_BIN)

enum {
	SIGNAL_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

GsApp *
gs_popular_tile_get_app (GsPopularTile *tile)
{
        GsPopularTilePrivate *priv;

	g_return_val_if_fail (GS_IS_POPULAR_TILE (tile), NULL);

        priv = gs_popular_tile_get_instance_private (tile);
	return priv->app;
}

static gboolean
transform_state_func (GBinding *binding,
                      const GValue *source,
                      GValue *target,
                      gpointer user_data)
{
        GsAppState state;
        gboolean installed;

        state = g_value_get_uint (source);

        switch (state) {
        case GS_APP_STATE_INSTALLED:
        case GS_APP_STATE_INSTALLING:
        case GS_APP_STATE_REMOVING:
        case GS_APP_STATE_UPDATABLE:
                installed = TRUE;
                break;
        case GS_APP_STATE_AVAILABLE:
        default:
                installed = FALSE;
                break;
        }
        g_value_set_boolean (target, installed);

        return TRUE;
}

void
gs_popular_tile_set_app (GsPopularTile *tile, GsApp *app)
{
        GsPopularTilePrivate *priv;

	g_return_if_fail (GS_IS_POPULAR_TILE (tile));
	g_return_if_fail (GS_IS_APP (app));

        priv = gs_popular_tile_get_instance_private (tile);

        g_clear_object (&priv->app);
	priv->app = g_object_ref (app);

        g_object_bind_property_full (priv->app, "state",
                                     priv->eventbox, "visible",
                                     G_BINDING_SYNC_CREATE,
                                     transform_state_func,
                                     NULL, NULL, NULL);

        gtk_image_set_from_pixbuf (GTK_IMAGE (priv->image), gs_app_get_pixbuf (priv->app));

        gtk_label_set_label (GTK_LABEL (priv->label), gs_app_get_name (app));
}

static void
gs_popular_tile_destroy (GtkWidget *widget)
{
	GsPopularTile *tile = GS_POPULAR_TILE (widget);
	GsPopularTilePrivate *priv;

        priv = gs_popular_tile_get_instance_private (tile);

	g_clear_object (&priv->app);

	GTK_WIDGET_CLASS (gs_popular_tile_parent_class)->destroy (widget);
}

static void
button_clicked (GsPopularTile *tile)
{
        g_signal_emit (tile, signals[SIGNAL_CLICKED], 0);
}

static void
gs_popular_tile_init (GsPopularTile *tile)
{
        GsPopularTilePrivate *priv;

        gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
        gtk_widget_init_template (GTK_WIDGET (tile));
        priv = gs_popular_tile_get_instance_private (tile);
        g_signal_connect_swapped (priv->button, "clicked",
                                  G_CALLBACK (button_clicked), tile);
}

static void
gs_popular_tile_class_init (GsPopularTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_popular_tile_destroy;

	signals [SIGNAL_CLICKED] =
		g_signal_new ("clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPopularTileClass, clicked),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

        gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/software/popular-tile.ui");

        gtk_widget_class_bind_template_child_private (widget_class, GsPopularTile, button);
        gtk_widget_class_bind_template_child_private (widget_class, GsPopularTile, label);
        gtk_widget_class_bind_template_child_private (widget_class, GsPopularTile, image);
        gtk_widget_class_bind_template_child_private (widget_class, GsPopularTile, eventbox);
}

GtkWidget *
gs_popular_tile_new (GsApp *app)
{
        GsPopularTile *tile;

        tile = g_object_new (GS_TYPE_POPULAR_TILE, NULL);
        gs_popular_tile_set_app (tile, app);

        return GTK_WIDGET (tile);
}

