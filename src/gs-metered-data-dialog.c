/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright © 2020 Endless Mobile, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-metered-data-dialog.h"

struct _GsMeteredDataDialog
{
	GtkDialog	 parent_instance;

	GtkWidget	*button_network_settings;
};

G_DEFINE_TYPE (GsMeteredDataDialog, gs_metered_data_dialog, GTK_TYPE_DIALOG)

static void
button_network_settings_clicked_cb (GtkButton *button,
				    gpointer   user_data)
{
	g_autoptr(GError) error_local = NULL;

	if (!g_spawn_command_line_async ("gnome-control-center wifi", &error_local)) {
		g_warning ("Error opening GNOME Control Center: %s",
			   error_local->message);
		return;
	}
}

static void
gs_metered_data_dialog_init (GsMeteredDataDialog *dialog)
{
	gtk_widget_init_template (GTK_WIDGET (dialog));
}

static void
gs_metered_data_dialog_class_init (GsMeteredDataDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-metered-data-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsMeteredDataDialog, button_network_settings);

	gtk_widget_class_bind_template_callback (widget_class, button_network_settings_clicked_cb);
}

GtkWidget *
gs_metered_data_dialog_new (GtkWindow *parent)
{
	GsMeteredDataDialog *dialog;

	dialog = g_object_new (GS_TYPE_METERED_DATA_DIALOG,
			       "use-header-bar", TRUE,
			       "transient-for", parent,
			       "modal", TRUE,
			       NULL);

	return GTK_WIDGET (dialog);
}
