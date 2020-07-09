/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <math.h>

#include "gs-common.h"
#include "gs-star-widget.h"

typedef struct
{
	gboolean	 interactive;
	gint		 rating;
	guint		 icon_size;
	GtkWidget	*box1;
	GtkImage	*images[5];
} GsStarWidgetPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsStarWidget, gs_star_widget, GTK_TYPE_BIN)

typedef enum {
	PROP_ICON_SIZE = 1,
	PROP_INTERACTIVE,
	PROP_RATING,
} GsStarWidgetProperty;

enum {
	RATING_CHANGED,
	SIGNAL_LAST
};

static GParamSpec *properties[PROP_RATING + 1] = { 0, };
static guint signals [SIGNAL_LAST] = { 0 };

const gint rate_to_star[] = {20, 40, 60, 80, 100, -1};

static void gs_star_widget_refresh (GsStarWidget *star);

gint
gs_star_widget_get_rating (GsStarWidget *star)
{
	GsStarWidgetPrivate *priv;
	g_return_val_if_fail (GS_IS_STAR_WIDGET (star), -1);
	priv = gs_star_widget_get_instance_private (star);
	return priv->rating;
}

void
gs_star_widget_set_icon_size (GsStarWidget *star, guint pixel_size)
{
	GsStarWidgetPrivate *priv;
	g_return_if_fail (GS_IS_STAR_WIDGET (star));
	priv = gs_star_widget_get_instance_private (star);

	if (priv->icon_size == pixel_size)
		return;

	priv->icon_size = pixel_size;
	g_object_notify_by_pspec (G_OBJECT (star), properties[PROP_ICON_SIZE]);
	gs_star_widget_refresh (star);
}

static void
gs_star_widget_button_clicked_cb (GtkButton *button, GsStarWidget *star)
{
	GsStarWidgetPrivate *priv;
	gint rating;

	priv = gs_star_widget_get_instance_private (star);
	if (!priv->interactive)
		return;

	rating = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
						     "GsStarWidget::value"));
	gs_star_widget_set_rating (star, rating);
	g_signal_emit (star, signals[RATING_CHANGED], 0, priv->rating);
}

/* Update the star styles to display the new rating */
static void
gs_star_widget_refresh_rating (GsStarWidget *star)
{
	GsStarWidgetPrivate *priv = gs_star_widget_get_instance_private (star);

	if (!gtk_widget_get_realized (GTK_WIDGET (star)))
		return;

	for (guint i = 0; i < G_N_ELEMENTS (priv->images); i++) {
		GtkWidget *im = GTK_WIDGET (priv->images[i]);
		gboolean enabled;

		/* add fudge factor so we can actually get 5 stars in reality */
		enabled = priv->rating >= rate_to_star[i] - 10;

		gtk_style_context_add_class (gtk_widget_get_style_context (im),
					     enabled ? "star-enabled" : "star-disabled");
		gtk_style_context_remove_class (gtk_widget_get_style_context (im),
						enabled ? "star-disabled" : "star-enabled");
	}
}

static void
gs_star_widget_refresh (GsStarWidget *star)
{
	GsStarWidgetPrivate *priv = gs_star_widget_get_instance_private (star);

	if (!gtk_widget_get_realized (GTK_WIDGET (star)))
		return;

	/* remove all existing widgets */
	gs_container_remove_all (GTK_CONTAINER (priv->box1));

	for (guint i = 0; i < G_N_ELEMENTS (priv->images); i++) {
		GtkWidget *w;
		GtkWidget *im;

		/* create image */
		im = gtk_image_new_from_icon_name ("starred-symbolic",
						   GTK_ICON_SIZE_DIALOG);
		gtk_image_set_pixel_size (GTK_IMAGE (im), (gint) priv->icon_size);
		priv->images[i] = GTK_IMAGE (im);

		/* create button */
		if (priv->interactive) {
			w = gtk_button_new ();
			g_signal_connect (w, "clicked",
					  G_CALLBACK (gs_star_widget_button_clicked_cb), star);
			g_object_set_data (G_OBJECT (w),
					   "GsStarWidget::value",
					   GINT_TO_POINTER (rate_to_star[i]));
			gtk_container_add (GTK_CONTAINER (w), im);
			gtk_widget_set_visible (im, TRUE);
		} else {
			w = im;
		}
		gtk_widget_set_sensitive (w, priv->interactive);
		gtk_style_context_add_class (gtk_widget_get_style_context (w), "star");
		gtk_widget_set_visible (w, TRUE);
		gtk_container_add (GTK_CONTAINER (priv->box1), w);
	}

	gs_star_widget_refresh_rating (star);
}

void
gs_star_widget_set_interactive (GsStarWidget *star, gboolean interactive)
{
	GsStarWidgetPrivate *priv;
	g_return_if_fail (GS_IS_STAR_WIDGET (star));
	priv = gs_star_widget_get_instance_private (star);

	if (priv->interactive == interactive)
		return;

	priv->interactive = interactive;
	g_object_notify_by_pspec (G_OBJECT (star), properties[PROP_INTERACTIVE]);
	gs_star_widget_refresh (star);
}

void
gs_star_widget_set_rating (GsStarWidget *star,
			   gint rating)
{
	GsStarWidgetPrivate *priv;
	g_return_if_fail (GS_IS_STAR_WIDGET (star));
	priv = gs_star_widget_get_instance_private (star);

	if (priv->rating == rating)
		return;

	priv->rating = rating;
	g_object_notify_by_pspec (G_OBJECT (star), properties[PROP_RATING]);
	gs_star_widget_refresh_rating (star);
}

static void
gs_star_widget_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	GsStarWidget *self = GS_STAR_WIDGET (object);
	GsStarWidgetPrivate *priv = gs_star_widget_get_instance_private (self);

	switch ((GsStarWidgetProperty) prop_id) {
	case PROP_ICON_SIZE:
		g_value_set_uint (value, priv->icon_size);
		break;
	case PROP_INTERACTIVE:
		g_value_set_boolean (value, priv->interactive);
		break;
	case PROP_RATING:
		g_value_set_int (value, priv->rating);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_star_widget_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	GsStarWidget *self = GS_STAR_WIDGET (object);

	switch ((GsStarWidgetProperty) prop_id) {
	case PROP_ICON_SIZE:
		gs_star_widget_set_icon_size (self, g_value_get_uint (value));
		break;
	case PROP_INTERACTIVE:
		gs_star_widget_set_interactive (self, g_value_get_boolean (value));
		break;
	case PROP_RATING:
		gs_star_widget_set_rating (self, g_value_get_int (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_star_widget_destroy (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (gs_star_widget_parent_class)->destroy (widget);
}

static void
gs_star_widget_realize (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (gs_star_widget_parent_class)->realize (widget);

	/* Create child widgets. */
	gs_star_widget_refresh (GS_STAR_WIDGET (widget));
}

static void
gs_star_widget_init (GsStarWidget *star)
{
	gtk_widget_set_has_window (GTK_WIDGET (star), FALSE);
	gtk_widget_init_template (GTK_WIDGET (star));
}

static void
gs_star_widget_class_init (GsStarWidgetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_star_widget_destroy;
	widget_class->realize = gs_star_widget_realize;
	object_class->get_property = gs_star_widget_get_property;
	object_class->set_property = gs_star_widget_set_property;

	/**
	 * GsStarWidget:icon-size:
	 *
	 * Size of the star icons to use in the widget, in pixels.
	 *
	 * Since: 3.38
	 */
	properties[PROP_ICON_SIZE] =
		 g_param_spec_uint ("icon-size",
				    "Icon Size",
				    "Size of icons to use, in pixels",
				    0, G_MAXUINT, 12,
				    G_PARAM_READWRITE);

	/**
	 * GsStarWidget:interactive:
	 *
	 * Whether the widget accepts user input to change #GsStarWidget:rating.
	 *
	 * Since: 3.38
	 */
	properties[PROP_INTERACTIVE] =
		 g_param_spec_boolean ("interactive",
				       "Interactive",
				       "Whether the rating is interactive",
				       FALSE,
				       G_PARAM_READWRITE);

	/**
	 * GsStarWidget:rating:
	 *
	 * The rating to display on the widget, as a percentage. `-1` indicates
	 * that the rating is unknown.
	 *
	 * Since: 3.38
	 */
	properties[PROP_RATING] =
		 g_param_spec_int ("rating",
				   "Rating",
				   "Rating, out of 100%, or -1 for unknown",
				   -1, 100, -1,
				   G_PARAM_READWRITE);

	signals [RATING_CHANGED] =
		g_signal_new ("rating-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsStarWidgetClass, rating_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-star-widget.ui");
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, box1);
}

GtkWidget *
gs_star_widget_new (void)
{
	GsStarWidget *star;
	star = g_object_new (GS_TYPE_STAR_WIDGET, NULL);
	return GTK_WIDGET (star);
}
