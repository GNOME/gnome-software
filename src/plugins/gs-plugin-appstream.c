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
#include <glib/gi18n.h>

#include <gs-plugin.h>
#include <gs-plugin-loader.h>

#include "appstream-app.h"
#include "appstream-cache.h"

#define	GS_PLUGIN_APPSTREAM_MAX_SCREENSHOTS	5

struct GsPluginPrivate {
	AppstreamCache		*cache;
	GPtrArray		*file_monitors;
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
 * gs_plugin_appstream_icons_are_new_layout:
 */
static gboolean
gs_plugin_appstream_icons_are_new_layout (const gchar *dirname)
{
	GDir *dir;
	const gchar *tmp;
	gboolean ret = TRUE;

	/* simply test if the first item is a file and if not, the icons are
	 * in the new /var/cache/app-info/icons/${repo}/gimp.png layout */
	dir = g_dir_open (dirname, 0, NULL);
	if (dir == NULL)
		goto out;
	tmp = g_dir_read_name (dir);
	ret = g_strstr_len (tmp, -1, ".") == NULL;
out:
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * gs_plugin_parse_xml_file:
 */
static gboolean
gs_plugin_parse_xml_file (GsPlugin *plugin,
			  const gchar *parent_dir,
			  const gchar *filename,
			  const gchar *path_icons,
			  GCancellable *cancellable,
			  GError **error)
{
	GError *error_local = NULL;
	GFile *file = NULL;
	gboolean ret = FALSE;
	gchar *path_icons_full = NULL;
	gchar *path_xml = NULL;
	gchar *repo_id;
	gchar *tmp;

	/* the first component of the file (e.g. "fedora-20.xml.gz)
	 * is used for the icon directory as we might want to clean up
	 * the icons manually if they are installed in /var/cache */
	repo_id = g_strdup (filename);
	tmp = g_strstr_len (repo_id, -1, ".xml");
	if (tmp == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "AppStream metadata name %s/%s not valid, "
			     "expected .xml[.*]",
			     parent_dir, filename);
		goto out;
	}
	tmp[0] = '\0';

	/* support both old and new layouts */
	if (gs_plugin_appstream_icons_are_new_layout (path_icons)) {
		path_icons_full = g_build_filename (path_icons,
						    repo_id,
						    NULL);
	} else {
		path_icons_full = g_strdup (path_icons);
	}

	/* load this specific file */
	path_xml  = g_build_filename (parent_dir, filename, NULL);
	g_debug ("Loading AppStream XML %s with icon path %s",
		 path_xml,
		 path_icons_full);
	file = g_file_new_for_path (path_xml);
	ret = appstream_cache_parse_file (plugin->priv->cache,
					  file,
					  path_icons_full,
					  NULL,
					  &error_local);
	if (!ret) {
		if (g_error_matches (error_local,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED)) {
			ret = TRUE;
			g_warning ("AppStream XML invalid: %s", error_local->message);
			g_error_free (error_local);
		} else {
			g_propagate_error (error, error_local);
		}
		goto out;
	}
out:
	g_free (path_icons_full);
	g_free (path_xml);
	g_free (repo_id);
	if (file != NULL)
		g_object_unref (file);
	return ret;
}

/**
 * gs_plugin_appstream_cache_changed_cb:
 */
static void
gs_plugin_appstream_cache_changed_cb (GFileMonitor *monitor,
				      GFile *file, GFile *other_file,
				      GFileMonitorEvent event_type,
				      GsPlugin *plugin)
{
	gchar *path;
	path = g_file_get_path (file);
	g_debug ("AppStream metadata %s changed, reloading cache", path);
	plugin->priv->done_init = FALSE;
	g_free (path);
}

/**
 * gs_plugin_parse_xml_dir:
 */
static gboolean
gs_plugin_parse_xml_dir (GsPlugin *plugin,
			 const gchar *path_xml,
			 const gchar *path_icons,
			 GCancellable *cancellable,
			 GError **error)
{
	GDir *dir = NULL;
	GFile *file_xml = NULL;
	GFileMonitor *monitor = NULL;
	const gchar *tmp;
	gboolean ret = TRUE;

	/* watch the directory for changes */
	file_xml = g_file_new_for_path (path_xml);
	monitor = g_file_monitor_directory (file_xml,
					    G_FILE_MONITOR_NONE,
					    cancellable,
					    error);
	if (monitor == NULL)
		goto out;
	g_signal_connect (monitor, "changed",
			  G_CALLBACK (gs_plugin_appstream_cache_changed_cb),
			  plugin);
	g_ptr_array_add (plugin->priv->file_monitors, g_object_ref (monitor));

	/* search all files */
	if (!g_file_test (path_xml, G_FILE_TEST_EXISTS))
		goto out;
	dir = g_dir_open (path_xml, 0, error);
	if (dir == NULL) {
		ret = FALSE;
		goto out;
	}
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		ret = gs_plugin_parse_xml_file (plugin,
						path_xml,
						tmp,
						path_icons,
						cancellable,
						error);
		if (!ret)
			goto out;
	}
out:
	if (file_xml != NULL)
		g_object_unref (file_xml);
	if (monitor != NULL)
		g_object_unref (monitor);
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * gs_plugin_parse_xml:
 */
static gboolean
gs_plugin_parse_xml (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	const gchar * const * data_dirs;
	gboolean ret;
	gchar *path_xml = NULL;
	gchar *path_icons = NULL;
	guint i;

	/* search all files */
	data_dirs = g_get_system_data_dirs ();
	for (i = 0; data_dirs[i] != NULL; i++) {
		path_xml = g_build_filename (data_dirs[i], "app-info", "xmls", NULL);
		path_icons = g_build_filename (data_dirs[i], "app-info", "icons", NULL);
		ret = gs_plugin_parse_xml_dir (plugin,
					       path_xml,
					       path_icons,
					       cancellable,
					       error);
		g_free (path_xml);
		g_free (path_icons);
		if (!ret)
			goto out;
	}
	path_xml = g_build_filename (g_get_user_data_dir (), "app-info", "xmls", NULL);
	path_icons = g_build_filename (g_get_user_data_dir (), "app-info", "icons", NULL);
	ret = gs_plugin_parse_xml_dir (plugin,
				       path_xml,
				       path_icons,
				       cancellable,
				       error);
	g_free (path_xml);
	g_free (path_icons);
	if (!ret)
		goto out;
	path_xml = g_build_filename (LOCALSTATEDIR, "cache", "app-info", "xmls", NULL);
	path_icons = g_build_filename (LOCALSTATEDIR, "cache", "app-info", "icons", NULL);
	ret = gs_plugin_parse_xml_dir (plugin,
				       path_xml,
				       path_icons,
				       cancellable,
				       error);
	g_free (path_xml);
	g_free (path_icons);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->file_monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	plugin->priv->cache = appstream_cache_new ();
	plugin->priv->cachedir = g_build_filename (DATADIR,
						   "app-info",
						   "icons",
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
	g_ptr_array_unref (plugin->priv->file_monitors);
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	AppstreamApp *app;
	GPtrArray *items;
	gboolean ret;
	guint i;

	/* clear all existing file monitors */
	g_ptr_array_set_size (plugin->priv->file_monitors, 0);

	/* Parse the XML */
	gs_profile_start (plugin->profile, "appstream::startup");
	ret = gs_plugin_parse_xml (plugin, cancellable, error);
	if (!ret)
		goto out;
	items = appstream_cache_get_items (plugin->priv->cache);
	if (items->len == 0) {
		g_warning ("No AppStream data, try 'make install-sample-data' in data/");
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     _("No AppStream data found"));
		goto out;
	}

	/* add icons to the icon name cache */
	for (i = 0; i < items->len; i++) {
		app = g_ptr_array_index (items, i);
		if (appstream_app_get_icon_kind (app) != APPSTREAM_APP_ICON_KIND_CACHED)
			continue;
		g_hash_table_insert (plugin->icon_cache,
				     g_strdup (appstream_app_get_id (app)),
				     g_build_filename (appstream_app_get_userdata (app),
						       appstream_app_get_icon (app),
						       NULL));
	}
out:
	gs_profile_stop (plugin->profile, "appstream::startup");
	return ret;
}

/**
 * gs_plugin_refine_search_pixbuf:
 *
 * This method allows us to cope with metadata like this:
 *
 * <icon type="local">gnome-chess</icon>
 * <icon type="local">geary</icon>
 *
 * Where .../app-info/icons/gnome-chess.png and .../app-info/icons/geary.svg
 * exist.
 *
 * Basically, we have to stick on the extension and stat() each file in turn.
 */
static gchar *
gs_plugin_refine_search_pixbuf (GsPlugin *plugin,
				const gchar *icon_dir,
				const gchar *icon)
{
	const gchar *exensions[] = { "png", "svg", "gif", "ico", "xcf", NULL };
	gchar *icon_path;
	guint i;

	/* exists */
	for (i = 0; exensions[i] != NULL; i++) {
		icon_path = g_strdup_printf ("%s/%s.%s",
					     icon_dir,
					     icon,
					     exensions[i]);
		if (g_file_test (icon_path, G_FILE_TEST_EXISTS))
			goto out;
		g_free (icon_path);
		icon_path = NULL;
	}
out:
	return icon_path;
}

/**
 * gs_plugin_refine_item_pixbuf:
 */
static void
gs_plugin_refine_item_pixbuf (GsPlugin *plugin, GsApp *app, AppstreamApp *item)
{
	GError *error = NULL;
	const gchar *icon;
	const gchar *icon_dir;
	gboolean ret;
	gchar *icon_path = NULL;

	icon = appstream_app_get_icon (item);
	switch (appstream_app_get_icon_kind (item)) {
	case APPSTREAM_APP_ICON_KIND_REMOTE:
		gs_app_set_icon (app, icon);
		break;
	case APPSTREAM_APP_ICON_KIND_STOCK:
		gs_app_set_icon (app, icon);
		ret = gs_app_load_icon (app, &error);
		if (!ret) {
			g_warning ("failed to load stock icon %s: %s",
				   icon, error->message);
			g_error_free (error);
			goto out;
		}
		break;
	case APPSTREAM_APP_ICON_KIND_CACHED:

		/* we assume <icon type="local">gnome-chess.png</icon> */
		icon_dir = appstream_app_get_userdata (item);
		icon_path = g_build_filename (icon_dir, icon, NULL);
		gs_app_set_icon (app, icon_path);
		ret = gs_app_load_icon (app, &error);
		if (!ret) {
			g_warning ("falling back to searching for %s", icon_path);
			g_clear_error (&error);
			g_free (icon_path);

			/* we are not going to be doing this forever,
			 * SO FIX YOUR APPSTREAM METADATA */
			icon_path = gs_plugin_refine_search_pixbuf (plugin, icon_dir, icon);
			if (icon_path == NULL) {
				g_warning ("failed to load cached icon %s", icon);
				goto out;
			}
			gs_app_set_icon (app, icon_path);
			ret = gs_app_load_icon (app, &error);
			if (!ret) {
				g_warning ("failed to load cached icon %s: %s",
					   icon, error->message);
				g_error_free (error);
			}
			goto out;
		}
		break;
	default:
		break;
	}
out:
	g_free (icon_path);
}

/**
 * gs_plugin_refine_add_screenshots:
 */
static void
gs_plugin_refine_add_screenshots (GsApp *app, AppstreamApp *item)
{
	AppstreamImage *im;
	AppstreamScreenshot *ss;
	AppstreamScreenshotKind ss_kind;
	GPtrArray *images_as;
	GPtrArray *screenshots_as;
	GsScreenshot *screenshot;
	guint i;
	guint j;

	/* do we have any to add */
	screenshots_as = appstream_app_get_screenshots (item);
	if (screenshots_as->len == 0)
		return;

	/* does the app already have some */
	if (gs_app_get_screenshots(app)->len > 0)
		return;

	/* add any we know */
	for (i = 0; i < screenshots_as->len &&
		    i < GS_PLUGIN_APPSTREAM_MAX_SCREENSHOTS; i++) {
		ss = g_ptr_array_index (screenshots_as, i);
		images_as = appstream_screenshot_get_images (ss);
		if (images_as->len == 0)
			continue;
		ss_kind = appstream_screenshot_get_kind (ss);
		if (ss_kind == APPSTREAM_SCREENSHOT_KIND_UNKNOWN)
			continue;

		/* create a new application screenshot and add each image */
		screenshot = gs_screenshot_new ();
		gs_screenshot_set_is_default (screenshot,
					      ss_kind == APPSTREAM_SCREENSHOT_KIND_DEFAULT);
		gs_screenshot_set_caption (screenshot,
					   appstream_screenshot_get_caption (ss));
		for (j = 0; j < images_as->len; j++) {
			im = g_ptr_array_index (images_as, j);
			gs_screenshot_add_image	(screenshot,
						 appstream_image_get_url (im),
						 appstream_image_get_width (im),
						 appstream_image_get_height (im));
		}
		gs_app_add_screenshot (app, screenshot);
		g_object_unref (screenshot);
	}
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
	GHashTable *urls;
	GPtrArray *tmp;
	gboolean ret = TRUE;

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

	/* add urls */
	urls = appstream_app_get_urls (item);
	if (g_hash_table_size (urls) > 0 &&
	    gs_app_get_url (app, GS_APP_URL_KIND_HOMEPAGE) == NULL) {
		GList *keys;
		GList *l;
		keys = g_hash_table_get_keys (urls);
		for (l = keys; l != NULL; l = l->next) {
			gs_app_set_url (app,
					l->data,
					g_hash_table_lookup (urls, l->data));
		}
		g_list_free (keys);
	}

	/* set licence */
	if (appstream_app_get_licence (item) != NULL && gs_app_get_licence (app) == NULL)
		gs_app_set_licence (app, appstream_app_get_licence (item));

	/* set keywords */
	if (appstream_app_get_keywords (item) != NULL &&
	    gs_app_get_keywords (app) == NULL)
		gs_app_set_keywords (app, appstream_app_get_keywords (item));

	/* set description */
	if (appstream_app_get_description (item) != NULL && gs_app_get_description (app) == NULL)
		gs_app_set_description (app, appstream_app_get_description (item));

	/* set icon */
	if (appstream_app_get_icon (item) != NULL && gs_app_get_pixbuf (app) == NULL)
		gs_plugin_refine_item_pixbuf (plugin, app, item);

	/* set categories */
	if (appstream_app_get_categories (item) != NULL && gs_app_get_categories (app) == NULL)
		gs_app_set_categories (app, appstream_app_get_categories (item));

	/* set project group */
	if (appstream_app_get_project_group (item) != NULL &&
	    gs_app_get_project_group (app) == NULL)
		gs_app_set_project_group (app, appstream_app_get_project_group (item));

	/* this is a core application for the desktop and cannot be removed */
	if (appstream_app_get_desktop_core (item, "GNOME") &&
	    gs_app_get_kind (app) == GS_APP_KIND_NORMAL)
		gs_app_set_kind (app, GS_APP_KIND_SYSTEM);

	/* set id kind */
	if (gs_app_get_id_kind (app) == GS_APP_ID_KIND_UNKNOWN)
		gs_app_set_id_kind (app, appstream_app_get_id_kind (item));

	/* set package names */
	tmp = appstream_app_get_pkgnames (item);
	if (tmp->len > 0 && gs_app_get_sources(app)->len == 0)
		gs_app_set_sources (app, tmp);

	/* set screenshots */
	gs_plugin_refine_add_screenshots (app, item);

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
	id = gs_app_get_id_full (app);
	if (id == NULL)
		goto out;
	item = appstream_cache_get_item_by_id (plugin->priv->cache, id);
	if (item == NULL)
		goto out;

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
	AppstreamApp *item = NULL;
	GPtrArray *sources;
	const gchar *pkgname;
	gboolean ret = TRUE;
	guint i;

	/* find anything that matches the ID */
	sources = gs_app_get_sources (app);
	for (i = 0; i < sources->len && item == NULL; i++) {
		pkgname = g_ptr_array_index (sources, i);
		item = appstream_cache_get_item_by_pkgname (plugin->priv->cache,
							    pkgname);
		if (item == NULL)
			g_debug ("no AppStream match for {pkgname} %s", pkgname);
	}

	/* nothing found */
	if (item == NULL)
		goto out;

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
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	gboolean ret;
	GList *l;
	GsApp *app;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	gs_profile_start (plugin->profile, "appstream::refine");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		ret = gs_plugin_refine_from_id (plugin, app, error);
		if (!ret) {
			gs_profile_stop (plugin->profile, "appstream::refine");
			goto out;
		}
		ret = gs_plugin_refine_from_pkgname (plugin, app, error);
		if (!ret) {
			gs_profile_stop (plugin->profile, "appstream::refine");
			goto out;
		}
	}
	gs_profile_stop (plugin->profile, "appstream::refine");

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
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* get the two search terms */
	gs_profile_start (plugin->profile, "appstream::add-category-apps");
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
	gs_profile_stop (plugin->profile, "appstream::add-category-apps");
out:
	return ret;
}

/**
 * gs_plugin_appstream_match_item:
 */
static gboolean
gs_plugin_appstream_match_item (AppstreamApp *item, gchar **values)
{
	guint matches = 0;
	guint i;

	/* does the GsApp match *all* search keywords */
	for (i = 0; values[i] != NULL; i++) {
		matches = appstream_app_search_matches (item, values[i]);
		if (matches == 0)
			break;
	}
	return matches != 0;
}

/**
 * gs_plugin_add_search:
 */
gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GList **list,
		      GCancellable *cancellable,
		      GError **error)
{
	AppstreamApp *item;
	gboolean ret = TRUE;
	GPtrArray *array;
	GsApp *app;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* search categories for the search term */
	gs_profile_start (plugin->profile, "appstream::search");
	array = appstream_cache_get_items (plugin->priv->cache);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (gs_plugin_appstream_match_item (item, values)) {
			app = gs_app_new (appstream_app_get_id (item));
			ret = gs_plugin_refine_item (plugin, app, item, error);
			if (!ret)
				goto out;
			gs_plugin_add_app (list, app);
		}
	}
	gs_profile_stop (plugin->profile, "appstream::search");
out:
	return ret;
}

/**
 * gs_plugin_add_categories:
 */
gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GList **list,
			  GCancellable *cancellable,
			  GError **error)
{
	AppstreamApp *item;
	const gchar *search_id1;
	const gchar *search_id2 = NULL;
	gboolean ret = TRUE;
	GList *l;
	GList *l2;
	GList *children;
	GPtrArray *array;
	GsCategory *category;
	GsCategory *parent;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* find out how many packages are in each category */
	gs_profile_start (plugin->profile, "appstream::add-categories");
	for (l = *list; l != NULL; l = l->next) {
		parent = GS_CATEGORY (l->data);
		children = gs_category_get_subcategories (parent);
		for (l2 = children; l2 != NULL; l2 = l2->next) {
			category = GS_CATEGORY (l2->data);
			search_id1 = gs_category_get_id (category);
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
				if (appstream_app_get_priority (item) < 0)
					continue;
				if (!appstream_app_has_category (item, search_id1))
					continue;
				if (search_id2 != NULL && !appstream_app_has_category (item, search_id2))
					continue;

				/* we have another result */
				gs_category_increment_size (category);
				gs_category_increment_size (parent);
			}
		}
		g_list_free (children);
	}
	gs_profile_stop (plugin->profile, "appstream::add-categories");
out:
	return ret;
}
