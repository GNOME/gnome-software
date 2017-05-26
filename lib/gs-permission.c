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

#include <glib/gi18n.h>

#include "gs-permission.h"

struct _GsPermission
{
	GObject			 parent_instance;

	gchar			*label;
	GPtrArray		*values;
	GsPermissionValue	*value;
	GHashTable		*metadata;	/* utf8: utf8 */
};

G_DEFINE_TYPE (GsPermission, gs_permission, G_TYPE_OBJECT)

/**
 * gs_permission_get_metadata_item:
 * @permission: a #GsPermission
 * @key: a string
 *
 * Gets some metadata from a permission object.
 * It is left for the the plugin to use this method as required, but a
 * typical use would be to retrieve an ID for this permission.
 *
 * Returns: A string value, or %NULL for not found
 */
const gchar *
gs_permission_get_metadata_item (GsPermission *permission, const gchar *key)
{
	g_return_val_if_fail (GS_IS_PERMISSION (permission), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	return g_hash_table_lookup (permission->metadata, key);
}

/**
 * gs_permission_add_metadata:
 * @permission: a #GsPermission
 * @key: a string
 * @value: a string
 *
 * Adds metadata to the permission object.
 * It is left for the the plugin to use this method as required, but a
 * typical use would be to store an ID for this permission.
 */
void
gs_permission_add_metadata (GsPermission *permission, const gchar *key, const gchar *value)
{
	g_return_if_fail (GS_IS_PERMISSION (permission));
	g_hash_table_insert (permission->metadata, g_strdup (key), g_strdup (value));
}

/**
 * gs_permission_get_label:
 * @permission: a #GsPermission
 *
 * Get the label for this permission.
 *
 * Returns: a label string.
 */
const gchar *
gs_permission_get_label (GsPermission *permission)
{
	g_return_val_if_fail (GS_IS_PERMISSION (permission), NULL);
	return permission->label;
}

/**
 * gs_permission_add_value:
 * @permission: a #GsPermission
 * @value: a #GsPermissionValue
 *
 * Add a possible values for this permission.
 */
void
gs_permission_add_value (GsPermission *permission, GsPermissionValue *value)
{
	g_return_if_fail (GS_IS_PERMISSION (permission));
	g_ptr_array_add (permission->values, g_object_ref (value));
}

/**
 * gs_permission_get_values:
 * @permission: a #GsPermission
 *
 * Get the possible values for this permission.
 *
 * Returns: (element-type GsPermissionValue) (transfer none): a list
 */
GPtrArray *
gs_permission_get_values (GsPermission *permission)
{
	g_return_val_if_fail (GS_IS_PERMISSION (permission), NULL);
	return permission->values;
}

/**
 * gs_permission_get_value:
 * @permission: a #GsPermission
 *
 * Get the value for this permission.
 *
 * Returns: a %GsPermissionValue or %NULL.
 */
GsPermissionValue *
gs_permission_get_value (GsPermission *permission)
{
	g_return_val_if_fail (GS_IS_PERMISSION (permission), NULL);
	return permission->value;
}

/**
 * gs_permission_set_value:
 * @permission: a #GsPermission
 * @value: a #GsPermissionValue to set for this permission
 *
 * Set the value of this permission.
 */
void
gs_permission_set_value (GsPermission *permission, GsPermissionValue *value)
{
	g_return_if_fail (GS_IS_PERMISSION (permission));
	g_set_object (&permission->value, value);
}

static void
gs_permission_dispose (GObject *object)
{
	GsPermission *permission = GS_PERMISSION (object);

	g_clear_pointer (&permission->values, g_ptr_array_unref);
	g_clear_object (&permission->value);

	G_OBJECT_CLASS (gs_permission_parent_class)->dispose (object);
}

static void
gs_permission_finalize (GObject *object)
{
	GsPermission *permission = GS_PERMISSION (object);

	g_free (permission->label);
	g_hash_table_unref (permission->metadata);

	G_OBJECT_CLASS (gs_permission_parent_class)->finalize (object);
}

static void
gs_permission_class_init (GsPermissionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_permission_dispose;
	object_class->finalize = gs_permission_finalize;
}

static void
gs_permission_init (GsPermission *permission)
{
	permission->metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
						      g_free, g_free);
	permission->values = g_ptr_array_new_with_free_func (g_object_unref);
}

GsPermission *
gs_permission_new (const gchar *label)
{
	GsPermission *permission;
	permission = g_object_new (GS_TYPE_PERMISSION, NULL);
	permission->label = g_strdup (label);
	return GS_PERMISSION (permission);
}

/* vim: set noexpandtab: */
