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
	gboolean ret;

	/* simply test if the first item is a file and if not, the icons are
	 * in the new /var/cache/app-info/icons/${repo}/gimp.png layout */
	dir = g_dir_open (dirname, 0, NULL);
	tmp = g_dir_read_name (dir);
	ret = g_strstr_len (tmp, -1, ".") == NULL;
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
 * gs_plugin_parse_xml_dir:
 */
static gboolean
gs_plugin_parse_xml_dir (GsPlugin *plugin,
			 const gchar *path_xml,
			 const gchar *path_icons,
			 GError **error)
{
	const gchar *tmp;
	gboolean ret = TRUE;
	GDir *dir = NULL;

	/* search all files */
	if (!g_file_test (path_xml, G_FILE_TEST_EXISTS))
		goto out;
	dir = g_dir_open (path_xml, 0, error);
	if (dir == NULL) {
		ret = FALSE;
		goto out;
	}
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		ret = gs_plugin_parse_xml_file (plugin, path_xml, tmp, path_icons, error);
		if (!ret)
			goto out;
	}
out:
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * gs_plugin_parse_xml:
 */
static gboolean
gs_plugin_parse_xml (GsPlugin *plugin, GError **error)
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
		ret = gs_plugin_parse_xml_dir (plugin, path_xml, path_icons, error);
		g_free (path_xml);
		g_free (path_icons);
		if (!ret)
			goto out;
	}
	path_xml = g_build_filename (LOCALSTATEDIR, "cache", "app-info", "xmls", NULL);
	path_icons = g_build_filename (LOCALSTATEDIR, "cache", "app-info", "icons", NULL);
	ret = gs_plugin_parse_xml_dir (plugin, path_xml, path_icons, error);
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
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
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
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GError **error)
{
	gboolean ret;
	guint size;

	/* Parse the XML */
	gs_profile_start (plugin->profile, "appstream::startup");
	ret = gs_plugin_parse_xml (plugin, error);
	if (!ret)
		goto out;
	size = appstream_cache_get_size (plugin->priv->cache);
	if (size == 0) {
		g_warning ("No AppStream data, try 'make install-sample-data' in data/");
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     _("No AppStream data found"));
		goto out;
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
static GdkPixbuf *
gs_plugin_refine_search_pixbuf (GsPlugin *plugin,
				const gchar *icon_dir,
				const gchar *icon)
{
	GdkPixbuf *pixbuf = NULL;
	const gchar *exensions[] = { "png", "svg", "gif", "ico", "xcf", NULL };
	gchar *icon_path;
	guint i;

	/* exists */
	for (i = 0; exensions[i] != NULL; i++) {
		icon_path = g_strdup_printf ("%s/%s.%s",
					     icon_dir,
					     icon,
					     exensions[i]);
		pixbuf = gdk_pixbuf_new_from_file_at_size (icon_path,
							   plugin->pixbuf_size,
							   plugin->pixbuf_size,
							   NULL);
		g_free (icon_path);
		if (pixbuf != NULL)
			break;
	}
	return pixbuf;
}

/**
 * gs_plugin_refine_item_pixbuf:
 */
static void
gs_plugin_refine_item_pixbuf (GsPlugin *plugin, GsApp *app, AppstreamApp *item)
{
	const gchar *icon;
	const gchar *icon_dir;
	gchar *icon_path = NULL;
	GdkPixbuf *pixbuf = NULL;
	GError *error = NULL;

	icon = appstream_app_get_icon (item);
	switch (appstream_app_get_icon_kind (item)) {
	case APPSTREAM_APP_ICON_KIND_STOCK:
		pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						   icon,
						   plugin->pixbuf_size,
						   GTK_ICON_LOOKUP_USE_BUILTIN |
						   GTK_ICON_LOOKUP_FORCE_SIZE,
						   &error);
		if (pixbuf == NULL) {
			g_warning ("failed to load stock icon %s", icon);
			g_error_free (error);
		}
		break;
	case APPSTREAM_APP_ICON_KIND_CACHED:

		/* we assume <icon type="local">gnome-chess.png</icon> */
		icon_dir = appstream_app_get_userdata (item);
		icon_path = g_build_filename (icon_dir, icon, NULL);
		pixbuf = gdk_pixbuf_new_from_file_at_size (icon_path,
							   plugin->pixbuf_size,
							   plugin->pixbuf_size,
							   &error);
		if (pixbuf == NULL) {
			g_warning ("falling back to searching for %s", icon_path);
			g_error_free (error);

			/* we are not going to be doing this forever,
			 * SO FIX YOUR APPSTREAM METADATA */
			pixbuf = gs_plugin_refine_search_pixbuf (plugin, icon_dir, icon);
			if (pixbuf == NULL)
				g_warning ("failed to load cached icon %s", icon_path);
		}
		break;
	default:
		break;
	}

	/* did we load anything */
	if (pixbuf != NULL) {
		gs_app_set_pixbuf (app, pixbuf);
		g_object_unref (pixbuf);
	}
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
		for (j = 0; j < images_as->len; j++) {
			im = g_ptr_array_index (images_as, j);
			if (appstream_image_get_kind (im) == APPSTREAM_IMAGE_KIND_SOURCE)
				continue;
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

	/* set url */
	if (appstream_app_get_url (item) != NULL && gs_app_get_url (app) == NULL)
		gs_app_set_url (app, appstream_app_get_url (item));

	/* set licence */
	if (appstream_app_get_licence (item) != NULL && gs_app_get_licence (app) == NULL)
		gs_app_set_licence (app, appstream_app_get_licence (item));

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

	/* set package name */
	if (appstream_app_get_pkgname (item) != NULL && gs_app_get_source (app) == NULL)
		gs_app_set_source (app, appstream_app_get_pkgname (item));

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
	id = gs_app_get_id (app);
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
	const gchar *pkgname;
	gboolean ret = TRUE;
	AppstreamApp *item;

	/* find anything that matches the ID */
	pkgname = gs_app_get_source (app);
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
		  GsPluginRefineFlags flags,
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

	gs_profile_start_full (plugin->profile, "appstream::refine");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		ret = gs_plugin_refine_from_id (plugin, app, error);
		if (!ret)
			goto out;
		ret = gs_plugin_refine_from_pkgname (plugin, app, error);
		if (!ret)
			goto out;
	}
	gs_profile_stop_full (plugin->profile, "appstream::refine");

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
	gs_profile_start_full (plugin->profile, "appstream::add-category-apps");
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
	gs_profile_stop_full (plugin->profile, "appstream::add-category-apps");
out:
	return ret;
}

/**
 * gs_plugin_add_search:
 */
gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      const gchar *value,
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
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* search categories for the search term */
	array = appstream_cache_get_items (plugin->priv->cache);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (appstream_app_search_matches (item, value)) {
			app = gs_app_new (appstream_app_get_id (item));
			ret = gs_plugin_refine_item (plugin, app, item, error);
			if (!ret)
				goto out;
			gs_plugin_add_app (list, app);
		}
	}
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
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* find out how many packages are in each category */
	gs_profile_start_full (plugin->profile, "appstream::add-categories");
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
	gs_profile_stop_full (plugin->profile, "appstream::add-categories");
out:
	return ret;
}
