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

/* A colour represented in hue, saturation, brightness form; with an additional
 * field for its contrast calculated with respect to some external colour.
 *
 * See https://en.wikipedia.org/wiki/HSL_and_HSV */
typedef struct
{
	gdouble hue;  /* [0.0, 1.0] */
	gdouble saturation;  /* [0.0, 1.0] */
	gdouble brightness;  /* [0.0, 1.0]; also known as lightness (HSL) or value (HSV) */
	gdouble contrast;  /* [-1.0, ∞], may actually be `INF` */
} GsHSBC;

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

/* These are subjectively chosen. See below. */
static const gdouble min_valid_saturation = 0.5;
static const gdouble max_valid_saturation = 0.85;

/* Subjectively chosen as the minimum absolute contrast ratio between the
 * foreground and background colours.
 *
 * Note that contrast is in the range [-1.0, ∞], so @min_abs_contrast always has
 * to be handled with positive and negative branches.
 */
static const gdouble min_abs_contrast = 0.78;

/* Sort two candidate background colours for the feature tile, ranking them by
 * suitability for being chosen as the background colour, with the most suitable
 * first.
 *
 * There are several criteria being used here:
 *  1. First, colours are sorted by whether their saturation is in the range
 *     [0.5, 0.85], which is a subjectively-chosen range of ‘light, but not too
 *     saturated’ colours.
 *  2. Colours with saturation in that valid range are then sorted by contrast,
 *     with higher contrast being preferred. The contrast is calculated against
 *     an external colour by the caller.
 *  3. Colours with saturation outside that valid range are sorted by their
 *     absolute distance from the range, so that colours which are nearer to
 *     having a valid saturation are preferred. This is useful in the case where
 *     none of the key colours in this array have valid saturations; the caller
 *     will want the one which is closest to being valid.
 */
static gboolean
saturation_is_valid (const GsHSBC *hsbc,
                     gdouble      *distance_from_valid_range)
{
	*distance_from_valid_range = (hsbc->saturation > max_valid_saturation) ? hsbc->saturation - max_valid_saturation : min_valid_saturation - hsbc->saturation;
	return (hsbc->saturation >= min_valid_saturation && hsbc->saturation <= max_valid_saturation);
}

static gint
colors_sort_cb (gconstpointer a,
		gconstpointer b)
{
	const GsHSBC *hsbc_a = a;
	const GsHSBC *hsbc_b = b;
	gdouble hsbc_a_distance_from_range, hsbc_b_distance_from_range;
	gboolean hsbc_a_saturation_in_range = saturation_is_valid (hsbc_a, &hsbc_a_distance_from_range);
	gboolean hsbc_b_saturation_in_range = saturation_is_valid (hsbc_b, &hsbc_b_distance_from_range);

	if (hsbc_a_saturation_in_range && !hsbc_b_saturation_in_range)
		return -1;
	else if (!hsbc_a_saturation_in_range && hsbc_b_saturation_in_range)
		return 1;
	else if (!hsbc_a_saturation_in_range && !hsbc_b_saturation_in_range)
		return hsbc_a_distance_from_range - hsbc_b_distance_from_range;
	else
		return ABS (hsbc_b->contrast) - ABS (hsbc_a->contrast);
}

/* Calculate the weber contrast between @foreground and @background. This is
 * only valid if the area covered by @foreground is significantly smaller than
 * that covered by @background.
 *
 * See https://en.wikipedia.org/wiki/Contrast_(vision)#Weber_contrast
 *
 * The return value is in the range [-1.0, ∞], and may actually be `INF`.
 */
static gdouble
weber_contrast (const GsHSBC *foreground,
                const GsHSBC *background)
{
	/* Note that this may divide by zero, and that’s fine. However, in
	 * IEEE 754, dividing ±0.0 by ±0.0 results in NAN, so avoid that. */
	if (foreground->brightness == background->brightness)
		return 0.0;

	return (foreground->brightness - background->brightness) / background->brightness;
}

/* Inverse of the Weber contrast function which finds a brightness (luminance)
 * level for the background which gives an absolute contrast of at least
 * @desired_abs_contrast against @foreground. The same validity restrictions
 * apply as for weber_contrast().
 *
 * The return value is in the range [0.0, 1.0].
 */
static gdouble
weber_contrast_find_brightness (const GsHSBC *foreground,
                                gdouble       desired_abs_contrast)
{
	g_assert (desired_abs_contrast >= 0.0);

	/* There are two solutions to solving
	 *    |(I - I_B) / I_B| ≥ C
	 * in the general case, although given that I (`foreground->brightness`)
	 * and I_B (the return value) are only valid in the range [0.0, 1.0],
	 * there are many cases where only one solution is valid.
	 *
	 * Solutions are:
	 *    I_B ≤ I / (1 + C)
	 *    I_B ≥ I / (1 - C)
	 *
	 * When given a choice, prefer the solution which gives a higher
	 * brightness.
	 *
	 * In the case I == 0.0, and value of I_B is valid (as per the second
	 * solution), so arbitrarily choose 0.5 as a solution.
	 */
	if (foreground->brightness == 0.0)
		return 0.5;
	else if (foreground->brightness <= 1.0 - desired_abs_contrast &&
		 desired_abs_contrast < 1.0)
		return foreground->brightness / (1.0 - desired_abs_contrast);
	else
		return foreground->brightness / (1.0 + desired_abs_contrast);
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
	g_autoptr(GIcon) icon = NULL;
	guint icon_size;

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
	if (!tile->narrow_mode) {
		icon = gs_app_get_icon_for_size (app,
						 160,
						 gtk_widget_get_scale_factor (tile->image),
						 NULL);
		icon_size = 160;
	}
	if (icon == NULL) {
		icon = gs_app_get_icon_for_size (app,
						 128,
						 gtk_widget_get_scale_factor (tile->image),
						 NULL);
		icon_size = 128;
	}

	if (icon != NULL) {
		gtk_image_set_from_gicon (GTK_IMAGE (tile->image), icon, GTK_ICON_SIZE_INVALID);
		gtk_image_set_pixel_size (GTK_IMAGE (tile->image), icon_size);
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

		/* If there is no override CSS for the app, default to a solid
		 * background colour based on the app’s key colors.
		 *
		 * Choose an arbitrary key color from the app’s key colors, and
		 * ensure that it’s:
		 *  - a light, not too saturated version of the dominant color
		 *    of the icon
		 *  - always light enough that grey text is visible on it
		 *
		 * Cache the result until the app’s key colours change, as the
		 * amount of calculation going on here is not entirely trivial.
		 */
		if (key_colors != tile->key_colors_cache) {
			g_autoptr(GArray) colors = NULL;
			GdkRGBA fg_rgba;
			gboolean fg_rgba_valid;
			GsHSBC fg_hsbc;

			/* Look up the foreground colour for the feature tile,
			 * which is the colour of the text. This should always
			 * be provided as a named colour by the theme.
			 *
			 * Knowing the foreground colour allows calculation of
			 * the contrast between candidate background colours and
			 * the foreground which will be rendered on top of them.
			 *
			 * We want to choose a background colour with at least
			 * @min_abs_contrast contrast with the foreground, so
			 * that the text is legible.
			 */
			fg_rgba_valid = gtk_style_context_lookup_color (context, "theme_fg_color", &fg_rgba);
			g_assert (fg_rgba_valid);

			gtk_rgb_to_hsv (fg_rgba.red, fg_rgba.green, fg_rgba.blue,
					&fg_hsbc.hue, &fg_hsbc.saturation, &fg_hsbc.brightness);

			g_debug ("FG color: RGB: (%f, %f, %f), HSB: (%f, %f, %f)",
				 fg_rgba.red, fg_rgba.green, fg_rgba.blue,
				 fg_hsbc.hue, fg_hsbc.saturation, fg_hsbc.brightness);

			/* Convert all the RGBA key colours to HSB, and
			 * calculate their contrast against the foreground
			 * colour.
			 *
			 * The contrast is calculated as the Weber contrast,
			 * which is valid for small amounts of foreground colour
			 * (i.e. text) against larger background areas. Contrast
			 * is strictly calculated using luminance, but it’s OK
			 * to subjectively calculate it using brightness, as
			 * brightness is the subjective impression of luminance.
			 */
			if (key_colors != NULL)
				colors = g_array_sized_new (FALSE, FALSE, sizeof (GsHSBC), key_colors->len);

			g_debug ("Candidate background colors for %s:", gs_app_get_id (app));
			for (guint i = 0; key_colors != NULL && i < key_colors->len; i++) {
				const GdkRGBA *rgba = &g_array_index (key_colors, GdkRGBA, i);
				GsHSBC hsbc;

				gtk_rgb_to_hsv (rgba->red, rgba->green, rgba->blue,
						&hsbc.hue, &hsbc.saturation, &hsbc.brightness);
				hsbc.contrast = weber_contrast (&fg_hsbc, &hsbc);
				g_array_append_val (colors, hsbc);

				g_debug (" • RGB: (%f, %f, %f), HSB: (%f, %f, %f), contrast: %f",
					 rgba->red, rgba->green, rgba->blue,
					 hsbc.hue, hsbc.saturation, hsbc.brightness,
					 hsbc.contrast);
			}

			/* Sort the candidate background colours to find the
			 * most appropriate one. */
			g_array_sort (colors, colors_sort_cb);

			/* Take the top colour. If it’s not good enough, modify
			 * its brightness to improve the contrast, and clamp its
			 * saturation to the valid range. */
			if (colors != NULL && colors->len > 0) {
				const GsHSBC *chosen_hsbc = &g_array_index (colors, GsHSBC, 0);
				GdkRGBA chosen_rgba;
				gdouble modified_saturation, modified_brightness;

				modified_saturation = CLAMP (chosen_hsbc->saturation, min_valid_saturation, max_valid_saturation);

				if (chosen_hsbc->contrast < -min_abs_contrast ||
				    chosen_hsbc->contrast > min_abs_contrast)
					modified_brightness = chosen_hsbc->brightness;
				else
					modified_brightness = weber_contrast_find_brightness (&fg_hsbc, min_abs_contrast);

				gtk_hsv_to_rgb (chosen_hsbc->hue,
						modified_saturation,
						modified_brightness,
						&chosen_rgba.red, &chosen_rgba.green, &chosen_rgba.blue);

				g_debug ("Chosen background colour for %s (saturation %s, brightness %s): RGB: (%f, %f, %f), HSB: (%f, %f, %f)",
					 gs_app_get_id (app),
					 (modified_saturation == chosen_hsbc->saturation) ? "not modified" : "modified",
					 (modified_brightness == chosen_hsbc->brightness) ? "not modified" : "modified",
					 chosen_rgba.red, chosen_rgba.green, chosen_rgba.blue,
					 chosen_hsbc->hue, modified_saturation, modified_brightness);

				css = g_strdup_printf ("background-color: rgb(%.0f,%.0f,%.0f);",
						       chosen_rgba.red * 255.f,
						       chosen_rgba.green * 255.f,
						       chosen_rgba.blue * 255.f);
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
gs_feature_tile_style_updated (GtkWidget *widget)
{
	GsFeatureTile *tile = GS_FEATURE_TILE (widget);

	/* Clear the key colours cache, as the tile background colour will
	 * potentially need recalculating if the widget’s foreground colour has
	 * changed. */
	tile->key_colors_cache = NULL;

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
	widget_class->style_updated = gs_feature_tile_style_updated;
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
	return g_object_new (GS_TYPE_FEATURE_TILE,
			     "vexpand", FALSE,
			     "app", app,
			     NULL);
}
