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

#include "gs-review-dialog.h"
#include "gs-star-widget.h"

struct _GsReviewDialog
{
	GtkDialog	 parent_instance;

	GtkWidget	*star;
	GtkWidget	*summary_entry;
	GtkWidget	*text_view;
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
gs_review_dialog_init (GsReviewDialog *dialog)
{
	gtk_widget_init_template (GTK_WIDGET (dialog));
	gs_star_widget_set_icon_size (GS_STAR_WIDGET (dialog->star), 32);
}

static void
gs_review_dialog_class_init (GsReviewDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-review-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsReviewDialog, star);
	gtk_widget_class_bind_template_child (widget_class, GsReviewDialog, summary_entry);
	gtk_widget_class_bind_template_child (widget_class, GsReviewDialog, text_view);
}

GtkWidget *
gs_review_dialog_new (void)
{
	return GTK_WIDGET (g_object_new (GS_TYPE_REVIEW_DIALOG,
					 "use-header-bar", TRUE,
					 NULL));
}

/* vim: set noexpandtab: */
