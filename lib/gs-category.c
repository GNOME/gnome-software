/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
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
	gint		 score;
	GPtrArray	*key_colors;
	GPtrArray	*desktop_groups;
	GsCategory	*parent;
	guint		 size;
	GPtrArray	*children;
};

G_DEFINE_TYPE (GsCategory, gs_category, G_TYPE_OBJECT)

/**
 * gs_category_to_string:
 * @category: a #GsCategory
 *
 * Returns a string representation of the category
 *
 * Returns: a string
 *
 * Since: 3.22
 **/
gchar *
gs_category_to_string (GsCategory *category)
{
	guint i;
	GString *str = g_string_new (NULL);
	g_string_append_printf (str, "GsCategory[%p]:\n", category);
	g_string_append_printf (str, "  id: %s\n",
				category->id);
	if (category->name != NULL) {
		g_string_append_printf (str, "  name: %s\n",
					category->name);
	}
	if (category->icon != NULL) {
		g_string_append_printf (str, "  icon: %s\n",
					category->icon);
	}
	g_string_append_printf (str, "  size: %u\n",
				category->size);
	g_string_append_printf (str, "  key-colors: %u\n",
				category->key_colors->len);
	g_string_append_printf (str, "  desktop-groups: %u\n",
				category->desktop_groups->len);
	if (category->parent != NULL) {
		g_string_append_printf (str, "  parent: %s\n",
					gs_category_get_id (category->parent));
	}
	g_string_append_printf (str, "  score: %i\n", category->score);
	if (category->children->len == 0) {
		g_string_append_printf (str, "  children: %u\n",
					category->children->len);
	} else {
		g_string_append (str, "  children:\n");
		for (i = 0; i < category->children->len; i++) {
			GsCategory *child = g_ptr_array_index (category->children, i);
			g_string_append_printf (str, "  - %s\n",
						gs_category_get_id (child));
		}
	}
	return g_string_free (str, FALSE);
}

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
 *
 * Since: 3.22
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
 *
 * Since: 3.22
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
 *
 * Since: 3.22
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
 *
 * Since: 3.22
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
 *
 * Since: 3.22
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
 *
 * Since: 3.22
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
 *
 * Since: 3.22
 **/
const gchar *
gs_category_get_icon (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);

	/* special case */
	if (g_strcmp0 (category->id, "other") == 0)
		return "emblem-system-symbolic";
	if (g_strcmp0 (category->id, "all") == 0)
		return "emblem-default-symbolic";
	if (g_strcmp0 (category->id, "featured") == 0)
		return "emblem-favorite-symbolic";

	return category->icon;
}

/**
 * gs_category_set_icon:
 * @category: a #GsCategory
 * @icon: a category icon, or %NULL
 *
 * Sets the category icon.
 *
 * Since: 3.22
 **/
void
gs_category_set_icon (GsCategory *category, const gchar *icon)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	g_free (category->icon);
	category->icon = g_strdup (icon);
}

/**
 * gs_category_get_score:
 * @category: a #GsCategory
 *
 * Gets if the category score.
 * Important categories may be shown before other categories, or tagged in a
 * different way, for example with color or in a different section.
 *
 * Returns: the string, or %NULL
 *
 * Since: 3.22
 **/
gint
gs_category_get_score (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), FALSE);
	return category->score;
}

/**
 * gs_category_set_score:
 * @category: a #GsCategory
 * @score: a category score, or %NULL
 *
 * Sets the category score, where larger numbers get sorted before lower
 * numbers.
 *
 * Since: 3.22
 **/
void
gs_category_set_score (GsCategory *category, gint score)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	category->score = score;
}

/**
 * gs_category_get_key_colors:
 * @category: a #GsCategory
 *
 * Gets the list of key colors for the category.
 *
 * Returns: (element-type GdkRGBA) (transfer none): An array
 *
 * Since: 3.22
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
 * Adds a key color to the category.
 *
 * Since: 3.22
 **/
void
gs_category_add_key_color (GsCategory *category, const GdkRGBA *key_color)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	g_return_if_fail (key_color != NULL);
	g_ptr_array_add (category->key_colors, gdk_rgba_copy (key_color));
}

/**
 * gs_category_get_desktop_groups:
 * @category: a #GsCategory
 *
 * Gets the list of AppStream groups for the category.
 *
 * Returns: (element-type utf8) (transfer none): An array
 *
 * Since: 3.22
 **/
GPtrArray *
gs_category_get_desktop_groups (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return category->desktop_groups;
}

/**
 * gs_category_has_desktop_group:
 * @category: a #GsCategory
 * @desktop_group: a group of categories found in AppStream, e.g. "AudioVisual::Player"
 *
 * Finds out if the category has the specific AppStream desktop group.
 *
 * Returns: %TRUE if found, %FALSE otherwise
 *
 * Since: 3.22
 **/
gboolean
gs_category_has_desktop_group (GsCategory *category, const gchar *desktop_group)
{
	guint i;

	g_return_val_if_fail (GS_IS_CATEGORY (category), FALSE);
	g_return_val_if_fail (desktop_group != NULL, FALSE);

	for (i = 0; i < category->desktop_groups->len; i++) {
		const gchar *tmp = g_ptr_array_index (category->desktop_groups, i);
		if (g_strcmp0 (tmp, desktop_group) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * gs_category_add_desktop_group:
 * @category: a #GsCategory
 * @desktop_group: a group of categories found in AppStream, e.g. "AudioVisual::Player"
 *
 * Adds a desktop group to the category.
 * A desktop group is a set of category strings that all must exist.
 *
 * Since: 3.22
 **/
void
gs_category_add_desktop_group (GsCategory *category, const gchar *desktop_group)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	g_return_if_fail (desktop_group != NULL);

	/* add if not already found */
	if (gs_category_has_desktop_group (category, desktop_group))
		return;
	g_ptr_array_add (category->desktop_groups, g_strdup (desktop_group));
}

/**
 * gs_category_find_child:
 * @category: a #GsCategory
 * @id: a category ID, e.g. "other"
 *
 * Find a child category with a specific ID.
 *
 * Returns: (transfer none): the #GsCategory, or %NULL
 *
 * Since: 3.22
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
 *
 * Since: 3.22
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
 *
 * Since: 3.22
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
 *
 * Since: 3.22
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
	return g_strdup_printf ("%u:%s",
				sort_order,
				gs_category_get_name (category));
}

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
 *
 * Since: 3.22
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
	g_ptr_array_unref (category->desktop_groups);
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
	category->desktop_groups = g_ptr_array_new_with_free_func (g_free);
}

/**
 * gs_category_new:
 * @id: an ID, e.g. "all"
 *
 * Creates a new category object.
 *
 * Returns: the new #GsCategory
 *
 * Since: 3.22
 **/
GsCategory *
gs_category_new (const gchar *id)
{
	GsCategory *category;
	category = g_object_new (GS_TYPE_CATEGORY, NULL);
	category->id = g_strdup (id);
	return GS_CATEGORY (category);
}
