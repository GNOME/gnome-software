/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#ifdef HAVE_GSPELL
#include <gspell/gspell.h>
#endif

#include "gs-review-dialog.h"
#include "gs-star-widget.h"

#define DESCRIPTION_LENGTH_MAX		3000	/* chars */
#define DESCRIPTION_LENGTH_MIN		15	/* chars */
#define SUMMARY_LENGTH_MAX		70	/* chars */
#define SUMMARY_LENGTH_MIN		3	/* chars */
#define WRITING_TIME_MIN		5	/* seconds */

struct _GsReviewDialog
{
	GtkDialog	 parent_instance;

	GtkWidget	*star;
	GtkWidget	*label_rating_desc;
	GtkWidget	*summary_entry;
	GtkWidget	*post_button;
	GtkWidget	*text_view;
	guint		 timer_id;
};

G_DEFINE_TYPE (GsReviewDialog, gs_review_dialog, GTK_TYPE_DIALOG)

gint
gs_review_dialog_get_rating (GsReviewDialog *dialog)
{
	return gs_star_widget_get_rating (GS_STAR_WIDGET (dialog->star));
}

void
gs_review_dialog_set_rating (GsReviewDialog *dialog, gint rating)
{
	gs_star_widget_set_rating (GS_STAR_WIDGET (dialog->star), rating);
}

const gchar *
gs_review_dialog_get_summary (GsReviewDialog *dialog)
{
	return gtk_entry_get_text (GTK_ENTRY (dialog->summary_entry));
}

gchar *
gs_review_dialog_get_text (GsReviewDialog *dialog)
{
	GtkTextBuffer *buffer;
	GtkTextIter start, end;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (dialog->text_view));
	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter (buffer, &end);
	return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}

static void
gs_review_dialog_update_review_comment (GsReviewDialog *dialog)
{
	const gchar *msg = NULL;
	gint perc;

	/* update the rating description */
	perc = gs_star_widget_get_rating (GS_STAR_WIDGET (dialog->star));
	if (perc == 20) {
		/* TRANSLATORS: lighthearted star rating description;
		 *		A really bad application */
		msg = _("Hate it");
	} else if (perc == 40) {
		/* TRANSLATORS: lighthearted star rating description;
		 *		Not a great application */
		msg = _("Don’t like it");
	} else if (perc == 60) {
		/* TRANSLATORS: lighthearted star rating description;
		 *		A fairly-good application */
		msg = _("It’s OK");
	} else if (perc == 80) {
		/* TRANSLATORS: lighthearted star rating description;
		 *		A good application */
		msg = _("Like it");
	} else if (perc == 100) {
		/* TRANSLATORS: lighthearted star rating description;
		 *		A really awesome application */
		msg = _("Love it");
	} else {
		/* just reserve space */
		msg = "";
	}
	gtk_label_set_label (GTK_LABEL (dialog->label_rating_desc), msg);
}

static void
gs_review_dialog_changed_cb (GsReviewDialog *dialog)
{
	GtkTextBuffer *buffer;
	gboolean all_okay = TRUE;
	const gchar *msg = NULL;

	/* update review text */
	gs_review_dialog_update_review_comment (dialog);

	/* require rating, summary and long review */
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (dialog->text_view));
	if (dialog->timer_id != 0) {
		/* TRANSLATORS: the review can't just be copied and pasted */
		msg = _("Please take more time writing the review");
		all_okay = FALSE;
	} else if (gs_star_widget_get_rating (GS_STAR_WIDGET (dialog->star)) == 0) {
		/* TRANSLATORS: the review is not acceptable */
		msg = _("Please choose a star rating");
		all_okay = FALSE;
	} else if (gtk_entry_get_text_length (GTK_ENTRY (dialog->summary_entry)) < SUMMARY_LENGTH_MIN) {
		/* TRANSLATORS: the review is not acceptable */
		msg = _("The summary is too short");
		all_okay = FALSE;
	} else if (gtk_entry_get_text_length (GTK_ENTRY (dialog->summary_entry)) > SUMMARY_LENGTH_MAX) {
		/* TRANSLATORS: the review is not acceptable */
		msg = _("The summary is too long");
		all_okay = FALSE;
	} else if (gtk_text_buffer_get_char_count (buffer) < DESCRIPTION_LENGTH_MIN) {
		/* TRANSLATORS: the review is not acceptable */
		msg = _("The description is too short");
		all_okay = FALSE;
	} else if (gtk_text_buffer_get_char_count (buffer) > DESCRIPTION_LENGTH_MAX) {
		/* TRANSLATORS: the review is not acceptable */
		msg = _("The description is too long");
		all_okay = FALSE;
	}

	/* tell the user what's happening */
	gtk_widget_set_tooltip_text (dialog->post_button, msg);

	/* can the user submit this? */
	gtk_widget_set_sensitive (dialog->post_button, all_okay);
}

static gboolean
gs_review_dialog_timeout_cb (gpointer user_data)
{
	GsReviewDialog *dialog = GS_REVIEW_DIALOG (user_data);
	dialog->timer_id = 0;
	gs_review_dialog_changed_cb (dialog);
	return FALSE;
}

static void
gs_review_dialog_init (GsReviewDialog *dialog)
{
	GtkTextBuffer *buffer;
	gtk_widget_init_template (GTK_WIDGET (dialog));
	gs_star_widget_set_icon_size (GS_STAR_WIDGET (dialog->star), 32);

#ifdef HAVE_GSPELL
	/* allow checking spelling */
	{
		GspellEntry *gspell_entry;
		GspellTextView *gspell_view;

		gspell_entry = gspell_entry_get_from_gtk_entry (GTK_ENTRY (dialog->summary_entry));
		gspell_entry_basic_setup (gspell_entry);

		gspell_view = gspell_text_view_get_from_gtk_text_view (GTK_TEXT_VIEW (dialog->text_view));
		gspell_text_view_basic_setup (gspell_view);
	}
#endif

	/* require the user to spend at least 30 seconds on writing a review */
	dialog->timer_id = g_timeout_add_seconds (WRITING_TIME_MIN,
						  gs_review_dialog_timeout_cb,
						  dialog);

	/* update UI */
	gs_star_widget_set_interactive (GS_STAR_WIDGET (dialog->star), TRUE);
	g_signal_connect_swapped (dialog->star, "rating-changed",
				  G_CALLBACK (gs_review_dialog_changed_cb), dialog);
	g_signal_connect_swapped (dialog->summary_entry, "notify::text",
				  G_CALLBACK (gs_review_dialog_changed_cb), dialog);
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (dialog->text_view));
	g_signal_connect_swapped (buffer, "changed",
				  G_CALLBACK (gs_review_dialog_changed_cb), dialog);

	gtk_widget_set_sensitive (dialog->post_button, FALSE);

}

static void
gs_review_row_dispose (GObject *object)
{
	GsReviewDialog *dialog = GS_REVIEW_DIALOG (object);
	if (dialog->timer_id > 0) {
		g_source_remove (dialog->timer_id);
		dialog->timer_id = 0;
	}
	G_OBJECT_CLASS (gs_review_dialog_parent_class)->dispose (object);
}

static void
gs_review_dialog_class_init (GsReviewDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_review_row_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-review-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsReviewDialog, star);
	gtk_widget_class_bind_template_child (widget_class, GsReviewDialog, label_rating_desc);
	gtk_widget_class_bind_template_child (widget_class, GsReviewDialog, summary_entry);
	gtk_widget_class_bind_template_child (widget_class, GsReviewDialog, text_view);
	gtk_widget_class_bind_template_child (widget_class, GsReviewDialog, post_button);
}

GtkWidget *
gs_review_dialog_new (void)
{
	return GTK_WIDGET (g_object_new (GS_TYPE_REVIEW_DIALOG,
					 "use-header-bar", TRUE,
					 NULL));
}

/* vim: set noexpandtab: */
