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
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

static gboolean
_gs_app_has_desktop_group (GsApp *app, const gchar *desktop_group)
{
	guint i;
	g_auto(GStrv) split = g_strsplit (desktop_group, "::", -1);
	for (i = 0; split[i] != NULL; i++) {
		if (!gs_app_has_category (app, split[i]))
			return FALSE;
	}
	return TRUE;
}

/* adds the menu-path for applications */
gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *strv[] = { "", NULL, NULL };
	const GsDesktopData *msdata;
	gboolean found = FALSE;
	guint i, j, k;

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH) == 0)
		return TRUE;
	if (gs_app_get_menu_path (app) != NULL)
		return TRUE;

	/* find a top level category the app has */
	msdata = gs_desktop_get_data ();
	for (i = 0; !found && msdata[i].id != NULL; i++) {
		const GsDesktopData *data = &msdata[i];
		for (j = 0; !found && data->mapping[j].id != NULL; j++) {
			const GsDesktopMap *map = &data->mapping[j];
			if (g_strcmp0 (map->id, "all") == 0)
				continue;
			if (g_strcmp0 (map->id, "featured") == 0)
				continue;
			for (k = 0; !found && map->fdo_cats[k] != NULL; k++) {
				const gchar *tmp = msdata[i].mapping[j].fdo_cats[k];
				if (_gs_app_has_desktop_group (app, tmp)) {
					strv[0] = msdata[i].name;
					strv[1] = msdata[i].mapping[j].name;
					found = TRUE;
					break;
				}
			}
		}
	}

	/* always set something to avoid keep searching for this */
	gs_app_set_menu_path (app, (gchar **) strv);
	return TRUE;
}
