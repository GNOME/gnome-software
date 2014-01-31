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

#include <string.h>
#include <gs-plugin.h>
#include <gs-utils.h>

struct GsPluginPrivate {
	GMutex		 plugin_mutex;
	GHashTable	*cache;
};

typedef struct {
	gchar		*id;
	gchar		*pkgname;
	gchar		*name;
	gchar		*summary;
	gchar		*icon_name;
	GdkPixbuf	*pixbuf;
} GsPluginDataDirAppsCacheItem;

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "datadir-apps";
}

/**
 * gs_plugin_datadir_apps_cache_item_free:
 */
static void
gs_plugin_datadir_apps_cache_item_free (GsPluginDataDirAppsCacheItem *cache_item)
{
	g_free (cache_item->id);
	g_free (cache_item->name);
	g_free (cache_item->summary);
	g_free (cache_item->icon_name);
	if (cache_item->pixbuf != NULL)
		g_object_unref (cache_item->pixbuf);
	g_slice_free (GsPluginDataDirAppsCacheItem, cache_item);
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
						     (GDestroyNotify) gs_plugin_datadir_apps_cache_item_free);
	g_mutex_init (&plugin->priv->plugin_mutex);
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"datadir-filename",	/* requires DataDir::desktop-filename */
		"datadir-filename-local",	/* ^^^ */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_hash_table_unref (plugin->priv->cache);
	g_mutex_clear (&plugin->priv->plugin_mutex);
}

/**
 * gs_plugin_datadir_apps_set_from_cache_item:
 */
static void
gs_plugin_datadir_apps_set_from_cache_item (GsApp *app,
					    GsPluginDataDirAppsCacheItem *cache_item)
{
	gs_app_set_id (app, cache_item->id);
	gs_app_set_metadata (app, "DataDir::desktop-icon", cache_item->icon_name);
	if (cache_item->name != NULL)
		gs_app_set_name (app, GS_APP_QUALITY_HIGHEST, cache_item->name);
	if (cache_item->pkgname != NULL)
		gs_app_add_source (app, cache_item->pkgname);
	if (cache_item->summary != NULL)
		gs_app_set_summary (app, GS_APP_QUALITY_HIGHEST, cache_item->summary);
	if (cache_item->pixbuf != NULL)
		gs_app_set_pixbuf (app, cache_item->pixbuf);

	/* mark as an application */
	switch (gs_app_get_kind (app)) {
	case GS_APP_KIND_UNKNOWN:
	case GS_APP_KIND_PACKAGE:
		gs_app_set_kind (app, GS_APP_KIND_NORMAL);
		break;
	default:
		break;
	}
}

/**
 * gs_plugin_datadir_apps_extract_desktop_data:
 */
static gboolean
gs_plugin_datadir_apps_extract_desktop_data (GsPlugin *plugin,
					     GsApp *app,
					     const gchar *desktop_file,
					     GError **error)
{
	const gchar *app_id_full_noprefix;
	const gchar *icon_tmp = NULL;
	gboolean ret = TRUE;
	gchar *app_id_full = NULL;
	gchar *app_id = NULL;
	gchar *comment = NULL;
	gchar *name = NULL;
	gchar *pkgname = NULL;
	gchar *icon = NULL;
	gchar *dot;
	GKeyFile *key_file = NULL;
	GdkPixbuf *pixbuf = NULL;
	GsPluginDataDirAppsCacheItem *cache_item;

	/* is in cache */
	g_mutex_lock (&plugin->priv->plugin_mutex);
	cache_item = g_hash_table_lookup (plugin->priv->cache, desktop_file);
	if (cache_item != NULL) {
		gs_plugin_datadir_apps_set_from_cache_item (app, cache_item);
		g_mutex_unlock (&plugin->priv->plugin_mutex);
		goto out;
	}
	g_mutex_unlock (&plugin->priv->plugin_mutex);

	/* load desktop file */
	key_file = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file,
					 desktop_file,
					 G_KEY_FILE_NONE,
					 error);
	if (!ret)
		goto out;

	/* set new id */
	app_id_full = g_path_get_basename (desktop_file);
	app_id_full_noprefix = app_id_full;
	if (g_str_has_prefix (app_id_full_noprefix, "fedora-"))
		app_id_full_noprefix += 7;
	app_id = g_strdup (app_id_full_noprefix);
	dot = strrchr (app_id, '.');
	if (dot)
		*dot = '\0';

	/* get desktop name */
	name = g_key_file_get_locale_string (key_file,
					     G_KEY_FILE_DESKTOP_GROUP,
					     G_KEY_FILE_DESKTOP_KEY_NAME,
					     NULL,
					     NULL);
	if (name == NULL || name[0] == '\0')
		goto out;

	/* get desktop summary */
	comment = g_key_file_get_locale_string (key_file,
						G_KEY_FILE_DESKTOP_GROUP,
						G_KEY_FILE_DESKTOP_KEY_COMMENT,
						NULL,
						NULL);
	if (comment == NULL || comment[0] == '\0')
		goto out;

	/* get desktop icon */
	icon = g_key_file_get_string (key_file,
				      G_KEY_FILE_DESKTOP_GROUP,
				      G_KEY_FILE_DESKTOP_KEY_ICON,
				      NULL);
	if (icon == NULL)
		goto out;

	/* do we have an icon in the cache? */
	icon_tmp = g_hash_table_lookup (plugin->icon_cache, app_id_full_noprefix);
	if (icon_tmp != NULL) {
		pixbuf = gdk_pixbuf_new_from_file (icon_tmp, NULL);
	} else {
		pixbuf = gs_pixbuf_load (icon, plugin->pixbuf_size, NULL);
		if (pixbuf == NULL)
			goto out;
	}

	/* create a new cache entry */
	cache_item = g_slice_new0 (GsPluginDataDirAppsCacheItem);
	cache_item->id = g_strdup (app_id_full_noprefix);
	cache_item->name = g_strdup (name);
	cache_item->summary = g_strdup (comment);
	cache_item->icon_name = g_strdup (icon);
	cache_item->pixbuf = g_object_ref (pixbuf);

	/* set pkgname if set (only Ubuntu) */
	pkgname = g_key_file_get_string (key_file,
					 G_KEY_FILE_DESKTOP_GROUP,
					 "X-AppInstall-Package",
					 NULL);
	if (pkgname != NULL && pkgname[0] != '\0')
		cache_item->pkgname = g_strdup (pkgname);

	/* add to cache */
	gs_plugin_datadir_apps_set_from_cache_item (app, cache_item);
	g_mutex_lock (&plugin->priv->plugin_mutex);
	g_hash_table_insert (plugin->priv->cache,
			     g_strdup (desktop_file),
			     cache_item);
	g_mutex_unlock (&plugin->priv->plugin_mutex);
out:
	if (key_file != NULL)
		g_key_file_unref (key_file);
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	g_free (app_id);
	g_free (app_id_full);
	g_free (pkgname);
	g_free (icon);
	g_free (name);
	g_free (comment);
	return ret;
}

/**
 * gs_plugin_refine_app:
 */
static gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *fn;
	gboolean ret = TRUE;
	gchar *profile_id = NULL;

	/* already set */
	fn = gs_app_get_metadata_item (app, "DataDir::desktop-filename");
	if (fn == NULL)
		goto out;
	profile_id = g_strdup_printf ("GsPlugin::datadir-apps(refine:%s)",
				      gs_app_get_id (app));
	gs_profile_start (plugin->profile, profile_id);
	ret = gs_plugin_datadir_apps_extract_desktop_data (plugin, app, fn, error);
	if (!ret)
		goto out;

	/* we know it's installed as we read the desktop file */
	gs_app_set_id_kind (app, GS_APP_ID_KIND_DESKTOP);
	if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
out:
	if (profile_id != NULL)
		gs_profile_stop (plugin->profile, profile_id);
	g_free (profile_id);
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
	GError *error_local = NULL;
	GList *l;
	GsApp *app;
	gboolean ret;

	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		ret = gs_plugin_refine_app (plugin,
					    app,
					    flags,
					    cancellable,
					    &error_local);
		if (!ret) {
			if (!g_error_matches (error_local, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
				g_warning ("failed to extract desktop data for %s: %s",
					   gs_app_get_id (app),
					   error_local->message);
			}
			g_clear_error (&error_local);
			continue;
		}
	}

	/* success */
	return TRUE;
}
