/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2015-2019 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2019 Joaquim Rocha <jrocha@endlessm.com>
 * Copyright (C) 2021 Adrien Plazas <adrien.plazas@puri.sm>
 *
 * SPDX-License-Identifier: GPL-2.0+
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

#include <glib/gi18n.h>
#include <handy.h>
#include <locale.h>
#include <math.h>
#include <string.h>

#include "gs-common.h"
#include "gs-utils.h"

#include "gs-screenshot-carousel.h"
#include "gs-screenshot-image.h"

struct _GsScreenshotCarousel
{
	GtkStack		 parent_instance;

	SoupSession		*session;  /* (owned) (not nullable) */
	gboolean		 has_screenshots;

	GtkWidget		*button_next;
	GtkWidget		*button_next_image;
	GtkWidget		*button_next_revealer;
	GtkWidget		*button_previous;
	GtkWidget		*button_previous_image;
	GtkWidget		*button_previous_revealer;
	GtkWidget		*carousel;
	GtkWidget		*carousel_indicator;
};

typedef enum {
	PROP_HAS_SCREENSHOTS = 1,
} GsScreenshotCarouselProperty;

static GParamSpec *obj_props[PROP_HAS_SCREENSHOTS + 1] = { NULL, };

G_DEFINE_TYPE (GsScreenshotCarousel, gs_screenshot_carousel, GTK_TYPE_STACK)

static void
_set_state (GsScreenshotCarousel *self, guint length, gboolean allow_fallback, gboolean is_online)
{
	gboolean has_screenshots;

	gtk_widget_set_visible (self->carousel_indicator, length > 1);
	gtk_stack_set_visible_child_name (GTK_STACK (self), length > 0 ? "carousel" : "fallback");

	has_screenshots = length > 0 || (allow_fallback && is_online);
	if (self->has_screenshots != has_screenshots) {
		self->has_screenshots = has_screenshots;
		g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_HAS_SCREENSHOTS]);
	}
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
	gboolean allow_fallback;
	guint num_screenshots_loaded = 0;

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
	gs_container_remove_all (GTK_CONTAINER (self->carousel));

	for (guint i = 0; i < screenshots->len && !g_cancellable_is_cancelled (cancellable); i++) {
		AsScreenshot *ss = g_ptr_array_index (screenshots, i);
		GtkWidget *ssimg = gs_screenshot_image_new (self->session);
		gtk_widget_set_can_focus (gtk_bin_get_child (GTK_BIN (ssimg)), FALSE);
		gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), ss);
		gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg),
					      AS_IMAGE_NORMAL_WIDTH,
					      AS_IMAGE_NORMAL_HEIGHT);
		gtk_style_context_add_class (gtk_widget_get_style_context (ssimg),
					     "screenshot-image-main");
		gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg), cancellable);

		/* when we're offline, the load will be immediate, so we
		 * can check if it succeeded, and just skip it and its
		 * thumbnails otherwise */
		if (!is_online &&
		    !gs_screenshot_image_is_showing (GS_SCREENSHOT_IMAGE (ssimg))) {
			g_object_unref (ssimg);
			continue;
		}

		gtk_container_add (GTK_CONTAINER (self->carousel),
				   ssimg);
		gtk_widget_show (ssimg);
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
_carousel_navigate (HdyCarousel *carousel, HdyNavigationDirection direction)
{
	g_autoptr (GList) children = NULL;
	GtkWidget *child;
	gdouble position;
	guint n_children;

	children = gtk_container_get_children (GTK_CONTAINER (carousel));
	n_children = g_list_length (children);

	position = hdy_carousel_get_position (carousel);
	position += (direction == HDY_NAVIGATION_DIRECTION_BACK) ? -1 : 1;
	/* Round the position to the closest integer in the valid range. */
	position = round (position);
	position = MIN (position, n_children - 1);
	position = MAX (0, position);

	child = g_list_nth_data (children, position);
	if (child)
		hdy_carousel_scroll_to (carousel, child);
}

static void
gs_screenshot_carousel_update_buttons (GsScreenshotCarousel *self)
{
	gdouble position = hdy_carousel_get_position (HDY_CAROUSEL (self->carousel));
	guint n_pages = hdy_carousel_get_n_pages (HDY_CAROUSEL (self->carousel));
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
	_carousel_navigate (HDY_CAROUSEL (self->carousel),
			    HDY_NAVIGATION_DIRECTION_BACK);
}

static void
gs_screenshot_carousel_button_next_clicked_cb (GsScreenshotCarousel *self)
{
	_carousel_navigate (HDY_CAROUSEL (self->carousel),
			    HDY_NAVIGATION_DIRECTION_FORWARD);
}

static void
gs_screenshot_carousel_navigate_button_direction_changed_cb (GtkWidget        *widget,
							     GtkTextDirection  previous_direction,
							     gpointer          user_data)
{
	const gchar *ltr_icon_name, *rtl_icon_name, *icon_name;

	g_assert (g_strcmp0 (gtk_widget_get_name (widget), "previous") == 0 ||
		  g_strcmp0 (gtk_widget_get_name (widget), "next") == 0);

	if (g_strcmp0 (gtk_widget_get_name (widget), "previous") == 0) {
		ltr_icon_name = "carousel-arrow-previous-symbolic";
		rtl_icon_name = "carousel-arrow-next-symbolic";
	} else {
		ltr_icon_name = "carousel-arrow-next-symbolic";
		rtl_icon_name = "carousel-arrow-previous-symbolic";
	}

	icon_name = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL) ? rtl_icon_name : ltr_icon_name;
	gtk_image_set_from_icon_name (GTK_IMAGE (widget), icon_name, GTK_ICON_SIZE_MENU);
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
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, button_next_image);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, button_next_revealer);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, button_previous);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, button_previous_image);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, button_previous_revealer);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, carousel);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotCarousel, carousel_indicator);

	gtk_widget_class_bind_template_callback (widget_class, gs_screenshot_carousel_notify_n_pages_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_screenshot_carousel_notify_position_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_screenshot_carousel_button_previous_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_screenshot_carousel_button_next_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_screenshot_carousel_navigate_button_direction_changed_cb);

	gtk_widget_class_set_css_name (widget_class, "screenshot-carousel");
}

static void
gs_screenshot_carousel_init (GsScreenshotCarousel *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	/* Ensure the arrow direction matches the text direction */
	gs_screenshot_carousel_navigate_button_direction_changed_cb (GTK_WIDGET (self->button_next_image), GTK_TEXT_DIR_NONE, self);
	gs_screenshot_carousel_navigate_button_direction_changed_cb (GTK_WIDGET (self->button_previous_image), GTK_TEXT_DIR_NONE, self);

#if HDY_CHECK_VERSION(1, 3, 0)
	/* Disable scrolling through the carousel, as it’s typically used
	 * in application pages which are themselves scrollable. */
	hdy_carousel_set_allow_scroll_wheel (HDY_CAROUSEL (self->carousel), FALSE);
#endif

	/* setup networking */
	self->session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, gs_user_agent (),
	                                               NULL);
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
