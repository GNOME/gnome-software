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
#include <gtkspell/gtkspell.h>

#include "gs-review-dialog.h"
#include "gs-star-widget.h"

#define DESCRIPTION_LENGTH_MAX		3000	/* chars */
#define DESCRIPTION_LENGTH_MIN		60	/* chars */
#define SUMMARY_LENGTH_MAX		70	/* chars */
#define SUMMARY_LENGTH_MIN		15	/* chars */
#define WRITING_TIME_MIN		20	/* seconds */

struct _GsReviewDialog
{
	GtkDialog	 parent_instance;

	GtkWidget	*star;
	GtkWidget	*summary_entry;
	GtkWidget	*post_button;
	GtkWidget	*text_view;
	GtkSpellChecker	*spell_checker;
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
gs_review_dialog_changed_cb (GsReviewDialog *dialog)
{
	GtkTextBuffer *buffer;
	gboolean all_okay = TRUE;
	const gchar *msg = NULL;

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

	/* allow checking spelling */
	dialog->spell_checker = gtk_spell_checker_new ();
	gtk_spell_checker_attach (dialog->spell_checker,
				  GTK_TEXT_VIEW (dialog->text_view));

	/* require the user to spend at least 30 seconds on writing a review */
	dialog->timer_id = g_timeout_add_seconds (WRITING_TIME_MIN,
						  gs_review_dialog_timeout_cb,
						  dialog);

	/* update UI */
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
	if (dialog->timer_id > 0)
		g_source_remove (dialog->timer_id);
	g_clear_object (&dialog->spell_checker);
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
