/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <gs-plugin.h>

#include "appstream-common.h"

#define	GS_PLUGIN_APPSTREAM_MAX_SCREENSHOTS	5

/**
 * _as_app_has_compulsory_for_desktop:
 */
static gboolean
_as_app_has_compulsory_for_desktop (AsApp *app, const gchar *compulsory_for_desktop)
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
 * gs_appstream_refine_app_pixbuf:
 */
static void
gs_appstream_refine_app_pixbuf (GsPlugin *plugin, GsApp *app, AsApp *item)
{
	AsIcon *icon;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *path = NULL;

	icon = as_app_get_icon_default (item);
	switch (as_icon_get_kind (icon)) {
	case AS_ICON_KIND_REMOTE:
		gs_app_set_icon (app, icon);
		if (as_icon_get_filename (icon) == NULL) {
			path = g_build_filename (g_get_user_data_dir (),
						 "gnome-software",
						 "icons",
						 NULL);
			fn = g_build_filename (path, as_icon_get_name (icon), NULL);
			as_icon_set_filename (icon, fn);
			as_icon_set_prefix (icon, path);
		}
		if (g_file_test (fn, G_FILE_TEST_EXISTS)) {
			as_icon_set_kind (icon, AS_ICON_KIND_LOCAL);
			ret = gs_app_load_icon (app, plugin->scale, &error);
			if (!ret) {
				g_warning ("failed to load icon %s: %s",
					   as_icon_get_name (icon),
					   error->message);
				return;
			}
		}
		break;
	case AS_ICON_KIND_STOCK:
	case AS_ICON_KIND_LOCAL:
		gs_app_set_icon (app, icon);

		/* does not exist, so try to find using the icon theme */
		if (as_icon_get_kind (icon) == AS_ICON_KIND_LOCAL &&
		    as_icon_get_filename (icon) == NULL)
			as_icon_set_kind (icon, AS_ICON_KIND_STOCK);

		/* load */
		ret = gs_app_load_icon (app, plugin->scale, &error);
		if (!ret) {
			g_warning ("failed to load %s icon %s: %s",
				   as_icon_kind_to_string (as_icon_get_kind (icon)),
				   as_icon_get_name (icon),
				   error->message);
				return;
		}
		break;
	case AS_ICON_KIND_CACHED:
		if (plugin->scale == 2)
			icon = as_app_get_icon_for_size (item, 128, 128);
		if (icon == NULL)
			icon = as_app_get_icon_for_size (item, 64, 64);
		if (icon == NULL) {
			g_warning ("failed to find cached icon %s",
				   as_icon_get_name (icon));
			return;
		}
		if (!as_icon_load (icon, AS_ICON_LOAD_FLAG_SEARCH_SIZE, &error)) {
			g_warning ("failed to load cached icon %s: %s",
				   as_icon_get_name (icon), error->message);
				return;
		}
		gs_app_set_pixbuf (app, as_icon_get_pixbuf (icon));
		break;
	default:
		g_warning ("icon kind unknown for %s", as_app_get_id (item));
		break;
	}
}

/**
 * gs_appstream_refine_add_addons:
 */
static void
gs_appstream_refine_add_addons (GsPlugin *plugin, GsApp *app, AsApp *item)
{
	GPtrArray *addons;
	guint i;

	addons = as_app_get_addons (item);
	if (addons == NULL)
		return;

	for (i = 0; i < addons->len; i++) {
		AsApp *as_addon = g_ptr_array_index (addons, i);
		g_autoptr(GError) error = NULL;
		g_autoptr(GsApp) addon = NULL;

		addon = gs_app_new (as_app_get_id (as_addon));

		/* add all the data we can */
		if (!gs_appstream_refine_app (plugin, addon, as_addon, &error)) {
			g_warning ("failed to refine addon: %s", error->message);
			continue;
		}
		gs_app_add_addon (app, addon);
	}
}
/**
 * gs_appstream_refine_add_screenshots:
 */
static void
gs_appstream_refine_add_screenshots (GsApp *app, AsApp *item)
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
 * gs_appstream_is_recent_release:
 */
static gboolean
gs_appstream_is_recent_release (AsApp *app)
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
 * gs_appstream_are_screenshots_perfect:
 */
static gboolean
gs_appstream_are_screenshots_perfect (AsApp *app)
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
 * gs_appstream_copy_metadata:
 */
static void
gs_appstream_copy_metadata (GsApp *app, AsApp *item)
{
	GHashTable *hash;
	GList *l;
	g_autoptr(GList) keys = NULL;

	hash = as_app_get_metadata (item);
	keys = g_hash_table_get_keys (hash);
	for (l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup (hash, key);
		gs_app_set_metadata (app, key, value);
	}
}

/**
 * gs_appstream_refine_app:
 */
gboolean
gs_appstream_refine_app (GsPlugin *plugin, GsApp *app, AsApp *item, GError **error)
{

	GHashTable *urls;
	GPtrArray *pkgnames;
	GPtrArray *kudos;
	const gchar *tmp;
	guint i;

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
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
			break;
		case AS_APP_SOURCE_KIND_METAINFO:
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
			break;
		case AS_APP_SOURCE_KIND_APPSTREAM:
			gs_app_set_state (app, as_app_get_state (item));
			break;
		default:
			break;
		}
	}

	/* allow the PackageKit plugin to match up installed local files
	 * with packages when the component isn't in the AppStream XML */
	switch (as_app_get_source_kind (item)) {
	case AS_APP_SOURCE_KIND_DESKTOP:
	case AS_APP_SOURCE_KIND_APPDATA:
	case AS_APP_SOURCE_KIND_METAINFO:
		if (as_app_get_source_file (item) != NULL &&
		    gs_app_get_metadata_item (app, "DataDir::desktop-filename") == NULL) {
			gs_app_set_metadata (app, "DataDir::desktop-filename",
					     as_app_get_source_file (item));
		}
		break;
	default:
		break;
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
		GList *l;
		g_autoptr(GList) keys = NULL;
		keys = g_hash_table_get_keys (urls);
		for (l = keys; l != NULL; l = l->next) {
			gs_app_set_url (app,
					as_url_kind_from_string (l->data),
					g_hash_table_lookup (urls, l->data));
		}
	}

	/* set licence */
	if (as_app_get_project_license (item) != NULL && gs_app_get_licence (app) == NULL)
		gs_app_set_licence (app,
				    as_app_get_project_license (item),
				    GS_APP_QUALITY_HIGHEST);

	/* set keywords */
	if (as_app_get_keywords (item, NULL) != NULL &&
	    gs_app_get_keywords (app) == NULL) {
		gs_app_set_keywords (app, as_app_get_keywords (item, NULL));
		gs_app_add_kudo (app, GS_APP_KUDO_HAS_KEYWORDS);
	}

	/* set description */
	tmp = as_app_get_description (item, NULL);
	if (tmp != NULL) {
		g_autofree gchar *from_xml = NULL;
		from_xml = as_markup_convert_simple (tmp, error);
		if (from_xml == NULL) {
			g_prefix_error (error, "trying to parse '%s': ", tmp);
			return FALSE;
		}
		gs_app_set_description (app,
					GS_APP_QUALITY_HIGHEST,
					from_xml);
	}

	/* set icon */
	if (as_app_get_icon_default (item) != NULL && gs_app_get_pixbuf (app) == NULL)
		gs_appstream_refine_app_pixbuf (plugin, app, item);

	/* set categories */
	if (as_app_get_categories (item) != NULL &&
	    gs_app_get_categories (app)->len == 0)
		gs_app_set_categories (app, as_app_get_categories (item));

	/* set project group */
	if (as_app_get_project_group (item) != NULL &&
	    gs_app_get_project_group (app) == NULL)
		gs_app_set_project_group (app, as_app_get_project_group (item));

	/* set default bundle (if any) */
	if (as_app_get_bundle_default (item) != NULL &&
	    gs_app_get_bundle (app) == NULL)
		gs_app_set_bundle (app, as_app_get_bundle_default (item));

	/* this is a core application for the desktop and cannot be removed */
	if (_as_app_has_compulsory_for_desktop (item, "GNOME") &&
	    gs_app_get_kind (app) == GS_APP_KIND_NORMAL)
		gs_app_set_kind (app, GS_APP_KIND_SYSTEM);

	/* set id kind */
	if (gs_app_get_id_kind (app) == AS_ID_KIND_UNKNOWN)
		gs_app_set_id_kind (app, as_app_get_id_kind (item));

	/* copy all the metadata */
	gs_appstream_copy_metadata (app, item);

	/* set package names */
	pkgnames = as_app_get_pkgnames (item);
	if (pkgnames->len > 0 && gs_app_get_sources(app)->len == 0)
		gs_app_set_sources (app, pkgnames);

	/* set addons */
	gs_appstream_refine_add_addons (plugin, app, item);

	/* set screenshots */
	gs_appstream_refine_add_screenshots (app, item);

	/* are the screenshots perfect */
	if (gs_appstream_are_screenshots_perfect (item))
		gs_app_add_kudo (app, GS_APP_KUDO_PERFECT_SCREENSHOTS);

	/* was this application released recently */
	if (gs_appstream_is_recent_release (item))
		gs_app_add_kudo (app, GS_APP_KUDO_RECENT_RELEASE);

	/* add kudos */
	if (as_app_get_language (item, plugin->locale) > 50)
		gs_app_add_kudo (app, GS_APP_KUDO_MY_LANGUAGE);

	/* add new-style kudos */
	kudos = as_app_get_kudos (item);
	for (i = 0; i < kudos->len; i++) {
		tmp = g_ptr_array_index (kudos, i);
		switch (as_kudo_kind_from_string (tmp)) {
		case AS_KUDO_KIND_SEARCH_PROVIDER:
			gs_app_add_kudo (app, GS_APP_KUDO_SEARCH_PROVIDER);
			break;
		case AS_KUDO_KIND_USER_DOCS:
			gs_app_add_kudo (app, GS_APP_KUDO_INSTALLS_USER_DOCS);
			break;
		case AS_KUDO_KIND_APP_MENU:
			gs_app_add_kudo (app, GS_APP_KUDO_USES_APP_MENU);
			break;
		case AS_KUDO_KIND_MODERN_TOOLKIT:
			gs_app_add_kudo (app, GS_APP_KUDO_MODERN_TOOLKIT);
			break;
		case AS_KUDO_KIND_NOTIFICATIONS:
			gs_app_add_kudo (app, GS_APP_KUDO_USES_NOTIFICATIONS);
			break;
		case AS_KUDO_KIND_HIGH_CONTRAST:
			gs_app_add_kudo (app, GS_APP_KUDO_HIGH_CONTRAST);
			break;
		case AS_KUDO_KIND_HI_DPI_ICON:
			gs_app_add_kudo (app, GS_APP_KUDO_HI_DPI_ICON);
			break;
		default:
			g_debug ("no idea how to handle kudo '%s'", tmp);
			break;
		}
	}

	return TRUE;
}
