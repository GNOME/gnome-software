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

#include <config.h>

#include <gs-plugin.h>

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "datadir-filename-local";
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"datadir-filename",	/* prefer apps installed system-wide over local */
		NULL };
	return deps;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList **list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *tmp;
	GList *l;
	GsApp *app;

	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_state (app) != GS_APP_STATE_UNKNOWN)
			continue;
		tmp = gs_app_get_metadata_item (app, "DataDir::desktop-filename");
		if (tmp == NULL)
			continue;
		if (!g_file_test (tmp, G_FILE_TEST_EXISTS))
			continue;
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		if (gs_app_get_kind (app) == GS_APP_KIND_UNKNOWN)
			gs_app_set_kind (app, GS_APP_KIND_NORMAL);
	}
	return TRUE;
}
