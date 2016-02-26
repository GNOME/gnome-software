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

#include <gs-utils.h>
#include <gs-plugin.h>
#include <gs-plugin-loader.h>

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
	gboolean		 done_init;
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
	g_debug ("AppStream metadata changed");

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
 * gs_plugin_appstream_startup:
 *
 * This must be called with plugin->priv->store_mutex held.
 */
static gboolean
gs_plugin_appstream_startup (GsPlugin *plugin, GError **error)
{
	AsApp *app;
	GPtrArray *items;
	gboolean ret;
	const gchar *origin;
	guint *perc;
	guint i;
	g_autoptr(GHashTable) origins = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* already done */
	if (plugin->priv->done_init)
		return TRUE;

	ptask = as_profile_start_literal (plugin->profile, "appstream::startup");

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
			     AS_STORE_LOAD_FLAG_XDG_APP_USER |
			     AS_STORE_LOAD_FLAG_APP_INSTALL,
			     NULL,
			     error);
	if (!ret)
		return FALSE;
	items = as_store_get_apps (plugin->priv->store);
	if (items->len == 0) {
		g_warning ("No AppStream data, try 'make install-sample-data' in data/");
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     _("No AppStream data found"));
		return FALSE;
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
		if (origin == NULL || origin[0] == '\0')
			continue;
		perc = g_hash_table_lookup (origins, origin);
		if (*perc < 10) {
			g_debug ("Adding keyword '%s' to %s",
				 origin, as_app_get_id (app));
			as_app_add_keyword (app, NULL, origin);
		}
	}

	/* rely on the store keeping itself updated */
	plugin->priv->done_init = TRUE;
	return TRUE;
}

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
 * gs_plugin_refine_item_pixbuf:
 */
static void
gs_plugin_refine_item_pixbuf (GsPlugin *plugin, GsApp *app, AsApp *item)
{
	AsIcon *icon;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *cachedir = NULL;

	icon = as_app_get_icon_default (item);
	switch (as_icon_get_kind (icon)) {
	case AS_ICON_KIND_REMOTE:
		gs_app_set_icon (app, icon);
		if (as_icon_get_filename (icon) == NULL) {
			cachedir = gs_utils_get_cachedir ("icons", NULL);
			fn = g_build_filename (cachedir, as_icon_get_name (icon), NULL);
			as_icon_set_filename (icon, fn);
			as_icon_set_prefix (icon, cachedir);
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
 * gs_plugin_appstream_refine_add_addons:
 */
static void
gs_plugin_appstream_refine_add_addons (GsPlugin *plugin, GsApp *app, AsApp *item)
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
		if (!gs_plugin_refine_item (plugin, addon, as_addon, &error)) {
			g_warning ("failed to refine addon: %s", error->message);
			continue;
		}
		gs_app_add_addon (app, addon);
	}
}
/**
 * gs_plugin_appstream_refine_add_screenshots:
 */
static void
gs_plugin_appstream_refine_add_screenshots (GsApp *app, AsApp *item)
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
 * gs_plugin_appstream_copy_metadata:
 */
static void
gs_plugin_appstream_copy_metadata (GsApp *app, AsApp *item)
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
 * gs_plugin_appstream_create_runtime:
 */
static GsApp *
gs_plugin_appstream_create_runtime (GsApp *parent, const gchar *runtime)
{
	g_autofree gchar *id = NULL;
	g_autofree gchar *source = NULL;
	g_auto(GStrv) id_split = NULL;
	g_auto(GStrv) runtime_split = NULL;
	g_autoptr(GsApp) app = NULL;

	/* get the name/arch/branch */
	runtime_split = g_strsplit (runtime, "/", -1);
	if (g_strv_length (runtime_split) != 3)
		return NULL;

	/* find the parent app ID prefix */
	id_split = g_strsplit (gs_app_get_id (parent), ":", 2);
	if (g_strv_length (id_split) == 2) {
		id = g_strdup_printf ("%s:%s.runtime",
				      id_split[0],
				      runtime_split[0]);
	} else {
		id = g_strdup_printf ("%s.runtime", runtime_split[0]);
	}

	/* create the complete GsApp from the single string */
	app = gs_app_new (id);
	source = g_strdup_printf ("runtime/%s", runtime);
	gs_app_add_source (app, source);
	gs_app_set_kind (app, AS_APP_KIND_RUNTIME);
	gs_app_set_version (app, id_split[2]);

	return g_steal_pointer (&app);
}

/**
 * gs_plugin_refine_item_management_plugin:
 */
static void
gs_plugin_refine_item_management_plugin (GsApp *app, AsApp *item)
{
	GPtrArray *bundles;
	const gchar *management_plugin = NULL;
	const gchar *runtime = NULL;
	guint i;

	/* find the default bundle kind */
	bundles = as_app_get_bundles (item);
	for (i = 0; i < bundles->len; i++) {
		AsBundle *bundle = g_ptr_array_index (bundles, i);
		if (as_bundle_get_kind (bundle) == AS_BUNDLE_KIND_XDG_APP) {
			management_plugin = "XgdApp";
			gs_app_add_source (app, as_bundle_get_id (bundle));

#if AS_CHECK_VERSION(0,5,10)
			/* automatically add runtime */
			runtime = as_bundle_get_runtime (bundle);
			if (runtime != NULL) {
				g_autoptr(GsApp) app2 = NULL;
				app2 = gs_plugin_appstream_create_runtime (app, runtime);
				if (app2 != NULL) {
					g_debug ("runtime for %s is %s",
						 gs_app_get_id (app), runtime);
					gs_app_set_runtime (app, app2);
				}
			}
#endif
			break;
		}
		if (as_bundle_get_kind (bundle) == AS_BUNDLE_KIND_LIMBA) {
			management_plugin = "Limba";
			gs_app_add_source (app, as_bundle_get_id (bundle));
			break;
		}
	}

	/* fall back to PackageKit */
	if (management_plugin == NULL &&
	    as_app_get_pkgname_default (item) != NULL)
		management_plugin = "PackageKit";
	if (management_plugin != NULL)
		gs_app_set_management_plugin (app, management_plugin);
}

/**
 * gs_plugin_refine_item:
 */
static gboolean
gs_plugin_refine_item (GsPlugin *plugin, GsApp *app, AsApp *item, GError **error)
{
	AsRelease *rel;
	GHashTable *urls;
	GPtrArray *pkgnames;
	GPtrArray *kudos;
	const gchar *tmp;
	guint i;

	/* is an app */
	if (gs_app_get_kind (app) == AS_APP_KIND_UNKNOWN ||
	    gs_app_get_kind (app) == AS_APP_KIND_GENERIC) {
		if (as_app_get_kind (item) == AS_APP_KIND_SOURCE) {
			gs_app_set_kind (app, AS_APP_KIND_SOURCE);
		} else {
			gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
		}
	}

	/* is installed already */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN) {
		switch (as_app_get_source_kind (item)) {
		case AS_APP_SOURCE_KIND_APPDATA:
		case AS_APP_SOURCE_KIND_DESKTOP:
			gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
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

	/* set management plugin automatically */
	gs_plugin_refine_item_management_plugin (app, item);

	/* set id */
	if (as_app_get_id (item) != NULL && gs_app_get_id (app) == NULL)
		gs_app_set_id (app, as_app_get_id (item));

	/* set name */
	tmp = as_app_get_name (item, NULL);
	if (tmp != NULL) {
		if (g_str_has_prefix (tmp, "(Nightly) ")) {
			tmp += 10;
			if (gs_app_get_metadata_item (app, "X-XdgApp-Tags") == NULL)
				gs_app_set_metadata (app, "X-XdgApp-Tags", "nightly");
		}
		gs_app_set_name (app, GS_APP_QUALITY_HIGHEST, tmp);
	}

	/* set summary */
	tmp = as_app_get_comment (item, NULL);
	if (tmp != NULL) {
		gs_app_set_summary (app, GS_APP_QUALITY_HIGHEST, tmp);
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
				    GS_APP_QUALITY_HIGHEST,
				    as_app_get_project_license (item));

	/* set keywords */
	if (as_app_get_keywords (item, NULL) != NULL &&
	    gs_app_get_keywords (app) == NULL) {
		gs_app_set_keywords (app, as_app_get_keywords (item, NULL));
		gs_app_add_kudo (app, GS_APP_KUDO_HAS_KEYWORDS);
	}

	/* set origin */
	if (as_app_get_origin (item) != NULL &&
	    gs_app_get_origin (app) == NULL) {
		gs_app_set_origin (app, as_app_get_origin (item));
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
		gs_app_set_description (app, GS_APP_QUALITY_HIGHEST, from_xml);
	}

	/* set icon */
	if (as_app_get_icon_default (item) != NULL && gs_app_get_pixbuf (app) == NULL)
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
	if (_as_app_has_compulsory_for_desktop (item, "GNOME") &&
	    gs_app_get_kind (app) == AS_APP_KIND_DESKTOP)
		gs_app_set_compulsory (app, TRUE);

	/* set id kind */
	if (gs_app_get_kind (app) == AS_APP_KIND_UNKNOWN)
		gs_app_set_kind (app, as_app_get_kind (item));

	/* copy all the metadata */
	gs_plugin_appstream_copy_metadata (app, item);

	/* set package names */
	pkgnames = as_app_get_pkgnames (item);
	if (pkgnames->len > 0 && gs_app_get_sources(app)->len == 0)
		gs_app_set_sources (app, pkgnames);

	/* set addons */
	gs_plugin_appstream_refine_add_addons (plugin, app, item);

	/* set screenshots */
	gs_plugin_appstream_refine_add_screenshots (app, item);

	/* are the screenshots perfect */
	if (gs_plugin_appstream_are_screenshots_perfect (item))
		gs_app_add_kudo (app, GS_APP_KUDO_PERFECT_SCREENSHOTS);

	/* was this application released recently */
	if (gs_plugin_appstream_is_recent_release (item))
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

	/* is there any update information */
	rel = as_app_get_release_default (item);
	if (rel != NULL) {
		tmp = as_release_get_description (rel, NULL);
		if (tmp != NULL) {
			g_autofree gchar *desc = NULL;
			desc = as_markup_convert (tmp,
						  AS_MARKUP_CONVERT_FORMAT_MARKDOWN,
						  error);
			if (desc == NULL)
				return FALSE;
			gs_app_set_update_details (app, desc);
		}
		gs_app_set_update_urgency (app, as_release_get_urgency (rel));
		gs_app_set_update_version (app, as_release_get_version (rel));
	}

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
	const gchar *id;
	AsApp *item = NULL;

	/* unfound */
	*found = FALSE;

	/* find anything that matches the ID */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;
	item = as_store_get_app_by_id (plugin->priv->store, id);
	if (item == NULL)
		return TRUE;

	/* set new properties */
	if (!gs_plugin_refine_item (plugin, app, item, error))
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
	AsApp *item = NULL;
	GPtrArray *sources;
	const gchar *pkgname;
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
		return TRUE;

	/* set new properties */
	return gs_plugin_refine_item (plugin, app, item, error);
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
	guint i;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin->priv->store_mutex);

	/* load XML files */
	if (!gs_plugin_appstream_startup (plugin, error))
		return FALSE;

	/* find any upgrades */
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		item = g_ptr_array_index (array, i);

		// FIXME: AS_APP_KIND_OS_UPGRADE
		if (as_app_get_kind (item) != AS_APP_KIND_UNKNOWN)
			continue;
		if (as_app_get_metadata_item (item, "X-IsUpgrade") == NULL)
			continue;

		/* create */
		app = gs_app_new (as_app_get_id (item));
		gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		if (!gs_plugin_refine_item (plugin, app, item, error))
			return FALSE;
		gs_plugin_add_app (list, app);
	}
	return TRUE;
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
	gboolean found = FALSE;
	GList *l;
	GsApp *app;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin->priv->store_mutex);

	/* load XML files */
	if (!gs_plugin_appstream_startup (plugin, error))
		return FALSE;

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
	AsApp *item;
	GsCategory *parent;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin->priv->store_mutex);

	/* load XML files */
	if (!gs_plugin_appstream_startup (plugin, error))
		return FALSE;

	/* get the two search terms */
	ptask = as_profile_start_literal (plugin->profile, "appstream::add-category-apps");
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
		if (g_strcmp0 (search_id1, "all") != 0 &&
		    !as_app_has_category (item, search_id1))
			continue;
		if (search_id2 != NULL && !as_app_has_category (item, search_id2))
			continue;

		/* got a search match, so add all the data we can */
		app = gs_app_new (as_app_get_id (item));
		if (!gs_plugin_refine_item (plugin, app, item, error))
			return FALSE;
		gs_plugin_add_app (list, app);
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
	if (!gs_plugin_refine_item (plugin, app, item, error))
		return FALSE;
	gs_app_set_search_sort_key (app, match_value);
	gs_plugin_add_app (list, app);
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
	AsApp *item;
	GPtrArray *array;
	gboolean ret = TRUE;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin->priv->store_mutex);

	/* load XML files */
	if (!gs_plugin_appstream_startup (plugin, error))
		return FALSE;

	/* search categories for the search term */
	ptask = as_profile_start_literal (plugin->profile, "appstream::search");
	array = as_store_get_apps (plugin->priv->store);
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
	AsApp *item;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin->priv->store_mutex);

	/* load XML files */
	if (!gs_plugin_appstream_startup (plugin, error))
		return FALSE;

	/* search categories for the search term */
	ptask = as_profile_start_literal (plugin->profile, "appstream::add_installed");
	array = as_store_get_apps (plugin->priv->store);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (as_app_get_state (item) == AS_APP_STATE_INSTALLED) {
			g_autoptr(GsApp) app = NULL;
			app = gs_app_new (as_app_get_id (item));
			if (!gs_plugin_refine_item (plugin, app, item, error))
				return FALSE;
			gs_plugin_add_app (list, app);
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
	AsApp *app;
	GPtrArray *array;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin->priv->store_mutex);

	/* load XML files */
	if (!gs_plugin_appstream_startup (plugin, error))
		return FALSE;

	/* find out how many packages are in each category */
	ptask = as_profile_start_literal (plugin->profile, "appstream::add-categories");
	array = as_store_get_apps (plugin->priv->store);
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
