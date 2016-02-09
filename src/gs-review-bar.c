/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
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

#include <math.h>

#include "gs-review-bar.h"

struct _GsReviewBar
{
	GtkBin		 parent_instance;
	gdouble		 fraction;
};

G_DEFINE_TYPE (GsReviewBar, gs_review_bar, GTK_TYPE_BIN)

/**
 * gs_review_bar_set_fraction:
 **/
void
gs_review_bar_set_fraction (GsReviewBar *bar, gdouble fraction)
{
	g_return_if_fail (GS_IS_REVIEW_BAR (bar));
	bar->fraction = fraction;
}

/**
 * gs_review_bar_init:
 **/
static void
gs_review_bar_init (GsReviewBar *bar)
{
}

/**
 * gs_review_bar_draw:
 **/
static gboolean
gs_review_bar_draw (GtkWidget *widget, cairo_t *cr)
{
	GtkStyleContext *context;
	gdouble y_offset, bar_height;
	GdkRGBA color;

	context = gtk_widget_get_style_context (widget);

	/* don't fill the complete height (too heavy beside GtkLabel of that height) */
	y_offset = floor (0.15 * gtk_widget_get_allocated_height (widget));
	bar_height = gtk_widget_get_allocated_height (widget) - (y_offset * 2);

	gtk_render_background (context, cr,
			       0, y_offset,
			       gtk_widget_get_allocated_width (widget),
			       bar_height);

	cairo_rectangle (cr,
			 0, y_offset,
			 round (GS_REVIEW_BAR (widget)->fraction * gtk_widget_get_allocated_width (widget)),
			 bar_height);
	gtk_style_context_get_color (context, gtk_widget_get_state_flags (widget), &color);
	cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
	cairo_fill (cr);

	return GTK_WIDGET_CLASS (gs_review_bar_parent_class)->draw (widget, cr);
}

/**
 * gs_review_bar_class_init:
 **/
static void
gs_review_bar_class_init (GsReviewBarClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->draw = gs_review_bar_draw;
}

/**
 * gs_review_bar_new:
 **/
GtkWidget *
gs_review_bar_new (void)
{
	GsReviewBar *bar;
	bar = g_object_new (GS_TYPE_REVIEW_BAR, NULL);
	return GTK_WIDGET (bar);
}

/* vim: set noexpandtab: */
