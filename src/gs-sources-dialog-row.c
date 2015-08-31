/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
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

#include "gs-sources-dialog-row.h"

struct _GsSourcesDialogRow
{
	GtkListBoxRow	 parent_instance;

	GtkWidget	*name_label;
	GtkWidget	*description_label;
};

G_DEFINE_TYPE (GsSourcesDialogRow, gs_sources_dialog_row, GTK_TYPE_LIST_BOX_ROW)

void
gs_sources_dialog_row_set_name (GsSourcesDialogRow *row,
                                const gchar        *name)
{
	gtk_label_set_text (GTK_LABEL (row->name_label), name);
}

void
gs_sources_dialog_row_set_description (GsSourcesDialogRow *row,
                                       const gchar        *description)
{
	gtk_label_set_text (GTK_LABEL (row->description_label), description);
}

static void
gs_sources_dialog_row_init (GsSourcesDialogRow *row)
{
	gtk_widget_init_template (GTK_WIDGET (row));
}

static void
gs_sources_dialog_row_class_init (GsSourcesDialogRowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-sources-dialog-row.ui");

	gtk_widget_class_bind_template_child (widget_class, GsSourcesDialogRow, name_label);
	gtk_widget_class_bind_template_child (widget_class, GsSourcesDialogRow, description_label);
}

GtkWidget *
gs_sources_dialog_row_new (void)
{
	return g_object_new (GS_TYPE_SOURCES_DIALOG_ROW, NULL);
}

/* vim: set noexpandtab: */
