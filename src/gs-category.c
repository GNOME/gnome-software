/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <glib/gi18n.h>

#include "gs-category.h"

struct GsCategoryPrivate
{
	gchar		*id;
	gchar		*name;
	GsCategory	*parent;
	guint		 size;
	GList		*subcategories;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsCategory, gs_category, G_TYPE_OBJECT)

/**
 * gs_category_get_size:
 *
 * Returns how many applications the category could contain.
 *
 * NOTE: This may over-estimate the number if duplicate applications are
 * filtered or core applications are not shown.
 **/
guint
gs_category_get_size (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), 0);
	return category->priv->size;
}

/**
 * gs_category_set_size:
 **/
void
gs_category_set_size (GsCategory *category, guint size)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	category->priv->size = size;
}

/**
 * gs_category_increment_size:
 *
 * Adds one to the size count if an application is available
 **/
void
gs_category_increment_size (GsCategory *category)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	category->priv->size++;
}

const gchar *
gs_category_get_id (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return category->priv->id;
}

const gchar *
gs_category_get_name (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return category->priv->name;
}

GsCategory *
gs_category_find_child (GsCategory *category, const gchar *id)
{
	GList *l;
	GsCategoryPrivate *priv = category->priv;
	GsCategory *tmp;

	/* find the subcategory */
	if (priv->subcategories == NULL)
		return NULL;
	for (l = priv->subcategories; l != NULL; l = l->next) {
		tmp = GS_CATEGORY (l->data);
		if (g_strcmp0 (id, gs_category_get_id (tmp)) == 0)
			return tmp;
	}
	return NULL;
}

GsCategory *
gs_category_get_parent (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return category->priv->parent;
}

/**
 * gs_category_get_subcategories:
 *
 * Return value: (element-type GsApp) (transfer container): A list of subcategories
 **/
GList *
gs_category_get_subcategories (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return g_list_copy (category->priv->subcategories);
}

/**
 * gs_category_add_subcategory:
 **/
void
gs_category_add_subcategory (GsCategory *category, GsCategory *subcategory)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	category->priv->subcategories = g_list_prepend (category->priv->subcategories,
							g_object_ref (subcategory));
}

/**
 * gs_category_sort_subcategories_cb:
 **/
static gint
gs_category_sort_subcategories_cb (gconstpointer a, gconstpointer b)
{
	GsCategory *ca = GS_CATEGORY (a);
	GsCategory *cb = GS_CATEGORY (b);
	const gchar *id_a = gs_category_get_id (ca);
	const gchar *id_b = gs_category_get_id (cb);

	if (g_strcmp0 (id_a, "other") == 0)
		return 1;
	else if (g_strcmp0 (id_a, "featured") == 0)
		return -1;

	if (g_strcmp0 (id_b, "other") == 0)
		return -1;
	else if (g_strcmp0 (id_b, "featured") == 0)
		return 1;

	return g_strcmp0 (gs_category_get_name (ca), gs_category_get_name (cb));
}

/**
 * gs_category_sort_subcategories:
 **/
void
gs_category_sort_subcategories (GsCategory *category)
{
	GsCategoryPrivate *priv = category->priv;

	/* nothing here */
	if (priv->subcategories == NULL)
		return;

	/* actually sort the data */
	priv->subcategories = g_list_sort (priv->subcategories,
					   gs_category_sort_subcategories_cb);
}

static void
gs_category_dispose (GObject *object)
{
	GsCategory *category = GS_CATEGORY (object);
	GsCategoryPrivate *priv = category->priv;

	if (priv->subcategories != NULL) {
		g_list_free_full (priv->subcategories, g_object_unref);
		priv->subcategories = NULL;
	}

	G_OBJECT_CLASS (gs_category_parent_class)->dispose (object);
}

static void
gs_category_finalize (GObject *object)
{
	GsCategory *category = GS_CATEGORY (object);
	GsCategoryPrivate *priv = category->priv;

	if (priv->parent != NULL)
		g_object_remove_weak_pointer (G_OBJECT (priv->parent),
		                              (gpointer *) &priv->parent);
	g_free (priv->id);
	g_free (priv->name);

	G_OBJECT_CLASS (gs_category_parent_class)->finalize (object);
}

static void
gs_category_class_init (GsCategoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_category_dispose;
	object_class->finalize = gs_category_finalize;
}

static void
gs_category_init (GsCategory *category)
{
	category->priv = gs_category_get_instance_private (category);
}

GsCategory *
gs_category_new (GsCategory *parent, const gchar *id, const gchar *name)
{
	GsCategory *category;

	/* special case, we don't want translations in the plugins */
	if (g_strcmp0 (id, "other") == 0) {
		/* TRANSLATORS: this is where all applications that don't
		 * fit in other groups are put */
		name =_("Other");
	}

	category = g_object_new (GS_TYPE_CATEGORY, NULL);
	category->priv->parent = parent;
	if (category->priv->parent != NULL)
		g_object_add_weak_pointer (G_OBJECT (category->priv->parent),
		                           (gpointer *) &category->priv->parent);
	category->priv->id = g_strdup (id);
	category->priv->name = g_strdup (name);
	return GS_CATEGORY (category);
}

/* vim: set noexpandtab: */
