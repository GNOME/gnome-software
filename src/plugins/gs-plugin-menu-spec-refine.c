/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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

#include "menu-spec-common.h"

/*
 * SECTION:
 * Sets the menu path of the applcation using the Freedesktop menu spec
 * previously set.
 */

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* need category and menu-spec data */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "menu-spec-categories");
}

static void
gs_plugin_refine_app_category (GsPlugin *plugin,
			       GsApp *app,
			       const MenuSpecData *cat)
{
	const gchar *menu_path[] = { NULL, NULL, NULL };
	const MenuSpecData *msdata;
	gboolean ret = FALSE;
	gchar *tmp;
	guint i;

	/* find a sub-level category the app has */
	msdata = menu_spec_get_data ();
	for (i = 0; msdata[i].path != NULL; i++) {
		tmp = g_strstr_len (msdata[i].path, -1, "::");
		if (tmp == NULL)
			continue;
		if (!g_str_has_prefix (msdata[i].path, cat->path))
			continue;
		ret = gs_app_has_category (app, tmp + 2);
		if (ret) {
			g_autofree gchar *msgctxt = NULL;
			msgctxt = g_strdup_printf ("Menu subcategory of %s", cat->name);
			menu_path[1] = g_dpgettext2 (GETTEXT_PACKAGE, msgctxt, msdata[i].name);
			break;
		}
	}

	/* the top-level category always exists */
	menu_path[0] = gettext (cat->name);
	gs_app_set_menu_path (app, (gchar **) menu_path);
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	const MenuSpecData *msdata;
	gboolean ret = FALSE;
	gchar *tmp;
	guint i;
	const gchar *EMPTY[] = { "", NULL };

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH) == 0)
		return TRUE;
	if (gs_app_get_menu_path (app) != NULL)
		return TRUE;

	/* find a top level category the app has */
	msdata = menu_spec_get_data ();
	for (i = 0; msdata[i].path != NULL; i++) {
		tmp = g_strstr_len (msdata[i].path, -1, "::");
		if (tmp != NULL)
			continue;
		ret = gs_app_has_category (app, msdata[i].path);
		if (ret) {
			gs_plugin_refine_app_category (plugin, app,
			                               &msdata[i]);
			break;
		}
	}

	/* don't keep searching for this */
	if (!ret)
		gs_app_set_menu_path (app, (gchar **) EMPTY);

	return TRUE;
}
