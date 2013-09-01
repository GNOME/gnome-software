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

struct GsPluginPrivate {
	GHashTable		*cache;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "datadir-filename";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->cache = g_hash_table_new_full (g_str_hash,
						     g_str_equal,
						     g_free,
						     g_free);
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 1.1f;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_hash_table_unref (plugin->priv->cache);
}

/**
 * gs_plugin_datadir_filename_find:
 */
static const gchar *
gs_plugin_datadir_filename_find (GsPlugin *plugin,
				 GsApp *app)
{
	const gchar *id;
	const gchar *path_tmp = NULL;
	gboolean ret;
	gchar *path = NULL;

	/* try and get from cache */
	id = gs_app_get_id (app);
	if (id == NULL)
		goto out;
	ret = g_hash_table_lookup_extended (plugin->priv->cache,
					    id,
					    NULL,
					    (gpointer *) &path_tmp);
	if (ret) {
		g_debug ("found existing %s", id);
		goto out;
	}

	/* find if the file exists */
	path = g_strdup_printf ("/usr/share/applications/%s.desktop",
				gs_app_get_id (app));
	if (g_file_test (path, G_FILE_TEST_EXISTS)) {
		path_tmp = g_strdup (path);
		g_hash_table_insert (plugin->priv->cache,
				     g_strdup (id),
				     (gpointer) path_tmp);
	} else {
		/* add an empty key to the cache to avoid stat'ing again */
		g_hash_table_insert (plugin->priv->cache,
				     g_strdup (id),
				     NULL);
	}
out:
	g_free (path);
	return path_tmp;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *tmp;
	GList *l;
	GsApp *app;

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_name (app) != NULL)
			continue;
		tmp = gs_app_get_metadata_item (app, "datadir-desktop-filename");
		if (tmp != NULL)
			continue;
		tmp = gs_plugin_datadir_filename_find (plugin, app);
		if (tmp != NULL) {
			gs_app_set_metadata (app,
					     "datadir-desktop-filename",
					     tmp);
		}
	}
	return TRUE;
}
