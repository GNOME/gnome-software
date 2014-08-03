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

#include "gs-popular-tile.h"
#include "gs-star-widget.h"

struct _GsPopularTilePrivate
{
	GsApp		*app;
	GtkWidget	*label;
	GtkWidget	*image;
	GtkWidget	*eventbox;
	GtkWidget	*stars;
	GtkWidget	*waiting;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsPopularTile, gs_popular_tile, GTK_TYPE_BUTTON)

GsApp *
gs_popular_tile_get_app (GsPopularTile *tile)
{
	GsPopularTilePrivate *priv;

	g_return_val_if_fail (GS_IS_POPULAR_TILE (tile), NULL);

	priv = gs_popular_tile_get_instance_private (tile);
	return priv->app;
}

static void
app_state_changed (GsApp *app, GParamSpec *pspec, GsPopularTile *tile)
{
	AtkObject *accessible;
	GsPopularTilePrivate *priv;
	GtkWidget *label;
	gboolean installed;
	gchar *name;

	priv = gs_popular_tile_get_instance_private (tile);
	accessible = gtk_widget_get_accessible (GTK_WIDGET (tile));

	label = gtk_bin_get_child (GTK_BIN (priv->eventbox));
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_INSTALLING:
	case AS_APP_STATE_REMOVING:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (app),
					_("Installed"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user the application is installed */
		gtk_label_set_label (GTK_LABEL (label), _("Installed"));
		break;
	case AS_APP_STATE_UPDATABLE:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (app),
					_("Updates"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user there is an update for the installed
		 * application available */
		gtk_label_set_label (GTK_LABEL (label), _("Updates"));
		break;
	case AS_APP_STATE_AVAILABLE:
	default:
		installed = FALSE;
		name = g_strdup (gs_app_get_name (app));
		break;
	}

	gtk_widget_set_visible (priv->eventbox, installed);

	if (GTK_IS_ACCESSIBLE (accessible)) {
		atk_object_set_name (accessible, name);
		atk_object_set_description (accessible, gs_app_get_summary (app));
	}
	g_free (name);
}

void
gs_popular_tile_set_app (GsPopularTile *tile, GsApp *app)
{
	GsPopularTilePrivate *priv;

	g_return_if_fail (GS_IS_POPULAR_TILE (tile));
	g_return_if_fail (GS_IS_APP (app) || app == NULL);

	priv = gs_popular_tile_get_instance_private (tile);

	g_clear_object (&priv->app);
	if (!app)
		return;
	priv->app = g_object_ref (app);

	if (gs_app_get_rating_kind (priv->app) == GS_APP_RATING_KIND_USER) {
		gs_star_widget_set_rating (GS_STAR_WIDGET (priv->stars),
		                           GS_APP_RATING_KIND_USER,
		                           gs_app_get_rating (priv->app));
	} else {
		gs_star_widget_set_rating (GS_STAR_WIDGET (priv->stars),
		                           GS_APP_RATING_KIND_KUDOS,
		                           gs_app_get_kudos_percentage (priv->app));
	}
	gtk_widget_show (priv->stars);

        gtk_widget_hide (priv->waiting);

	g_signal_connect (priv->app, "notify::state",
		 	  G_CALLBACK (app_state_changed), tile);
	app_state_changed (priv->app, NULL, tile);

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
gs_popular_tile_init (GsPopularTile *tile)
{
	GsPopularTilePrivate *priv;

	priv = gs_popular_tile_get_instance_private (tile);

	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	gtk_widget_init_template (GTK_WIDGET (tile));
	gs_star_widget_set_icon_size (GS_STAR_WIDGET (priv->stars), 12);
}

static void
gs_popular_tile_class_init (GsPopularTileClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_popular_tile_destroy;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/popular-tile.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsPopularTile, label);
	gtk_widget_class_bind_template_child_private (widget_class, GsPopularTile, image);
	gtk_widget_class_bind_template_child_private (widget_class, GsPopularTile, eventbox);
	gtk_widget_class_bind_template_child_private (widget_class, GsPopularTile, stars);
	gtk_widget_class_bind_template_child_private (widget_class, GsPopularTile, waiting);
}

GtkWidget *
gs_popular_tile_new (GsApp *app)
{
	GsPopularTile *tile;

	tile = g_object_new (GS_TYPE_POPULAR_TILE, NULL);
	gs_popular_tile_set_app (tile, app);

	return GTK_WIDGET (tile);
}

/* vim: set noexpandtab: */
