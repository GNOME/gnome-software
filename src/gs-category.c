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

static void	gs_category_finalize	(GObject	*object);

#define GS_CATEGORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_CATEGORY, GsCategoryPrivate))

struct GsCategoryPrivate
{
	gchar		*id;
	gchar		*name;
	GsCategory	*parent;
	GList		*subcategories;
};

G_DEFINE_TYPE (GsCategory, gs_category, G_TYPE_OBJECT)

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

void
gs_category_set_name (GsCategory *category, const gchar *name)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	g_free (category->priv->name);
	category->priv->name = g_strdup (name);
}

GsCategory *
gs_category_get_parent (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return category->priv->parent;
}

GList *
gs_category_get_subcategories (GsCategory *category)
{
	g_return_val_if_fail (GS_IS_CATEGORY (category), NULL);
	return g_list_copy (category->priv->subcategories);
}

void
gs_category_add_subcategory (GsCategory *category, GsCategory *subcategory)
{
	g_return_if_fail (GS_IS_CATEGORY (category));
	category->priv->subcategories = g_list_prepend (category->priv->subcategories, g_object_ref (subcategory));
}

/**
 * gs_category_sort_subcategories_cb:
 **/
static gint
gs_category_sort_subcategories_cb (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (gs_category_get_name (GS_CATEGORY (a)),
			  gs_category_get_name (GS_CATEGORY (b)));
}

/**
 * gs_category_sort_subcategories:
 **/
void
gs_category_sort_subcategories (GsCategory *category)
{
	gboolean subcat_all = FALSE;
	GList *l;
	GsCategory *all;
	GsCategoryPrivate *priv = category->priv;

	/* nothing here */
	if (priv->subcategories == NULL)
		return;

	/* ensure there is a general entry */
	for (l = priv->subcategories; l != NULL; l = l->next) {
		if (gs_category_get_id (GS_CATEGORY (l->data)) == NULL) {
			subcat_all = TRUE;
			break;
		}
	}
	if (!subcat_all) {
		/* TRANSLATORS: this is where all applications that don't
		 * fit in other groups are put */
		all = gs_category_new (category, NULL, _("General"));
		gs_category_add_subcategory (category, all);
	}

	/* actually sort the data */
	priv->subcategories = g_list_sort (priv->subcategories,
					   gs_category_sort_subcategories_cb);
}

static void
gs_category_class_init (GsCategoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_category_finalize;
	g_type_class_add_private (klass, sizeof (GsCategoryPrivate));
}

static void
gs_category_init (GsCategory *category)
{
	category->priv = GS_CATEGORY_GET_PRIVATE (category);
}

static void
gs_category_finalize (GObject *object)
{
	GsCategory *category = GS_CATEGORY (object);
	GsCategoryPrivate *priv = category->priv;

	g_free (priv->id);
	g_free (priv->name);
	g_list_free_full (priv->subcategories, g_object_unref);

	G_OBJECT_CLASS (gs_category_parent_class)->finalize (object);
}

GsCategory *
gs_category_new (GsCategory *parent, const gchar *id, const gchar *name)
{
	GsCategory *category;
	category = g_object_new (GS_TYPE_CATEGORY, NULL);
	category->priv->parent = parent;
	category->priv->id = g_strdup (id);
	category->priv->name = g_strdup (name);
	return GS_CATEGORY (category);
}

/* vim: set noexpandtab: */
