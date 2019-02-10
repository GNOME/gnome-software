/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-review-row.h"
#include "gs-star-widget.h"

typedef struct
{
	GtkListBoxRow	 parent_instance;

	AsReview	*review;
	gboolean	 network_available;
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
	GtkWidget	*box_voting;
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
	g_autofree gchar *text = NULL;

	gs_star_widget_set_rating (GS_STAR_WIDGET (priv->stars),
				   as_review_get_rating (priv->review));
	reviewer = as_review_get_reviewer_name (priv->review);
	if (reviewer == NULL) {
		/* TRANSLATORS: this is when a user doesn't specify a name */
		reviewer = _("Unknown");
	}
	gtk_label_set_text (GTK_LABEL (priv->author_label), reviewer);
	date = as_review_get_date (priv->review);
	if (date != NULL)
		text = g_date_time_format (date, "%e %B %Y");
	else
		text = g_strdup ("");
	gtk_label_set_text (GTK_LABEL (priv->date_label), text);
	gtk_label_set_text (GTK_LABEL (priv->summary_label),
			    as_review_get_summary (priv->review));
	gtk_label_set_text (GTK_LABEL (priv->text_label),
			    as_review_get_description (priv->review));

	/* if we voted, we can't do any actions */
	if (as_review_get_flags (priv->review) & AS_REVIEW_FLAG_VOTED)
		priv->actions = 0;

	/* set actions up */
	if ((priv->actions & (1 << GS_PLUGIN_ACTION_REVIEW_UPVOTE |
			1 << GS_PLUGIN_ACTION_REVIEW_DOWNVOTE |
			1 << GS_PLUGIN_ACTION_REVIEW_DISMISS)) == 0) {
		gtk_widget_set_visible (priv->box_voting, FALSE);
	} else {
		gtk_widget_set_visible (priv->box_voting, TRUE);
		gtk_widget_set_visible (priv->button_yes,
					priv->actions & 1 << GS_PLUGIN_ACTION_REVIEW_UPVOTE);
		gtk_widget_set_visible (priv->button_no,
					priv->actions & 1 << GS_PLUGIN_ACTION_REVIEW_DOWNVOTE);
		gtk_widget_set_visible (priv->button_dismiss,
					priv->actions & 1 << GS_PLUGIN_ACTION_REVIEW_DISMISS);
	}
	gtk_widget_set_visible (priv->button_remove,
				priv->actions & 1 << GS_PLUGIN_ACTION_REVIEW_REMOVE);
	gtk_widget_set_visible (priv->button_report,
				priv->actions & 1 << GS_PLUGIN_ACTION_REVIEW_REPORT);

	/* mark insensitive if no network */
	if (priv->network_available) {
		gtk_widget_set_sensitive (priv->button_yes, TRUE);
		gtk_widget_set_sensitive (priv->button_no, TRUE);
		gtk_widget_set_sensitive (priv->button_remove, TRUE);
		gtk_widget_set_sensitive (priv->button_report, TRUE);
	} else {
		gtk_widget_set_sensitive (priv->button_yes, FALSE);
		gtk_widget_set_sensitive (priv->button_no, FALSE);
		gtk_widget_set_sensitive (priv->button_remove, FALSE);
		gtk_widget_set_sensitive (priv->button_report, FALSE);
	}
}

void
gs_review_row_set_network_available (GsReviewRow *review_row, gboolean network_available)
{
	GsReviewRowPrivate *priv = gs_review_row_get_instance_private (review_row);
	priv->network_available = network_available;
	gs_review_row_refresh (review_row);
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
	GsReviewRowPrivate *priv = gs_review_row_get_instance_private (row);
	priv->network_available = TRUE;
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
	gtk_widget_class_bind_template_child_private (widget_class, GsReviewRow, box_voting);
}

static void
gs_review_row_button_clicked_upvote_cb (GtkButton *button, GsReviewRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
		       GS_PLUGIN_ACTION_REVIEW_UPVOTE);
}

static void
gs_review_row_button_clicked_downvote_cb (GtkButton *button, GsReviewRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
		       GS_PLUGIN_ACTION_REVIEW_DOWNVOTE);
}

static void
gs_review_row_confirm_cb (GtkDialog *dialog, gint response_id, GsReviewRow *row)
{
	if (response_id == GTK_RESPONSE_YES) {
		g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
			       GS_PLUGIN_ACTION_REVIEW_REPORT);
	}
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
gs_review_row_button_clicked_report_cb (GtkButton *button, GsReviewRow *row)
{
	GtkWidget *dialog;
	GtkWidget *toplevel;
	GtkWidget *widget;
	g_autoptr(GString) str = NULL;

	str = g_string_new ("");

	/* TRANSLATORS: we explain what the action is going to do */
	g_string_append (str, _("You can report reviews for abusive, rude, or "
				"discriminatory behavior."));
	g_string_append (str, " ");

	/* TRANSLATORS: we ask the user if they really want to do this */
	g_string_append (str, _("Once reported, a review will be hidden until "
				"it has been checked by an administrator."));

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
	dialog = gtk_message_dialog_new (GTK_WINDOW (toplevel),
					 GTK_DIALOG_MODAL |
					 GTK_DIALOG_DESTROY_WITH_PARENT |
					 GTK_DIALOG_USE_HEADER_BAR,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CANCEL,
					 "%s",
					 /* TRANSLATORS: window title when
					  * reporting a user-submitted review
					  * for moderation */
					 _("Report Review?"));
	widget = gtk_dialog_add_button (GTK_DIALOG (dialog),
					/* TRANSLATORS: button text when
					 * sending a review for moderation */
					_("Report"),
					GTK_RESPONSE_YES);
	gtk_style_context_add_class (gtk_widget_get_style_context (widget),
				     "destructive-action");
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", str->str);
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gs_review_row_confirm_cb), row);
	gtk_window_present (GTK_WINDOW (dialog));
}

static void
gs_review_row_button_clicked_dismiss_cb (GtkButton *button, GsReviewRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
		       GS_PLUGIN_ACTION_REVIEW_DISMISS);
}

static void
gs_review_row_button_clicked_remove_cb (GtkButton *button, GsReviewRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0,
		       GS_PLUGIN_ACTION_REVIEW_REMOVE);
}

AsReview *
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
gs_review_row_new (AsReview *review)
{
	GsReviewRow *row;
	GsReviewRowPrivate *priv;

	g_return_val_if_fail (AS_IS_REVIEW (review), NULL);

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
