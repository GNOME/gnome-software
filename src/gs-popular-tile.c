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
#include "gs-utils.h"

struct _GsPopularTile
{
	GtkButton	 parent_instance;

	GsApp		*app;
	GtkWidget	*label;
	GtkWidget	*image;
	GtkWidget	*eventbox;
	GtkWidget	*stack;
	GtkWidget	*stars;
};

G_DEFINE_TYPE (GsPopularTile, gs_popular_tile, GTK_TYPE_BUTTON)

GsApp *
gs_popular_tile_get_app (GsPopularTile *tile)
{
	g_return_val_if_fail (GS_IS_POPULAR_TILE (tile), NULL);

	return tile->app;
}

static gboolean
app_state_changed_idle (gpointer user_data)
{
	GsPopularTile *tile = GS_POPULAR_TILE (user_data);
	AtkObject *accessible;
	GtkWidget *label;
	gboolean installed;
	g_autofree gchar *name = NULL;

	accessible = gtk_widget_get_accessible (GTK_WIDGET (tile));

	label = gtk_bin_get_child (GTK_BIN (tile->eventbox));
	switch (gs_app_get_state (tile->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_INSTALLING:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (tile->app),
					_("Installed"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user the application is installed */
		gtk_label_set_label (GTK_LABEL (label), _("Installed"));
		break;
	case AS_APP_STATE_AVAILABLE:
	default:
		installed = FALSE;
		name = g_strdup (gs_app_get_name (tile->app));
		break;
	}

	gtk_widget_set_visible (tile->eventbox, installed);

	if (GTK_IS_ACCESSIBLE (accessible)) {
		atk_object_set_name (accessible, name);
		atk_object_set_description (accessible, gs_app_get_summary (tile->app));
	}

	g_object_unref (tile);
	return G_SOURCE_REMOVE;
}

static void
app_state_changed (GsApp *app, GParamSpec *pspec, GsPopularTile *tile)
{
	g_idle_add (app_state_changed_idle, g_object_ref (tile));
}

void
gs_popular_tile_set_app (GsPopularTile *tile, GsApp *app)
{
	const gchar *css;

	g_return_if_fail (GS_IS_POPULAR_TILE (tile));
	g_return_if_fail (GS_IS_APP (app) || app == NULL);

	if (tile->app)
		g_signal_handlers_disconnect_by_func (tile->app, app_state_changed, tile);

	g_set_object (&tile->app, app);
	if (!app)
		return;

	if (gs_app_get_rating (tile->app) >= 0) {
		gtk_widget_set_visible (tile->stars, TRUE);
		gs_star_widget_set_rating (GS_STAR_WIDGET (tile->stars),
					   gs_app_get_rating (tile->app));
	} else {
		gtk_widget_set_visible (tile->stars, FALSE);
	}
	gtk_stack_set_visible_child_name (GTK_STACK (tile->stack), "content");

	g_signal_connect (tile->app, "notify::state",
		 	  G_CALLBACK (app_state_changed), tile);
	app_state_changed (tile->app, NULL, tile);

	/* set custom css */
	css = gs_app_get_metadata_item (app, "GnomeSoftware::PopularTile-css");
	if (css != NULL)
		gs_utils_widget_set_custom_css (GTK_WIDGET (tile), css);

	gs_image_set_from_pixbuf (GTK_IMAGE (tile->image), gs_app_get_pixbuf (tile->app));

	gtk_label_set_label (GTK_LABEL (tile->label), gs_app_get_name (app));
}

static void
gs_popular_tile_destroy (GtkWidget *widget)
{
	GsPopularTile *tile = GS_POPULAR_TILE (widget);

	if (tile->app)
		g_signal_handlers_disconnect_by_func (tile->app, app_state_changed, tile);

	g_clear_object (&tile->app);

	GTK_WIDGET_CLASS (gs_popular_tile_parent_class)->destroy (widget);
}

static void
gs_popular_tile_init (GsPopularTile *tile)
{
	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	gtk_widget_init_template (GTK_WIDGET (tile));
	gs_star_widget_set_icon_size (GS_STAR_WIDGET (tile->stars), 12);
}

static void
gs_popular_tile_class_init (GsPopularTileClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_popular_tile_destroy;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-popular-tile.ui");

	gtk_widget_class_bind_template_child (widget_class, GsPopularTile, label);
	gtk_widget_class_bind_template_child (widget_class, GsPopularTile, image);
	gtk_widget_class_bind_template_child (widget_class, GsPopularTile, eventbox);
	gtk_widget_class_bind_template_child (widget_class, GsPopularTile, stack);
	gtk_widget_class_bind_template_child (widget_class, GsPopularTile, stars);
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
