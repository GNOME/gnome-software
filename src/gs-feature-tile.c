/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-feature-tile.h"
#include "gs-common.h"
#include "gs-css.h"

struct _GsFeatureTile
{
	GsAppTile	 parent_instance;
	GtkWidget	*stack;
	GtkWidget	*title;
	GtkWidget	*subtitle;
	const gchar	*markup_cache;  /* (unowned) (nullable) */
	GtkCssProvider	*tile_provider;  /* (owned) (nullable) */
	GtkCssProvider	*title_provider;  /* (owned) (nullable) */
	GtkCssProvider	*subtitle_provider;  /* (owned) (nullable) */
};

G_DEFINE_TYPE (GsFeatureTile, gs_feature_tile, GS_TYPE_APP_TILE)

static void
gs_feature_tile_dispose (GObject *object)
{
	GsFeatureTile *tile = GS_FEATURE_TILE (object);

	g_clear_object (&tile->tile_provider);
	g_clear_object (&tile->title_provider);
	g_clear_object (&tile->subtitle_provider);

	G_OBJECT_CLASS (gs_feature_tile_parent_class)->dispose (object);
}

static void
gs_feature_tile_refresh (GsAppTile *self)
{
	GsFeatureTile *tile = GS_FEATURE_TILE (self);
	GsApp *app = gs_app_tile_get_app (self);
	AtkObject *accessible;
	const gchar *markup;
	g_autofree gchar *name = NULL;

	if (app == NULL)
		return;

	gtk_stack_set_visible_child_name (GTK_STACK (tile->stack), "content");

	/* update text */
	gtk_label_set_label (GTK_LABEL (tile->title), gs_app_get_name (app));
	gtk_label_set_label (GTK_LABEL (tile->subtitle), gs_app_get_summary (app));

	/* perhaps set custom css; cache it so that images don’t get reloaded
	 * unnecessarily */
	markup = gs_app_get_metadata_item (app, "GnomeSoftware::FeatureTile-css");
	if (tile->markup_cache != markup) {
		g_autoptr(GsCss) css = gs_css_new ();
		if (markup != NULL)
			gs_css_parse (css, markup, NULL);
		gs_utils_widget_set_css (GTK_WIDGET (tile), &tile->tile_provider, "feature-tile",
					 gs_css_get_markup_for_id (css, "tile"));
		gs_utils_widget_set_css (tile->title, &tile->title_provider, "feature-tile-name",
					 gs_css_get_markup_for_id (css, "name"));
		gs_utils_widget_set_css (tile->subtitle, &tile->subtitle_provider, "feature-tile-subtitle",
					 gs_css_get_markup_for_id (css, "summary"));
		tile->markup_cache = markup;
	}

	accessible = gtk_widget_get_accessible (GTK_WIDGET (tile));

	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (app),
					_("Installed"));
		break;
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_INSTALLING:
	default:
		name = g_strdup (gs_app_get_name (app));
		break;
	}

	if (GTK_IS_ACCESSIBLE (accessible) && name != NULL) {
		atk_object_set_name (accessible, name);
		atk_object_set_description (accessible, gs_app_get_summary (app));
	}
}

static void
gs_feature_tile_init (GsFeatureTile *tile)
{
	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	gtk_widget_init_template (GTK_WIDGET (tile));
}

static void
gs_feature_tile_class_init (GsFeatureTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GsAppTileClass *app_tile_class = GS_APP_TILE_CLASS (klass);

	object_class->dispose = gs_feature_tile_dispose;

	app_tile_class->refresh = gs_feature_tile_refresh;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-feature-tile.ui");

	gtk_widget_class_bind_template_child (widget_class, GsFeatureTile, stack);
	gtk_widget_class_bind_template_child (widget_class, GsFeatureTile, title);
	gtk_widget_class_bind_template_child (widget_class, GsFeatureTile, subtitle);
}

GtkWidget *
gs_feature_tile_new (GsApp *app)
{
	GsFeatureTile *tile;
	tile = g_object_new (GS_TYPE_FEATURE_TILE,
			     "vexpand", FALSE,
			     NULL);
	if (app != NULL)
		gs_app_tile_set_app (GS_APP_TILE (tile), app);
	return GTK_WIDGET (tile);
}
