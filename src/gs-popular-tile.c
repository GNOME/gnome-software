/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-popular-tile.h"
#include "gs-star-widget.h"
#include "gs-common.h"

struct _GsPopularTile
{
	GsAppTile	 parent_instance;
	GtkWidget	*label;
	GtkWidget	*image;
	GtkWidget	*eventbox;
	GtkWidget	*stack;
	GtkWidget	*stars;
};

G_DEFINE_TYPE (GsPopularTile, gs_popular_tile, GS_TYPE_APP_TILE)

static void
gs_popular_tile_refresh (GsAppTile *self)
{
	GsPopularTile *tile = GS_POPULAR_TILE (self);
	GsApp *app = gs_app_tile_get_app (self);
	AtkObject *accessible;
	gboolean installed;
	g_autofree gchar *name = NULL;
	const gchar *css;

	if (app == NULL)
		return;

	accessible = gtk_widget_get_accessible (GTK_WIDGET (tile));

	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		installed = TRUE;
		/* TRANSLATORS: this refers to an app (by name) that is installed */
		name = g_strdup_printf (_("%s (Installed)"),
					gs_app_get_name (app));
		break;
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_INSTALLING:
	default:
		installed = FALSE;
		name = g_strdup (gs_app_get_name (app));
		break;
	}

	gtk_widget_set_visible (tile->eventbox, installed);

	if (GTK_IS_ACCESSIBLE (accessible)) {
		atk_object_set_name (accessible, name);
		atk_object_set_description (accessible, gs_app_get_summary (app));
	}

	gtk_widget_set_sensitive (tile->stars, gs_app_get_rating (app) >= 0);
	gs_star_widget_set_rating (GS_STAR_WIDGET (tile->stars),
				   gs_app_get_rating (app));
	gtk_stack_set_visible_child_name (GTK_STACK (tile->stack), "content");

	/* perhaps set custom css */
	css = gs_app_get_metadata_item (app, "GnomeSoftware::PopularTile-css");
	gs_utils_widget_set_css (GTK_WIDGET (tile), "popular-tile", css);

	if (gs_app_get_pixbuf (app) != NULL) {
		gs_image_set_from_pixbuf (GTK_IMAGE (tile->image), gs_app_get_pixbuf (app));
	} else {
		gtk_image_set_from_icon_name (GTK_IMAGE (tile->image),
					      "application-x-executable",
					      GTK_ICON_SIZE_DIALOG);
	}

	gtk_label_set_label (GTK_LABEL (tile->label), gs_app_get_name (app));
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
	GsAppTileClass *app_tile_class = GS_APP_TILE_CLASS (klass);

	app_tile_class->refresh = gs_popular_tile_refresh;

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
	GsPopularTile *tile = g_object_new (GS_TYPE_POPULAR_TILE, NULL);
	if (app != NULL)
		gs_app_tile_set_app (GS_APP_TILE (tile), app);
	return GTK_WIDGET (tile);
}
