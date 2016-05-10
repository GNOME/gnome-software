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
};

/**
 * gs_plugin_appstream_store_changed_cb:
 */
static void
gs_plugin_appstream_store_changed_cb (AsStore *store, GsPlugin *plugin)
{
	g_debug ("AppStream metadata changed");

	/* this is not strictly true, but it causes all the UI to be reloaded
	 * which is what we really want */
	if (!gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_RUNNING_OTHER))
		gs_plugin_updates_changed (plugin);
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->store = as_store_new ();
	as_store_set_watch_flags (priv->store,
				  AS_STORE_WATCH_FLAG_ADDED |
				  AS_STORE_WATCH_FLAG_REMOVED);
}

/**
 * gs_plugin_order_after:
 */
const gchar **
gs_plugin_order_after (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"menu-spec-categories",	/* need category list */
		"dpkg",			/* need package name */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->store);
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
 * gs_plugin_setup:
 */
gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *app;
	GPtrArray *items;
	gboolean ret;
	const gchar *origin;
	const gchar *tmp;
	guint *perc;
	guint i;
	g_autoptr(GHashTable) origins = NULL;

	/* Parse the XML */
	if (g_getenv ("GNOME_SOFTWARE_PREFER_LOCAL") != NULL) {
		as_store_set_add_flags (priv->store,
					AS_STORE_ADD_FLAG_PREFER_LOCAL);
	}

	/* only when in self test */
	tmp = g_getenv ("GS_SELF_TEST_APPSTREAM_XML");
	if (tmp != NULL) {
		g_debug ("using self test data of %s", tmp);
		if (!as_store_from_xml (priv->store, tmp, NULL, error))
			return FALSE;
	} else {
		ret = as_store_load (priv->store,
				     AS_STORE_LOAD_FLAG_APP_INFO_SYSTEM |
				     AS_STORE_LOAD_FLAG_APP_INFO_USER |
				     AS_STORE_LOAD_FLAG_APPDATA |
				     AS_STORE_LOAD_FLAG_DESKTOP |
				     AS_STORE_LOAD_FLAG_XDG_APP_USER |
				     AS_STORE_LOAD_FLAG_APP_INSTALL,
				     NULL,
				     error);
		if (!ret)
			return FALSE;
	}
	items = as_store_get_apps (priv->store);
	if (items->len == 0) {
		g_warning ("No AppStream data, try 'make install-sample-data' in data/");
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "No AppStream data found");
		return FALSE;
	}

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

	/* rely on the store keeping itself updated */
	return TRUE;
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *id;
	guint i;
	AsApp *item = NULL;

	/* unfound */
	*found = FALSE;

	/* find anything that matches the ID */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	/* find the best app when matching any prefixes */
	if (gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX)) {
		g_autoptr(GPtrArray) items = NULL;
		items = as_store_get_apps_by_id (priv->store, id);
		for (i = 0; i < items->len; i++) {
			AsApp *item_tmp = NULL;

			/* does the app have an installation method */
			item_tmp = g_ptr_array_index (items, i);
			if (as_app_get_pkgname_default (item_tmp) == NULL &&
			    as_app_get_bundle_default (item_tmp) == NULL) {
				g_debug ("not using %s for wildcard as "
					 "no bundle or pkgname",
					 as_app_get_id (item_tmp));
				continue;
			}

			/* we could match more than one, so list all and return
			 * the last entry -- fingers crossed it's only one... */
			g_debug ("found %s for wildcard %s",
				 as_app_get_id (item_tmp), id);
			item = item_tmp;
		}
	} else {
		item = as_store_get_app_by_id (priv->store, id);
	}

	/* nothing found */
	if (item == NULL)
		return TRUE;

	/* set new properties */
	if (!gs_appstream_refine_app (plugin, app, item, error))
		return FALSE;

	*found = TRUE;
	return TRUE;
}

/**
 * gs_plugin_refine_from_pkgname:
 */
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

/**
 * gs_plugin_add_distro_upgrades:
 */
gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
			       GList **list,
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
		app = gs_app_new (as_app_get_id (item));
		gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		if (!gs_appstream_refine_app (plugin, app, item, error))
			return FALSE;
		gs_app_list_add (list, app);
	}
	return TRUE;
}

/**
 * gs_plugin_refine_app:
 */
gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	gboolean found = FALSE;

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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *search_id1;
	const gchar *search_id2 = NULL;
	AsApp *item;
	GsCategory *parent;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* get the two search terms */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::add-category-apps");
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
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		item = g_ptr_array_index (array, i);
		if (as_app_get_id (item) == NULL)
			continue;
		if (g_strcmp0 (search_id1, "all") != 0 &&
		    !as_app_has_category (item, search_id1))
			continue;
		if (search_id2 != NULL && !as_app_has_category (item, search_id2))
			continue;

		/* got a search match, so add all the data we can */
		app = gs_app_new (as_app_get_id (item));
		if (!gs_appstream_refine_app (plugin, app, item, error))
			return FALSE;
		gs_app_list_add (list, app);
	}
	return TRUE;
}

/**
 * gs_plugin_add_search_item:
 */
static gboolean
gs_plugin_add_search_item (GsPlugin *plugin,
			   GList **list,
			   AsApp *item,
			   gchar **values,
			   GCancellable *cancellable,
			   GError **error)
{
	AsApp *item_tmp;
	GPtrArray *addons;
	guint i;
	guint match_value;
	g_autoptr(GsApp) app = NULL;

	/* match against the app or any of the addons */
	match_value = as_app_search_matches_all (item, values);
	addons = as_app_get_addons (item);
	for (i = 0; i < addons->len; i++) {
		item_tmp = g_ptr_array_index (addons, i);
		match_value |= as_app_search_matches_all (item_tmp, values);
	}

	/* no match */
	if (match_value == 0)
		return TRUE;

	/* create app */
	app = gs_app_new (as_app_get_id (item));
	if (!gs_appstream_refine_app (plugin, app, item, error))
		return FALSE;
	gs_app_set_match_value (app, match_value);
	gs_app_list_add (list, app);
	return TRUE;
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *item;
	GPtrArray *array;
	gboolean ret = TRUE;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* search categories for the search term */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::search");
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			return FALSE;

		item = g_ptr_array_index (array, i);
		ret = gs_plugin_add_search_item (plugin, list, item, values, cancellable, error);
		if (!ret)
			return FALSE;
	}
	return TRUE;
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *item;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* search categories for the search term */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::add_installed");
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (as_app_get_state (item) == AS_APP_STATE_INSTALLED) {
			g_autoptr(GsApp) app = NULL;
			app = gs_app_new (as_app_get_id (item));
			if (!gs_appstream_refine_app (plugin, app, item, error))
				return FALSE;
			gs_app_list_add (list, app);
		}
	}
	return TRUE;
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *app;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* find out how many packages are in each category */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::add-categories");
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		app = g_ptr_array_index (array, i);
		if (as_app_get_id (app) == NULL)
			continue;
		if (as_app_get_priority (app) < 0)
			continue;
		gs_plugin_add_categories_for_app (*list, app);
	}
	return TRUE;
}

/**
 * gs_plugin_add_popular:
 */
gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GList **list,
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
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		item = g_ptr_array_index (array, i);
		if (as_app_get_id (item) == NULL)
			continue;
		if (!as_app_has_kudo (item, "GnomeSoftware::popular"))
			continue;
		app = gs_app_new (as_app_get_id_no_prefix (item));
		gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
		gs_app_list_add (list, app);
	}
	return TRUE;
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *item;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* find out how many packages are in each category */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "appstream::add-featured");
	array = as_store_get_apps (priv->store);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		item = g_ptr_array_index (array, i);
		if (as_app_get_id (item) == NULL)
			continue;
		if (as_app_get_metadata_item (item, "GnomeSoftware::FeatureTile-css") == NULL)
			continue;
		app = gs_app_new (as_app_get_id_no_prefix (item));
		gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
		gs_app_list_add (list, app);
	}
	return TRUE;
}
