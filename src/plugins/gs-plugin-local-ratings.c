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

#include <sqlite3.h>
#include <stdlib.h>

#include <gs-plugin.h>
#include <gs-utils.h>

struct GsPluginPrivate {
	gsize                    loaded;
	gchar			*db_path;
	sqlite3			*db;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "local-ratings";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->db_path = g_build_filename (g_get_user_data_dir (),
						  "gnome-software",
						  "hardcoded-ratings.db",
						  NULL);
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",	/* requires the app_id from the pkgname */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_free (plugin->priv->db_path);
	sqlite3_close (plugin->priv->db);
}

/**
 * gs_plugin_local_ratings_load_db:
 */
static gboolean
gs_plugin_local_ratings_load_db (GsPlugin *plugin,
				 GError **error)
{
	const gchar *statement;
	gchar *error_msg = NULL;
	gint rc;

	g_debug ("trying to open database '%s'", plugin->priv->db_path);
	if (!gs_mkdir_parent (plugin->priv->db_path, error))
		return FALSE;
	rc = sqlite3_open (plugin->priv->db_path, &plugin->priv->db);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Can't open transaction database: %s",
			     sqlite3_errmsg (plugin->priv->db));
		return FALSE;
	}

	/* we don't need to keep doing fsync */
	sqlite3_exec (plugin->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);

	/* create table if required */
	rc = sqlite3_exec (plugin->priv->db, "SELECT * FROM ratings LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("creating table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "CREATE TABLE ratings ("
			    "app_id TEXT PRIMARY KEY,"
			    "rating INTEGER DEFAULT 0);";
		sqlite3_exec (plugin->priv->db, statement, NULL, NULL, NULL);
	}
	return TRUE;
}

/**
 * gs_plugin_local_ratings_sqlite_cb:
 **/
static gint
gs_plugin_local_ratings_sqlite_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint *rating = (gint *) data;
	*rating = atoi (argv[0]);
	return 0;
}

/**
 * gs_plugin_local_find_app:
 */
static gint
gs_plugin_local_find_app (GsPlugin *plugin, const gchar *app_id)
{
	gint rating = -1;
	g_autofree gchar *statement = NULL;

	statement = g_strdup_printf ("SELECT rating FROM ratings WHERE app_id = '%s'", app_id);
	sqlite3_exec (plugin->priv->db,
		      statement,
		      gs_plugin_local_ratings_sqlite_cb,
		      &rating,
		      NULL);
	return rating;
}

/**
 * gs_plugin_app_set_rating:
 */
gboolean
gs_plugin_app_set_rating (GsPlugin *plugin,
			  GsApp *app,
			  GCancellable *cancellable,
			  GError **error)
{
	char *error_msg = NULL;
	gboolean ret;
	gint rc;
	g_autofree gchar *statement = NULL;

	/* already loaded */
	if (g_once_init_enter (&plugin->priv->loaded)) {
		ret = gs_plugin_local_ratings_load_db (plugin, error);
		g_once_init_leave (&plugin->priv->loaded, TRUE);

		if (!ret)
			return FALSE;
	}

	/* insert the entry */
	statement = g_strdup_printf ("INSERT OR REPLACE INTO ratings (app_id, rating) "
				     "VALUES ('%s', '%i');",
				     gs_app_get_id (app),
				     gs_app_get_rating (app));
	rc = sqlite3_exec (plugin->priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}
	return TRUE;

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
	gboolean ret;
	gint rating;
	GList *l;
	GsApp *app;

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING) == 0)
		return TRUE;

	/* already loaded */
	if (g_once_init_enter (&plugin->priv->loaded)) {
		ret = gs_plugin_local_ratings_load_db (plugin, error);
		g_once_init_leave (&plugin->priv->loaded, TRUE);
		if (!ret)
			return FALSE;
	}

	/* add any missing ratings data */
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_id (app) == NULL)
			continue;
		if (gs_app_get_rating (app) != -1)
			continue;
		rating = gs_plugin_local_find_app (plugin, gs_app_get_id (app));
		if (rating != -1) {
			gs_app_set_rating (app, rating);
			gs_app_set_rating_confidence (app, 100);
			gs_app_set_rating_kind (app, GS_APP_RATING_KIND_USER);
			if (rating > 80)
				gs_app_add_kudo (app, GS_APP_KUDO_POPULAR);
		}
	}
	return TRUE;
}
