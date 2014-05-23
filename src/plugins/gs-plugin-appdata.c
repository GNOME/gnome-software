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

#include <appstream-glib.h>

struct GsPluginPrivate {
	gsize			 done_init;
	GHashTable		*hash;		/* of "id" : "filename" */
};


/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "appdata";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal,
						    g_free, g_free);
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",		/* faster than parsing the local file */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_hash_table_unref (plugin->priv->hash);
}

/**
 * gs_plugin_add_datadir:
 */
static gboolean
gs_plugin_add_datadir (GsPlugin *plugin, const gchar *datadir, GError **error)
{
	GDir *dir = NULL;
	GError *error_local = NULL;
	const gchar *tmp;
	gboolean ret = TRUE;
	gchar *cachedir;
	gchar *ext_tmp;
	gchar *id;

	/* find all the files installed */
	cachedir = g_build_filename (datadir, "appdata", NULL);
	if (!g_file_test (cachedir, G_FILE_TEST_EXISTS))
		goto out;
	dir = g_dir_open (cachedir, 0, &error_local);
	if (dir == NULL) {
		g_debug ("Could not open AppData directory: %s",
			 error_local->message);
		g_error_free (error_local);
		goto out;
	}
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		if (g_strcmp0 (tmp, "schema") == 0)
			continue;
		if (!g_str_has_suffix (tmp, ".appdata.xml")) {
			g_warning ("AppData: not a data file: %s/%s",
				   cachedir, tmp);
			continue;
		}
		id = g_strdup (tmp);
		ext_tmp = g_strstr_len (id, -1, ".appdata.xml");
		if (ext_tmp != NULL)
			*ext_tmp = '\0';
		g_hash_table_insert (plugin->priv->hash, id,
				     g_build_filename (cachedir, tmp, NULL));
	}
out:
	g_free (cachedir);
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GError **error)
{
	const gchar * const *dirs;
	gboolean ret = TRUE;
	guint i;

	/* add all possible locations */
	dirs = g_get_system_data_dirs ();
	for (i = 0; dirs[i] != NULL; i++) {
		ret = gs_plugin_add_datadir (plugin, dirs[i], error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * gs_plugin_refine_by_local_appdata:
 */
static gboolean
gs_plugin_refine_by_local_appdata (GsApp *app,
				   const gchar *filename,
				   GError **error)
{
	AsApp *appdata;
	GPtrArray *screenshots;
	const gchar *tmp;
	gboolean ret;
	gchar *desc = NULL;

	/* parse */
	appdata = as_app_new ();
	ret = as_app_parse_file (appdata,
				 filename,
				 AS_APP_PARSE_FLAG_USE_HEURISTICS,
				 error);
	if (!ret)
		goto out;

	/* <name> */
	tmp = as_app_get_name (appdata, NULL);
	if (tmp != NULL)
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, tmp);

	/* <summary> */
	tmp = as_app_get_comment (appdata, NULL);
	if (tmp != NULL)
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, tmp);

	/* <screenshots> */
	screenshots = as_app_get_screenshots (appdata);
	if (screenshots->len > 0)
		gs_app_add_kudo (app, GS_APP_KUDO_HAS_SCREENSHOTS);

	/* <url> */
	tmp = as_app_get_url_item (appdata, AS_URL_KIND_HOMEPAGE);
	if (tmp != NULL && gs_app_get_url (app, GS_APP_URL_KIND_HOMEPAGE) == NULL)
		gs_app_set_url (app, GS_APP_URL_KIND_HOMEPAGE, tmp);

	/* <project_group> */
	tmp = as_app_get_project_group (appdata);
	if (tmp != NULL && gs_app_get_project_group (app) == NULL)
		gs_app_set_project_group (app, tmp);

	/* <description> */
	tmp = as_app_get_description (appdata, NULL);
	if (tmp != NULL) {
		desc = as_markup_convert_simple (tmp, -1, error);
		if (desc == NULL) {
			ret = FALSE;
			goto out;
		}
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, desc);
	}
out:
	g_free (desc);
	g_object_unref (appdata);
	return ret;
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
	GList *l;
	GsApp *app;
	const gchar *id;
	const gchar *tmp;
	gboolean ret = TRUE;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		id = gs_app_get_id (app);
		if (id == NULL)
			continue;
		tmp = g_hash_table_lookup (plugin->priv->hash, id);
		if (tmp != NULL) {
			g_debug ("AppData: refine %s with %s", id, tmp);
			ret = gs_plugin_refine_by_local_appdata (app, tmp, error);
			if (!ret)
				goto out;
		}
	}
out:
	return ret;
}
