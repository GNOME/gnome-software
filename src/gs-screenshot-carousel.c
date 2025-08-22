/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2015-2019 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2019 Joaquim Rocha <jrocha@endlessm.com>
 * Copyright (C) 2021 Adrien Plazas <adrien.plazas@puri.sm>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-screenshot-carousel
 * @short_description: A carousel presenting the screenshots of a #GsApp
 *
 * #GsScreenshotCarousel loads screenshots from a #GsApp and present them to the
 * users.
 *
 * If the carousel doesn't have any screenshot to display, an empty state
 * fallback will be presented, and it will be considered to have screenshots as
 * long as it is trying to load some.
 *
 * Since: 41
 */

#include "config.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <math.h>
#include <string.h>

#include "gs-common.h"
#include "gs-download-utils.h"
#include "gs-utils.h"

#include "gs-screenshot-carousel.h"
#include "gs-screenshot-image.h"

struct _GsScreenshotCarousel
{
	GtkWidget		 parent_instance;

	SoupSession		*session;  /* (owned) (not nullable) */
	gboolean		 has_screenshots;

	GtkWidget		*button_next;
	GtkWidget		*button_next_revealer;
	GtkWidget		*button_previous;
	GtkWidget		*button_previous_revealer;
	GtkWidget		*carousel;
	GtkWidget		*carousel_indicator;
	GtkStack                *stack;
};

typedef enum {
	PROP_HAS_SCREENSHOTS = 1,
} GsScreenshotCarouselProperty;

static GParamSpec *obj_props[PROP_HAS_SCREENSHOTS + 1] = { NULL, };

G_DEFINE_TYPE (GsScreenshotCarousel, gs_screenshot_carousel, GTK_TYPE_WIDGET)

static void
_set_state (GsScreenshotCarousel *self, guint length, gboolean allow_fallback, gboolean is_online)
{
	gboolean has_screenshots;

	gtk_widget_set_visible (self->carousel_indicator, length > 1);
	gtk_stack_set_visible_child_name (self->stack, length > 0 ? "carousel" : "fallback");

	has_screenshots = length > 0 || (allow_fallback && is_online);
	if (self->has_screenshots != has_screenshots) {
		self->has_screenshots = has_screenshots;
		g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_HAS_SCREENSHOTS]);
	}
}

static void
gs_screenshot_carousel_img_clicked_cb (GtkWidget *ssimg,
				       gpointer user_data)
{
	GsScreenshotCarousel *self = user_data;
	adw_carousel_scroll_to (ADW_CAROUSEL (self->carousel), ssimg, TRUE);
}

static gboolean
is_current_environment (const gchar *env,
			const gchar *current_desktop)
{
	size_t len = current_desktop ? strlen (current_desktop) : 0;
	if (current_desktop == NULL || *current_desktop == '\0')
		return FALSE;
	return g_str_has_prefix (env, current_desktop) && (env[len] == '\0' || env[len] == ':');
}

typedef struct {
	GHashTable *indexes; /* AsScreenshot *~>gint, index in original array */
	char *desktop;
	gboolean is_dark;
} SortData;

/* Sort function to sort screenshots by environment (e.g. light/dark theme).
 *
 * Screenshots are sorted by:
 *  - Darkness/Lightness: if the user has a dark theme, dark screenshots are ordered first, otherwise light ones are
 *  - Then theme: screenshots whose environment matches the user’s desktop theme are then ordered first
 *  - Then the screenshots’ original order in the metainfo file
 */
static gint
_sort_by_environment_cb (gconstpointer aa,
			 gconstpointer bb,
			 gpointer user_data)
{
	AsScreenshot *screenshot1 = (AsScreenshot *) aa;
	AsScreenshot *screenshot2 = (AsScreenshot *) bb;
	SortData *sd = user_data;
	const char *env1, *env2;
	gboolean is_current_env1, is_current_env2;
	gboolean is_dark1, is_dark2;

	env1 = as_screenshot_get_environment (screenshot1);
	env2 = as_screenshot_get_environment (screenshot2);

	if (env1 == NULL || *env1 == '\0') {
		is_current_env1 = FALSE;
		is_dark1 = FALSE;
	} else {
		is_current_env1 = is_current_environment (env1, sd->desktop);
		is_dark1 = g_str_has_suffix (env1, ":dark");
	}
	if (env2 == NULL || *env2 == '\0') {
		is_current_env2 = FALSE;
		is_dark2 = FALSE;
	} else {
		is_current_env2 = is_current_environment (env2, sd->desktop);
		is_dark2 = g_str_has_suffix (env2, ":dark");
	}

	if (is_dark1 != is_dark2) {
		gint multiplier = sd->is_dark ? 1 : -1;

		if (is_dark1)
			return -1 * multiplier;
		else
			return 1 * multiplier;
	}

	if (is_current_env1 != is_current_env2) {
		if (is_current_env1)
			return -1;
		else
			return 1;
	}

	return (int) GPOINTER_TO_UINT (g_hash_table_lookup (sd->indexes, screenshot1)) -
	       (int) GPOINTER_TO_UINT (g_hash_table_lookup (sd->indexes, screenshot2));
}

/**
 * gs_screenshot_carousel_load_screenshots:
 * @self: a #GsScreenshotCarousel
 * @app: app to load the screenshots for
 * @is_online: %TRUE if the network is expected to work to load screenshots, %FALSE otherwise
 *
 * Clear the existing set of screenshot images, and load the
 * screenshots for @app instead. Display them, or display a
 * fallback if no screenshots could be loaded (and the fallback
 * is enabled).
 *
 * This will start some asynchronous network requests to download
 * screenshots. Those requests may continue after this function
 * call returns.
 *
 * Since: 41
 */
void
gs_screenshot_carousel_load_screenshots (GsScreenshotCarousel *self, GsApp *app, gboolean is_online, GCancellable *cancellable)
{
	GPtrArray *screenshots;
	g_autoptr(GPtrArray) screenshots_sorted = NULL;
	gboolean allow_fallback;
	guint num_screenshots_loaded = 0;
	gboolean has_environment = FALSE;

	g_return_if_fail (GS_IS_SCREENSHOT_CAROUSEL (self));
	g_return_if_fail (GS_IS_APP (app));

	/* fallback warning */
	screenshots = gs_app_get_screenshots (app);
	switch (gs_app_get_kind (app)) {
	case AS_COMPONENT_KIND_GENERIC:
	case AS_COMPONENT_KIND_CODEC:
	case AS_COMPONENT_KIND_ADDON:
	case AS_COMPONENT_KIND_REPOSITORY:
	case AS_COMPONENT_KIND_FIRMWARE:
	case AS_COMPONENT_KIND_DRIVER:
	case AS_COMPONENT_KIND_INPUT_METHOD:
	case AS_COMPONENT_KIND_LOCALIZATION:
	case AS_COMPONENT_KIND_RUNTIME:
		allow_fallback = FALSE;
		break;
	default:
		allow_fallback = TRUE;
		break;
	}

	/* reset screenshots */
	gs_widget_remove_all (self->carousel, (GsRemoveFunc) adw_carousel_remove);

	for (guint i = 0; !has_environment && i < screenshots->len; i++) {
		AsScreenshot *ss = g_ptr_array_index (screenshots, i);
		has_environment = as_screenshot_get_environment (ss) != NULL;
	}

	/* Sort by light/dark to match the user’s theme; see
	 * https://www.freedesktop.org/software/appstream/docs/chap-Metadata.html#tag-screenshots */
	if (has_environment) {
		g_autoptr(GHashTable) indexes = g_hash_table_new (g_direct_hash, g_direct_equal);
		SortData sd = { NULL, };

		screenshots_sorted = g_ptr_array_copy (screenshots, (GCopyFunc) (void *) g_object_ref, NULL);

		for (guint i = 0; i < screenshots_sorted->len; i++) {
			AsScreenshot *ss = g_ptr_array_index (screenshots_sorted, i);
			g_hash_table_insert (indexes, ss, GUINT_TO_POINTER (i));
		}

		sd.indexes = indexes;
		sd.is_dark = adw_style_manager_get_dark (adw_style_manager_get_default ());
		sd.desktop = g_strdup (g_getenv ("DESKTOP_SESSION"));
		if (sd.desktop != NULL) {
			for (guint i = 0; sd.desktop[i] != '\0'; i++) {
				sd.desktop[i] = g_ascii_tolower (sd.desktop[i]);
			}
			/* cover "gnome-classic" */
			if (g_str_has_prefix (sd.desktop, "gnome"))
				sd.desktop[5] = '\0';
		}

		g_ptr_array_sort_values_with_data (screenshots_sorted, _sort_by_environment_cb, &sd);

		screenshots = screenshots_sorted;

		g_free (sd.desktop);
	}

	for (guint i = 0; i < screenshots->len && !g_cancellable_is_cancelled (cancellable); i++) {
		AsScreenshot *ss = g_ptr_array_index (screenshots, i);
		GtkWidget *ssimg = gs_screenshot_image_new (self->session);
		gtk_widget_set_can_focus (gtk_widget_get_first_child (ssimg), FALSE);
		gtk_widget_set_tooltip_text (ssimg, as_screenshot_get_caption (ss));
		gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), ss);
		gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg),
					      GS_IMAGE_NORMAL_WIDTH,
					      GS_IMAGE_NORMAL_HEIGHT);
		gtk_widget_add_css_class (ssimg, "screenshot-image-main");
		gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg), cancellable);

		/* when we're offline, the load will be immediate, so we
		 * can check if it succeeded, and just skip it and its
		 * thumbnails otherwise */
		if (!is_online &&
		    !gs_screenshot_image_is_showing (GS_SCREENSHOT_IMAGE (ssimg))) {
			g_object_ref_sink (ssimg);
			g_object_unref (ssimg);
			continue;
		}

		g_signal_connect_object (ssimg, "clicked",
			G_CALLBACK (gs_screenshot_carousel_img_clicked_cb), self, 0);

		adw_carousel_append (ADW_CAROUSEL (self->carousel), ssimg);
		gtk_widget_set_visible (ssimg, TRUE);
		gs_screenshot_image_set_description (GS_SCREENSHOT_IMAGE (ssimg),
						     as_screenshot_get_caption (ss));
		++num_screenshots_loaded;
	}

	_set_state (self, num_screenshots_loaded, allow_fallback, is_online);
}

/**
 * gs_screenshot_carousel_get_has_screenshots:
 * @self: a #GsScreenshotCarousel
 *
 * Get whether the carousel contains any screenshots.
 *
 * Returns: %TRUE if there are screenshots, %FALSE otherwise
 *
 * Since: 41
 */
gboolean
gs_screenshot_carousel_get_has_screenshots (GsScreenshotCarousel *self)
{
	g_return_val_if_fail (GS_IS_SCREENSHOT_CAROUSEL (self), FALSE);

	return self->has_screenshots;
}

static void
_carousel_navigate (AdwCarousel *carousel, AdwNavigationDirection direction)
{
	g_autoptr (GList) children = NULL;
	GtkWidget *child;
	gdouble position;
	guint n_children;

	n_children = 0;
	for (child = gtk_widget_get_first_child (GTK_WIDGET (carousel));
	     child != NULL;
	     child = gtk_widget_get_next_sibling (child)) {
		children = g_list_prepend (children, child);
		n_children++;
	}
	children = g_list_reverse (children);

	position = adw_carousel_get_position (carousel);
	position += (direction == ADW_NAVIGATION_DIRECTION_BACK) ? -1 : 1;
	/* Round the position to the closest integer in the valid range. */
	position = round (position);
	position = MIN (position, n_children - 1);
	position = MAX (0, position);

	child = g_list_nth_data (children, position);
	if (child)
		adw_carousel_scroll_to (carousel, child, TRUE);
}

static void
gs_screenshot_carousel_update_buttons (GsScreenshotCarousel *self)
{
	gdouble position = adw_carousel_get_position (ADW_CAROUSEL (self->carousel));
	guint n_pages = adw_carousel_get_n_pages (ADW_CAROUSEL (self->carousel));
	gtk_revealer_set_reveal_child (GTK_REVEALER (self->button_previous_revealer), position >= 0.5);
	gtk_revealer_set_reveal_child (GTK_REVEALER (self->button_next_revealer), position < n_pages - 1.5);
}

static void
gs_screenshot_carousel_notify_n_pages_cb (GsScreenshotCarousel *self)
{
	gs_screenshot_carousel_update_buttons (self);
}

static void
gs_screenshot_carousel_notify_position_cb (GsScreenshotCarousel *self)
{
	gs_screenshot_carousel_update_buttons (self);
}

static void
gs_screenshot_carousel_button_previous_clicked_cb (GsScreenshotCarousel *self)
{
	_carousel_navigate (ADW_CAROUSEL (self->carousel),
			    ADW_NAVIGATION_DIRECTION_BACK);
}

static void
gs_screenshot_carousel_button_next_clicked_cb (GsScreenshotCarousel *self)
{
	_carousel_navigate (ADW_CAROUSEL (self->carousel),
			    ADW_NAVIGATION_DIRECTION_FORWARD);
}

static void
gs_screenshot_carousel_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsScreenshotCarousel *self = GS_SCREENSHOT_CAROUSEL (object);

	switch ((GsScreenshotCarouselProperty) prop_id) {
	case PROP_HAS_SCREENSHOTS:
		g_value_set_boolean (value, self->has_screenshots);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_screenshot_carousel_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch ((GsScreenshotCarouselProperty) prop_id) {
	case PROP_HAS_SCREENSHOTS:
		/* Read only */
		g_assert_not_reached ();
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_screenshot_carousel_dispose (GObject *object)
{
	GsScreenshotCarousel *self = GS_SCREENSHOT_CAROUSEL (object);

	gs_widget_remove_all (GTK_WIDGET (self), NULL);

	g_clear_object (&self->session);

	G_OBJECT_CLASS (gs_screenshot_carousel_parent_class)->dispose (object);
}

static void
gs_screenshot_carousel_class_init (GsScreenshotCarouselClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_screenshot_carousel_dispose;
	object_class->get_property = gs_screenshot_carousel_get_property;
	object_class->set_property = gs_screenshot_carousel_set_property;

	/**
	 * GsScreenshotCarousel:has-screenshots:
	 *
	 * Whether the carousel contains any screenshots.
	 *
	 * Since: 41
	 */
	obj_props[PROP_HAS_SCREENSHOTS] =
		g_param_spec_boolean ("has-screenshots", NULL, NULL,
				      FALSE,
				      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-screenshot-carousel.ui");

	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, button_next);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, button_next_revealer);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, button_previous);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, button_previous_revealer);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, carousel);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, carousel_indicator);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, stack);

	gtk_widget_class_bind_template_callback (widget_class, gs_screenshot_carousel_notify_n_pages_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_screenshot_carousel_notify_position_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_screenshot_carousel_button_previous_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_screenshot_carousel_button_next_clicked_cb);

	gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
	gtk_widget_class_set_css_name (widget_class, "screenshot-carousel");
}

static void
gs_screenshot_carousel_init (GsScreenshotCarousel *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	/* Disable scrolling through the carousel, as it’s typically used
	 * in app pages which are themselves scrollable. */
	adw_carousel_set_allow_scroll_wheel (ADW_CAROUSEL (self->carousel), FALSE);

	/* setup networking */
	self->session = gs_build_soup_session ();
}

/**
 * gs_screenshot_carousel_new:
 *
 * Create a new #GsScreenshotCarousel.
 *
 * Returns: (transfer full): a new #GsScreenshotCarousel
 *
 * Since: 41
 */
GsScreenshotCarousel *
gs_screenshot_carousel_new (void)
{
	return GS_SCREENSHOT_CAROUSEL (g_object_new (GS_TYPE_SCREENSHOT_CAROUSEL, NULL));
}
