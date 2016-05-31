/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

/**
 * SECTION:gs-category
 * @short_description: An category that contains applications
 *
 * This object provides functionality that allows a plugin to create
 * a tree structure of categories that each contain #GsApp's.
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-category-private.h"

struct _GsCategory
{
	GObject		 parent_instance;

	gchar		*id;
	gchar		*name;
	gchar		*icon;
	gboolean	 important;
	GPtrArray	*key_colors;
	GsCategory	*parent;
	guint		 size;
	GPtrArray	*children;
};

G_DEFINE_TYPE (GsCategory, gs_category, G_TYPE_OBJECT)

/**
 * gs_category_get_size:
 * @category: a #GsCategory
 *
 * Returns how many applications the category could contain.
 *
 * NOTE: This may over-estimate the number if duplicate applications are
 * filtered or core applications are not shown.
 *
 * Returns: the number of apps in the category
 **/
guint
gs_category_get_size (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), 0);
	return category->size;
}

/**
 * gs_category_set_size:
 * @category: a #GsCategory
 * @size: the number of applications
 *
 * Sets the number of applications in the category.
 * Most plugins do not need to call this function.
 **/
void
gs_category_set_size (GsCategory *category, guint size)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	category->size = size;
}

/**
 * gs_category_increment_size:
 * @category: a #GsCategory
 *
 * Adds one to the size count if an application is available
 **/
void
gs_category_increment_size (GsCategory *category)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	category->size++;
}

/**
 * gs_category_get_id:
 * @category: a #GsCategory
 *
 * Gets the category ID.
 *
 * Returns: the string, e.g. "other"
 **/
const gchar *
gs_category_get_id (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return category->id;
}

/**
 * gs_category_get_name:
 * @category: a #GsCategory
 *
 * Gets the category name.
 *
 * Returns: the string, or %NULL
 **/
const gchar *
gs_category_get_name (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);

	/* special case, we don't want translations in the plugins */
	if (g_strcmp0 (category->id, "other") == 0) {
		/* TRANSLATORS: this is where all applications that don't
		 * fit in other groups are put */
		return _("Other");
	}
	if (g_strcmp0 (category->id, "all") == 0) {
		/* TRANSLATORS: this is a subcategory matching all the
		 * different apps in the parent category, e.g. "Games" */
		return _("All");
	}
	if (g_strcmp0 (category->id, "featured") == 0) {
		/* TRANSLATORS: this is a subcategory of featured apps */
		return _("Featured");
	}

	return category->name;
}

/**
 * gs_category_set_name:
 * @category: a #GsCategory
 * @name: a category name, or %NULL
 *
 * Sets the category name.
 **/
void
gs_category_set_name (GsCategory *category, const gchar *name)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	g_free (category->name);
	category->name = g_strdup (name);
}

/**
 * gs_category_get_icon:
 * @category: a #GsCategory
 *
 * Gets the category icon.
 *
 * Returns: the string, or %NULL
 **/
const gchar *
gs_category_get_icon (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);

	/* special case, we don't want translations in the plugins */
	if (g_strcmp0 (category->id, "other") == 0) {
		/* TRANSLATORS: this is where all applications that don't
		 * fit in other groups are put */
		return _("Other");
	}
	if (g_strcmp0 (category->id, "all") == 0) {
		/* TRANSLATORS: this is a subcategory matching all the
		 * different apps in the parent category, e.g. "Games" */
		return _("All");
	}
	if (g_strcmp0 (category->id, "featured") == 0) {
		/* TRANSLATORS: this is a subcategory of featured apps */
		return _("Featured");
	}

	return category->icon;
}

/**
 * gs_category_set_icon:
 * @category: a #GsCategory
 * @icon: a category icon, or %NULL
 *
 * Sets the category icon.
 **/
void
gs_category_set_icon (GsCategory *category, const gchar *icon)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	g_free (category->icon);
	category->icon = g_strdup (icon);
}

/**
 * gs_category_get_important:
 * @category: a #GsCategory
 *
 * Gets if the category is important.
 * Important categories may be shown before other categories, or tagged in a
 * different way, for example with color or in a different section.
 *
 * Returns: the string, or %NULL
 **/
gboolean
gs_category_get_important (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), FALSE);
	return category->important;
}

/**
 * gs_category_set_important:
 * @category: a #GsCategory
 * @important: a category important, or %NULL
 *
 * Sets if the category is important.
 **/
void
gs_category_set_important (GsCategory *category, gboolean important)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	category->important = important;
}

/**
 * gs_category_get_key_colors:
 * @category: a #GsCategory
 *
 * Gets the list of key colors for the category.
 *
 * Returns: (element-type GdkRGBA) (transfer none): An array
 **/
GPtrArray *
gs_category_get_key_colors (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return category->key_colors;
}

/**
 * gs_category_add_key_color:
 * @category: a #GsCategory
 * @key_color: a #GdkRGBA
 *
 * Adds a key color to the category icon.
 *
 * Returns: the string, or %NULL
 **/
void
gs_category_add_key_color (GsCategory *category, const GdkRGBA *key_color)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	g_return_if_fail (key_color != NULL);
	g_ptr_array_add (category->key_colors, gdk_rgba_copy (key_color));
}

/**
 * gs_category_find_child:
 * @category: a #GsCategory
 * @id: a category ID, e.g. "other"
 *
 * Find a child category with a specific ID.
 *
 * Returns: (transfer none): the #GsCategory, or %NULL
 **/
GsCategory *
gs_category_find_child (GsCategory *category, const gchar *id)
{
	GsCategory *tmp;
	guint i;

	/* find the subcategory */
	for (i = 0; i < category->children->len; i++) {
		tmp = GS_CATEGORY (g_ptr_array_index (category->children, i));
		if (g_strcmp0 (id, gs_category_get_id (tmp)) == 0)
			return tmp;
	}
	return NULL;
}

/**
 * gs_category_get_parent:
 * @category: a #GsCategory
 *
 * Gets the parent category.
 *
 * Returns: the #GsCategory or %NULL
 **/
GsCategory *
gs_category_get_parent (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return category->parent;
}

/**
 * gs_category_get_children:
 * @category: a #GsCategory
 *
 * Gets the list if children for a category.
 *
 * Return value: (element-type GsApp) (transfer none): A list of children
 **/
GPtrArray *
gs_category_get_children (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return category->children;
}

/**
 * gs_category_add_child:
 * @category: a #GsCategory
 * @subcategory: a #GsCategory
 *
 * Adds a child category to a parent category.
 **/
void
gs_category_add_child (GsCategory *category, GsCategory *subcategory)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	g_return_if_fail (GS_IS_CATEGORY (subcategory));

	/* FIXME: do we need this? */
	subcategory->parent = category;
	g_object_add_weak_pointer (G_OBJECT (subcategory->parent),
				   (gpointer *) &subcategory->parent);

	g_ptr_array_add (category->children,
			 g_object_ref (subcategory));
}

/**
 * gs_category_get_sort_key:
 **/
static gchar *
gs_category_get_sort_key (GsCategory *category)
{
	guint sort_order = 5;
	if (g_strcmp0 (gs_category_get_id (category), "featured") == 0)
		sort_order = 0;
	else if (g_strcmp0 (gs_category_get_id (category), "all") == 0)
		sort_order = 2;
	else if (g_strcmp0 (gs_category_get_id (category), "other") == 0)
		sort_order = 9;
	return g_strdup_printf ("%i:%s",
				sort_order,
				gs_category_get_name (category));
}

/**
 * gs_category_sort_children_cb:
 **/
static gint
gs_category_sort_children_cb (gconstpointer a, gconstpointer b)
{
	GsCategory *ca = GS_CATEGORY (*(GsCategory **) a);
	GsCategory *cb = GS_CATEGORY (*(GsCategory **) b);
	g_autofree gchar *id_a = gs_category_get_sort_key (ca);
	g_autofree gchar *id_b = gs_category_get_sort_key (cb);
	return g_strcmp0 (id_a, id_b);
}

/**
 * gs_category_sort_children:
 * @category: a #GsCategory
 *
 * Sorts the list of children.
 **/
void
gs_category_sort_children (GsCategory *category)
{
	g_ptr_array_sort (category->children,
			  gs_category_sort_children_cb);
}

static void
gs_category_finalize (GObject *object)
{
	GsCategory *category = GS_CATEGORY (object);

	if (category->parent != NULL)
		g_object_remove_weak_pointer (G_OBJECT (category->parent),
		                              (gpointer *) &category->parent);
	g_ptr_array_unref (category->children);
	g_ptr_array_unref (category->key_colors);
	g_free (category->id);
	g_free (category->name);
	g_free (category->icon);

	G_OBJECT_CLASS (gs_category_parent_class)->finalize (object);
}

static void
gs_category_class_init (GsCategoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_category_finalize;
}

static void
gs_category_init (GsCategory *category)
{
	category->children = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	category->key_colors = g_ptr_array_new_with_free_func ((GDestroyNotify) gdk_rgba_free);
}

/**
 * gs_category_new:
 * @id: an ID, e.g. "all"
 * @name: a localised name
 *
 * Creates a new category object.
 *
 * Returns: the new #GsCategory
 **/
GsCategory *
gs_category_new (const gchar *id)
{
	GsCategory *category;
	category = g_object_new (GS_TYPE_CATEGORY, NULL);
	category->id = g_strdup (id);
	return GS_CATEGORY (category);
}

/* vim: set noexpandtab: */
