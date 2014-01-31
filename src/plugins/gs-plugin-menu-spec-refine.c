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

#include <gs-plugin.h>
#include <glib/gi18n.h>

#include "menu-spec-common.h"

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "menu-spec-refine";
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",		/* need GsApp category data */
		"menu-spec-categories",	/* need menu-spec data */
		NULL };
	return deps;
}

/**
 * gs_plugin_refine_app_category:
 */
static gboolean
gs_plugin_refine_app_category (GsPlugin *plugin,
			       GsApp *app,
			       const MenuSpecData *cat)
{
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
			tmp = g_strdup_printf ("%s → %s",
					       gettext (cat->text),
					       gettext (msdata[i].text));
			gs_app_set_menu_path (app, tmp);
			g_free (tmp);
			break;
		}
	}
	return ret;
}

/**
 * gs_plugin_refine_app:
 */
static gboolean
gs_plugin_refine_app (GsPlugin *plugin, GsApp *app)
{
	const MenuSpecData *msdata;
	gboolean ret = FALSE;
	gchar *tmp;
	guint i;

	/* find a top level category the app has */
	msdata = menu_spec_get_data ();
	for (i = 0; msdata[i].path != NULL; i++) {
		tmp = g_strstr_len (msdata[i].path, -1, "::");
		if (tmp != NULL)
			continue;
		ret = gs_app_has_category (app, msdata[i].path);
		if (ret) {
			ret = gs_plugin_refine_app_category (plugin, app,
							     &msdata[i]);
			break;
		}
	}
	return ret;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	GList *l;
	GsApp *app;
	gboolean ret;

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_menu_path (app) == NULL) {
			ret = gs_plugin_refine_app (plugin, app);
			if (!ret) {
				/* don't keep searching for this */
				gs_app_set_menu_path (app, "");
			}
		}
	}
	return TRUE;
}
