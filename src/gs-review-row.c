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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-review-row.h"
#include "gs-star-widget.h"

struct _GsReviewRow
{
	GtkListBoxRow	 parent_instance;

	GsReview	*review;
	GtkWidget	*stars;
	GtkWidget	*summary_label;
	GtkWidget	*author_label;
	GtkWidget	*date_label;
	GtkWidget	*text_label;
};

G_DEFINE_TYPE (GsReviewRow, gs_review_row, GTK_TYPE_LIST_BOX_ROW)

static void
gs_review_row_refresh (GsReviewRow *row)
{
	const gchar *reviewer;
	GDateTime *date;
	g_autofree gchar *text;

	gs_star_widget_set_rating (GS_STAR_WIDGET (row->stars), gs_review_get_rating (row->review));
	reviewer = gs_review_get_reviewer (row->review);
	gtk_label_set_text (GTK_LABEL (row->author_label), reviewer ? reviewer : "");
	date = gs_review_get_date (row->review);
	if (date != NULL)
		text = g_date_time_format (date, "%e %B %Y");
	else
		text = g_strdup ("");
	gtk_label_set_text (GTK_LABEL (row->date_label), text);
	gtk_label_set_text (GTK_LABEL (row->summary_label), gs_review_get_summary (row->review));
	gtk_label_set_text (GTK_LABEL (row->text_label), gs_review_get_text (row->review));
}

static gboolean
gs_review_row_refresh_idle (gpointer user_data)
{
	GsReviewRow *row = GS_REVIEW_ROW (user_data);

	gs_review_row_refresh (row);

	g_object_unref (row);
	return G_SOURCE_REMOVE;
}

static void
gs_review_row_notify_props_changed_cb (GsApp *app,
				       GParamSpec *pspec,
				       GsReviewRow *row)
{
	g_idle_add (gs_review_row_refresh_idle, g_object_ref (row));
}

static void
gs_review_row_init (GsReviewRow *row)
{
	gtk_widget_set_has_window (GTK_WIDGET (row), FALSE);
	gtk_widget_init_template (GTK_WIDGET (row));
}

static void
gs_review_row_dispose (GObject *object)
{
	GsReviewRow *row = GS_REVIEW_ROW (object);

	g_clear_object (&row->review);

	G_OBJECT_CLASS (gs_review_row_parent_class)->dispose (object);
}

static void
gs_review_row_class_init (GsReviewRowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_review_row_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-review-row.ui");

	gtk_widget_class_bind_template_child (widget_class, GsReviewRow, stars);
	gtk_widget_class_bind_template_child (widget_class, GsReviewRow, summary_label);
	gtk_widget_class_bind_template_child (widget_class, GsReviewRow, author_label);
	gtk_widget_class_bind_template_child (widget_class, GsReviewRow, date_label);
	gtk_widget_class_bind_template_child (widget_class, GsReviewRow, text_label);
}

/**
 * gs_review_row_new:
 * @review: The review to show
 *
 * Create a widget suitable for showing an application review.
 *
 * Return value: A new @GsReviewRow.
 **/
GtkWidget *
gs_review_row_new (GsReview *review)
{
	GsReviewRow *row;

	g_return_val_if_fail (GS_IS_REVIEW (review), NULL);

	row = g_object_new (GS_TYPE_REVIEW_ROW, NULL);
	row->review = g_object_ref (review);
	g_signal_connect_object (row->review, "notify::state",
				 G_CALLBACK (gs_review_row_notify_props_changed_cb),
				 row, 0);
	gs_review_row_refresh (row);

	return GTK_WIDGET (row);
}

/* vim: set noexpandtab: */
