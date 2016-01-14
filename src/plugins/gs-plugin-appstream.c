/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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
#include <appstream-glib.h>

#include <gs-plugin.h>
#include <gs-plugin-loader.h>

#include "appstream-common.h"

/*
 * SECTION:
 * Uses offline AppStream data to populate and refine package results.
 *
 * This plugin calls UpdatesChanged() if any of the AppStream stores are
 * changed in any way.
 *
 * Methods:     | AddCategory
 * Refines:     | [source]->[name,summary,pixbuf,id,kind]
 */

struct GsPluginPrivate {
	AsStore			*store;
	GMutex			 store_mutex;
	gsize			 done_init;
};

static gboolean gs_plugin_refine_item (GsPlugin *plugin, GsApp *app, AsApp *item, GError **error);

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "appstream";
}

/**
 * gs_plugin_appstream_store_changed_cb:
 */
static void
gs_plugin_appstream_store_changed_cb (AsStore *store, GsPlugin *plugin)
{
	g_debug ("AppStream metadata changed, reloading cache");
	plugin->priv->done_init = FALSE;

	/* this is not strictly true, but it causes all the UI to be reloaded
	 * which is what we really want */
	gs_plugin_updates_changed (plugin);
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	g_mutex_init (&plugin->priv->store_mutex);
	plugin->priv->store = as_store_new ();
	as_store_set_watch_flags (plugin->priv->store,
				  AS_STORE_WATCH_FLAG_ADDED |
				  AS_STORE_WATCH_FLAG_REMOVED);

	/* AppInstall does not ever give us a long description */
	if (gs_plugin_check_distro_id (plugin, "debian") ||
	    gs_plugin_check_distro_id (plugin, "ubuntu")) {
		plugin->use_pkg_descriptions = TRUE;
	}
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"menu-spec-categories",	/* need category list */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->store);
	g_mutex_clear (&plugin->priv->store_mutex);
}

/**
 * gs_plugin_appstream_get_origins_hash:
 *
 * Returns: A hash table with a string key of the application origin and a
 * value of the guint percentage of the store is made up by that origin.
 */
static GHashTable *
gs_plugin_appstream_get_origins_hash (GPtrArray *array)
{
	AsApp *app;
	GHashTable *origins = NULL;
	GList *l;
	const gchar *tmp;
	gdouble perc;
	guint *cnt;
	guint i;
	g_autoptr(GList) keys = NULL;

	/* create a hash table with origin:cnt */
	origins = g_hash_table_new_full (g_str_hash, g_str_equal,
					 g_free, g_free);
	for (i = 0; i < array->len; i++) {
		app = g_ptr_array_index (array, i);
		tmp = as_app_get_origin (app);
		if (tmp == NULL)
			continue;
		cnt = g_hash_table_lookup (origins, tmp);
		if (cnt == NULL) {
			cnt = g_new0 (guint, 1);
			g_hash_table_insert (origins, g_strdup (tmp), cnt);
		}
		(*cnt)++;
	}

	/* convert the cnt to a percentage */
	keys = g_hash_table_get_keys (origins);
	for (l = keys; l != NULL; l = l->next) {
		tmp = l->data;
		cnt = g_hash_table_lookup (origins, tmp);
		perc = (100.f / (gdouble) array->len) * (gdouble) (*cnt);
		g_debug ("origin %s provides %i apps (%.0f%%)", tmp, *cnt, perc);
		*cnt = perc;
	}

	return origins;
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GError **error)
{
	AsApp *app;
	GPtrArray *items;
	gboolean ret;
	const gchar *origin;
	guint *perc;
	guint i;
	g_autoptr(GHashTable) origins = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	ptask = as_profile_start_literal (plugin->profile, "appstream::startup");
	g_mutex_lock (&plugin->priv->store_mutex);

	/* clear all existing applications if the store was invalidated */
	as_store_remove_all (plugin->priv->store);

	/* Parse the XML */
	if (g_getenv ("GNOME_SOFTWARE_PREFER_LOCAL") != NULL) {
		as_store_set_add_flags (plugin->priv->store,
					AS_STORE_ADD_FLAG_PREFER_LOCAL);
	}
	ret = as_store_load (plugin->priv->store,
			     AS_STORE_LOAD_FLAG_APP_INFO_SYSTEM |
			     AS_STORE_LOAD_FLAG_APP_INFO_USER |
			     AS_STORE_LOAD_FLAG_APPDATA |
			     AS_STORE_LOAD_FLAG_DESKTOP |
			     AS_STORE_LOAD_FLAG_APP_INSTALL,
			     NULL,
			     error);
	if (!ret)
		goto out;
	items = as_store_get_apps (plugin->priv->store);
	if (items->len == 0) {
		g_warning ("No AppStream data, try 'make install-sample-data' in data/");
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     _("No AppStream data found"));
		goto out;
	}

	/* watch for changes */
	g_signal_connect (plugin->priv->store, "changed",
			  G_CALLBACK (gs_plugin_appstream_store_changed_cb),
			  plugin);

	/* add search terms for apps not in the main source */
	origins = gs_plugin_appstream_get_origins_hash (items);
	for (i = 0; i < items->len; i++) {
		app = g_ptr_array_index (items, i);
		origin = as_app_get_origin (app);
		if (origin == NULL)
			continue;
		perc = g_hash_table_lookup (origins, origin);
		if (*perc < 10) {
			g_debug ("Adding keyword '%s' to %s",
				 origin, as_app_get_id (app));
			as_app_add_keyword (app, NULL, origin);
		}
	}
out:
	g_mutex_unlock (&plugin->priv->store_mutex);
	return ret;
}

/**
 * gs_plugin_refine_item:
 */
static gboolean
gs_plugin_refine_item (GsPlugin *plugin,
		       GsApp *app,
		       AsApp *item,
		       GError **error)
{
	return gs_appstream_refine_app (plugin, app, item, error);
}

/**
 * gs_plugin_refine_from_id:
 */
static gboolean
gs_plugin_refine_from_id (GsPlugin *plugin,
			  GsApp *app,
			  gboolean *found,
			  GError **error)
{
	const gchar *id;
	gboolean ret = TRUE;
	AsApp *item = NULL;

	g_mutex_lock (&plugin->priv->store_mutex);

	/* find anything that matches the ID */
	id = gs_app_get_id (app);
	if (id == NULL)
		goto out;
	item = as_store_get_app_by_id (plugin->priv->store, id);
	if (item == NULL)
		goto out;

	/* set new properties */
	ret = gs_plugin_refine_item (plugin, app, item, error);
	if (!ret)
		goto out;
out:
	g_mutex_unlock (&plugin->priv->store_mutex);
	*found = (item != NULL);
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
	AsApp *item = NULL;
	GPtrArray *sources;
	gboolean ret = TRUE;
	const gchar *pkgname;
	guint i;

	g_mutex_lock (&plugin->priv->store_mutex);

	/* find anything that matches the ID */
	sources = gs_app_get_sources (app);
	for (i = 0; i < sources->len && item == NULL; i++) {
		pkgname = g_ptr_array_index (sources, i);
		item = as_store_get_app_by_pkgname (plugin->priv->store,
						    pkgname);
		if (item == NULL)
			g_debug ("no AppStream match for {pkgname} %s", pkgname);
	}

	/* nothing found */
	if (item == NULL)
		goto out;

	/* set new properties */
	ret = gs_plugin_refine_item (plugin, app, item, error);
out:
	g_mutex_unlock (&plugin->priv->store_mutex);
	return ret;
}

/**
 * gs_plugin_add_distro_upgrades:
 */
gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
			       GList **list,
			       GCancellable *cancellable,
			       GError **error)
{
	AsApp *item;
	GPtrArray *array;
	gboolean ret = TRUE;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	/* find any upgrades */
	g_mutex_lock (&plugin->priv->store_mutex);
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		item = g_ptr_array_index (array, i);

		// FIXME: AS_ID_KIND_DISTRO_UPGRADE
		if (as_app_get_id_kind (item) != AS_ID_KIND_UNKNOWN)
			continue;
		if (as_app_get_metadata_item (item, "X-IsUpgrade") == NULL)
			continue;

		/* create */
		app = gs_app_new (as_app_get_id (item));
		gs_app_set_kind (app, GS_APP_KIND_DISTRO_UPGRADE);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		ret = gs_plugin_refine_item (plugin, app, item, error);
		if (!ret)
			goto out;
		gs_plugin_add_app (list, app);
	}
out:
	g_mutex_unlock (&plugin->priv->store_mutex);
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
	gboolean ret;
	gboolean found;
	GList *l;
	GsApp *app;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	ptask = as_profile_start_literal (plugin->profile, "appstream::refine");
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (!gs_plugin_refine_from_id (plugin, app, &found, error))
			return FALSE;
		if (!found) {
			if (!gs_plugin_refine_from_pkgname (plugin, app, error))
				return FALSE;
		}
	}

	/* sucess */
	return TRUE;
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
	AsApp *item;
	GsCategory *parent;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	/* get the two search terms */
	ptask = as_profile_start_literal (plugin->profile, "appstream::add-category-apps");
	g_mutex_lock (&plugin->priv->store_mutex);
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
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		item = g_ptr_array_index (array, i);
		if (as_app_get_id (item) == NULL)
			continue;
		if (!as_app_has_category (item, search_id1))
			continue;
		if (search_id2 != NULL && !as_app_has_category (item, search_id2))
			continue;

		/* got a search match, so add all the data we can */
		app = gs_app_new (as_app_get_id (item));
		ret = gs_plugin_refine_item (plugin, app, item, error);
		if (!ret)
			goto out;
		gs_plugin_add_app (list, app);
	}
out:
	g_mutex_unlock (&plugin->priv->store_mutex);
	return ret;
}

/**
 * gs_plugin_add_search_item_add:
 */
static gboolean
gs_plugin_add_search_item_add (GsPlugin *plugin,
			       GList **list,
			       AsApp *item,
			       guint match_value,
			       GError **error)
{
	g_autoptr(GsApp) app = NULL;
	app = gs_app_new (as_app_get_id (item));
	if (!gs_plugin_refine_item (plugin, app, item, error))
		return FALSE;
	gs_app_set_search_sort_key (app, match_value);
	gs_plugin_add_app (list, app);
	return TRUE;
}

/**
 * gs_plugin_add_search_item:
 */
static gboolean
gs_plugin_add_search_item (GsPlugin *plugin,
			   GList **list,
			   AsApp *app,
			   gchar **values,
			   GCancellable *cancellable,
			   GError **error)
{
	AsApp *item;
	GPtrArray *extends;
	const gchar *id;
	gboolean ret = TRUE;
	guint i;
	guint match_value;

	/* no match */
	match_value = as_app_search_matches_all (app, values);
	if (match_value == 0)
		goto out;

	/* if the app does not extend an application, then just add it */
	extends = as_app_get_extends (app);
	if (extends->len == 0) {
		ret = gs_plugin_add_search_item_add (plugin,
						     list,
						     app,
						     match_value,
						     error);
		goto out;
	}

	/* add the thing that we extend, not the addon itself */
	for (i = 0; i < extends->len; i++) {
		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			goto out;

		id = g_ptr_array_index (extends, i);
		item = as_store_get_app_by_id (plugin->priv->store, id);
		if (item == NULL)
			continue;
		ret = gs_plugin_add_search_item_add (plugin,
						     list,
						     item,
						     match_value,
						     error);
		if (!ret)
			goto out;
	}
out:
	return ret;
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
	AsApp *item;
	GPtrArray *array;
	gboolean ret = TRUE;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	/* search categories for the search term */
	ptask = as_profile_start_literal (plugin->profile, "appstream::search");
	g_mutex_lock (&plugin->priv->store_mutex);
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			goto out;

		item = g_ptr_array_index (array, i);
		ret = gs_plugin_add_search_item (plugin, list, item, values, cancellable, error);
		if (!ret)
			goto out;
	}
out:
	g_mutex_unlock (&plugin->priv->store_mutex);
	return ret;
}

/**
 * gs_plugin_add_installed:
 */
gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GList **list,
			 GCancellable *cancellable,
			 GError **error)
{
	AsApp *item;
	gboolean ret = TRUE;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	/* search categories for the search term */
	ptask = as_profile_start_literal (plugin->profile, "appstream::add_installed");
	g_mutex_lock (&plugin->priv->store_mutex);
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (as_app_get_source_kind (item) == AS_APP_SOURCE_KIND_APPDATA ||
		    as_app_get_source_kind (item) == AS_APP_SOURCE_KIND_DESKTOP) {
			g_autoptr(GsApp) app = NULL;
			app = gs_app_new (as_app_get_id (item));
			ret = gs_plugin_refine_item (plugin, app, item, error);
			if (!ret)
				goto out;
			gs_plugin_add_app (list, app);
		}
	}
out:
	g_mutex_unlock (&plugin->priv->store_mutex);
	return ret;
}

/**
 * gs_plugin_add_categories_for_app:
 */
static void
gs_plugin_add_categories_for_app (GList *list, AsApp *app)
{
	GList *l;
	GList *l2;
	GsCategory *category;
	GsCategory *parent;
	gboolean found_subcat;

	/* does it match the main category */
	for (l = list; l != NULL; l = l->next) {
		g_autoptr(GList) children = NULL;
		parent = GS_CATEGORY (l->data);
		if (!as_app_has_category (app, gs_category_get_id (parent)))
			continue;
		gs_category_increment_size (parent);

		/* does it match any sub-categories */
		found_subcat = FALSE;
		children = gs_category_get_subcategories (parent);
		for (l2 = children; l2 != NULL; l2 = l2->next) {
			category = GS_CATEGORY (l2->data);
			if (!as_app_has_category (app, gs_category_get_id (category)))
				continue;
			gs_category_increment_size (category);
			found_subcat = TRUE;
		}

		/* matching the main category but no subcategories means we have
		 * to create a new 'Other' subcategory manually */
		if (!found_subcat) {
			category = gs_category_find_child (parent, "other");
			if (category == NULL) {
				category = gs_category_new (parent, "other", NULL);
				gs_category_add_subcategory (parent, category);
				g_object_unref (category);
			}
			as_app_add_category (app, gs_category_get_id (category));
			gs_category_increment_size (category);
		}
	}
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
	AsApp *app;
	GPtrArray *array;
	gboolean ret = TRUE;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	/* find out how many packages are in each category */
	ptask = as_profile_start_literal (plugin->profile, "appstream::add-categories");
	g_mutex_lock (&plugin->priv->store_mutex);
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		app = g_ptr_array_index (array, i);
		if (as_app_get_id (app) == NULL)
			continue;
		if (as_app_get_priority (app) < 0)
			continue;
		gs_plugin_add_categories_for_app (*list, app);
	}
	g_mutex_unlock (&plugin->priv->store_mutex);
	return ret;
}
