/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "gs-appstream-item.h"

struct GsAppstreamItem
{
	gchar			*id;
	gchar			*pkgname;
	gchar			*name;
	gchar			*summary;
	gchar			*url;
	gchar			*description;
	gchar			*icon;
	GsAppstreamItemIconKind	 icon_kind;
	GPtrArray		*appcategories;
};

/**
 * gs_appstream_item_free:
 */
void
gs_appstream_item_free (GsAppstreamItem *item)
{
	g_free (item->id);
	g_free (item->pkgname);
	g_free (item->name);
	g_free (item->summary);
	g_free (item->url);
	g_free (item->description);
	g_free (item->icon);
	g_ptr_array_unref (item->appcategories);
	g_slice_free (GsAppstreamItem, item);
}

/**
 * gs_appstream_item_new:
 */
GsAppstreamItem *
gs_appstream_item_new (void)
{
	GsAppstreamItem *item;
	item = g_slice_new0 (GsAppstreamItem);
	item->appcategories = g_ptr_array_new_with_free_func (g_free);
	item->icon_kind = GS_APPSTREAM_ITEM_ICON_KIND_UNKNOWN;
	return item;
}

/**
 * gs_appstream_item_get_id:
 */
const gchar *
gs_appstream_item_get_id (GsAppstreamItem *item)
{
	return item->id;
}

/**
 * gs_appstream_item_get_pkgname:
 */
const gchar *
gs_appstream_item_get_pkgname (GsAppstreamItem *item)
{
	return item->pkgname;
}

/**
 * gs_appstream_item_get_name:
 */
const gchar *
gs_appstream_item_get_name (GsAppstreamItem *item)
{
	return item->name;
}

/**
 * gs_appstream_item_get_summary:
 */
const gchar *
gs_appstream_item_get_summary (GsAppstreamItem *item)
{
	return item->summary;
}

/**
 * gs_appstream_item_get_url:
 */
const gchar *
gs_appstream_item_get_url (GsAppstreamItem *item)
{
	return item->url;
}

/**
 * gs_appstream_item_get_description:
 */
const gchar *
gs_appstream_item_get_description (GsAppstreamItem *item)
{
	return item->description;
}

/**
 * gs_appstream_item_get_icon:
 */
const gchar *
gs_appstream_item_get_icon (GsAppstreamItem *item)
{
	return item->icon;
}

/**
 * gs_appstream_item_get_icon_kind:
 */
GsAppstreamItemIconKind 
gs_appstream_item_get_icon_kind (GsAppstreamItem *item)
{
	return item->icon_kind;
}

/**
 * gs_appstream_item_has_category:
 */
gboolean 
gs_appstream_item_has_category (GsAppstreamItem *item,
			        const gchar *category)
{
	const gchar *tmp;
	guint i;

	for (i = 0; i < item->appcategories->len; i++) {
		tmp = g_ptr_array_index (item->appcategories, i);
		if (g_strcmp0 (tmp, category) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * gs_appstream_item_set_id:
 */
void 
gs_appstream_item_set_id (GsAppstreamItem *item,
			  const gchar *id,
			  gsize length)
{
	item->id = g_strndup (id, length);
}

/**
 * gs_appstream_item_set_pkgname:
 */
void 
gs_appstream_item_set_pkgname (GsAppstreamItem *item,
			       const gchar *pkgname,
			       gsize length)
{
	item->pkgname = g_strndup (pkgname, length);
}

/**
 * gs_appstream_item_set_name:
 */
void 
gs_appstream_item_set_name (GsAppstreamItem *item,
			    const gchar *name,
			    gsize length)
{
	item->name = g_strndup (name, length);
}

/**
 * gs_appstream_item_set_summary:
 */
void 
gs_appstream_item_set_summary (GsAppstreamItem *item,
			       const gchar *summary,
			       gsize length)
{
	item->summary = g_strndup (summary, length);
}

/**
 * gs_appstream_item_set_url:
 */
void 
gs_appstream_item_set_url (GsAppstreamItem *item,
			   const gchar *url,
			   gsize length)
{
	item->url = g_strndup (url, length);
}

/**
 * gs_appstream_item_set_description:
 */
void 
gs_appstream_item_set_description (GsAppstreamItem *item,
				   const gchar *description,
				   gsize length)
{
	item->description = g_strndup (description, length);
}

/**
 * gs_appstream_item_set_icon:
 */
void 
gs_appstream_item_set_icon (GsAppstreamItem *item,
			    const gchar *icon,
			    gsize length)
{
	item->icon = g_strndup (icon, length);
}

/**
 * gs_appstream_item_add_category:
 */
void 
gs_appstream_item_add_category (GsAppstreamItem *item,
				const gchar *category,
				gsize length)
{
	g_ptr_array_add (item->appcategories,
			 g_strndup (category, length));
}

/**
 * gs_appstream_item_set_icon_kind:
 */
void 
gs_appstream_item_set_icon_kind (GsAppstreamItem *item,
				 GsAppstreamItemIconKind icon_kind)
{
	item->icon_kind = icon_kind;
}
