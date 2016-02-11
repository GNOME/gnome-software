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

typedef struct
{
	GtkListBoxRow	 parent_instance;

	GsReview	*review;
	guint64		 actions;
	GtkWidget	*stars;
	GtkWidget	*summary_label;
	GtkWidget	*author_label;
	GtkWidget	*date_label;
	GtkWidget	*text_label;
	GtkWidget	*button_yes;
	GtkWidget	*button_no;
	GtkWidget	*button_dismiss;
	GtkWidget	*button_report;
	GtkWidget	*button_remove;
	GtkWidget	*box_vote_buttons;
} GsReviewRowPrivate;

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (GsReviewRow, gs_review_row, GTK_TYPE_LIST_BOX_ROW)

static void
gs_review_row_refresh (GsReviewRow *row)
{
	GsReviewRowPrivate *priv = gs_review_row_get_instance_private (row);
	const gchar *reviewer;
	GDateTime *date;
	g_autofree gchar *text;

	gs_star_widget_set_rating (GS_STAR_WIDGET (priv->stars),
				   gs_review_get_rating (priv->review));
	reviewer = gs_review_get_reviewer (priv->review);
	gtk_label_set_text (GTK_LABEL (priv->author_label), reviewer ? reviewer : "");
	date = gs_review_get_date (priv->review);
	if (date != NULL)
		text = g_date_time_format (date, "%e %B %Y");
	else
		text = g_strdup ("");
	gtk_label_set_text (GTK_LABEL (priv->date_label), text);
	gtk_label_set_text (GTK_LABEL (priv->summary_label),
			    gs_review_get_summary (priv->review));
	gtk_label_set_text (GTK_LABEL (priv->text_label),
			    gs_review_get_text (priv->review));

	/* if we voted, we can't do any actions */
	if (gs_review_get_flags (priv->review) & GS_REVIEW_FLAG_VOTED)
		priv->actions = 0;

	/* set actions up */
	if ((priv->actions & (1 << GS_REVIEW_ACTION_UPVOTE |
			      1 << GS_REVIEW_ACTION_DOWNVOTE |
			      1 << GS_REVIEW_ACTION_DISMISS)) == 0) {
		gtk_widget_set_visible (priv->box_vote_buttons, FALSE);
	} else {
		gtk_widget_set_visible (priv->box_vote_buttons, TRUE);
		gtk_widget_set_visible (priv->button_yes,
					priv->actions & 1 << GS_REVIEW_ACTION_UPVOTE);
		gtk_widget_set_visible (priv->button_no,
					priv->actions & 1 << GS_REVIEW_ACTION_DOWNVOTE);
		gtk_widget_set_visible (priv->button_dismiss,
					priv->actions & 1 << GS_REVIEW_ACTION_DISMISS);
	}
	gtk_widget_set_visible (priv->button_remove,
				priv->actions & 1 << GS_REVIEW_ACTION_REMOVE);
	gtk_widget_set_visible (priv->button_report,
				priv->actions & 1 << GS_REVIEW_ACTION_REPORT);
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
	GsReviewRowPrivate *priv = gs_review_row_get_instance_private (row);

	g_clear_object (&priv->review);

	G_OBJECT_CLASS (gs_review_row_parent_class)->dispose (object);
}

static void
gs_review_row_class_init (GsReviewRowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_review_row_dispose;

	signals [SIGNAL_BUTTON_CLICKED] =
		g_signal_new ("button-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsReviewRowClass, button_clicked),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-review-row.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, stars);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, summary_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, author_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, date_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, text_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, button_yes);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, button_no);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, button_dismiss);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, button_report);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, button_remove);
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, box_vote_buttons);
}

static void
gs_review_row_button_clicked_upvote_cb (GtkButton *button, GsReviewRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
		       GS_REVIEW_ACTION_UPVOTE);
}

static void
gs_review_row_button_clicked_downvote_cb (GtkButton *button, GsReviewRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
		       GS_REVIEW_ACTION_DOWNVOTE);
}

static void
gs_review_row_button_clicked_report_cb (GtkButton *button, GsReviewRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
		       GS_REVIEW_ACTION_REPORT);
}

static void
gs_review_row_button_clicked_dismiss_cb (GtkButton *button, GsReviewRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
		       GS_REVIEW_ACTION_DISMISS);
}

static void
gs_review_row_button_clicked_remove_cb (GtkButton *button, GsReviewRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
		       GS_REVIEW_ACTION_REMOVE);
}

GsReview *
gs_review_row_get_review (GsReviewRow *review_row)
{
	GsReviewRowPrivate *priv = gs_review_row_get_instance_private (review_row);
	return priv->review;
}

void
gs_review_row_set_actions (GsReviewRow *review_row, guint64 actions)
{
	GsReviewRowPrivate *priv = gs_review_row_get_instance_private (review_row);
	priv->actions = actions;
	gs_review_row_refresh (review_row);
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
	GsReviewRowPrivate *priv;

	g_return_val_if_fail (GS_IS_REVIEW (review), NULL);

	row = g_object_new (GS_TYPE_REVIEW_ROW, NULL);
	priv = gs_review_row_get_instance_private (row);
	priv->review = g_object_ref (review);
	g_signal_connect_object (priv->review, "notify::state",
				 G_CALLBACK (gs_review_row_notify_props_changed_cb),
				 row, 0);
	g_signal_connect_object (priv->button_yes, "clicked",
				 G_CALLBACK (gs_review_row_button_clicked_upvote_cb),
				 row, 0);
	g_signal_connect_object (priv->button_no, "clicked",
				 G_CALLBACK (gs_review_row_button_clicked_downvote_cb),
				 row, 0);
	g_signal_connect_object (priv->button_dismiss, "clicked",
				 G_CALLBACK (gs_review_row_button_clicked_dismiss_cb),
				 row, 0);
	g_signal_connect_object (priv->button_report, "clicked",
				 G_CALLBACK (gs_review_row_button_clicked_report_cb),
				 row, 0);
	g_signal_connect_object (priv->button_remove, "clicked",
				 G_CALLBACK (gs_review_row_button_clicked_remove_cb),
				 row, 0);
	gs_review_row_refresh (row);

	return GTK_WIDGET (row);
}

/* vim: set noexpandtab: */
