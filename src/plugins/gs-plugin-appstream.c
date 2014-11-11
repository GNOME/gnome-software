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

#if AS_CHECK_VERSION(0,3,0)
#define as_app_get_id_full(a)	as_app_get_id(a)
#endif

struct GsPluginPrivate {
	AsStore			*store;
	gchar			*locale;
	gsize			 done_init;
	gboolean		 has_hi_dpi_support;
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

#if AS_CHECK_VERSION(0,3,2)
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
	GList *keys = NULL;
	GList *l;
	const gchar *tmp;
	gdouble perc;
	guint *cnt;
	guint i;

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

	g_list_free (keys);
	return origins;
}
#endif

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GError **error)
{
	GPtrArray *items;
	gboolean ret;
	gchar *tmp;
#if AS_CHECK_VERSION(0,3,1)
	guint i;
#endif
#if AS_CHECK_VERSION(0,3,2)
	AsApp *app;
	GHashTable *origins = NULL;
	const gchar *origin;
	guint *perc;
#endif

	/* clear all existing applications if the store was invalidated */
	as_store_remove_all (plugin->priv->store);

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

	/* add search terms for apps not in the main source */
#if AS_CHECK_VERSION(0,3,2)
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
			as_app_add_keyword (app, NULL, origin, -1);
		}
	}
#endif

	/* look for any application with a HiDPI icon kudo */
#if AS_CHECK_VERSION(0,3,1)
	for (i = 0; i < items->len; i++) {
		app = g_ptr_array_index (items, i);
		if (as_app_has_kudo_kind (app, AS_KUDO_KIND_HI_DPI_ICON)) {
			plugin->priv->has_hi_dpi_support = TRUE;
			break;
		}
	}
#endif
out:
#if AS_CHECK_VERSION(0,3,2)
	if (origins != NULL)
		g_hash_table_unref (origins);
#endif
	gs_profile_stop (plugin->profile, "appstream::startup");
	return ret;
}

#if AS_CHECK_VERSION(0,3,1)

/**
 * gs_plugin_refine_item_pixbuf:
 */
static void
gs_plugin_refine_item_pixbuf (GsPlugin *plugin, GsApp *app, AsApp *item)
{
	GError *error = NULL;
	AsIcon *icon;
	gboolean ret;

	icon = as_app_get_icon_default (item);
	switch (as_icon_get_kind (icon)) {
	case AS_ICON_KIND_REMOTE:
		gs_app_set_icon (app, as_icon_get_name (icon));
		break;
	case AS_ICON_KIND_STOCK:
	case AS_ICON_KIND_LOCAL:
		gs_app_set_icon (app, as_icon_get_name (icon));
		ret = gs_app_load_icon (app, plugin->scale, &error);
		if (!ret) {
			g_warning ("failed to load stock icon %s: %s",
				   as_icon_get_name (icon), error->message);
			g_error_free (error);
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
			g_error_free (error);
			return;
		}
		gs_app_set_pixbuf (app, as_icon_get_pixbuf (icon));
		break;
	default:
		g_warning ("icon kind unknown for %s", as_app_get_id_full (item));
		break;
	}
}

#else

/**
 * _as_app_get_icon_for_scale:
 */
static GdkPixbuf *
_as_app_get_icon_for_scale (AsApp *app, gint scale, GError **error)
{
	GdkPixbuf *pixbuf = NULL;
	const gchar *icon;
	const gchar *icon_path;
	gchar *filename_hidpi = NULL;
	gchar *filename_lodpi = NULL;
	gchar *filename = NULL;

	/* HiDPI */
	icon = as_app_get_icon (app);
	if (icon == NULL)
		goto out;
	icon_path = as_app_get_icon_path (app);
	if (icon_path == NULL)
		goto out;
	if (scale == 2) {
		filename_hidpi = g_build_filename (icon_path, "128x128", icon, NULL);
		if (g_file_test (filename_hidpi, G_FILE_TEST_EXISTS)) {
			pixbuf = gdk_pixbuf_new_from_file (filename_hidpi, error);
			if (pixbuf == NULL)
				goto out;
			goto out;
		}
	}

	/* LoDPI */
	filename_lodpi = g_build_filename (icon_path, "64x64", icon, NULL);
	if (g_file_test (filename_lodpi, G_FILE_TEST_EXISTS)) {
		pixbuf = gdk_pixbuf_new_from_file (filename_lodpi, error);
		goto out;
	}

	/* fallback */
	filename = g_build_filename (icon_path, icon, NULL);
	pixbuf = gdk_pixbuf_new_from_file_at_scale (filename, 64, 64,
						    FALSE, error);
	if (pixbuf == NULL)
		goto out;
out:
	g_free (filename_hidpi);
	g_free (filename_lodpi);
	g_free (filename);
	return pixbuf;
}

/**
 * gs_plugin_refine_item_pixbuf:
 */
static void
gs_plugin_refine_item_pixbuf (GsPlugin *plugin, GsApp *app, AsApp *item)
{
	GdkPixbuf *pb = NULL;
	GError *error = NULL;
	const gchar *icon;
	gboolean ret;

	icon = as_app_get_icon (item);
	switch (as_app_get_icon_kind (item)) {
	case AS_ICON_KIND_REMOTE:
		gs_app_set_icon (app, icon);
		break;
#if AS_CHECK_VERSION(0,2,7)
	case AS_ICON_KIND_LOCAL:
#endif
	case AS_ICON_KIND_STOCK:
		gs_app_set_icon (app, icon);
		ret = gs_app_load_icon (app, plugin->scale, &error);
		if (!ret) {
			g_warning ("failed to load stock icon %s: %s",
				   icon, error->message);
			g_error_free (error);
			goto out;
		}
		break;
	case AS_ICON_KIND_CACHED:
		pb = _as_app_get_icon_for_scale (item, plugin->scale, &error);
		if (pb == NULL) {
			g_warning ("failed to load cached icon %s: %s",
				   as_app_get_icon (item), error->message);
			g_error_free (error);
			goto out;
		}
		gs_app_set_pixbuf (app, pb);
		break;
	default:
		g_warning ("icon kind unknown for %s", as_app_get_id_full (item));
		break;
	}
out:
	if (pb != NULL)
		g_object_unref (pb);
}
#endif

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
			g_object_unref (addon);
			continue;
		}

		gs_app_add_addon (app, addon);
		g_object_unref (addon);
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

		/* translated join */
		if (g_strcmp0 (tokens[i], "&") == 0) {
			/* TRANSLATORS: This is how we join the licences and can
			 * be considered a "Conjunctive AND Operator" according
			 * to the SPDX specification. For example:
			 * "LGPL-2.1 and MIT and BSD-2-Clause" */
			g_string_append (urld, _(" and "));
			continue;
		}
		if (g_strcmp0 (tokens[i], "|") == 0) {
			/* TRANSLATORS: This is how we join the licences and can
			 * be considered a "Disjunctive OR Operator" according
			 * to the SPDX specification. For example:
			 * "LGPL-2.1 or MIT" */
			g_string_append (urld, _(" or "));
			continue;
		}

		/* legacy literal text */
		if (g_str_has_prefix (tokens[i], "#")) {
			g_string_append (urld, tokens[i] + 1);
			continue;
		}

		/* SPDX value */
		if (g_str_has_prefix (tokens[i], "@")) {
			g_string_append_printf (urld,
						"<a href=\"http://spdx.org/licenses/%s\">%s</a>",
						tokens[i] + 1, tokens[i] + 1);
			continue;
		}

		/* new SPDX value the extractor didn't know about */
		if (as_utils_is_spdx_license_id (tokens[i])) {
			g_string_append_printf (urld,
						"<a href=\"http://spdx.org/licenses/%s\">%s</a>",
						tokens[i], tokens[i]);
			continue;
		}

		/* unknown value */
		g_string_append (urld, tokens[i]);
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
	GPtrArray *kudos;
	const gchar *tmp;
	gboolean ret = TRUE;
	gchar *from_xml;
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
	if (as_app_get_id_full (item) != NULL && gs_app_get_id (app) == NULL)
		gs_app_set_id (app, as_app_get_id_full (item));

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
#if AS_CHECK_VERSION(0,3,0)
	if (as_app_get_keywords (item, NULL) != NULL &&
	    gs_app_get_keywords (app) == NULL) {
		gs_app_set_keywords (app, as_app_get_keywords (item, NULL));
		gs_app_add_kudo (app, GS_APP_KUDO_HAS_KEYWORDS);
	}
#else
	if (as_app_get_keywords (item) != NULL &&
	    gs_app_get_keywords (app) == NULL) {
		gs_app_set_keywords (app, as_app_get_keywords (item));
		gs_app_add_kudo (app, GS_APP_KUDO_HAS_KEYWORDS);
	}
#endif

	/* set description */
	tmp = as_app_get_description (item, NULL);
	if (tmp != NULL) {
		from_xml = as_markup_convert_simple (tmp, -1, error);
		if (from_xml == NULL) {
			g_prefix_error (error, "trying to parse '%s': ", tmp);
			ret = FALSE;
			goto out;
		}
		gs_app_set_description (app,
					GS_APP_QUALITY_HIGHEST,
					from_xml);
		g_free (from_xml);
	}

	/* set icon */
#if AS_CHECK_VERSION(0,3,1)
	if (as_app_get_icon_default (item) != NULL && gs_app_get_pixbuf (app) == NULL)
		gs_plugin_refine_item_pixbuf (plugin, app, item);
#else
	if (as_app_get_icon (item) != NULL && gs_app_get_pixbuf (app) == NULL)
		gs_plugin_refine_item_pixbuf (plugin, app, item);
#endif

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
#if AS_CHECK_VERSION(0,3,0)
		case AS_KUDO_KIND_HIGH_CONTRAST:
			gs_app_add_kudo (app, GS_APP_KUDO_HIGH_CONTRAST);
			break;
#endif
		default:
			g_debug ("no idea how to handle kudo '%s'", tmp);
			break;
		}
	}
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
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	gs_profile_start (plugin->profile, "appstream::refine");
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		ret = gs_plugin_refine_from_id (plugin, app, &found, error);
		if (!ret)
			goto out;
		if (!found) {
			ret = gs_plugin_refine_from_pkgname (plugin, app, error);
			if (!ret)
				goto out;
		}
	}

	/* sucess */
	ret = TRUE;
out:
	gs_profile_stop (plugin->profile, "appstream::refine");
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
		ret = gs_plugin_startup (plugin, error);
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
		if (as_app_get_id_full (item) == NULL)
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
out:
	gs_profile_stop (plugin->profile, "appstream::add-category-apps");
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
	GsApp *app;
	app = gs_app_new (as_app_get_id_full (item));
	if (!gs_plugin_refine_item (plugin, app, item, error)) {
		g_object_unref (app);
		return FALSE;
	}
	gs_app_set_search_sort_key (app, match_value);
	gs_plugin_add_app (list, app);
	g_object_unref (app);
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

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* search categories for the search term */
	gs_profile_start (plugin->profile, "appstream::search");
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
	gs_profile_stop (plugin->profile, "appstream::search");
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
		ret = gs_plugin_startup (plugin, error);
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
out:
	gs_profile_stop (plugin->profile, "appstream::add_installed");
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
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* find out how many packages are in each category */
	gs_profile_start (plugin->profile, "appstream::add-categories");
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		app = g_ptr_array_index (array, i);
		if (as_app_get_id_full (app) == NULL)
			continue;
		if (as_app_get_priority (app) < 0)
			continue;
		gs_plugin_add_categories_for_app (*list, app);
	}
out:
	gs_profile_stop (plugin->profile, "appstream::add-categories");
	return ret;
}

/**
 * gs_plugin_appstream_is_app_awesome:
 */
static gboolean
gs_plugin_appstream_is_app_awesome (GsApp *app)
{
	if ((gs_app_get_kudos (app) & GS_APP_KUDO_PERFECT_SCREENSHOTS) == 0)
		return FALSE;
	if (gs_app_get_kudos_weight (app) < 4)
		return FALSE;
	return TRUE;
}

/**
 * gs_plugin_add_popular_from_category:
 */
static gboolean
gs_plugin_add_popular_from_category (GsPlugin *plugin,
				     GList **list,
				     const gchar *category,
				     const gchar *category_exclude,
				     GHashTable *ignore_apps,
				     GError **error)
{
	AsApp *item;
	GError *error_local = NULL;
	GPtrArray *array;
	GsApp *app;
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
		if (category_exclude != NULL && as_app_has_category (item, category_exclude))
			continue;
#if AS_CHECK_VERSION(0,3,1)
		if (plugin->priv->has_hi_dpi_support &&
		    !as_app_has_kudo_kind (item, AS_KUDO_KIND_HI_DPI_ICON))
			continue;
#endif

		/* add application */
		app = gs_app_new (as_app_get_id_full (item));
		if (!gs_plugin_refine_item (plugin, app, item, &error_local)) {
			g_warning ("Failed to refine %s: %s",
				   as_app_get_id (item),
				   error_local->message);
			g_clear_error (&error_local);
			g_object_unref (app);
			continue;
		}

		/* only suggest awesome applications */
		if (gs_plugin_appstream_is_app_awesome (app)) {
			g_debug ("suggesting %s as others installed from category %s",
				 as_app_get_id_full (item), category);
			gs_plugin_add_app (list, app);
			g_hash_table_insert (ignore_apps,
					     (gpointer) as_app_get_id_full (item),
					     GINT_TO_POINTER (1));
		}
		g_object_unref (app);
	}
	return TRUE;
}

/**
 * gs_plugin_add_popular_by_cat:
 */
static gboolean
gs_plugin_add_popular_by_cat (GsPlugin *plugin,
			      GList **list,
			      const gchar *category_exclude,
			      GHashTable *ignore_apps,
			      GCancellable *cancellable,
			      GError **error)
{
	AsApp *item;
	GHashTable *ignore_cats = NULL;
	GPtrArray *array;
	GPtrArray *categories;
	const gchar *tmp;
	gboolean ret = TRUE;
	guint i;
	guint j;

	/* ignore main categories */
	gs_profile_start (plugin->profile, "appstream::add_popular[cat]");
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
	g_hash_table_insert (ignore_cats, (gpointer) "Settings", GINT_TO_POINTER (1));
	g_hash_table_insert (ignore_cats, (gpointer) "other", GINT_TO_POINTER (1));

	/* search categories for the search term */
	array = as_store_get_apps (plugin->priv->store);
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
								   list,
								   tmp,
								   category_exclude,
								   ignore_apps,
								   error);
			if (!ret)
				goto out;
		}

	}
out:
	gs_profile_stop (plugin->profile, "appstream::add_popular[cat]");
	if (ignore_cats != NULL)
		g_hash_table_unref (ignore_cats);
	return ret;
}

/**
 * gs_plugin_add_popular_by_source:
 */
static gboolean
gs_plugin_add_popular_by_source (GsPlugin *plugin,
				 GList **list,
				 GCancellable *cancellable,
				 GError **error)
{
	AsApp *item;
	GHashTable *installed = NULL;	/* source_pkgname : AsApp */
	GPtrArray *array;
	GsApp *app;
	gboolean ret = TRUE;
	guint i;

	/* get already installed applications */
	gs_profile_start (plugin->profile, "appstream::add_popular[source]");
	installed = g_hash_table_new (g_str_hash, g_str_equal);
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (as_app_get_state (item) != AS_APP_STATE_INSTALLED)
			continue;
		if (as_app_get_id_kind (item) != AS_ID_KIND_DESKTOP)
			continue;
		if (as_app_get_source_pkgname (item) == NULL)
			continue;
		g_hash_table_insert (installed,
				     (gpointer) as_app_get_source_pkgname (item),
				     (gpointer) item);
	}

	/* search categories for the search term */
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* find not installed desktop applications */
		if (as_app_get_state (item) == AS_APP_STATE_INSTALLED)
			continue;
		if (as_app_get_id_kind (item) != AS_ID_KIND_DESKTOP)
			continue;
		if (as_app_get_source_pkgname (item) == NULL)
			continue;

		/* have we got an app installed with the same source name */
		if (g_hash_table_lookup (installed, as_app_get_source_pkgname (item)) == NULL)
			continue;

		/* add application */
		app = gs_app_new (as_app_get_id_full (item));
		ret = gs_plugin_refine_item (plugin, app, item, error);
		if (!ret)
			goto out;

		/* only suggest awesome apps */
		if (gs_plugin_appstream_is_app_awesome (app)) {
			g_debug ("suggesting %s as others installed from source %s",
				 as_app_get_id_full (item),
				 as_app_get_source_pkgname (item));
			gs_plugin_add_app (list, app);
		} else {
			g_debug ("not suggesting %s as not awesome enough",
				 as_app_get_id_full (item));
		}
		g_object_unref (app);
	}
out:
	gs_profile_stop (plugin->profile, "appstream::add_popular[source]");
	if (installed != NULL)
		g_hash_table_unref (installed);
	return ret;
}

/**
 * gs_plugin_add_popular:
 */
gboolean
gs_plugin_add_popular (GsPlugin *plugin,
			GList **list,
			const gchar *category,
			const gchar *category_exclude,
			GCancellable *cancellable,
			GError **error)
{
	AsApp *item;
	GHashTable *ignore_apps = NULL;
	GPtrArray *array;
	gboolean ret = TRUE;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}
	gs_profile_start (plugin->profile, "appstream::add_popular");

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

	if (category == NULL) {
		/* use category heuristic */
		ret = gs_plugin_add_popular_by_cat (plugin,
		                                    list,
		                                    category_exclude,
		                                    ignore_apps,
		                                    cancellable,
		                                    error);
		if (!ret)
			goto out;

		/* use source-package heuristic */
		ret = gs_plugin_add_popular_by_source (plugin, list, cancellable, error);
		if (!ret)
			goto out;
	} else {
		ret = gs_plugin_add_popular_from_category (plugin,
							   list,
							   category,
							   NULL,
							   ignore_apps,
							   error);
		if (!ret)
			goto out;
	}

out:
	gs_profile_stop (plugin->profile, "appstream::add_popular");
	if (ignore_apps != NULL)
		g_hash_table_unref (ignore_apps);
	return ret;
}
