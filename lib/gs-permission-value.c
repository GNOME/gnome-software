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

#include "gs-permission-value.h"

struct _GsPermissionValue
{
	GObject			 parent_instance;

	gchar			*label;
	GHashTable		*metadata;	/* utf8: utf8 */
};

G_DEFINE_TYPE (GsPermissionValue, gs_permission_value, G_TYPE_OBJECT)

/**
 * gs_permission_value_get_metadata_item:
 * @value: a #GsPermissionValue
 * @key: a string
 *
 * Gets some metadata from a permission value object.
 * It is left for the the plugin to use this method as required, but a
 * typical use would be to retrieve an ID for this permission value.
 *
 * Returns: A string value, or %NULL for not found
 */
const gchar *
gs_permission_value_get_metadata_item (GsPermissionValue *value, const gchar *key)
{
	g_return_val_if_fail (GS_IS_PERMISSION_VALUE (value), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	return g_hash_table_lookup (value->metadata, key);
}

/**
 * gs_permission_value_add_metadata:
 * @value: a #GsPermissionValue
 * @key: a string
 * @value: a string
 *
 * Adds metadata to the permission object.
 * It is left for the the plugin to use this method as required, but a
 * typical use would be to store an ID for this permission.
 */
void
gs_permission_value_add_metadata (GsPermissionValue *value, const gchar *key, const gchar *val)
{
	g_return_if_fail (GS_IS_PERMISSION_VALUE (value));
	g_hash_table_insert (value->metadata, g_strdup (key), g_strdup (val));
}

/**
 * gs_permission_value_get_label:
 * @permission: a #GsPermissionValue
 *
 * Get the label for this permission.
 *
 * Returns: a label string.
 */
const gchar *
gs_permission_value_get_label (GsPermissionValue *value)
{
	g_return_val_if_fail (GS_IS_PERMISSION_VALUE (value), NULL);
	return value->label;
}

static void
gs_permission_value_finalize (GObject *object)
{
	GsPermissionValue *value = GS_PERMISSION_VALUE (object);

	g_free (value->label);
	g_hash_table_unref (value->metadata);

	G_OBJECT_CLASS (gs_permission_value_parent_class)->finalize (object);
}

static void
gs_permission_value_class_init (GsPermissionValueClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_permission_value_finalize;
}

static void
gs_permission_value_init (GsPermissionValue *value)
{
	value->metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
						      g_free, g_free);
}

GsPermissionValue *
gs_permission_value_new (const gchar *label)
{
	GsPermissionValue *value;
	value = g_object_new (GS_TYPE_PERMISSION_VALUE, NULL);
	value->label = g_strdup (label);
	return GS_PERMISSION_VALUE (value);
}

/* vim: set noexpandtab: */
