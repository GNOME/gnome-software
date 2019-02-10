/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014-2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <gtk/gtk.h>

#include "gs-first-run-dialog.h"

struct _GsFirstRunDialog
{
	GtkDialog	 parent_instance;

	GtkWidget	*button;
};

G_DEFINE_TYPE (GsFirstRunDialog, gs_first_run_dialog, GTK_TYPE_DIALOG)

static void
button_clicked_cb (GtkWidget *widget, GsFirstRunDialog *dialog)
{
	gtk_window_close (GTK_WINDOW (dialog));
}

static void
gs_first_run_dialog_init (GsFirstRunDialog *dialog)
{
	GtkWidget *button_label;

	gtk_widget_init_template (GTK_WIDGET (dialog));

	button_label = gtk_bin_get_child (GTK_BIN (dialog->button));
	gtk_widget_set_margin_start (button_label, 16);
	gtk_widget_set_margin_end (button_label, 16);

	g_signal_connect (dialog->button, "clicked", G_CALLBACK (button_clicked_cb), dialog);
}

static void
gs_first_run_dialog_class_init (GsFirstRunDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-first-run-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsFirstRunDialog, button);
}

GtkWidget *
gs_first_run_dialog_new (void)
{
	return GTK_WIDGET (g_object_new (GS_TYPE_FIRST_RUN_DIALOG,
					 "use-header-bar", TRUE,
					 NULL));
}
