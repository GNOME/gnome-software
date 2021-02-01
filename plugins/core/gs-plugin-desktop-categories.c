/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2011-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <gnome-software.h>
#include <glib/gi18n.h>

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
