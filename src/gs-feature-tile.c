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
	GtkWidget	*image;
	GtkWidget	*title;
	GtkWidget	*subtitle;
	const gchar	*markup_cache;  /* (unowned) (nullable) */
	GtkCssProvider	*tile_provider;  /* (owned) (nullable) */
	GtkCssProvider	*title_provider;  /* (owned) (nullable) */
	GtkCssProvider	*subtitle_provider;  /* (owned) (nullable) */
	GArray		*key_colors_cache;  /* (unowned) (nullable) */
	gboolean	 narrow_mode;
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
	const gchar *markup = NULL;
	g_autofree gchar *name = NULL;
	GtkStyleContext *context;
	g_autoptr(GdkPixbuf) pixbuf = NULL;

	if (app == NULL)
		return;

	gtk_stack_set_visible_child_name (GTK_STACK (tile->stack), "content");

	/* Set the narrow mode. */
	context = gtk_widget_get_style_context (GTK_WIDGET (self));
	if (tile->narrow_mode)
		gtk_style_context_add_class (context, "narrow");
	else
		gtk_style_context_remove_class (context, "narrow");

	/* Update the icon. Try a 160px version if not in narrow mode, and it’s
	 * available; otherwise use 128px. */
	if (!tile->narrow_mode)
		pixbuf = gs_app_load_pixbuf (app, 160 * gtk_widget_get_scale_factor (tile->image));
	if (pixbuf == NULL)
		pixbuf = gs_app_load_pixbuf (app, 128 * gtk_widget_get_scale_factor (tile->image));
	if (pixbuf != NULL) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (tile->image), pixbuf);
		gtk_widget_show (tile->image);
	} else {
		gtk_widget_hide (tile->image);
	}

	/* Update text and let it wrap if the widget is narrow. */
	gtk_label_set_label (GTK_LABEL (tile->title), gs_app_get_name (app));
	gtk_label_set_label (GTK_LABEL (tile->subtitle), gs_app_get_summary (app));

	gtk_label_set_line_wrap (GTK_LABEL (tile->subtitle), tile->narrow_mode);
	gtk_label_set_lines (GTK_LABEL (tile->subtitle), tile->narrow_mode ? 2 : 1);

	/* perhaps set custom css; cache it so that images don’t get reloaded
	 * unnecessarily. The custom CSS is direction-dependent, and will be
	 * reloaded when the direction changes. If RTL CSS isn’t set, fall back
	 * to the LTR CSS. */
	if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
		markup = gs_app_get_metadata_item (app, "GnomeSoftware::FeatureTile-css-rtl");
	if (markup == NULL)
		markup = gs_app_get_metadata_item (app, "GnomeSoftware::FeatureTile-css");

	if (tile->markup_cache != markup && markup != NULL) {
		g_autoptr(GsCss) css = gs_css_new ();
		g_autofree gchar *modified_markup = gs_utils_set_key_colors_in_css (markup, app);
		if (modified_markup != NULL)
			gs_css_parse (css, modified_markup, NULL);
		gs_utils_widget_set_css (GTK_WIDGET (tile), &tile->tile_provider, "feature-tile",
					 gs_css_get_markup_for_id (css, "tile"));
		gs_utils_widget_set_css (tile->title, &tile->title_provider, "feature-tile-name",
					 gs_css_get_markup_for_id (css, "name"));
		gs_utils_widget_set_css (tile->subtitle, &tile->subtitle_provider, "feature-tile-subtitle",
					 gs_css_get_markup_for_id (css, "summary"));
		tile->markup_cache = markup;
	} else if (markup == NULL) {
		GArray *key_colors = gs_app_get_key_colors (app);
		g_autofree gchar *css = NULL;

		if (key_colors != tile->key_colors_cache) {
			/* If there is no override CSS for the app, default to a solid
			 * background colour based on the app’s key colors.
			 *
			 * Choose an arbitrary key color from the app’s key colors, and
			 * hope that it’s:
			 *  - a light, not too saturated version of the dominant color
			 *    of the icon
			 *  - always light enough that grey text is visible on it
			 */
			if (key_colors != NULL && key_colors->len > 0) {
				const GdkRGBA *color = &g_array_index (key_colors, GdkRGBA, key_colors->len - 1);

				css = g_strdup_printf (
					"background-color: rgb(%.0f,%.0f,%.0f);",
					color->red * 255.f,
					color->green * 255.f,
					color->blue * 255.f);
			}

			gs_utils_widget_set_css (GTK_WIDGET (tile), &tile->tile_provider, "feature-tile", css);
			gs_utils_widget_set_css (tile->title, &tile->title_provider, "feature-tile-name", NULL);
			gs_utils_widget_set_css (tile->subtitle, &tile->subtitle_provider, "feature-tile-subtitle", NULL);

			tile->key_colors_cache = key_colors;
		}
	}

	accessible = gtk_widget_get_accessible (GTK_WIDGET (tile));

	switch (gs_app_get_state (app)) {
	case GS_APP_STATE_INSTALLED:
	case GS_APP_STATE_REMOVING:
	case GS_APP_STATE_UPDATABLE:
	case GS_APP_STATE_UPDATABLE_LIVE:
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (app),
					_("Installed"));
		break;
	case GS_APP_STATE_AVAILABLE:
	case GS_APP_STATE_INSTALLING:
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
gs_feature_tile_direction_changed (GtkWidget *widget, GtkTextDirection previous_direction)
{
	GsFeatureTile *tile = GS_FEATURE_TILE (widget);

	gs_feature_tile_refresh (GS_APP_TILE (tile));
}

static void
gs_feature_tile_size_allocate (GtkWidget     *widget,
                               GtkAllocation *allocation)
{
	GsFeatureTile *tile = GS_FEATURE_TILE (widget);
	gboolean narrow_mode;

	/* Chain up. */
	GTK_WIDGET_CLASS (gs_feature_tile_parent_class)->size_allocate (widget, allocation);

	/* Engage ‘narrow mode’ if the allocation becomes too narrow. The exact
	 * choice of width is arbitrary here. */
	narrow_mode = (allocation->width < 600);
	if (tile->narrow_mode != narrow_mode) {
		tile->narrow_mode = narrow_mode;
		gs_feature_tile_refresh (GS_APP_TILE (tile));
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

	widget_class->direction_changed = gs_feature_tile_direction_changed;
	widget_class->size_allocate = gs_feature_tile_size_allocate;

	app_tile_class->refresh = gs_feature_tile_refresh;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-feature-tile.ui");

	gtk_widget_class_bind_template_child (widget_class, GsFeatureTile, stack);
	gtk_widget_class_bind_template_child (widget_class, GsFeatureTile, image);
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
