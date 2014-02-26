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

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "hardcoded-featured";
}

/**
 * gs_plugin_add_featured_app:
 */
static gboolean
gs_plugin_add_featured_app (GList **list,
			    GKeyFile *kf,
			    const gchar *id,
			    GError **error)
{
	GsApp *app = NULL;
	gboolean ret = TRUE;
	gchar *background = NULL;
	gchar *stroke_color = NULL;
	gchar *text_color = NULL;
	gchar *text_shadow = NULL;

	background = g_key_file_get_string (kf, id, "background", error);
	if (background == NULL) {
		ret = FALSE;
		goto out;
	}
	stroke_color = g_key_file_get_string (kf, id, "stroke", error);
	if (stroke_color == NULL) {
		ret = FALSE;
		goto out;
	}
	text_color = g_key_file_get_string (kf, id, "text", error);
	if (text_color == NULL) {
		ret = FALSE;
		goto out;
	}

	/* optional */
	text_shadow = g_key_file_get_string (kf, id, "text-shadow", NULL);

	/* add app */
	app = gs_app_new (id);
	gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
	gs_app_set_metadata (app, "Featured::background", background);
	gs_app_set_metadata (app, "Featured::stroke-color", stroke_color);
	gs_app_set_metadata (app, "Featured::text-color", text_color);
	if (text_shadow != NULL)
		gs_app_set_metadata (app, "Featured::text-shadow", text_shadow);
	gs_plugin_add_app (list, app);
out:
	if (app != NULL)
		g_object_unref (app);
	g_free (background);
	g_free (stroke_color);
	g_free (text_color);
	g_free (text_shadow);
	return ret;
}

/**
 * gs_plugin_add_featured:
 */
gboolean
gs_plugin_add_featured (GsPlugin *plugin,
			GList **list,
			GCancellable *cancellable,
			GError **error)
{
	GKeyFile *kf;
	gboolean ret = TRUE;
	gchar **apps = NULL;
	gchar *path;
	guint i;

	path = g_build_filename (DATADIR, "gnome-software", "featured.ini", NULL);
	kf = g_key_file_new ();
	ret = g_key_file_load_from_file (kf, path, 0, error);
	if (!ret)
		goto out;
	apps = g_key_file_get_groups (kf, NULL);
	for (i = 0; apps[i]; i++) {
		ret = gs_plugin_add_featured_app (list, kf, apps[i], error);
		if (!ret)
			goto out;
	}
out:
	if (kf != NULL)
		g_key_file_unref (kf);
	g_free (path);
	g_strfreev (apps);
	return ret;
}
