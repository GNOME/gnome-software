/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "gs-star-widget.h"

typedef struct
{
	gboolean	 interactive;
	gint		 rating;
	GtkWidget	*button1;
	GtkWidget	*button2;
	GtkWidget	*button3;
	GtkWidget	*button4;
	GtkWidget	*button5;
	GtkWidget	*image1;
	GtkWidget	*image2;
	GtkWidget	*image3;
	GtkWidget	*image4;
	GtkWidget	*image5;
} GsStarWidgetPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsStarWidget, gs_star_widget, GTK_TYPE_BIN)

enum {
	RATING_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

static const gint rate_to_star[] = {20, 40, 60, 80, 100, -1};

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
	gtk_image_set_pixel_size (GTK_IMAGE (priv->image1), pixel_size);
	gtk_image_set_pixel_size (GTK_IMAGE (priv->image2), pixel_size);
	gtk_image_set_pixel_size (GTK_IMAGE (priv->image3), pixel_size);
	gtk_image_set_pixel_size (GTK_IMAGE (priv->image4), pixel_size);
	gtk_image_set_pixel_size (GTK_IMAGE (priv->image5), pixel_size);
}

static void
gs_star_widget_style_class_enable (GtkWidget *widget, gboolean val)
{
	GtkStyleContext *context;
	context = gtk_widget_get_style_context (widget);
	if (val) {
		gtk_style_context_add_class (context, "star-enabled");
		gtk_style_context_remove_class (context, "star-disabled");
	} else {
		gtk_style_context_add_class (context, "star-disabled");
		gtk_style_context_remove_class (context, "star-enabled");
	}
}

static void
gs_star_widget_refresh (GsStarWidget *star)
{
	GsStarWidgetPrivate *priv;
	priv = gs_star_widget_get_instance_private (star);

	gtk_widget_set_sensitive (priv->button1, priv->interactive);
	gtk_widget_set_sensitive (priv->button2, priv->interactive);
	gtk_widget_set_sensitive (priv->button3, priv->interactive);
	gtk_widget_set_sensitive (priv->button4, priv->interactive);
	gtk_widget_set_sensitive (priv->button5, priv->interactive);

	gs_star_widget_style_class_enable (priv->image1, priv->rating > 0);
	gs_star_widget_style_class_enable (priv->image2, priv->rating > 20);
	gs_star_widget_style_class_enable (priv->image3, priv->rating > 40);
	gs_star_widget_style_class_enable (priv->image4, priv->rating > 60);
	gs_star_widget_style_class_enable (priv->image5, priv->rating > 80);
}

void
gs_star_widget_set_interactive (GsStarWidget *star, gboolean interactive)
{
	GsStarWidgetPrivate *priv;
	g_return_if_fail (GS_IS_STAR_WIDGET (star));
	priv = gs_star_widget_get_instance_private (star);
	priv->interactive = interactive;
	gs_star_widget_refresh (star);
}

void
gs_star_widget_set_rating (GsStarWidget *star,
			   gint rating)
{
	GsStarWidgetPrivate *priv;
	g_return_if_fail (GS_IS_STAR_WIDGET (star));
	priv = gs_star_widget_get_instance_private (star);
	priv->rating = rating;
	gs_star_widget_refresh (star);
}

static void
gs_star_widget_destroy (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (gs_star_widget_parent_class)->destroy (widget);
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
	priv->rating = rating;
	g_signal_emit (star, signals[RATING_CHANGED], 0, priv->rating);
	gs_star_widget_refresh (star);
}

static void
gs_star_widget_init (GsStarWidget *star)
{
	GsStarWidgetPrivate *priv;

	gtk_widget_set_has_window (GTK_WIDGET (star), FALSE);
	gtk_widget_init_template (GTK_WIDGET (star));
	priv = gs_star_widget_get_instance_private (star);
	g_signal_connect (priv->button1, "clicked",
			  G_CALLBACK (gs_star_widget_button_clicked_cb), star);
	g_signal_connect (priv->button2, "clicked",
			  G_CALLBACK (gs_star_widget_button_clicked_cb), star);
	g_signal_connect (priv->button3, "clicked",
			  G_CALLBACK (gs_star_widget_button_clicked_cb), star);
	g_signal_connect (priv->button4, "clicked",
			  G_CALLBACK (gs_star_widget_button_clicked_cb), star);
	g_signal_connect (priv->button5, "clicked",
			  G_CALLBACK (gs_star_widget_button_clicked_cb), star);

	/* assign the values */
	g_object_set_data (G_OBJECT (priv->button1),
			   "GsStarWidget::value",
			   GINT_TO_POINTER (rate_to_star[0]));
	g_object_set_data (G_OBJECT (priv->button2),
			   "GsStarWidget::value",
			   GINT_TO_POINTER (rate_to_star[1]));
	g_object_set_data (G_OBJECT (priv->button3),
			   "GsStarWidget::value",
			   GINT_TO_POINTER (rate_to_star[2]));
	g_object_set_data (G_OBJECT (priv->button4),
			   "GsStarWidget::value",
			   GINT_TO_POINTER (rate_to_star[3]));
	g_object_set_data (G_OBJECT (priv->button5),
			   "GsStarWidget::value",
			   GINT_TO_POINTER (rate_to_star[4]));
}

static void
gs_star_widget_class_init (GsStarWidgetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_star_widget_destroy;

	signals [RATING_CHANGED] =
		g_signal_new ("rating-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsStarWidgetClass, rating_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-star-widget.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, button1);
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, button2);
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, button3);
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, button4);
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, button5);
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, image1);
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, image2);
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, image3);
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, image4);
	gtk_widget_class_bind_template_child_private (widget_class, GsStarWidget, image5);
}

GtkWidget *
gs_star_widget_new (void)
{
	GsStarWidget *star;
	star = g_object_new (GS_TYPE_STAR_WIDGET, NULL);
	return GTK_WIDGET (star);
}

/* vim: set noexpandtab: */
