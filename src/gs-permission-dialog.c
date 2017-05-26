/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Canonical Ltd.
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

#include "gs-permission-dialog.h"
#include "gs-permission-switch.h"
#include "gs-permission-combo-box.h"

struct _GsPermissionDialog
{
	GtkDialog	 parent_instance;

	GsApp		*app;
	GtkWidget	*permission_grid;
	GtkWidget	*close_button;
};

G_DEFINE_TYPE (GsPermissionDialog, gs_permission_dialog, GTK_TYPE_DIALOG)

enum {
	SIGNAL_PERMISSION_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

static void
close_button_clicked (GtkWidget *widget, GsPermissionDialog *dialog)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
gs_permission_dialog_init (GsPermissionDialog *dialog)
{
	gtk_widget_init_template (GTK_WIDGET (dialog));

	g_signal_connect (dialog->close_button, "clicked",
			  G_CALLBACK (close_button_clicked), dialog);
}

static void
gs_permission_dialog_dispose (GObject *object)
{
	GsPermissionDialog *dialog = GS_PERMISSION_DIALOG (object);

	g_clear_object (&dialog->app);

	G_OBJECT_CLASS (gs_permission_dialog_parent_class)->dispose (object);
}

static void
gs_permission_dialog_class_init (GsPermissionDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_permission_dialog_dispose;

	signals [SIGNAL_PERMISSION_CHANGED] =
		g_signal_new ("permission-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2, GS_TYPE_PERMISSION, GS_TYPE_PERMISSION_VALUE);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-permission-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsPermissionDialog, permission_grid);
	gtk_widget_class_bind_template_child (widget_class, GsPermissionDialog, close_button);
}

// FIXME: Make a GsPermissionControl interfaces that can be shared between GsPermissionSwitch and GsPermissionComboBox

static void
permission_switch_changed_cb (GsPermissionSwitch *sw, GsPermissionValue *value, GsPermissionDialog *dialog)
{
	g_signal_emit (dialog, signals[SIGNAL_PERMISSION_CHANGED], 0,
		       gs_permission_switch_get_permission (sw),
		       value);
}

static void
permission_combo_box_changed_cb (GsPermissionComboBox *combo, GsPermissionValue *value, GsPermissionDialog *dialog)
{
	g_signal_emit (dialog, signals[SIGNAL_PERMISSION_CHANGED], 0,
		       gs_permission_combo_box_get_permission (combo),
		       value);
}

static void
set_row (GsPermissionDialog *dialog, int row, GsPermission *permission)
{
	GtkWidget *label;
	GtkWidget *control;

	label = gtk_label_new (gs_permission_get_label (permission));
	gtk_label_set_xalign (GTK_LABEL (label), 1.0);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_widget_show (label);
	gtk_grid_attach (GTK_GRID (dialog->permission_grid), label, 0, row, 1, 1);

	if (gs_permission_get_values (permission)->len == 1) {
		control = GTK_WIDGET (gs_permission_switch_new (permission));
		g_signal_connect (control, "changed", G_CALLBACK (permission_switch_changed_cb), dialog);
	}
	else {
		control = GTK_WIDGET (gs_permission_combo_box_new (permission));
		g_signal_connect (control, "value-changed", G_CALLBACK (permission_combo_box_changed_cb), dialog);
	}
	gtk_widget_show (control);
	gtk_grid_attach (GTK_GRID (dialog->permission_grid), control, 1, row, 1, 1);
}

GtkWidget *
gs_permission_dialog_new (GsApp *app)
{
	GsPermissionDialog *dialog;
	GPtrArray *permissions;
	guint i;

	dialog = g_object_new (GS_TYPE_PERMISSION_DIALOG,
			       "use-header-bar", TRUE,
			       NULL);
	dialog->app = g_object_ref (app);

	permissions = gs_app_get_permissions (app);
	for (i = 0; i < permissions->len; i++) {
		GsPermission *permission = g_ptr_array_index (permissions, i);
		set_row (dialog, i, permission);
	}

	return GTK_WIDGET (dialog);
}
