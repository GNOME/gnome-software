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
#include <locale.h>
#include <appstream-glib.h>

#include <gs-plugin.h>
#include <gs-plugin-loader.h>

#define	GS_PLUGIN_APPSTREAM_MAX_SCREENSHOTS	5

struct GsPluginPrivate {
	AsStore			*store;
	gchar			*locale;
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
 * as_app_has_compulsory_for_desktop:
 */
static gboolean
as_app_has_compulsory_for_desktop (AsApp *app, const gchar *compulsory_for_desktop)
{
	GPtrArray *compulsory_for_desktops;
	const gchar *tmp;
	guint i;

	compulsory_for_desktops = as_app_get_compulsory_for_desktops (app);
	for (i = 0; i < compulsory_for_desktops->len; i++) {
		tmp = g_ptr_array_index (compulsory_for_desktops, i);
		if (g_strcmp0 (tmp, compulsory_for_desktop) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * gs_plugin_appstream_store_changed_cb:
 */
static void
gs_plugin_appstream_store_changed_cb (AsStore *store, GsPlugin *plugin)
{
	g_debug ("AppStream metadata changed, reloading cache");
	plugin->priv->done_init = FALSE;
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->store = as_store_new ();
	g_signal_connect (plugin->priv->store, "changed",
			  G_CALLBACK (gs_plugin_appstream_store_changed_cb),
			  plugin);

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
		"hardcoded-categories",	/* need category list */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_free (plugin->priv->locale);
	g_object_unref (plugin->priv->store);
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	AsApp *app;
	GPtrArray *items;
	gboolean ret;
	gchar *tmp;
	guint i;

	/* get the locale without the UTF-8 suffix */
	plugin->priv->locale = g_strdup (setlocale (LC_MESSAGES, NULL));
	tmp = g_strstr_len (plugin->priv->locale, -1, ".UTF-8");
	if (tmp != NULL)
		*tmp = '\0';

	/* Parse the XML */
	gs_profile_start (plugin->profile, "appstream::startup");
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
			     cancellable,
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

	/* add icons to the icon name cache */
	for (i = 0; i < items->len; i++) {
		app = g_ptr_array_index (items, i);
		if (as_app_get_icon_kind (app) != AS_ICON_KIND_CACHED)
			continue;
		g_hash_table_insert (plugin->icon_cache,
				     g_strdup (as_app_get_id (app)),
				     g_build_filename (as_app_get_icon_path (app),
						       as_app_get_icon (app),
						       NULL));
	}
out:
	gs_profile_stop (plugin->profile, "appstream::startup");
	return ret;
}

/**
 * gs_plugin_refine_item_pixbuf:
 */
static void
gs_plugin_refine_item_pixbuf (GsPlugin *plugin, GsApp *app, AsApp *item)
{
	GError *error = NULL;
	const gchar *icon;
	const gchar *icon_dir;
	gboolean ret;
	gchar *icon_path = NULL;

	icon = as_app_get_icon (item);
	switch (as_app_get_icon_kind (item)) {
	case AS_ICON_KIND_REMOTE:
		gs_app_set_icon (app, icon);
		break;
	case AS_ICON_KIND_STOCK:
		gs_app_set_icon (app, icon);
		ret = gs_app_load_icon (app, &error);
		if (!ret) {
			g_warning ("failed to load stock icon %s: %s",
				   icon, error->message);
			g_error_free (error);
			goto out;
		}
		break;
	case AS_ICON_KIND_CACHED:

		/* we assume <icon type="local">gnome-chess.png</icon> */
		icon_dir = as_app_get_icon_path (item);
		icon_path = g_build_filename (icon_dir, icon, NULL);
		gs_app_set_icon (app, icon_path);
		ret = gs_app_load_icon (app, &error);
		if (!ret) {
			g_warning ("failed to load cached icon %s: %s",
				   icon, error->message);
			g_error_free (error);
			goto out;
		}
		break;
	default:
		g_warning ("icon kind unknown for %s", as_app_get_id_full (item));
		break;
	}
out:
	g_free (icon_path);
}

/**
 * gs_plugin_refine_add_addons:
 */
static void
gs_plugin_refine_add_addons (GsPlugin *plugin, GsApp *app, AsApp *item)
{
	GPtrArray *addons;
	guint i;

	addons = as_app_get_addons (item);
	if (addons == NULL)
		return;

	for (i = 0; i < addons->len; i++) {
		AsApp *as_addon = g_ptr_array_index (addons, i);
		GsApp *addon;
		GError *error = NULL;
		gboolean ret;

		addon = gs_app_new (as_app_get_id_full (as_addon));

		/* add all the data we can */
		ret = gs_plugin_refine_item (plugin, addon, as_addon, &error);
		if (!ret) {
			g_warning ("failed to refine addon: %s", error->message);
			g_error_free (error);
			continue;
		}

		gs_app_add_addon (app, addon);
	}
}
/**
 * gs_plugin_refine_add_screenshots:
 */
static void
gs_plugin_refine_add_screenshots (GsApp *app, AsApp *item)
{
	AsScreenshot *ss;
	GPtrArray *images_as;
	GPtrArray *screenshots_as;
	guint i;

	/* do we have any to add */
	screenshots_as = as_app_get_screenshots (item);
	if (screenshots_as->len == 0)
		return;

	/* does the app already have some */
	gs_app_add_kudo (app, GS_APP_KUDO_HAS_SCREENSHOTS);
	if (gs_app_get_screenshots(app)->len > 0)
		return;

	/* add any we know */
	for (i = 0; i < screenshots_as->len &&
		    i < GS_PLUGIN_APPSTREAM_MAX_SCREENSHOTS; i++) {
		ss = g_ptr_array_index (screenshots_as, i);
		images_as = as_screenshot_get_images (ss);
		if (images_as->len == 0)
			continue;
		if (as_screenshot_get_kind (ss) == AS_SCREENSHOT_KIND_UNKNOWN)
			continue;
		gs_app_add_screenshot (app, ss);
	}
}

/**
 * gs_plugin_appstream_set_license:
 */
static void
gs_plugin_appstream_set_license (GsApp *app, const gchar *license_string)
{
	GString *urld;
	gchar **tokens;
	guint i;

	/* tokenize the license string and URLify any SPDX IDs */
	urld = g_string_sized_new (strlen (license_string) + 1);
	tokens = as_utils_spdx_license_tokenize (license_string);
	for (i = 0; tokens[i] != NULL; i++) {

		/* literal text */
		if (g_str_has_prefix (tokens[i], "#")) {
			g_string_append (urld, tokens[i] + 1);
			continue;
		}

		/* unknown value */
		if (!as_utils_is_spdx_license_id (tokens[i])) {
			g_string_append (urld, tokens[i]);
			continue;
		}

		/* SPDX value */
		g_string_append_printf (urld,
					"<a href=\"http://spdx.org/licenses/%s\">%s</a>",
					tokens[i], tokens[i]);
	}
	gs_app_set_licence (app, urld->str);
	g_strfreev (tokens);
	g_string_free (urld, TRUE);
}

/**
 * gs_plugin_appstream_is_recent_release:
 */
static gboolean
gs_plugin_appstream_is_recent_release (AsApp *app)
{
	AsRelease *release;
	GPtrArray *releases;
	guint secs;

	/* get newest release */
	releases = as_app_get_releases (app);
	if (releases->len == 0)
		return FALSE;
	release = g_ptr_array_index (releases, 0);

	/* is last build less than one year ago? */
	secs = (g_get_real_time () / G_USEC_PER_SEC) -
		as_release_get_timestamp (release);
	return secs / (60 * 60 * 24) < 365;
}

/**
 * gs_plugin_appstream_are_screenshots_perfect:
 */
static gboolean
gs_plugin_appstream_are_screenshots_perfect (AsApp *app)
{
	AsImage *image;
	AsScreenshot *screenshot;
	GPtrArray *screenshots;
	guint height;
	guint i;
	guint width;

	screenshots = as_app_get_screenshots (app);
	if (screenshots->len == 0)
		return FALSE;
	for (i = 0; i < screenshots->len; i++) {

		/* get the source image as the thumbs will be resized & padded */
		screenshot = g_ptr_array_index (screenshots, i);
		image = as_screenshot_get_source (screenshot);
		if (image == NULL)
			return FALSE;

		width = as_image_get_width (image);
		height = as_image_get_height (image);

		/* too small */
		if (width < AS_IMAGE_LARGE_WIDTH || height < AS_IMAGE_LARGE_HEIGHT)
			return FALSE;

		/* too large */
		if (width > AS_IMAGE_LARGE_WIDTH * 2 || height > AS_IMAGE_LARGE_HEIGHT * 2)
			return FALSE;

		/* not 16:9 */
		if ((width / 16) * 9 != height)
			return FALSE;
	}
	return TRUE;
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
	GHashTable *urls;
	GPtrArray *pkgnames;
	const gchar *tmp;
	gboolean ret = TRUE;
	gchar *from_xml;

	/* is an app */
	if (gs_app_get_kind (app) == GS_APP_KIND_UNKNOWN ||
	    gs_app_get_kind (app) == GS_APP_KIND_PACKAGE) {
		if (as_app_get_id_kind (item) == AS_ID_KIND_SOURCE) {
			gs_app_set_kind (app, GS_APP_KIND_SOURCE);
		} else {
			gs_app_set_kind (app, GS_APP_KIND_NORMAL);
		}
	}

	/* is installed already */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN) {
		switch (as_app_get_source_kind (item)) {
		case AS_APP_SOURCE_KIND_APPDATA:
		case AS_APP_SOURCE_KIND_DESKTOP:
			gs_app_set_kind (app, GS_APP_KIND_NORMAL);
		case AS_APP_SOURCE_KIND_METAINFO:
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
			break;
		default:
			break;
		}
	}

	/* give the desktopdb plugin a fighting chance */
	if (as_app_get_source_file (item) != NULL &&
	    gs_app_get_metadata_item (app, "DataDir::desktop-filename") == NULL) {
		gs_app_set_metadata (app, "DataDir::desktop-filename",
				     as_app_get_source_file (item));
	}

	/* set id */
	if (as_app_get_id (item) != NULL && gs_app_get_id (app) == NULL)
		gs_app_set_id (app, as_app_get_id (item));

	/* set name */
	tmp = as_app_get_name (item, NULL);
	if (tmp != NULL) {
		gs_app_set_name (app,
				 GS_APP_QUALITY_HIGHEST,
				 as_app_get_name (item, NULL));
	}

	/* set summary */
	tmp = as_app_get_comment (item, NULL);
	if (tmp != NULL) {
		gs_app_set_summary (app,
				    GS_APP_QUALITY_HIGHEST,
				    as_app_get_comment (item, NULL));
	}

	/* add urls */
	urls = as_app_get_urls (item);
	if (g_hash_table_size (urls) > 0 &&
	    gs_app_get_url (app, AS_URL_KIND_HOMEPAGE) == NULL) {
		GList *keys;
		GList *l;
		keys = g_hash_table_get_keys (urls);
		for (l = keys; l != NULL; l = l->next) {
			gs_app_set_url (app,
					as_url_kind_from_string (l->data),
					g_hash_table_lookup (urls, l->data));
		}
		g_list_free (keys);
	}

	/* set licence */
	if (as_app_get_project_license (item) != NULL && gs_app_get_licence (app) == NULL)
		gs_plugin_appstream_set_license (app, as_app_get_project_license (item));

	/* set keywords */
	if (as_app_get_keywords (item) != NULL &&
	    gs_app_get_keywords (app) == NULL) {
		gs_app_set_keywords (app, as_app_get_keywords (item));
		gs_app_add_kudo (app, GS_APP_KUDO_HAS_KEYWORDS);
	}

	/* set description */
	tmp = as_app_get_description (item, NULL);
	if (tmp != NULL) {
		from_xml = as_markup_convert_simple (tmp, -1, error);
		if (from_xml == NULL) {
			ret = FALSE;
			goto out;
		}
		gs_app_set_description (app,
					GS_APP_QUALITY_HIGHEST,
					from_xml);
		g_free (from_xml);
	}

	/* set icon */
	if (as_app_get_icon (item) != NULL && gs_app_get_pixbuf (app) == NULL)
		gs_plugin_refine_item_pixbuf (plugin, app, item);

	/* set categories */
	if (as_app_get_categories (item) != NULL &&
	    gs_app_get_categories (app)->len == 0)
		gs_app_set_categories (app, as_app_get_categories (item));

	/* set project group */
	if (as_app_get_project_group (item) != NULL &&
	    gs_app_get_project_group (app) == NULL)
		gs_app_set_project_group (app, as_app_get_project_group (item));

	/* this is a core application for the desktop and cannot be removed */
	if (as_app_has_compulsory_for_desktop (item, "GNOME") &&
	    gs_app_get_kind (app) == GS_APP_KIND_NORMAL)
		gs_app_set_kind (app, GS_APP_KIND_SYSTEM);

	/* set id kind */
	if (gs_app_get_id_kind (app) == AS_ID_KIND_UNKNOWN)
		gs_app_set_id_kind (app, as_app_get_id_kind (item));

	/* set package names */
	pkgnames = as_app_get_pkgnames (item);
	if (pkgnames->len > 0 && gs_app_get_sources(app)->len == 0)
		gs_app_set_sources (app, pkgnames);

	/* set addons */
	gs_plugin_refine_add_addons (plugin, app, item);

	/* set screenshots */
	gs_plugin_refine_add_screenshots (app, item);

	/* are the screenshots perfect */
	if (gs_plugin_appstream_are_screenshots_perfect (item))
		gs_app_add_kudo (app, GS_APP_KUDO_PERFECT_SCREENSHOTS);

	/* was this application released recently */
	if (gs_plugin_appstream_is_recent_release (item))
		gs_app_add_kudo (app, GS_APP_KUDO_RECENT_RELEASE);

	/* add kudos */
	if (as_app_get_language (item, plugin->priv->locale) > 50)
		gs_app_add_kudo (app, GS_APP_KUDO_MY_LANGUAGE);
	if (as_app_get_metadata_item (item, "X-Kudo-GTK3") != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_MODERN_TOOLKIT);
	if (as_app_get_metadata_item (item, "X-Kudo-QT5") != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_MODERN_TOOLKIT);
	if (as_app_get_metadata_item (item, "X-Kudo-SearchProvider") != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_SEARCH_PROVIDER);
	if (as_app_get_metadata_item (item, "X-Kudo-InstallsUserDocs") != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_INSTALLS_USER_DOCS);
	if (as_app_get_metadata_item (item, "X-Kudo-UsesNotifications") != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_USES_NOTIFICATIONS);
	if (as_app_get_metadata_item (item, "X-Kudo-UsesAppMenu") != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_USES_APP_MENU);
	if (as_app_get_metadata_item (item, "X-Kudo-Popular") != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_POPULAR);
	if (as_app_get_metadata_item (item, "X-IBus-Symbol") != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_IBUS_HAS_SYMBOL);
out:
	return ret;
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

	/* find anything that matches the ID */
	id = gs_app_get_id_full (app);
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
	const gchar *pkgname;
	gboolean ret = TRUE;
	guint i;

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
		  GList **list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	gboolean ret;
	gboolean found;
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
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		ret = gs_plugin_refine_from_id (plugin, app, &found, error);
		if (!ret) {
			gs_profile_stop (plugin->profile, "appstream::refine");
			goto out;
		}
		if (!found) {
			ret = gs_plugin_refine_from_pkgname (plugin, app, error);
			if (!ret) {
				gs_profile_stop (plugin->profile, "appstream::refine");
				goto out;
			}
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
	AsApp *item;
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
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (as_app_get_id (item) == NULL)
			continue;
		if (!as_app_has_category (item, search_id1))
			continue;
		if (search_id2 != NULL && !as_app_has_category (item, search_id2))
			continue;

		/* got a search match, so add all the data we can */
		app = gs_app_new (as_app_get_id_full (item));
		ret = gs_plugin_refine_item (plugin, app, item, error);
		if (!ret)
			goto out;
		gs_plugin_add_app (list, app);
		g_object_unref (app);
	}
	gs_profile_stop (plugin->profile, "appstream::add-category-apps");
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
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (as_app_search_matches_all (item, values) != 0) {
			app = gs_app_new (as_app_get_id_full (item));
			ret = gs_plugin_refine_item (plugin, app, item, error);
			if (!ret)
				goto out;
			gs_plugin_add_app (list, app);
			g_object_unref (app);
		}
	}
	gs_profile_stop (plugin->profile, "appstream::search");
out:
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
	gs_profile_start (plugin->profile, "appstream::add_installed");
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (as_app_get_source_kind (item) == AS_APP_SOURCE_KIND_APPDATA) {
			app = gs_app_new (as_app_get_id_full (item));
			ret = gs_plugin_refine_item (plugin, app, item, error);
			if (!ret)
				goto out;
			gs_plugin_add_app (list, app);
			g_object_unref (app);
		}
	}
	gs_profile_stop (plugin->profile, "appstream::add_installed");
out:
	return ret;
}

/**
 * gs_plugin_add_categories_for_app:
 */
static void
gs_plugin_add_categories_for_app (GList *list, AsApp *app)
{
	GList *children;
	GList *l;
	GList *l2;
	GsCategory *category;
	GsCategory *parent;
	gboolean found_subcat;

	/* does it match the main category */
	for (l = list; l != NULL; l = l->next) {
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
		g_list_free (children);

		/* matching the main category but no subcategories means we have
		 * to create a new 'Other' subcategory manually */
		if (!found_subcat) {
			category = gs_category_find_child (parent, "other");
			if (category == NULL) {
				category = gs_category_new (parent, "other", NULL);
				gs_category_add_subcategory (parent, category);
			}
			as_app_add_category (app, gs_category_get_id (category), -1);
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

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* find out how many packages are in each category */
	gs_profile_start (plugin->profile, "appstream::add-categories");
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		app = g_ptr_array_index (array, i);
		if (as_app_get_id (app) == NULL)
			continue;
		if (as_app_get_priority (app) < 0)
			continue;
		gs_plugin_add_categories_for_app (*list, app);
	}
	gs_profile_stop (plugin->profile, "appstream::add-categories");
out:
	return ret;
}

/**
 * gs_plugin_add_popular_from_category:
 */
static gboolean
gs_plugin_add_popular_from_category (GsPlugin *plugin,
				     const gchar *category,
				     GList **list,
				     GHashTable *ignore_apps,
				     GError **error)
{
	AsApp *item;
	GPtrArray *array;
	GsApp *app;
	gboolean ret = TRUE;
	guint i;

	/* search categories for the search term */
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* find not-installed desktop application with long descriptions
		 * and perfect screenshots and that we've not suggested before */
		if (as_app_get_state (item) == AS_APP_STATE_INSTALLED)
			continue;
		if (as_app_get_id_kind (item) != AS_ID_KIND_DESKTOP)
			continue;
		if (as_app_get_description (item, NULL) == NULL)
			continue;
		if (g_hash_table_lookup (ignore_apps, as_app_get_id_full (item)) != NULL)
			continue;
		if (!as_app_has_category (item, category))
			continue;

		/* add application */
		app = gs_app_new (as_app_get_id_full (item));
		ret = gs_plugin_refine_item (plugin, app, item, error);
		if (!ret)
			goto out;

		/* only suggest awesome applications */
		if ((gs_app_get_kudos (app) & GS_APP_KUDO_PERFECT_SCREENSHOTS) > 0 &&
		    gs_app_get_kudos_weight (app) >= 4) {
			g_debug ("suggesting %s as others installed from %s",
				 as_app_get_id (item), category);
			gs_plugin_add_app (list, app);
			g_hash_table_insert (ignore_apps,
					     (gpointer) as_app_get_id_full (item),
					     GINT_TO_POINTER (1));
		}
		g_object_unref (app);
	}
out:
	return ret;
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
	AsApp *item;
	GHashTable *ignore_apps = NULL;
	GHashTable *ignore_cats = NULL;
	GPtrArray *array;
	GPtrArray *categories;
	const gchar *tmp;
	gboolean ret = TRUE;
	guint i;
	guint j;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* ignore main categories */
	ignore_cats = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (ignore_cats, (gpointer) "Audio", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Development", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Education", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Game", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Graphics", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Network", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Office", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Science", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "System", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Utility", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Video", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "Addons", GINT_TO_POINTER (1));

	/* ignore core apps */
	g_hash_table_insert (ignore_cats, (gpointer) "Core", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "PackageManager", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "TerminalEmulator", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "other", GINT_TO_POINTER (1));

	/* get already installed applications */
	ignore_apps = g_hash_table_new (g_str_hash, g_str_equal);
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (as_app_get_state (item) != AS_APP_STATE_INSTALLED)
			continue;
		g_hash_table_insert (ignore_apps,
				     (gpointer) as_app_get_id_full (item),
				     GINT_TO_POINTER (1));
	}

	/* search categories for the search term */
	gs_profile_start (plugin->profile, "appstream::add_popular");
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* find installed desktop applications */
		if (as_app_get_state (item) != AS_APP_STATE_INSTALLED)
			continue;
		if (as_app_get_id_kind (item) != AS_ID_KIND_DESKTOP)
			continue;
		if (as_app_get_source_kind (item) == AS_APP_SOURCE_KIND_DESKTOP)
			continue;

		/* find non-installed apps with appdata in any category */
		categories = as_app_get_categories (item);
		for (j = 0; j < categories->len; j++) {
			tmp = g_ptr_array_index (categories, j);
			if (g_hash_table_lookup (ignore_cats, tmp) != NULL)
				continue;
			ret = gs_plugin_add_popular_from_category (plugin,
								   tmp,
								   list,
								   ignore_apps,
								   error);
			if (!ret)
				goto out;
		}

	}
	gs_profile_stop (plugin->profile, "appstream::add_popular");
out:
	if (ignore_cats != NULL)
		g_hash_table_unref (ignore_cats);
	if (ignore_apps != NULL)
		g_hash_table_unref (ignore_apps);
	return ret;
}
