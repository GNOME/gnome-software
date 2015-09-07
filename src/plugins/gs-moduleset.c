/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#include <string.h>
#include <glib.h>

#include "gs-cleanup.h"
#include "gs-moduleset.h"

typedef struct {
	GsModulesetModuleKind	 module_kind;
	gchar			*name;
	gchar			*category;
	gchar			*id;
} GsModulesetEntry;

typedef enum {
	GS_MODULESET_PARSER_SECTION_UNKNOWN,
	GS_MODULESET_PARSER_SECTION_MODULESET,
	GS_MODULESET_PARSER_SECTION_MODULE,
	GS_MODULESET_PARSER_SECTION_LAST
} GsModulesetParserSection;

typedef struct {
	gchar			*name_tmp;
	GPtrArray		*array;
	GsModulesetEntry	*entry_tmp;
	GsModulesetParserSection section;
} GsModulesetPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsModuleset, gs_moduleset, G_TYPE_OBJECT)

/**
 * gs_moduleset_get_modules:
 **/
gchar **
gs_moduleset_get_modules (GsModuleset *moduleset,
			  GsModulesetModuleKind module_kind,
			  const gchar *name,
			  const gchar *category)
{
	GsModulesetPrivate *priv = gs_moduleset_get_instance_private (moduleset);
	GsModulesetEntry *entry;
	gchar **data;
	guint i;
	guint idx = 0;

	g_return_val_if_fail (GS_IS_MODULESET (moduleset), NULL);

	/* return results that match */
	data = g_new0 (gchar *, priv->array->len);
	for (i = 0; i < priv->array->len; i++) {
		entry = g_ptr_array_index (priv->array, i);
		if (entry->module_kind != module_kind)
			continue;
		if (name && g_strcmp0 (entry->name, name) != 0)
			continue;
		if (category && g_strcmp0 (entry->category, category) != 0)
			continue;
		data[idx++] = g_strdup (entry->id);
	}

	return data;
}

/**
 * gs_moduleset_get_core_packages:
 **/
gchar **
gs_moduleset_get_core_packages (GsModuleset *moduleset)
{
	return gs_moduleset_get_modules (moduleset,
	                                 GS_MODULESET_MODULE_KIND_PACKAGE,
	                                 "core",
	                                 NULL);
}

/**
 * gs_moduleset_get_system_apps:
 **/
gchar **
gs_moduleset_get_system_apps (GsModuleset *moduleset)
{
	return gs_moduleset_get_modules (moduleset,
	                                 GS_MODULESET_MODULE_KIND_APPLICATION,
	                                 "system",
	                                 NULL);
}

/**
 * gs_moduleset_get_popular_apps:
 **/
gchar **
gs_moduleset_get_popular_apps (GsModuleset *moduleset)
{
	return gs_moduleset_get_modules (moduleset,
	                                 GS_MODULESET_MODULE_KIND_APPLICATION,
	                                 "popular",
	                                 NULL);
}

/**
 * gs_moduleset_get_featured_apps:
 **/
gchar **
gs_moduleset_get_featured_apps (GsModuleset *moduleset,
                                const gchar *category)
{
	return gs_moduleset_get_modules (moduleset,
	                                 GS_MODULESET_MODULE_KIND_APPLICATION,
	                                 "featured",
	                                 category);
}

/**
 * gs_moduleset_get_categories:
 **/
gchar **
gs_moduleset_get_featured_categories (GsModuleset *moduleset)
{
	GsModulesetPrivate *priv = gs_moduleset_get_instance_private (moduleset);
	GsModulesetEntry *entry;
	guint i;
	g_autoptr(GHashTable) categories_hash = NULL;

	g_return_val_if_fail (GS_IS_MODULESET (moduleset), NULL);

	categories_hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < priv->array->len; i++) {
		entry = g_ptr_array_index (priv->array, i);
		if (g_strcmp0 (entry->name, "featured") != 0)
			continue;
		if (entry->category == NULL)
			continue;
		g_hash_table_insert (categories_hash, g_strdup (entry->category), GINT_TO_POINTER (1));
	}

	return (gchar **) g_hash_table_get_keys_as_array (categories_hash, NULL);
}

/**
 * gs_moduleset_get_n_featured:
 **/
guint
gs_moduleset_get_n_featured (GsModuleset *moduleset,
                             const gchar *category)
{
	GsModulesetPrivate *priv = gs_moduleset_get_instance_private (moduleset);
	GsModulesetEntry *entry;
	guint i;
	guint cnt;

	g_return_val_if_fail (GS_IS_MODULESET (moduleset), NULL);

	cnt = 0;
	for (i = 0; i < priv->array->len; i++) {
		entry = g_ptr_array_index (priv->array, i);
		if (entry->module_kind != GS_MODULESET_MODULE_KIND_APPLICATION)
			continue;
		if (g_strcmp0 (entry->name, "featured") != 0)
			continue;
		if (g_strcmp0 (entry->category, category) != 0)
			continue;
		cnt++;
	}

	return cnt;
}

/**
 * gs_moduleset_section_from_string:
 **/
static GsModulesetParserSection
gs_moduleset_section_from_string (const gchar *element_name)
{
	if (g_strcmp0 (element_name, "moduleset") == 0)
		return GS_MODULESET_PARSER_SECTION_MODULESET;
	if (g_strcmp0 (element_name, "module") == 0)
		return GS_MODULESET_PARSER_SECTION_MODULE;
	return GS_MODULESET_PARSER_SECTION_UNKNOWN;
}

/**
 * gs_moduleset_module_kind_from_string:
 **/
static GsModulesetModuleKind
gs_moduleset_module_kind_from_string (const gchar *str)
{
	if (g_strcmp0 (str, "pkgname") == 0)
		return GS_MODULESET_MODULE_KIND_PACKAGE;
	if (g_strcmp0 (str, "application") == 0)
		return GS_MODULESET_MODULE_KIND_APPLICATION;
	return GS_MODULESET_MODULE_KIND_UNKNOWN;
}

/**
 * gs_moduleset_parser_start_element:
 **/
static void
gs_moduleset_parser_start_element (GMarkupParseContext *context,
				   const gchar *element_name,
				   const gchar **attribute_names,
				   const gchar **attribute_values,
				   gpointer user_data,
				   GError **error)
{
	GsModuleset *moduleset = GS_MODULESET (user_data);
	GsModulesetPrivate *priv = gs_moduleset_get_instance_private (moduleset);
	GsModulesetParserSection section_new;
	GsModulesetModuleKind kind = GS_MODULESET_MODULE_KIND_UNKNOWN;
	const gchar *category = NULL;
	guint i;

	section_new = gs_moduleset_section_from_string (element_name);
	if (section_new == GS_MODULESET_PARSER_SECTION_UNKNOWN)
		return;

	switch (priv->section) {
	case GS_MODULESET_PARSER_SECTION_UNKNOWN:
		if (section_new == GS_MODULESET_PARSER_SECTION_MODULESET) {
			for (i = 0; attribute_names[i] != NULL; i++) {
				if (g_strcmp0 (attribute_names[i], "name") == 0) {
					g_free (priv->name_tmp);
					priv->name_tmp = g_strdup (attribute_values[i]);
				}
			}
			priv->section = section_new;
			return;
		}
		g_warning ("unknown->%s", element_name);
		break;
	case GS_MODULESET_PARSER_SECTION_MODULESET:
		if (section_new == GS_MODULESET_PARSER_SECTION_MODULE) {
			priv->section = section_new;
			priv->entry_tmp = g_slice_new0 (GsModulesetEntry);
			priv->entry_tmp->name = g_strdup (priv->name_tmp);
			for (i = 0; attribute_names[i] != NULL; i++) {
				if (g_strcmp0 (attribute_names[i], "type") == 0) {
					kind = gs_moduleset_module_kind_from_string (attribute_values[i]);
				}
				if (g_strcmp0 (attribute_names[i], "category") == 0) {
					category = attribute_values[i];
				}
			}
			priv->entry_tmp->module_kind = kind;
			priv->entry_tmp->category = g_strdup (category);
			return;
		}
		g_warning ("moduleset->%s", element_name);
		break;
	default:
		g_warning ("->%s", element_name);
		break;
	}
}

/**
 * gs_moduleset_parser_end_element:
 **/
static void
gs_moduleset_parser_end_element (GMarkupParseContext *context,
				 const gchar *element_name,
				 gpointer user_data,
				 GError **error)
{
	GsModuleset *moduleset = GS_MODULESET (user_data);
	GsModulesetPrivate *priv = gs_moduleset_get_instance_private (moduleset);

	switch (priv->section) {
	case GS_MODULESET_PARSER_SECTION_MODULESET:
		priv->section = GS_MODULESET_PARSER_SECTION_UNKNOWN;
		g_free (priv->name_tmp);
		priv->name_tmp = NULL;
		break;
	case GS_MODULESET_PARSER_SECTION_MODULE:
		priv->section = GS_MODULESET_PARSER_SECTION_MODULESET;
		g_ptr_array_add (priv->array, priv->entry_tmp);
		priv->entry_tmp = NULL;
		break;
	default:
		g_warning ("<-%s", element_name);
		break;
	}
}

/**
 * gs_moduleset_parser_text:
 **/
static void
gs_moduleset_parser_text (GMarkupParseContext *context,
			  const gchar *text,
			  gsize text_len,
			  gpointer user_data,
			  GError **error)
{
	GsModuleset *moduleset = GS_MODULESET (user_data);
	GsModulesetPrivate *priv = gs_moduleset_get_instance_private (moduleset);
	switch (priv->section) {
	case GS_MODULESET_PARSER_SECTION_MODULESET:
		break;
	case GS_MODULESET_PARSER_SECTION_MODULE:
		priv->entry_tmp->id = g_strndup (text, text_len);
		break;
	default:
		break;
	}
}

/**
 * gs_moduleset_parse_filename:
 **/
gboolean
gs_moduleset_parse_filename (GsModuleset *moduleset, const gchar *filename, GError **error)
{
	const GMarkupParser parser = {
		gs_moduleset_parser_start_element,
		gs_moduleset_parser_end_element,
		gs_moduleset_parser_text,
		NULL,
		NULL };
	GMarkupParseContext *ctx;
	gboolean ret;
	gsize data_len;
	g_autofree gchar *data = NULL;

	g_return_val_if_fail (GS_IS_MODULESET (moduleset), FALSE);

	/* parse */
	ctx = g_markup_parse_context_new (&parser,
					  G_MARKUP_PREFIX_ERROR_POSITION,
					  moduleset, NULL);
	ret = g_file_get_contents (filename, &data, &data_len, error);
	if (!ret)
		goto out;
	ret = g_markup_parse_context_parse (ctx, data, data_len, error);
	if (!ret)
		goto out;
out:
	g_markup_parse_context_free (ctx);
	return ret;
}

/**
 * gs_moduleset_parse_path:
 **/
gboolean
gs_moduleset_parse_path (GsModuleset *moduleset, const gchar *path, GError **error)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;

	/* search all the files in the path */
	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((filename = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *tmp = NULL;
		if (!g_str_has_suffix (filename, ".xml"))
			continue;
		tmp = g_build_filename (path, filename, NULL);
		if (!gs_moduleset_parse_filename (moduleset, tmp, error))
			return FALSE;
	}
	return TRUE;
}

static void
gs_moduleset_entry_free (GsModulesetEntry *entry)
{
	g_free (entry->id);
	g_free (entry->name);
	g_free (entry->category);
	g_slice_free (GsModulesetEntry, entry);
}

static void
gs_moduleset_finalize (GObject *object)
{
	GsModuleset *moduleset;
	GsModulesetPrivate *priv;

	g_return_if_fail (GS_IS_MODULESET (object));

	moduleset = GS_MODULESET (object);
	priv = gs_moduleset_get_instance_private (moduleset);
	g_ptr_array_unref (priv->array);

	G_OBJECT_CLASS (gs_moduleset_parent_class)->finalize (object);
}

static void
gs_moduleset_class_init (GsModulesetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_moduleset_finalize;
}

static void
gs_moduleset_init (GsModuleset *moduleset)
{
	GsModulesetPrivate *priv = gs_moduleset_get_instance_private (moduleset);
	priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_moduleset_entry_free);
}

GsModuleset *
gs_moduleset_new (void)
{
	GsModuleset *moduleset;
	moduleset = g_object_new (GS_TYPE_MODULESET, NULL);
	return GS_MODULESET (moduleset);
}
