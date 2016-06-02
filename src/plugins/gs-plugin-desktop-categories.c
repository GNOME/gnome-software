/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2016 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <gnome-software.h>
#include <glib/gi18n.h>

#include "gs-desktop-common.h"

/*
 * SECTION:
 * Adds categories from a hardcoded list based on the the desktop menu
 * specification.
 */

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* need categories */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "appstream");
}

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	const GsDesktopData *msdata;
	gchar msgctxt[100];
	guint i, j, k;

	msdata = gs_desktop_get_data ();
	for (i = 0; msdata[i].id != NULL; i++) {
		GdkRGBA key_color;
		GsCategory *category;

		/* add parent category */
		category = gs_category_new (msdata[i].id);
		gs_category_set_icon (category, msdata[i].icon);
		gs_category_set_name (category, gettext (msdata[i].name));
		gs_category_set_important (category, msdata[i].important);
		if (gdk_rgba_parse (&key_color, msdata[i].key_colors))
			gs_category_add_key_color (category, &key_color);
		g_ptr_array_add (list, category);
		g_snprintf (msgctxt, sizeof(msgctxt),
			    "Menu subcategory of %s", msdata[i].name);

		/* add subcategories */
		for (j = 0; msdata[i].mapping[j].id != NULL; j++) {
			const GsDesktopMap *map = &msdata[i].mapping[j];
			g_autoptr(GsCategory) sub = gs_category_new (map->id);
			for (k = 0; map->fdo_cats[k] != NULL; k++)
				gs_category_add_desktop_group (sub, map->fdo_cats[k]);
			gs_category_set_name (sub, g_dpgettext2 (GETTEXT_PACKAGE,
								 msgctxt,
								 map->name));
			gs_category_add_child (category, sub);
		}
	}
	return TRUE;
}

/* most of this time this won't be required, unless the user creates a
 * GsCategory manually and uses it to get results, for instance in the
 * overview page or `gnome-software-cmd get-category-apps games/featured` */
gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GPtrArray *desktop_groups;
	GsCategory *parent;
	const GsDesktopData *msdata;
	guint i, j, k;

	/* already set */
	desktop_groups = gs_category_get_desktop_groups (category);
	if (desktop_groups->len > 0)
		return TRUE;

	/* not valid */
	parent = gs_category_get_parent (category);
	if (parent == NULL)
		return TRUE;

	/* find desktop_groups for a parent::child category */
	msdata = gs_desktop_get_data ();
	for (i = 0; msdata[i].id != NULL; i++) {
		if (g_strcmp0 (gs_category_get_id (parent), msdata[i].id) != 0)
			continue;
		for (j = 0; msdata[i].mapping[j].id != NULL; j++) {
			const GsDesktopMap *map = &msdata[i].mapping[j];
			if (g_strcmp0 (gs_category_get_id (category), map->id) != 0)
				continue;
			for (k = 0; map->fdo_cats[k] != NULL; k++)
				gs_category_add_desktop_group (category, map->fdo_cats[k]);
		}
	}
	return TRUE;
}
