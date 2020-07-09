/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-review-histogram.h"
#include "gs-review-bar.h"

typedef struct
{
	GtkWidget	*bar1;
	GtkWidget	*bar2;
	GtkWidget	*bar3;
	GtkWidget	*bar4;
	GtkWidget	*bar5;
	GtkWidget	*label_count1;
	GtkWidget	*label_count2;
	GtkWidget	*label_count3;
	GtkWidget	*label_count4;
	GtkWidget	*label_count5;
	GtkWidget	*label_total;
} GsReviewHistogramPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsReviewHistogram, gs_review_histogram, GTK_TYPE_BIN)

static void
set_label (GtkWidget *label, gint value)
{
	g_autofree gchar *text = NULL;
	text = g_strdup_printf ("%i", value);
	gtk_label_set_text (GTK_LABEL (label), text);
}

void
gs_review_histogram_set_ratings (GsReviewHistogram *histogram,
				 GArray *review_ratings)
{
	GsReviewHistogramPrivate *priv = gs_review_histogram_get_instance_private (histogram);
	gdouble fraction[6] = { 0.0f };
	guint32 max = 0;
	guint32 total = 0;

	g_return_if_fail (GS_IS_REVIEW_HISTOGRAM (histogram));

	/* sanity check */
	if (review_ratings->len != 6) {
		g_warning ("ratings data incorrect expected 012345");
		return;
	}

	/* idx 0 is '0 stars' which we don't support in the UI */
	for (guint i = 1; i < review_ratings->len; i++) {
		guint32 c = g_array_index (review_ratings, guint32, i);
		max = MAX (c, max);
	}
	for (guint i = 1; i < review_ratings->len; i++) {
		guint32 c = g_array_index (review_ratings, guint32, i);
		fraction[i] = max > 0 ? (gdouble) c / (gdouble) max : 0.f;
		total += c;
	}

	gs_review_bar_set_fraction (GS_REVIEW_BAR (priv->bar5), fraction[5]);
	set_label (priv->label_count5, g_array_index (review_ratings, guint, 5));
	gs_review_bar_set_fraction (GS_REVIEW_BAR (priv->bar4), fraction[4]);
	set_label (priv->label_count4, g_array_index (review_ratings, guint, 4));
	gs_review_bar_set_fraction (GS_REVIEW_BAR (priv->bar3), fraction[3]);
	set_label (priv->label_count3, g_array_index (review_ratings, guint, 3));
	gs_review_bar_set_fraction (GS_REVIEW_BAR (priv->bar2), fraction[2]);
	set_label (priv->label_count2, g_array_index (review_ratings, guint, 2));
	gs_review_bar_set_fraction (GS_REVIEW_BAR (priv->bar1), fraction[1]);
	set_label (priv->label_count1, g_array_index (review_ratings, guint, 1));
	set_label (priv->label_total, total);
}

static void
gs_review_histogram_init (GsReviewHistogram *histogram)
{
	gtk_widget_init_template (GTK_WIDGET (histogram));
}

static void
gs_review_histogram_class_init (GsReviewHistogramClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-review-histogram.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, bar5);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, bar4);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, bar3);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, bar2);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, bar1);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, label_count5);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, label_count4);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, label_count3);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, label_count2);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, label_count1);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewHistogram, label_total);
}

GtkWidget *
gs_review_histogram_new (void)
{
	GsReviewHistogram *histogram;
	histogram = g_object_new (GS_TYPE_REVIEW_HISTOGRAM, NULL);
	return GTK_WIDGET (histogram);
}
