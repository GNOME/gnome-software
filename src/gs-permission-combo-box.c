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

#include "gs-permission-combo-box.h"

struct _GsPermissionComboBox
{
	GtkComboBox	 parent_instance;

	GsPermission	*permission;
};

G_DEFINE_TYPE (GsPermissionComboBox, gs_permission_combo_box, GTK_TYPE_COMBO_BOX)

enum {
	SIGNAL_VALUE_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

GsPermission *
gs_permission_combo_box_get_permission (GsPermissionComboBox *combo)
{
	g_return_val_if_fail (GS_IS_PERMISSION_COMBO_BOX (combo), NULL);
	return combo->permission;
}

static void
changed_cb (GsPermissionComboBox *combo)
{
	GtkTreeIter iter;
	GsPermissionValue *value = NULL;

	if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo), &iter))
		gtk_tree_model_get (gtk_combo_box_get_model (GTK_COMBO_BOX (combo)), &iter, 1, &value, -1);

	g_signal_emit (combo, signals[SIGNAL_VALUE_CHANGED], 0, value);
}

static void
gs_permission_combo_box_dispose (GObject *object)
{
	GsPermissionComboBox *combo = GS_PERMISSION_COMBO_BOX (object);

	g_clear_object (&combo->permission);

	G_OBJECT_CLASS (gs_permission_combo_box_parent_class)->dispose (object);
}

static void
gs_permission_combo_box_class_init (GsPermissionComboBoxClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_permission_combo_box_dispose;

	signals [SIGNAL_VALUE_CHANGED] =
		g_signal_new ("value-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, GS_TYPE_PERMISSION_VALUE);
}

static void
gs_permission_combo_box_init (GsPermissionComboBox *combo)
{
}

GsPermissionComboBox *
gs_permission_combo_box_new (GsPermission *permission)
{
	GsPermissionComboBox *combo;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	guint i;
	GtkTreeIter iter;
	GPtrArray *values;

	combo = g_object_new (GS_TYPE_PERMISSION_COMBO_BOX, NULL);
	combo->permission = g_object_ref (permission);

	store = gtk_list_store_new (2, G_TYPE_STRING, GS_TYPE_PERMISSION_VALUE);
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (store));

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (combo), renderer, "text", 0);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, "(disconnected)", 1, NULL, -1);
	values = gs_permission_get_values (permission);
	for (i = 0; i < values->len; i++) {
		GsPermissionValue *value = g_ptr_array_index (values, i);

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, gs_permission_value_get_label (value), 1, value, -1);

		if (value == gs_permission_get_value (permission))
			gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo), &iter);
	}

	g_signal_connect (combo, "changed", G_CALLBACK (changed_cb), NULL);

	return combo;
}
