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
#include "gs-star-widget.h"

struct _GsAppTilePrivate
{
	GsApp		*app;
	GtkWidget	*button;
	GtkWidget	*image;
	GtkWidget	*name;
	GtkWidget	*summary;
	GtkWidget	*eventbox;
	GtkWidget	*waiting;
	GtkWidget	*stars;
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

static void
app_state_changed (GsApp *app, GParamSpec *pspec, GsAppTile *tile)
{
	AtkObject *accessible;
	GsAppTilePrivate *priv;
	GtkWidget *label;
	gboolean installed;
	gchar *name;

        priv = gs_app_tile_get_instance_private (tile);
        accessible = gtk_widget_get_accessible (priv->button);

	label = gtk_bin_get_child (GTK_BIN (priv->eventbox));
	switch (gs_app_get_state (app)) {
	case GS_APP_STATE_INSTALLED:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (app),
					_("Installed"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user the application is installed */
		gtk_label_set_label (GTK_LABEL (label), _("Installed"));
		break;
	case GS_APP_STATE_INSTALLING:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (app),
					_("Installing"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user the application is being installing */
		gtk_label_set_label (GTK_LABEL (label), _("Installing"));
		break;
	case GS_APP_STATE_REMOVING:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (app),
					_("Removing"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user the application is being removed */
		gtk_label_set_label (GTK_LABEL (label), _("Removing"));
		break;
	case GS_APP_STATE_UPDATABLE:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (app),
					_("Updates"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user there is an update for the installed
		 * application available */
		gtk_label_set_label (GTK_LABEL (label), _("Updates"));
		break;
        case GS_APP_STATE_QUEUED:
        case GS_APP_STATE_AVAILABLE:
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
gs_app_tile_set_app (GsAppTile *tile, GsApp *app)
{
	GsAppTilePrivate *priv;
	const gchar *summary;

	g_return_if_fail (GS_IS_APP_TILE (tile));
	g_return_if_fail (GS_IS_APP (app) || app == NULL);

	priv = gs_app_tile_get_instance_private (tile);

	gtk_image_clear (GTK_IMAGE (priv->image));
	gtk_image_set_pixel_size (GTK_IMAGE (priv->image), 64);

	if (priv->app)
		g_signal_handlers_disconnect_by_func (priv->app, app_state_changed, tile);

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

	gtk_widget_hide (priv->waiting);

        g_signal_connect (priv->app, "notify::state",
                          G_CALLBACK (app_state_changed), tile);
        app_state_changed (priv->app, NULL, tile);

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

	if (priv->app)
		g_signal_handlers_disconnect_by_func (priv->app, app_state_changed, tile);
	g_clear_object (&priv->app);

	GTK_WIDGET_CLASS (gs_app_tile_parent_class)->destroy (widget);
}

static void
button_clicked (GsAppTile *tile)
{
	GsAppTilePrivate *priv;

	priv = gs_app_tile_get_instance_private (tile);
	if (priv->app)
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
	gs_star_widget_set_icon_size (GS_STAR_WIDGET (priv->stars), 12);
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
        gtk_widget_class_bind_template_child_private (widget_class, GsAppTile, eventbox);
        gtk_widget_class_bind_template_child_private (widget_class, GsAppTile, waiting);
        gtk_widget_class_bind_template_child_private (widget_class, GsAppTile, stars);
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
