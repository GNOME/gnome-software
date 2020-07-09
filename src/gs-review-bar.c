/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
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

void
gs_review_bar_set_fraction (GsReviewBar *bar, gdouble fraction)
{
	g_return_if_fail (GS_IS_REVIEW_BAR (bar));
	bar->fraction = fraction;
}

static void
gs_review_bar_init (GsReviewBar *bar)
{
}

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

static void
gs_review_bar_class_init (GsReviewBarClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->draw = gs_review_bar_draw;
}

GtkWidget *
gs_review_bar_new (void)
{
	GsReviewBar *bar;
	bar = g_object_new (GS_TYPE_REVIEW_BAR, NULL);
	return GTK_WIDGET (bar);
}
