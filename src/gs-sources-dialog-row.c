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

	GtkWidget	*active_switch;
	GtkWidget	*name_label;
	GtkWidget	*comment_label;
	GtkWidget	*description_label;
};

enum {
	PROP_0,
	PROP_SWITCH_ACTIVE,
	PROP_LAST
};

G_DEFINE_TYPE (GsSourcesDialogRow, gs_sources_dialog_row, GTK_TYPE_LIST_BOX_ROW)

void
gs_sources_dialog_row_set_switch_enabled (GsSourcesDialogRow *row,
				       gboolean switch_enabled)
{
	gtk_widget_set_visible (row->active_switch, switch_enabled);
}

void
gs_sources_dialog_row_set_switch_active (GsSourcesDialogRow *row,
					 gboolean switch_active)
{
	gtk_switch_set_active (GTK_SWITCH (row->active_switch), switch_active);
}

void
gs_sources_dialog_row_set_name (GsSourcesDialogRow *row, const gchar *name)
{
	gtk_label_set_text (GTK_LABEL (row->name_label), name);
	gtk_widget_set_visible (row->name_label, name != NULL);
}

void
gs_sources_dialog_row_set_comment (GsSourcesDialogRow *row, const gchar *comment)
{
	gtk_label_set_markup (GTK_LABEL (row->comment_label), comment);
	gtk_widget_set_visible (row->comment_label, comment != NULL);

	/* make the name bold */
	if (comment != NULL) {
		PangoAttrList *attr_list = pango_attr_list_new ();
		pango_attr_list_insert (attr_list,
					pango_attr_weight_new (PANGO_WEIGHT_BOLD));
		gtk_label_set_attributes (GTK_LABEL (row->name_label), attr_list);
		pango_attr_list_unref (attr_list);
	}
}

void
gs_sources_dialog_row_set_description (GsSourcesDialogRow *row, const gchar *description)
{
	gtk_label_set_markup (GTK_LABEL (row->description_label), description);
	gtk_widget_set_visible (row->description_label, description != NULL);
}

static void
gs_sources_dialog_switch_active_cb (GtkSwitch *active_switch,
				    GParamSpec *pspec,
				    GsSourcesDialogRow *row)
{
	g_object_notify (G_OBJECT (row), "switch-active");
}

gboolean
gs_sources_dialog_row_get_switch_active (GsSourcesDialogRow *row)
{
	return gtk_switch_get_active (GTK_SWITCH (row->active_switch));
}

static void
gs_sources_dialog_row_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec)
{
	GsSourcesDialogRow *row = GS_SOURCES_DIALOG_ROW (object);
	switch (prop_id) {
	case PROP_SWITCH_ACTIVE:
		g_value_set_boolean (value,
				     gs_sources_dialog_row_get_switch_active (row));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_sources_dialog_row_init (GsSourcesDialogRow *row)
{
	gtk_widget_init_template (GTK_WIDGET (row));
	g_signal_connect (row->active_switch, "notify::active",
			  G_CALLBACK (gs_sources_dialog_switch_active_cb), row);
}

static void
gs_sources_dialog_row_class_init (GsSourcesDialogRowClass *klass)
{
	GParamSpec *pspec;
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gs_sources_dialog_row_get_property;

	pspec = g_param_spec_string ("switch-active", NULL, NULL, FALSE,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_SWITCH_ACTIVE, pspec);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-sources-dialog-row.ui");

	gtk_widget_class_bind_template_child (widget_class, GsSourcesDialogRow, active_switch);
	gtk_widget_class_bind_template_child (widget_class, GsSourcesDialogRow, name_label);
	gtk_widget_class_bind_template_child (widget_class, GsSourcesDialogRow, comment_label);
	gtk_widget_class_bind_template_child (widget_class, GsSourcesDialogRow, description_label);
}

GtkWidget *
gs_sources_dialog_row_new (void)
{
	return g_object_new (GS_TYPE_SOURCES_DIALOG_ROW, NULL);
}

/* vim: set noexpandtab: */
