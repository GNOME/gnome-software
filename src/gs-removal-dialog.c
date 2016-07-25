/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
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

#include "gs-removal-dialog.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

struct _GsRemovalDialog
{
	GtkMessageDialog	 parent_instance;
	GtkWidget		*listbox;
	GtkWidget		*scrolledwindow;
};

G_DEFINE_TYPE (GsRemovalDialog, gs_removal_dialog, GTK_TYPE_MESSAGE_DIALOG)

static void
list_header_func (GtkListBoxRow *row,
                  GtkListBoxRow *before,
                  gpointer user_data)
{
	GtkWidget *header = NULL;
	if (before != NULL)
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

static gint
list_sort_func (GtkListBoxRow *a,
                GtkListBoxRow *b,
                gpointer user_data)
{
	GObject *o1 = G_OBJECT (gtk_bin_get_child (GTK_BIN (a)));
	GObject *o2 = G_OBJECT (gtk_bin_get_child (GTK_BIN (b)));
	const gchar *key1 = g_object_get_data (o1, "sort");
	const gchar *key2 = g_object_get_data (o2, "sort");
	return g_strcmp0 (key1, key2);
}

static void
add_app (GtkListBox *listbox, GsApp *app)
{
	GtkWidget *box;
	GtkWidget *widget;
	GtkWidget *row;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_top (box, 12);
	gtk_widget_set_margin_start (box, 12);
	gtk_widget_set_margin_bottom (box, 12);
	gtk_widget_set_margin_end (box, 12);

	widget = gtk_label_new (gs_app_get_name (app));
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_label_set_ellipsize (GTK_LABEL (widget), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

	g_object_set_data_full (G_OBJECT (box),
	                        "sort",
	                        g_utf8_casefold (gs_app_get_name (app), -1),
	                        g_free);

	gtk_list_box_prepend (listbox, box);
	gtk_widget_show (widget);
	gtk_widget_show (box);

	row = gtk_widget_get_parent (box);
	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
}

static void
insert_details_widget (GtkMessageDialog *dialog, GtkWidget *widget)
{
	GList *children, *l;
	GtkWidget *message_area;

	message_area = gtk_message_dialog_get_message_area (dialog);
	g_assert (GTK_IS_BOX (message_area));

	/* find all label children and set the width chars properties */
	children = gtk_container_get_children (GTK_CONTAINER (message_area));
	for (l = children; l != NULL; l = l->next) {
		if (!GTK_IS_LABEL (l->data))
			continue;

		gtk_label_set_width_chars (GTK_LABEL (l->data), 40);
		gtk_label_set_max_width_chars (GTK_LABEL (l->data), 40);
	}

	gtk_container_add (GTK_CONTAINER (message_area), widget);
}

void
gs_removal_dialog_show_upgrade_removals (GsRemovalDialog *self,
                                         GsApp *upgrade)
{
	GPtrArray *removals;
	guint i;
	g_autofree gchar *name_version = NULL;

	name_version = g_strdup_printf ("%s %s",
	                                gs_app_get_name (upgrade),
	                                gs_app_get_version (upgrade));
	/* TRANSLATORS: This is a text displayed during a distro upgrade. %s
	   will be replaced by the name and version of distro, e.g. 'Fedora 23'. */
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (self),
	                                          _("Some of the currently installed software is not compatible with %s. "
	                                            "If you continue, the following will be automatically removed during the upgrade:"),
	                                          name_version);

	removals = gs_app_get_related (upgrade);
	for (i = 0; i < removals->len; i++) {
		GsApp *app = g_ptr_array_index (removals, i);
		g_autofree gchar *tmp = NULL;

		if (gs_app_get_state (app) != AS_APP_STATE_UNAVAILABLE)
			continue;
		tmp = gs_app_to_string (app);
		g_debug ("removal %u: %s", i, tmp);
		add_app (GTK_LIST_BOX (self->listbox), app);
	}
}

static void
gs_removal_dialog_init (GsRemovalDialog *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	insert_details_widget (GTK_MESSAGE_DIALOG (self), self->scrolledwindow);

	gtk_list_box_set_header_func (GTK_LIST_BOX (self->listbox),
	                              list_header_func,
	                              self,
	                              NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->listbox),
	                            list_sort_func,
	                            self, NULL);
}

static void
gs_removal_dialog_class_init (GsRemovalDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-removal-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsRemovalDialog, listbox);
	gtk_widget_class_bind_template_child (widget_class, GsRemovalDialog, scrolledwindow);
}

GtkWidget *
gs_removal_dialog_new (void)
{
	GsRemovalDialog *dialog;

	dialog = g_object_new (GS_TYPE_REMOVAL_DIALOG,
	                       NULL);
	return GTK_WIDGET (dialog);
}

/* vim: set noexpandtab: */
