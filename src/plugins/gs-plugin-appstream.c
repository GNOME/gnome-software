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

#include "appstream-app.h"
#include "appstream-cache.h"

#define GS_PLUGIN_APPSTREAM_ICONS_HASH	"81f92592ce464df3baf4c480c4a4cc6e18efee52"

struct GsPluginPrivate {
	AppstreamCache		*cache;
	gchar			*cachedir;
	gsize			 done_init;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "appstream";
}

/**
 * gs_plugin_decompress_icons:
 */
static gboolean
gs_plugin_decompress_icons (GsPlugin *plugin, GError **error)
{
	const gchar *argv[6];
	gboolean ret = TRUE;
	gchar *hash_check_file = NULL;
	gint exit_status = 0;

	/* create directory */
	if (!g_file_test (plugin->priv->cachedir, G_FILE_TEST_EXISTS)) {
		exit_status = g_mkdir_with_parents (plugin->priv->cachedir, 0700);
		if (exit_status != 0) {
			ret = FALSE;
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "Failed to create %s",
				     plugin->priv->cachedir);
			goto out;
		}
	}

	/* is cache new enough? */
	hash_check_file = g_build_filename (plugin->priv->cachedir,
					    GS_PLUGIN_APPSTREAM_ICONS_HASH,
					    NULL);
	if (g_file_test (hash_check_file, G_FILE_TEST_EXISTS)) {
		g_debug ("Icon cache new enough, skipping decompression");
		goto out;
	}

	/* decompress */
	argv[0] = "tar";
	argv[1] = "-zxvf";
	argv[2] = DATADIR "/app-info/icons/fedora-20.tar.gz";
	argv[3] = "-C";
	argv[4] = plugin->priv->cachedir;
	argv[5] = NULL;
	g_debug ("Decompressing %s to %s", argv[2], plugin->priv->cachedir);
	ret = g_spawn_sync (NULL,
			    (gchar **) argv,
			    NULL,
			    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
			    NULL, NULL,
			    NULL,
			    NULL,
			    &exit_status,
			    error);
	if (!ret)
		goto out;
	if (exit_status != 0) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to extract icon data to %s [%i]",
			     plugin->priv->cachedir, exit_status);
		goto out;
	}

	/* mark as done */
	ret = g_file_set_contents (hash_check_file, "", -1, error);
	if (!ret)
		goto out;
out:
	g_free (hash_check_file);
	return ret;
}

/**
 * gs_plugin_parse_xml_file:
 */
static gboolean
gs_plugin_parse_xml_file (GsPlugin *plugin,
			  const gchar *parent_dir,
			  const gchar *filename,
			  GError **error)
{
	gboolean ret;
	gchar *path;
	GFile *file;

	/* load this specific file */
	path  = g_build_filename (parent_dir, filename, NULL);
	file = g_file_new_for_path (path);
	ret = appstream_cache_parse_file (plugin->priv->cache, file, NULL, error);
	if (!ret)
		goto out;
out:
	g_free (path);
	g_object_unref (file);
	return ret;
}

/**
 * gs_plugin_parse_xml:
 */
static gboolean
gs_plugin_parse_xml (GsPlugin *plugin, GError **error)
{
	GDir *dir;
	gchar *parent_dir;
	const gchar *tmp;
	gboolean ret = TRUE;

	/* search all files */
	parent_dir = g_build_filename (DATADIR, "app-info", "xmls", NULL);
	dir = g_dir_open (parent_dir, 0, error);
	if (dir == NULL) {
		ret = FALSE;
		goto out;
	}
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		ret = gs_plugin_parse_xml_file (plugin, parent_dir, tmp, error);
		if (!ret)
			goto out;
	}
out:
	g_free (parent_dir);
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->cache = appstream_cache_new ();

	/* this is per-user */
	plugin->priv->cachedir = g_build_filename (g_get_user_cache_dir(),
						   "gnome-software",
						   NULL);
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 1.0f;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_free (plugin->priv->cachedir);
	g_object_unref (plugin->priv->cache);
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GError **error)
{
	gboolean ret = TRUE;
	GTimer *timer = NULL;

	/* get the icons ready for use */
	timer = g_timer_new ();
	ret = gs_plugin_decompress_icons (plugin, error);
	if (!ret)
		goto out;
	g_debug ("Decompressing icons\t:%.1fms", g_timer_elapsed (timer, NULL) * 1000);

	/* Parse the XML */
	g_timer_reset (timer);
	ret = gs_plugin_parse_xml (plugin, error);
	if (!ret)
		goto out;
	g_debug ("Parsed %i entries of XML\t:%.1fms",
		 appstream_cache_get_size (plugin->priv->cache),
		 g_timer_elapsed (timer, NULL) * 1000);

out:
	if (timer != NULL)
		g_timer_destroy (timer);
	return ret;
}

/**
 * gs_plugin_refine_item:
 */
static gboolean
gs_plugin_refine_item (GsPlugin *plugin,
		       GsApp *app,
		       AppstreamApp *item,
		       GError **error)
{
	gboolean ret = TRUE;
	gchar *icon_path = NULL;
	GdkPixbuf *pixbuf = NULL;

	g_debug ("AppStream: Refining %s", gs_app_get_id (app));

	/* is an app */
	if (gs_app_get_kind (app) == GS_APP_KIND_UNKNOWN ||
	    gs_app_get_kind (app) == GS_APP_KIND_PACKAGE)
		gs_app_set_kind (app, GS_APP_KIND_NORMAL);

	/* set id */
	if (appstream_app_get_id (item) != NULL && gs_app_get_id (app) == NULL)
		gs_app_set_id (app, appstream_app_get_id (item));

	/* set name */
	if (appstream_app_get_name (item) != NULL && gs_app_get_name (app) == NULL)
		gs_app_set_name (app, appstream_app_get_name (item));

	/* set summary */
	if (appstream_app_get_summary (item) != NULL && gs_app_get_summary (app) == NULL)
		gs_app_set_summary (app, appstream_app_get_summary (item));

	/* set url */
	if (appstream_app_get_url (item) != NULL && gs_app_get_url (app) == NULL)
		gs_app_set_url (app, appstream_app_get_url (item));

	/* set description */
	if (appstream_app_get_description (item) != NULL && gs_app_get_description (app) == NULL)
		gs_app_set_description (app, appstream_app_get_description (item));

	/* set icon */
	if (appstream_app_get_icon (item) != NULL && gs_app_get_pixbuf (app) == NULL) {
		if (appstream_app_get_icon_kind (item) == APPSTREAM_APP_ICON_KIND_STOCK) {
			pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
							   appstream_app_get_icon (item),
							   plugin->pixbuf_size,
							   GTK_ICON_LOOKUP_USE_BUILTIN |
							   GTK_ICON_LOOKUP_FORCE_SIZE,
							   error);
		} else if (appstream_app_get_icon_kind (item) == APPSTREAM_APP_ICON_KIND_CACHED) {
			icon_path = g_strdup_printf ("%s/%s.png",
						     plugin->priv->cachedir,
						     appstream_app_get_icon (item));
			pixbuf = gdk_pixbuf_new_from_file_at_size (icon_path,
								   plugin->pixbuf_size,
								   plugin->pixbuf_size,
								   error);
		} else {
			g_assert_not_reached ();
		}
		if (pixbuf == NULL) {
			ret = FALSE;
			goto out;
		}
		gs_app_set_pixbuf (app, pixbuf);
	}

	/* set package name */
	if (appstream_app_get_pkgname (item) != NULL && gs_app_get_metadata_item (app, "package-name") == NULL)
		gs_app_set_metadata (app, "package-name", appstream_app_get_pkgname (item));
out:
	g_free (icon_path);
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	return ret;
}

/**
 * gs_plugin_refine_from_id:
 */
static gboolean
gs_plugin_refine_from_id (GsPlugin *plugin,
			  GsApp *app,
			  GError **error)
{
	const gchar *id;
	gboolean ret = TRUE;
	AppstreamApp *item;

	/* find anything that matches the ID */
	id = gs_app_get_id (app);
	if (id == NULL)
		goto out;
	item = appstream_cache_get_item_by_id (plugin->priv->cache, id);
	if (item == NULL) {
		g_debug ("no AppStream match for [id] %s", id);
		goto out;
	}

	/* set new properties */
	ret = gs_plugin_refine_item (plugin, app, item, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * gs_plugin_refine_from_pkgname:
 */
static gboolean
gs_plugin_refine_from_pkgname (GsPlugin *plugin,
			       GsApp *app,
			       GError **error)
{
	const gchar *pkgname;
	gboolean ret = TRUE;
	AppstreamApp *item;

	/* find anything that matches the ID */
	pkgname = gs_app_get_metadata_item (app, "package-name");
	if (pkgname == NULL)
		goto out;
	item = appstream_cache_get_item_by_pkgname (plugin->priv->cache, pkgname);
	if (item == NULL) {
		g_debug ("no AppStream match for {pkgname} %s", pkgname);
		goto out;
	}

	/* set new properties */
	ret = gs_plugin_refine_item (plugin, app, item, error);
	if (!ret)
		goto out;
out:
	return ret;
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
	gboolean ret;
	GList *l;
	GsApp *app;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);

		if (!ret)
			goto out;
	}

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		ret = gs_plugin_refine_from_id (plugin, app, error);
		if (!ret)
			goto out;
		ret = gs_plugin_refine_from_pkgname (plugin, app, error);
		if (!ret)
			goto out;
	}

	/* sucess */
	ret = TRUE;
out:
	return ret;
}

/**
 * gs_plugin_add_category_apps:
 */
gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GList **list,
			     GCancellable *cancellable,
			     GError **error)
{
	const gchar *search_id1;
	const gchar *search_id2 = NULL;
	gboolean ret = TRUE;
	GsApp *app;
	AppstreamApp *item;
	GsCategory *parent;
	GPtrArray *array;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* get the two search terms */
	search_id1 = gs_category_get_id (category);
	parent = gs_category_get_parent (category);
	if (parent != NULL)
		search_id2 = gs_category_get_id (parent);

	/* the "General" item has no ID */
	if (search_id1 == NULL) {
		search_id1 = search_id2;
		search_id2 = NULL;
	}

	/* just look at each app in turn */
	array = appstream_cache_get_items (plugin->priv->cache);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (appstream_app_get_id (item) == NULL)
			continue;
		if (!appstream_app_has_category (item, search_id1))
			continue;
		if (search_id2 != NULL && !appstream_app_has_category (item, search_id2))
			continue;

		/* got a search match, so add all the data we can */
		app = gs_app_new (appstream_app_get_id (item));
		ret = gs_plugin_refine_item (plugin, app, item, error);
		if (!ret)
			goto out;
		gs_plugin_add_app (list, app);
	}
out:
	return ret;
}
