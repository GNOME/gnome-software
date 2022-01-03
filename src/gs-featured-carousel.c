/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-featured-carousel
 * @short_description: A carousel widget containing #GsFeatureTile instances
 *
 * #GsFeaturedCarousel is a carousel widget which rotates through a set of
 * #GsFeatureTiles, displaying them to the user to advertise a given set of
 * featured apps, set with gs_featured_carousel_set_apps().
 *
 * The widget has no special appearance if the app list is empty, so callers
 * will typically want to hide the carousel in that case.
 *
 * Since: 40
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <handy.h>

#include "gs-app-list.h"
#include "gs-common.h"
#include "gs-feature-tile.h"
#include "gs-featured-carousel.h"

#define FEATURED_ROTATE_TIME                   15 /* seconds */

struct _GsFeaturedCarousel
{
	GtkBox		 parent_instance;

	GsAppList	*apps;  /* (nullable) (owned) */
	guint		 rotation_timer_id;

	HdyCarousel	*carousel;
	GtkButton	*next_button;
	GtkImage	*next_button_image;
	GtkButton	*previous_button;
	GtkImage	*previous_button_image;
};

G_DEFINE_TYPE (GsFeaturedCarousel, gs_featured_carousel, GTK_TYPE_BOX)

typedef enum {
	PROP_APPS = 1,
} GsFeaturedCarouselProperty;

static GParamSpec *obj_props[PROP_APPS + 1] = { NULL, };

typedef enum {
	SIGNAL_APP_CLICKED,
	SIGNAL_CLICKED,
} GsFeaturedCarouselSignal;

static guint obj_signals[SIGNAL_CLICKED + 1] = { 0, };

static void
show_relative_page (GsFeaturedCarousel *self,
                    gint                delta)
{
	gdouble current_page = hdy_carousel_get_position (self->carousel);
	guint n_pages = hdy_carousel_get_n_pages (self->carousel);
	gdouble new_page;
	g_autoptr(GList) children = gtk_container_get_children (GTK_CONTAINER (self->carousel));
	GtkWidget *new_page_widget;
	gint64 animation_duration_ms = hdy_carousel_get_animation_duration (self->carousel);

	if (n_pages == 0)
		return;

	/* FIXME: This would be simpler if HdyCarousel had a way to scroll to
	 * a page by index, rather than by GtkWidget pointer.
	 * See https://gitlab.gnome.org/GNOME/libhandy/-/issues/413 */
	new_page = ((guint) current_page + delta + n_pages) % n_pages;
	new_page_widget = g_list_nth_data (children, new_page);
	g_assert (new_page_widget != NULL);

	/* Don’t animate if we’re wrapping from the last page back to the first
     * or from the first page to the last going backwards as it means rapidly
     * spooling through all the pages, which looks confusing. */
	if ((new_page == 0.0 && delta > 0) || (new_page == n_pages - 1 && delta < 0))
		animation_duration_ms = 0;

	hdy_carousel_scroll_to_full (self->carousel, new_page_widget, animation_duration_ms);
}

static gboolean
rotate_cb (gpointer user_data)
{
	GsFeaturedCarousel *self = GS_FEATURED_CAROUSEL (user_data);

	show_relative_page (self, +1);

	return G_SOURCE_CONTINUE;
}

static void
start_rotation_timer (GsFeaturedCarousel *self)
{
	if (self->rotation_timer_id == 0) {
		self->rotation_timer_id = g_timeout_add_seconds (FEATURED_ROTATE_TIME,
								 rotate_cb, self);
	}
}

static void
stop_rotation_timer (GsFeaturedCarousel *self)
{
	if (self->rotation_timer_id != 0) {
		g_source_remove (self->rotation_timer_id);
		self->rotation_timer_id = 0;
	}
}

static void
image_set_icon_for_direction (GtkImage    *image,
                              const gchar *ltr_icon_name,
                              const gchar *rtl_icon_name)
{
	const gchar *icon_name = (gtk_widget_get_direction (GTK_WIDGET (image)) == GTK_TEXT_DIR_RTL) ? rtl_icon_name : ltr_icon_name;
	gtk_image_set_from_icon_name (image, icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
}

static void
next_button_clicked_cb (GtkButton *button,
                        gpointer   user_data)
{
	GsFeaturedCarousel *self = GS_FEATURED_CAROUSEL (user_data);

	show_relative_page (self, +1);

	/* Reset the rotation timer in case it’s about to fire. */
	stop_rotation_timer (self);
	start_rotation_timer (self);
}

static void
next_button_direction_changed_cb (GtkWidget        *widget,
                                  GtkTextDirection  previous_direction,
                                  gpointer          user_data)
{
	image_set_icon_for_direction (GTK_IMAGE (widget), "carousel-arrow-next-symbolic", "carousel-arrow-previous-symbolic");
}

static void
previous_button_clicked_cb (GtkButton *button,
                            gpointer   user_data)
{
	GsFeaturedCarousel *self = GS_FEATURED_CAROUSEL (user_data);

	show_relative_page (self, -1);

	/* Reset the rotation timer in case it’s about to fire. */
	stop_rotation_timer (self);
	start_rotation_timer (self);
}

static void
previous_button_direction_changed_cb (GtkWidget        *widget,
                                      GtkTextDirection  previous_direction,
                                      gpointer          user_data)
{
	image_set_icon_for_direction (GTK_IMAGE (widget), "carousel-arrow-previous-symbolic", "carousel-arrow-next-symbolic");
}

static void
app_tile_clicked_cb (GsAppTile *app_tile,
                     gpointer   user_data)
{
	GsFeaturedCarousel *self = GS_FEATURED_CAROUSEL (user_data);
	GsApp *app;

	app = gs_app_tile_get_app (app_tile);
	g_signal_emit (self, obj_signals[SIGNAL_APP_CLICKED], 0, app);
}

static void
gs_featured_carousel_init (GsFeaturedCarousel *self)
{
	gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
	gtk_widget_init_template (GTK_WIDGET (self));

#if HDY_CHECK_VERSION(1, 3, 0)
	/* Disable scrolling through the carousel, as it’s typically used
	 * in category pages which are themselves scrollable. */
	hdy_carousel_set_allow_scroll_wheel (HDY_CAROUSEL (self->carousel), FALSE);
#endif

	/* Ensure the text directions are up to date */
	next_button_direction_changed_cb (GTK_WIDGET (self->next_button_image), GTK_TEXT_DIR_NONE, self);
	previous_button_direction_changed_cb (GTK_WIDGET (self->previous_button_image), GTK_TEXT_DIR_NONE, self);
}

static void
gs_featured_carousel_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	GsFeaturedCarousel *self = GS_FEATURED_CAROUSEL (object);

	switch ((GsFeaturedCarouselProperty) prop_id) {
	case PROP_APPS:
		g_value_set_object (value, gs_featured_carousel_get_apps (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_featured_carousel_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	GsFeaturedCarousel *self = GS_FEATURED_CAROUSEL (object);

	switch ((GsFeaturedCarouselProperty) prop_id) {
	case PROP_APPS:
		gs_featured_carousel_set_apps (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_featured_carousel_dispose (GObject *object)
{
	GsFeaturedCarousel *self = GS_FEATURED_CAROUSEL (object);

	stop_rotation_timer (self);
	g_clear_object (&self->apps);

	G_OBJECT_CLASS (gs_featured_carousel_parent_class)->dispose (object);
}

static gboolean
gs_featured_carousel_key_press_event (GtkWidget   *widget,
                                      GdkEventKey *event)
{
	GsFeaturedCarousel *self = GS_FEATURED_CAROUSEL (widget);

	if (gtk_widget_is_visible (GTK_WIDGET (self->previous_button)) &&
	    gtk_widget_is_sensitive (GTK_WIDGET (self->previous_button)) &&
	    ((gtk_widget_get_direction (GTK_WIDGET (self->previous_button)) == GTK_TEXT_DIR_LTR && event->keyval == GDK_KEY_Left) ||
	     (gtk_widget_get_direction (GTK_WIDGET (self->previous_button)) == GTK_TEXT_DIR_RTL && event->keyval == GDK_KEY_Right))) {
		gtk_widget_activate (GTK_WIDGET (self->previous_button));
		return GDK_EVENT_STOP;
	}

	if (gtk_widget_is_visible (GTK_WIDGET (self->next_button)) &&
	    gtk_widget_is_sensitive (GTK_WIDGET (self->next_button)) &&
	    ((gtk_widget_get_direction (GTK_WIDGET (self->next_button)) == GTK_TEXT_DIR_LTR && event->keyval == GDK_KEY_Right) ||
	     (gtk_widget_get_direction (GTK_WIDGET (self->next_button)) == GTK_TEXT_DIR_RTL && event->keyval == GDK_KEY_Left))) {
		gtk_widget_activate (GTK_WIDGET (self->next_button));
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static void
carousel_clicked_cb (GsFeaturedCarousel *carousel,
                     gpointer            user_data)
{
	GsFeaturedCarousel *self = GS_FEATURED_CAROUSEL (user_data);
	GsAppTile *current_tile;
	GsApp *app;
	gdouble current_page;
	g_autoptr(GList) children = NULL;

	/* Get the currently visible tile. */
	current_page = hdy_carousel_get_position (self->carousel);
	children = gtk_container_get_children (GTK_CONTAINER (self->carousel));

	current_tile = g_list_nth_data (children, current_page);
	if (current_tile == NULL)
		return;

	app = gs_app_tile_get_app (current_tile);
	g_signal_emit (self, obj_signals[SIGNAL_APP_CLICKED], 0, app);
}

static void
gs_featured_carousel_class_init (GsFeaturedCarouselClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_featured_carousel_get_property;
	object_class->set_property = gs_featured_carousel_set_property;
	object_class->dispose = gs_featured_carousel_dispose;

	widget_class->key_press_event = gs_featured_carousel_key_press_event;

	/**
	 * GsFeaturedCarousel:apps: (nullable)
	 *
	 * The list of featured apps to display in the carousel. This should
	 * typically be 4–8 apps. They will be displayed in the order listed,
	 * so the caller may want to randomise that order first, using
	 * gs_app_list_randomize().
	 *
	 * This may be %NULL if no apps have been set. This is equivalent to
	 * an empty #GsAppList.
	 *
	 * Since: 40
	 */
	obj_props[PROP_APPS] =
		g_param_spec_object ("apps", NULL, NULL,
				     GS_TYPE_APP_LIST,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	/**
	 * GsFeaturedCarousel::app-clicked:
	 * @app: the #GsApp which was clicked on
	 *
	 * Emitted when one of the app tiles is clicked. Typically the caller
	 * should display the details of the given app in the callback.
	 *
	 * Since: 40
	 */
	obj_signals[SIGNAL_APP_CLICKED] =
		g_signal_new ("app-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, GS_TYPE_APP);

	/**
	 * GsFeaturedCarousel::clicked:
	 *
	 * Emitted when the carousel is clicked, and typically emitted shortly
	 * before #GsFeaturedCarousel::app-clicked is emitted. Most callers will
	 * want to connect to #GsFeaturedCarousel::app-clicked instead.
	 *
	 * Since: 40
	 */
	obj_signals[SIGNAL_CLICKED] =
		g_signal_new ("clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	widget_class->activate_signal = obj_signals[SIGNAL_CLICKED];

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-featured-carousel.ui");

	gtk_widget_class_bind_template_child (widget_class, GsFeaturedCarousel, carousel);
	gtk_widget_class_bind_template_child (widget_class, GsFeaturedCarousel, next_button);
	gtk_widget_class_bind_template_child (widget_class, GsFeaturedCarousel, next_button_image);
	gtk_widget_class_bind_template_child (widget_class, GsFeaturedCarousel, previous_button);
	gtk_widget_class_bind_template_child (widget_class, GsFeaturedCarousel, previous_button_image);
	gtk_widget_class_bind_template_callback (widget_class, next_button_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, next_button_direction_changed_cb);
	gtk_widget_class_bind_template_callback (widget_class, previous_button_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, previous_button_direction_changed_cb);
	gtk_widget_class_bind_template_callback (widget_class, carousel_clicked_cb);
}

/**
 * gs_featured_carousel_new:
 * @apps: (nullable): a list of apps to display in the carousel, or %NULL
 *
 * Create a new #GsFeaturedCarousel and set its initial app list to @apps.
 *
 * Returns: (transfer full): a new #GsFeaturedCarousel
 * Since: 40
 */
GtkWidget *
gs_featured_carousel_new (GsAppList *apps)
{
	g_return_val_if_fail (apps == NULL || GS_IS_APP_LIST (apps), NULL);

	return g_object_new (GS_TYPE_FEATURED_CAROUSEL,
			     "apps", apps,
			     NULL);
}

/**
 * gs_featured_carousel_get_apps:
 * @self: a #GsFeaturedCarousel
 *
 * Gets the value of #GsFeaturedCarousel:apps.
 *
 * Returns: (nullable) (transfer none): list of apps in the carousel, or %NULL
 *     if none are set
 * Since: 40
 */
GsAppList *
gs_featured_carousel_get_apps (GsFeaturedCarousel *self)
{
	g_return_val_if_fail (GS_IS_FEATURED_CAROUSEL (self), NULL);

	return self->apps;
}

/**
 * gs_featured_carousel_set_apps:
 * @self: a #GsFeaturedCarousel
 * @apps: (nullable) (transfer none): list of apps to display in the carousel,
 *     or %NULL for none
 *
 * Set the value of #GsFeaturedCarousel:apps.
 *
 * Since: 40
 */
void
gs_featured_carousel_set_apps (GsFeaturedCarousel *self,
                               GsAppList          *apps)
{
	g_return_if_fail (GS_IS_FEATURED_CAROUSEL (self));
	g_return_if_fail (apps == NULL || GS_IS_APP_LIST (apps));

	if (apps == self->apps)
		return;

	stop_rotation_timer (self);
	gs_container_remove_all (GTK_CONTAINER (self->carousel));

	g_set_object (&self->apps, apps);

	for (guint i = 0; i < gs_app_list_length (apps); i++) {
		GsApp *app = gs_app_list_index (apps, i);
		GtkWidget *tile = gs_feature_tile_new (app);
		gtk_widget_set_hexpand (tile, TRUE);
		gtk_widget_set_vexpand (tile, TRUE);
		gtk_widget_set_can_focus (tile, FALSE);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked_cb), self);
		gtk_container_add (GTK_CONTAINER (self->carousel), tile);
	}

	gtk_widget_set_visible (GTK_WIDGET (self->next_button), self->apps != NULL && gs_app_list_length (self->apps) > 1);
	gtk_widget_set_visible (GTK_WIDGET (self->previous_button), self->apps != NULL && gs_app_list_length (self->apps) > 1);

	if (self->apps != NULL && gs_app_list_length (self->apps) > 0)
		start_rotation_timer (self);

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_APPS]);
}
