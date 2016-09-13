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

#include <gnome-software.h>

#include "gs-appstream.h"

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

struct GsPluginData {
	AsStore			*store;
	GHashTable		*app_hash_old;
};

#define GS_PLUGIN_NUMBER_CHANGED_RELOAD	10

static GHashTable *
gs_plugin_appstream_create_app_hash (AsStore *store)
{
	GHashTable *hash;
	GPtrArray *apps;
	guint i;

	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, (GDestroyNotify) g_object_unref);
	apps = as_store_get_apps (store);
	for (i = 0; i < apps->len; i++) {
		AsApp *app = g_ptr_array_index (apps, i);
		gchar *key = g_strdup (as_app_get_id (app));
		g_hash_table_insert (hash, key, g_object_ref (app));
	}
	return hash;
}

static void
gs_plugin_detect_reload_apps (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *item;
	GsApp *app;
	GList *l;
	guint cnt = 0;
	g_autoptr(GHashTable) app_hash = NULL;
	g_autoptr(GList) keys = NULL;
	g_autoptr(GList) keys_old = NULL;

	/* find packages that have been added */
	app_hash = gs_plugin_appstream_create_app_hash (priv->store);
	keys = g_hash_table_get_keys (app_hash);
	for (l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		item = g_hash_table_lookup (priv->app_hash_old, key);
		if (item == NULL) {
			item = g_hash_table_lookup (app_hash, key);
			app = gs_plugin_cache_lookup (plugin,
						      as_app_get_id (item));
			if (app != NULL)
				g_debug ("added GsApp %s", gs_app_get_id (app));
			cnt++;
		}
	}

	/* find packages that have been removed */
	keys_old = g_hash_table_get_keys (priv->app_hash_old);
	for (l = keys_old; l != NULL; l = l->next) {
		const gchar *key = l->data;
		item = g_hash_table_lookup (app_hash, key);
		if (item == NULL) {
			item = g_hash_table_lookup (priv->app_hash_old, key);
			app = gs_plugin_cache_lookup (plugin,
						      as_app_get_id (item));
			if (app != NULL)
				g_debug ("removed GsApp %s", gs_app_get_id (app));
			cnt++;
		}
	}

	/* replace if any changes */
	if (cnt > 0) {
		if (priv->app_hash_old != NULL)
			g_hash_table_unref (priv->app_hash_old);
		priv->app_hash_old = g_hash_table_ref (app_hash);
	}

	/* invalidate all if a large number of apps changed */
	if (cnt > GS_PLUGIN_NUMBER_CHANGED_RELOAD) {
		g_debug ("%u is more than %i AsApps changed",
			 cnt, GS_PLUGIN_NUMBER_CHANGED_RELOAD);
		gs_plugin_reload (plugin);
	}
}

static void
gs_plugin_appstream_store_changed_cb (AsStore *store, GsPlugin *plugin)
{
	g_debug ("AppStream metadata changed");

	/* send ::reload-apps */
	gs_plugin_detect_reload_apps (plugin);

	/* all the UI is reloaded as something external has happened */
	if (!gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_RUNNING_OTHER))
		gs_plugin_reload (plugin);
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->store = as_store_new ();
	as_store_set_add_flags (priv->store,
				AS_STORE_ADD_FLAG_USE_UNIQUE_ID |
				AS_STORE_ADD_FLAG_USE_MERGE_HEURISTIC);
	as_store_set_watch_flags (priv->store,
				  AS_STORE_WATCH_FLAG_ADDED |
				  AS_STORE_WATCH_FLAG_REMOVED);

	/* set plugin flags */
	gs_plugin_add_flags (plugin, GS_PLUGIN_FLAGS_GLOBAL_CACHE);

	/* need package name */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "dpkg");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_hash_table_unref (priv->app_hash_old);
	g_object_unref (priv->store);
}

/*
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
		if (tmp == NULL || tmp[0] == '\0')
			continue;
		cnt = g_hash_table_lookup (origins, tmp);
		perc = (100.f / (gdouble) array->len) * (gdouble) (*cnt);
		g_debug ("origin %s provides %u apps (%.0f%%)", tmp, *cnt, perc);
		*cnt = (guint) perc;
	}

	return origins;
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *app;
	GPtrArray *items;
	gboolean ret;
	const gchar *origin;
	const gchar *test_xml;
	const gchar *test_icon_root;
	guint *perc;
	guint i;
	g_autoptr(GHashTable) origins = NULL;

	/* Parse the XML */
	if (g_getenv ("GNOME_SOFTWARE_PREFER_LOCAL") != NULL) {
		as_store_set_add_flags (priv->store,
					AS_STORE_ADD_FLAG_PREFER_LOCAL);
	}

	/* only when in self test */
	test_xml = g_getenv ("GS_SELF_TEST_APPSTREAM_XML");
	if (test_xml != NULL) {
		test_icon_root = g_getenv ("GS_SELF_TEST_APPSTREAM_ICON_ROOT");
		g_debug ("using self test data of %s... with icon root %s",
			 test_xml, test_icon_root);
		if (!as_store_from_xml (priv->store, test_xml, test_icon_root, error))
			return FALSE;
	} else {
		ret = as_store_load (priv->store,
				     AS_STORE_LOAD_FLAG_IGNORE_INVALID |
				     AS_STORE_LOAD_FLAG_APP_INFO_SYSTEM |
				     AS_STORE_LOAD_FLAG_APP_INFO_USER |
				     AS_STORE_LOAD_FLAG_APPDATA |
				     AS_STORE_LOAD_FLAG_DESKTOP |
				     AS_STORE_LOAD_FLAG_APP_INSTALL,
				     cancellable,
				     error);
		if (!ret) {
			gs_utils_error_convert_appstream (error);
			return FALSE;
		}
	}
	items = as_store_get_apps (priv->store);
	if (items->len == 0) {
		g_warning ("No AppStream data, try 'make install-sample-data' in data/");
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "No AppStream data found");
		return FALSE;
	}

	/* prime the cache */
	priv->app_hash_old = gs_plugin_appstream_create_app_hash (priv->store);

	/* watch for changes */
	g_signal_connect (priv->store, "changed",
			  G_CALLBACK (gs_plugin_appstream_store_changed_cb),
			  plugin);

	/* add search terms for apps not in the main source */
	origins = gs_plugin_appstream_get_origins_hash (items);
	for (i = 0; i < items->len; i++) {
		app = g_ptr_array_index (items, i);
		origin = as_app_get_origin (app);
		if (origin == NULL || origin[0] == '\0')
			continue;
		perc = g_hash_table_lookup (origins, origin);
		if (*perc < 10) {
			g_debug ("Adding keyword '%s' to %s",
				 origin, as_app_get_id (app));
			as_app_add_keyword (app, NULL, origin);
		}
	}

	/* add keyword for non-package sources */
	for (i = 0; i < items->len; i++) {
		AsBundle *bundle;
		app = g_ptr_array_index (items, i);
		bundle = as_app_get_bundle_default (app);
		if (bundle == NULL)
			continue;
		g_debug ("Adding keyword '%s' to %s",
			 as_bundle_kind_to_string (as_bundle_get_kind (bundle)),
			 as_app_get_unique_id (app));
		as_app_add_keyword (app, NULL,
				    as_bundle_kind_to_string (as_bundle_get_kind (bundle)));
	}

	/* fix up these */
	for (i = 0; i < items->len; i++) {
		app = g_ptr_array_index (items, i);
		if (as_app_get_kind (app) == AS_APP_KIND_LOCALIZATION &&
		    g_str_has_prefix (as_app_get_id (app),
				      "org.fedoraproject.LangPack-")) {
			g_autoptr(AsIcon) icon = NULL;

			/* add icon */
			icon = as_icon_new ();
			as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
			as_icon_set_name (icon, "accessories-dictionary-symbolic");
			as_app_add_icon (app, icon);

			/* add categories */
			as_app_add_category (app, "Addons");
			as_app_add_category (app, "Localization");
		}
	}

	/* fix up drivers with our nonstandard groups */
	for (i = 0; i < items->len; i++) {
		app = g_ptr_array_index (items, i);
		if (as_app_get_kind (app) == AS_APP_KIND_DRIVER) {
			as_app_add_category (app, "Addons");
			as_app_add_category (app, "Drivers");
		}
	}

	/* rely on the store keeping itself updated */
	return TRUE;
}

static gboolean
gs_plugin_refine_from_id (GsPlugin *plugin,
			  GsApp *app,
			  gboolean *found,
			  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *unique_id;
	AsApp *item;

	/* unfound */
	*found = FALSE;

	/* find anything that matches the ID */
	unique_id = gs_app_get_unique_id (app);
	if (unique_id == NULL)
		return TRUE;

	/* nothing found */
	g_debug ("searching appstream for %s", unique_id);
	item = as_store_get_app_by_unique_id (priv->store, unique_id,
					      AS_STORE_SEARCH_FLAG_USE_WILDCARDS);
	if (item == NULL) {
		guint i;
		GPtrArray *apps;
		g_debug ("no app with ID %s found in appstream", unique_id);
		apps = as_store_get_apps (priv->store);
		for (i = 0; i < apps->len; i++) {
			item = g_ptr_array_index (apps, i);
			if (g_strcmp0 (as_app_get_id (item), gs_app_get_id (app)) != 0)
				continue;
			g_debug ("possible match: %s",
				 as_app_get_unique_id (item));
		}
		return TRUE;
	}

	/* set new properties */
	if (!gs_appstream_refine_app (plugin, app, item, error))
		return FALSE;

	*found = TRUE;
	return TRUE;
}

static gboolean
gs_plugin_refine_from_pkgname (GsPlugin *plugin,
			       GsApp *app,
			       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *item = NULL;
	GPtrArray *sources;
	const gchar *pkgname;
	guint i;

	/* find anything that matches the ID */
	sources = gs_app_get_sources (app);
	for (i = 0; i < sources->len && item == NULL; i++) {
		pkgname = g_ptr_array_index (sources, i);
		item = as_store_get_app_by_pkgname (priv->store,
						    pkgname);
		if (item == NULL)
			g_debug ("no AppStream match for {pkgname} %s", pkgname);
	}

	/* nothing found */
	if (item == NULL)
		return TRUE;

	/* set new properties */
	return gs_appstream_refine_app (plugin, app, item, error);
}

gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
			       GsAppList *list,
			       GCancellable *cancellable,
			       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *item;
	GPtrArray *array;
	guint i;

	/* find any upgrades */
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		item = g_ptr_array_index (array, i);
		if (as_app_get_kind (item) != AS_APP_KIND_OS_UPDATE)
			continue;

		/* create */
		app = gs_appstream_create_app (plugin, item);
		gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		if (!gs_appstream_refine_app (plugin, app, item, error))
			return FALSE;
		gs_app_list_add (list, app);
	}
	return TRUE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	gboolean found = FALSE;

	/* handled already */
	if (gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX))
		return TRUE;

	/* find by ID then package name */
	if (!gs_plugin_refine_from_id (plugin, app, &found, error))
		return FALSE;
	if (!found) {
		if (!gs_plugin_refine_from_pkgname (plugin, app, error))
			return FALSE;
	}

	/* sucess */
	return TRUE;
}

static gboolean
gs_plugin_appstream_add_wildcards (GsPlugin *plugin,
				   GsAppList *list,
				   GsApp *app,
				   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *id;
	guint i;
	g_autoptr(GPtrArray) items = NULL;

	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	/* find all apps when matching any prefixes */
	items = as_store_get_apps_by_id (priv->store, id);
	for (i = 0; i < items->len; i++) {
		AsApp *item = NULL;
		g_autoptr(GsApp) new = NULL;

		/* does the app have an installation method */
		item = g_ptr_array_index (items, i);
		if (as_app_get_pkgname_default (item) == NULL &&
		    as_app_get_bundle_default (item) == NULL) {
			g_debug ("not using %s for wildcard as "
				 "no bundle or pkgname",
				 as_app_get_id (item));
			continue;
		}

		/* new app */
		g_debug ("found %s for wildcard %s",
			 as_app_get_id (item), id);
		new = gs_appstream_create_app (plugin, item);
		if (!gs_appstream_refine_app (plugin, new, item, error))
			return FALSE;
		gs_app_list_add (list, new);
	}
	return TRUE;
}

/* wildcard results get added to the list, not replaced */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GsAppList *list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	guint i;

	for (i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX))
			continue;
		if (!gs_plugin_appstream_add_wildcards (plugin,
							list,
							app,
							error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
_as_app_matches_desktop_group_set (AsApp *app, gchar **desktop_groups)
{
	guint i;
	for (i = 0; desktop_groups[i] != NULL; i++) {
		if (!as_app_has_category (app, desktop_groups[i]))
			return FALSE;
	}
	return TRUE;
}

static gboolean
_as_app_matches_desktop_group (AsApp *app, const gchar *desktop_group)
{
	g_auto(GStrv) split = g_strsplit (desktop_group, "::", -1);
	return _as_app_matches_desktop_group_set (app, split);
}

gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GPtrArray *array;
	GPtrArray *desktop_groups;
	guint i;
	guint j;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* just look at each app in turn */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::add-category-apps");
	g_assert (ptask != NULL);
	array = as_store_get_apps (priv->store);
	desktop_groups = gs_category_get_desktop_groups (category);
	if (desktop_groups->len == 0) {
		g_warning ("no desktop_groups for %s", gs_category_get_id (category));
		return TRUE;
	}
	for (j = 0; j < desktop_groups->len; j++) {
		const gchar *desktop_group = g_ptr_array_index (desktop_groups, j);
		g_auto(GStrv) split = g_strsplit (desktop_group, "::", -1);

		/* match the app */
		for (i = 0; i < array->len; i++) {
			AsApp *item;
			g_autoptr(GsApp) app = NULL;

			/* no ID is invalid */
			item = g_ptr_array_index (array, i);
			if (as_app_get_id (item) == NULL)
				continue;

			/* match all the desktop groups */
			if (!_as_app_matches_desktop_group_set (item, split))
				continue;

			/* add all the data we can */
			app = gs_appstream_create_app (plugin, item);
			if (!gs_appstream_refine_app (plugin, app, item, error))
				return FALSE;
			gs_app_list_add (list, app);
		}
	}
	return TRUE;
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_appstream_store_search (plugin,
					  priv->store,
					  values,
					  list,
					  cancellable,
					  error);
}

gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *item;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* search categories for the search term */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::add_installed");
	g_assert (ptask != NULL);
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (as_app_get_state (item) == AS_APP_STATE_INSTALLED) {
			g_autoptr(GsApp) app = NULL;
			app = gs_appstream_create_app (plugin, item);
			if (!gs_appstream_refine_app (plugin, app, item, error))
				return FALSE;
			gs_app_list_add (list, app);
		}
	}
	return TRUE;
}

static void
gs_plugin_add_categories_for_app (GsCategory *parent, AsApp *app)
{
	GPtrArray *children;
	GPtrArray *desktop_groups;
	GsCategory *category;
	guint i, j;

	/* find all the sub-categories */
	children = gs_category_get_children (parent);
	for (j = 0; j < children->len; j++) {
		gboolean matched = FALSE;
		category = GS_CATEGORY (g_ptr_array_index (children, j));

		/* do any desktop_groups match this application */
		desktop_groups = gs_category_get_desktop_groups (category);
		for (i = 0; i < desktop_groups->len; i++) {
			const gchar *desktop_group = g_ptr_array_index (desktop_groups, i);
			if (_as_app_matches_desktop_group (app, desktop_group)) {
				matched = TRUE;
				break;
			}
		}
		if (matched) {
			gs_category_increment_size (category);
			gs_category_increment_size (parent);
		}
	}
}

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *app;
	GPtrArray *array;
	guint i;
	guint j;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* find out how many packages are in each category */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::add-categories");
	g_assert (ptask != NULL);
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		app = g_ptr_array_index (array, i);
		if (as_app_get_id (app) == NULL)
			continue;
		if (as_app_get_priority (app) < 0)
			continue;
		for (j = 0; j < list->len; j++) {
			GsCategory *parent = GS_CATEGORY (g_ptr_array_index (list, j));
			gs_plugin_add_categories_for_app (parent, app);
		}
	}
	return TRUE;
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *item;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* find out how many packages are in each category */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::add-popular");
	g_assert (ptask != NULL);
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		item = g_ptr_array_index (array, i);
		if (as_app_get_id (item) == NULL)
			continue;
		if (!as_app_has_kudo (item, "GnomeSoftware::popular"))
			continue;
		app = gs_appstream_create_app (plugin, item);
		gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
		gs_app_list_add (list, app);
	}
	return TRUE;
}

gboolean
gs_plugin_add_featured (GsPlugin *plugin,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *item;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* find out how many packages are in each category */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::add-featured");
	g_assert (ptask != NULL);
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		item = g_ptr_array_index (array, i);
		if (as_app_get_id (item) == NULL)
			continue;
		if (as_app_get_metadata_item (item, "GnomeSoftware::FeatureTile-css") == NULL)
			continue;
		app = gs_appstream_create_app (plugin, item);
		if (!gs_appstream_refine_app (plugin, app, item, error))
			return FALSE;
		gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
		gs_app_list_add (list, app);
	}
	return TRUE;
}
