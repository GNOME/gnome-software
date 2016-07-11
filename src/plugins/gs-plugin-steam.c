/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
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
#include <string.h>

#define GS_PLUGIN_STEAM_SCREENSHOT_URI	"http://cdn.akamai.steamstatic.com/steam/apps"

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* need metadata */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

typedef enum {
	GS_PLUGIN_STEAM_TOKEN_START		= 0x00,
	GS_PLUGIN_STEAM_TOKEN_STRING		= 0x01,
	GS_PLUGIN_STEAM_TOKEN_INTEGER		= 0x02,
	GS_PLUGIN_STEAM_TOKEN_END		= 0x08,
	GS_PLUGIN_STEAM_TOKEN_LAST,
} GsPluginSteamToken;

static const gchar *
gs_plugin_steam_token_kind_to_str (guint8 data)
{
	static gchar tmp[2] = { 0x00, 0x00 };

	if (data == GS_PLUGIN_STEAM_TOKEN_START)
		return "[SRT]";
	if (data == GS_PLUGIN_STEAM_TOKEN_STRING)
		return "[STR]";
	if (data == GS_PLUGIN_STEAM_TOKEN_INTEGER)
		return "[INT]";
	if (data == GS_PLUGIN_STEAM_TOKEN_END)
		return "[END]";

	/* guess */
	if (data == 0x03)
		return "[ETX]";
	if (data == 0x04)
		return "[EOT]";
	if (data == 0x05)
		return "[ENQ]";
	if (data == 0x06)
		return "[ACK]";
	if (data == 0x07)
		return "[BEL]";
	if (data == 0x09)
		return "[SMI]";

	/* printable */
	if (g_ascii_isprint (data)) {
		tmp[0] = data;
		return tmp;
	}
	return "[?]";
}

static guint32
gs_plugin_steam_consume_uint32 (guint8 *data, gsize data_len, guint *idx)
{
	guint32 tmp = *((guint32 *) &data[*idx + 1]);
	*idx += 4;
	return tmp;
}

static const gchar *
gs_plugin_steam_consume_string (guint8 *data, gsize data_len, guint *idx)
{
	const gchar *tmp;

	/* this may be an empty string */
	tmp = (const gchar *) &data[*idx+1];
	if (tmp[0] == '\0') {
		(*idx)++;
		return NULL;
	}
	*idx += strlen (tmp) + 1;
	return tmp;
}

static void
gs_plugin_steam_find_next_sync_point (guint8 *data, gsize data_len, guint *idx)
{
	guint i;
	for (i = *idx; i < data_len - 9; i++) {
		if (memcmp (&data[i], "\0\x02\0common\0", 8) == 0) {
			*idx = i - 1;
			return;
		}
	}
	*idx = 0xfffffffe;
}

static GHashTable *
gs_plugin_steam_add_app (GPtrArray *apps)
{
	GHashTable *app;
	app = g_hash_table_new_full (g_str_hash, g_str_equal,
				     g_free, (GDestroyNotify) g_variant_unref);
	g_ptr_array_add (apps, app);
	return app;
}

static GPtrArray *
gs_plugin_steam_parse_appinfo_file (const gchar *filename, GError **error)
{
	GPtrArray *apps;
	GHashTable *app = NULL;
	const gchar *tmp;
	guint8 *data = NULL;
	gsize data_len = 0;
	guint i = 0;
	gboolean debug =  g_getenv ("GS_PLUGIN_STEAM_DEBUG") != NULL;

	/* load file */
	if (!g_file_get_contents (filename, (gchar **) &data, &data_len, error))
		return NULL;

	/* a GPtrArray of GHashTable */
	apps = g_ptr_array_new_with_free_func ((GDestroyNotify) g_hash_table_unref);

	/* find the first application and avoid header */
	gs_plugin_steam_find_next_sync_point (data, data_len, &i);
	for (i = i + 1; i < data_len; i++) {
		if (debug)
			g_debug ("%04x {0x%02x} %s", i, data[i], gs_plugin_steam_token_kind_to_str (data[i]));
		if (data[i] == GS_PLUGIN_STEAM_TOKEN_START) {

			/* this is a new application/game */
			if (data[i+1] == 0x02) {
				/* reset */
				app = gs_plugin_steam_add_app (apps);
				i++;
				continue;
			}

			/* new group */
			if (g_ascii_isprint (data[i+1])) {
				tmp = gs_plugin_steam_consume_string (data, data_len, &i);
				if (debug)
					g_debug ("[%s] {", tmp);
				continue;
			}

			/* something went wrong */
			if (debug)
				g_debug ("CORRUPTION DETECTED");
			gs_plugin_steam_find_next_sync_point (data, data_len, &i);
			continue;
		}
		if (data[i] == GS_PLUGIN_STEAM_TOKEN_END) {
			if (debug)
				g_debug ("}");
			continue;
		}
		if (data[i] == GS_PLUGIN_STEAM_TOKEN_STRING) {
			const gchar *value;
			tmp = gs_plugin_steam_consume_string (data, data_len, &i);
			value = gs_plugin_steam_consume_string (data, data_len, &i);
			if (debug)
				g_debug ("\t%s=%s", tmp, value);
			if (tmp != NULL && value != NULL) {
				if (g_hash_table_lookup (app, tmp) != NULL)
					continue;
				g_hash_table_insert (app,
						     g_strdup (tmp),
						     g_variant_new_string (value));
			}
			continue;
		}
		if (data[i] == GS_PLUGIN_STEAM_TOKEN_INTEGER) {
			guint32 value;
			tmp = gs_plugin_steam_consume_string (data, data_len, &i);
			value = gs_plugin_steam_consume_uint32 (data, data_len, &i);
			if (debug)
				g_debug ("\t%s=%i", tmp, value);
			if (tmp != NULL) {
				if (g_hash_table_lookup (app, tmp) != NULL)
					continue;
				g_hash_table_insert (app,
						     g_strdup (tmp),
						     g_variant_new_uint32 (value));
			}
			continue;
		}
	}

	return apps;
}

static void
gs_plugin_steam_dump_apps (GPtrArray *apps)
{
	guint i;
	GHashTable *app;

	for (i = 0; i < apps->len; i++) {
		g_autoptr(GList) keys = NULL;
		GList *l;
		app = g_ptr_array_index (apps, i);
		keys = g_hash_table_get_keys (app);
		for (l = keys; l != NULL; l = l->next) {
			const gchar *tmp;
			GVariant *value;
			tmp = l->data;
			value = g_hash_table_lookup (app, tmp);
			if (g_strcmp0 (g_variant_get_type_string (value), "s") == 0)
				g_print ("%s=%s\n", tmp, g_variant_get_string (value, NULL));
			else if (g_strcmp0 (g_variant_get_type_string (value), "u") == 0)
				g_print ("%s=%u\n", tmp, g_variant_get_uint32 (value));
		}
		g_print ("\n");
	}
}

/*
 * gs_plugin_steam_capture:
 *
 * Returns: A string between @start and @end, or %NULL
 **/
static gchar *
gs_plugin_steam_capture (const gchar *html,
			 const gchar *start,
			 const gchar *end,
			 guint *offset)
{
	guint i;
	guint j;
	guint start_len;
	guint end_len;

	/* invalid */
	if (html == NULL)
		return NULL;

	/* find @start */
	start_len = strlen (start);
	for (i = *offset; html[i] != '\0'; i++) {
		if (memcmp (&html[i], start, start_len) != 0)
			continue;
		/* find @end */
		end_len = strlen (end);
		for (j = i + start_len; html[j] != '\0'; j++) {
			if (memcmp (&html[j], end, end_len) != 0)
				continue;
			*offset = j + end_len;
			return g_strndup (&html[i + start_len],
					  j - i - start_len);
		}
	}
	return NULL;
}

static gboolean
gs_plugin_steam_update_screenshots (AsApp *app, const gchar *html, GError **error)
{
	const gchar *gameid_str;
	gchar *tmp1;
	guint i = 0;
	guint idx = 0;

	/* find all the screenshots */
	gameid_str = as_app_get_metadata_item (app, "X-Steam-GameID");
	while ((tmp1 = gs_plugin_steam_capture (html, "data-screenshotid=\"", "\"", &i))) {
		g_autoptr(AsImage) im = NULL;
		g_autoptr(AsScreenshot) ss = NULL;
		g_autofree gchar *cdn_uri = NULL;

		/* create an image */
		im = as_image_new ();
		as_image_set_kind (im, AS_IMAGE_KIND_SOURCE);
		cdn_uri = g_build_filename (GS_PLUGIN_STEAM_SCREENSHOT_URI,
					    gameid_str, tmp1, NULL);
		as_image_set_url (im, cdn_uri);

		/* create screenshot with no caption */
		ss = as_screenshot_new ();
		as_screenshot_set_kind (ss, idx == 0 ? AS_SCREENSHOT_KIND_DEFAULT :
						       AS_SCREENSHOT_KIND_NORMAL);
		as_screenshot_add_image (ss, im);
		as_app_add_screenshot (app, ss);
		g_free (tmp1);

		/* limit this to a sane number */
		if (idx++ >= 4)
			break;
	}
	return TRUE;
}

static gboolean
gs_plugin_steam_update_description (AsApp *app,
				    const gchar *html,
				    GError **error)
{
	guint i = 0;
	g_autofree gchar *desc = NULL;
	g_autofree gchar *subsect = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GString) subsect_str = NULL;

	/* get the game description div section */
	subsect = gs_plugin_steam_capture (html,
			"<div id=\"game_area_description\" class=\"game_area_description\">",
			"</div>", &i);

	/* fall back gracefully */
	if (subsect == NULL) {
		subsect = gs_plugin_steam_capture (html,
				"<meta name=\"Description\" content=\"",
				"\">", &i);
	}
	if (subsect == NULL) {
		g_warning ("Failed to get description for %s [%s]",
			   as_app_get_name (app, NULL),
			   as_app_get_id (app));
		return TRUE;
	}
	subsect_str = g_string_new (subsect);
	as_utils_string_replace (subsect_str, "About This Game", "");
	desc = as_markup_import (subsect_str->str,
				 AS_MARKUP_CONVERT_FORMAT_HTML,
				 &error_local);
	if (desc == NULL) {
		g_warning ("Failed to parse description for %s [%s]: %s",
			   as_app_get_name (app, NULL),
			   as_app_get_id (app),
			   error_local->message);
		return TRUE;
	}
	as_app_set_description (app, NULL, desc);
	return TRUE;
}

static gboolean
gs_plugin_steam_download_icon (GsPlugin *plugin,
			       AsApp *app,
			       const gchar *uri,
			       GError **error)
{
	gsize data_len;
	g_autofree gchar *cache_basename = NULL;
	g_autofree gchar *cache_fn = NULL;
	g_autofree gchar *cache_png = NULL;
	g_autofree gchar *data = NULL;
	g_autoptr(AsIcon) icon = NULL;
	g_autoptr(GdkPixbuf) pb = NULL;

	/* download icons from the cdn */
	cache_basename = g_path_get_basename (uri);
	cache_fn = gs_utils_get_cache_filename ("steam",
						cache_basename,
						GS_UTILS_CACHE_FLAG_NONE,
						error);
	if (cache_fn == NULL)
		return FALSE;
	if (g_file_test (cache_fn, G_FILE_TEST_EXISTS)) {
		if (!g_file_get_contents (cache_fn, &data, &data_len, error))
			return FALSE;
	} else {
		if (!gs_mkdir_parent (cache_fn, error))
			return FALSE;
		if (!gs_plugin_download_file (plugin,
					      NULL, /* GsApp */
					      uri,
					      cache_fn,
					      NULL, /* GCancellable */
					      error))
			return FALSE;
	}

	/* load the icon as large as possible */
	pb = gdk_pixbuf_new_from_file (cache_fn, error);
	if (pb == NULL)
		return FALSE;

	/* too small? */
	if (gdk_pixbuf_get_width (pb) < 48 ||
	    gdk_pixbuf_get_height (pb) < 48) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "icon is too small %ix%i",
			     gdk_pixbuf_get_width (pb),
			     gdk_pixbuf_get_height (pb));
		return FALSE;
	}

	/* save to cache */
	memcpy (cache_basename + 40, ".png\0", 5);
	cache_png = gs_utils_get_cache_filename ("steam",
						 cache_basename,
						 GS_UTILS_CACHE_FLAG_WRITEABLE,
						 error);
	if (cache_png == NULL)
		return FALSE;
	if (!gdk_pixbuf_save (pb, cache_png, "png", error, NULL))
		return FALSE;

	/* add an icon */
	icon = as_icon_new ();
	as_icon_set_kind (icon, AS_ICON_KIND_LOCAL);
	as_icon_set_filename (icon, cache_png);
	as_app_add_icon (app, icon);
	return TRUE;
}

static gboolean
gs_plugin_steam_update_store_app (GsPlugin *plugin,
				  AsStore *store,
				  GHashTable *app,
				  GError **error)
{
	const gchar *name;
	GVariant *tmp;
	guint32 gameid;
	gchar *app_id;
	g_autofree gchar *cache_basename = NULL;
	g_autofree gchar *cache_fn = NULL;
	g_autofree gchar *gameid_str = NULL;
	g_autofree gchar *html = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(AsApp) item = NULL;

	/* this is the key */
	tmp = g_hash_table_lookup (app, "gameid");
	if (tmp == NULL)
		return TRUE;
	gameid = g_variant_get_uint32 (tmp);

	/* valve use the name as the application ID, not the gameid */
	tmp = g_hash_table_lookup (app, "name");
	if (tmp == NULL)
		return TRUE;
	name = g_variant_get_string (tmp, NULL);
	app_id = g_strdup_printf ("%s.desktop", name);

	/* already exists */
	if (as_store_get_app_by_id (store, app_id) != NULL) {
		g_debug ("already exists %i, skipping", gameid);
		return TRUE;
	}

	/* create application with the gameid as the key */
	g_debug ("parsing steam %i", gameid);
	item = as_app_new ();
	as_app_set_kind (item, AS_APP_KIND_DESKTOP);
	as_app_set_project_license (item, "Steam");
	as_app_set_id (item, app_id);
	as_app_set_name (item, NULL, name);
	as_app_add_category (item, "Game");
	as_app_add_kudo_kind (item, AS_KUDO_KIND_MODERN_TOOLKIT);
	as_app_set_comment (item, NULL, "Available on Steam");

	/* this is for the GNOME Software plugin */
	gameid_str = g_strdup_printf ("%" G_GUINT32_FORMAT, gameid);
	as_app_add_metadata (item, "X-Steam-GameID", gameid_str);
	as_app_add_metadata (item, "GnomeSoftware::Plugin", "steam");

	/* ban certains apps based on the name */
	if (g_strstr_len (name, -1, "Dedicated Server") != NULL)
		as_app_add_veto (item, "Dedicated Server");

	/* oslist */
	tmp = g_hash_table_lookup (app, "oslist");
	if (tmp == NULL) {
		as_app_add_veto (item, "No operating systems listed");
	} else if (g_strstr_len (g_variant_get_string (tmp, NULL), -1, "linux") == NULL) {
		as_app_add_veto (item, "No Linux support");
	}

	/* url: homepage */
	tmp = g_hash_table_lookup (app, "homepage");
	if (tmp != NULL)
		as_app_add_url (item, AS_URL_KIND_HOMEPAGE, g_variant_get_string (tmp, NULL));

	/* developer name */
	tmp = g_hash_table_lookup (app, "developer");
	if (tmp != NULL)
		as_app_set_developer_name (item, NULL, g_variant_get_string (tmp, NULL));

	/* type */
	tmp = g_hash_table_lookup (app, "type");
	if (tmp != NULL) {
		const gchar *kind = g_variant_get_string (tmp, NULL);
		if (g_strcmp0 (kind, "DLC") == 0 ||
		    g_strcmp0 (kind, "Config") == 0 ||
		    g_strcmp0 (kind, "Tool") == 0)
			as_app_add_veto (item, "type is %s", kind);
	}

	/* don't bother saving apps with failures */
	if (as_app_get_vetos(item)->len > 0)
		return TRUE;

	/* icons */
	tmp = g_hash_table_lookup (app, "clienticns");
	if (tmp != NULL) {
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *ic_uri = NULL;
		ic_uri = g_strdup_printf ("https://steamcdn-a.akamaihd.net/steamcommunity/public/images/apps/%i/%s.icns",
					  gameid, g_variant_get_string (tmp, NULL));
		if (!gs_plugin_steam_download_icon (plugin, item, ic_uri, &error_local)) {
			g_warning ("Failed to parse clienticns: %s",
				   error_local->message);
		}
	}

	/* try clienticon */
	if (as_app_get_icons(item)->len == 0) {
		tmp = g_hash_table_lookup (app, "clienticon");
		if (tmp != NULL) {
			g_autoptr(GError) error_local = NULL;
			g_autofree gchar *ic_uri = NULL;
			ic_uri = g_strdup_printf ("http://cdn.akamai.steamstatic.com/steamcommunity/public/images/apps/%i/%s.ico",
						  gameid, g_variant_get_string (tmp, NULL));
			if (!gs_plugin_steam_download_icon (plugin, item, ic_uri, &error_local)) {
				g_warning ("Failed to parse clienticon: %s",
					   error_local->message);
			}
		}
	}

	/* fall back to a resized logo */
	if (as_app_get_icons(item)->len == 0) {
		tmp = g_hash_table_lookup (app, "logo");
		if (tmp != NULL) {
			AsIcon *icon = NULL;
			g_autofree gchar *ic_uri = NULL;
			ic_uri = g_strdup_printf ("http://cdn.akamai.steamstatic.com/steamcommunity/public/images/apps/%i/%s.jpg",
						  gameid, g_variant_get_string (tmp, NULL));
			icon = as_icon_new ();
			as_icon_set_kind (icon, AS_ICON_KIND_REMOTE);
			as_icon_set_url (icon, ic_uri);
			as_app_add_icon (item, icon);
		}
	}

	/* size */
	tmp = g_hash_table_lookup (app, "maxsize");
	if (tmp != NULL) {
		/* string when over 16Gb... :/ */
		if (g_strcmp0 (g_variant_get_type_string (tmp), "u") == 0) {
			g_autofree gchar *val = NULL;
			val = g_strdup_printf ("%" G_GUINT32_FORMAT,
					       g_variant_get_uint32 (tmp));
			as_app_add_metadata (item, "X-Steam-Size", val);
		} else {
			as_app_add_metadata (item, "X-Steam-Size",
					     g_variant_get_string (tmp, NULL));
		}
	}

	/* download page from the store */
	cache_basename = g_strdup_printf ("%s.html", gameid_str);
	cache_fn = gs_utils_get_cache_filename ("steam",
						cache_basename,
						GS_UTILS_CACHE_FLAG_WRITEABLE,
						error);
	if (cache_fn == NULL)
		return FALSE;
	if (!g_file_test (cache_fn, G_FILE_TEST_EXISTS)) {
		g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));
		uri = g_strdup_printf ("http://store.steampowered.com/app/%s/", gameid_str);
		if (!gs_plugin_download_file (plugin,
					      app_dl,
					      uri,
					      cache_fn,
					      NULL, /* GCancellable */
					      error))
			return FALSE;
	}

	/* get screenshots and descriptions */
	if (!g_file_get_contents (cache_fn, &html, NULL, error))
		return FALSE;
	if (!gs_plugin_steam_update_screenshots (item, html, error))
		return FALSE;
	if (!gs_plugin_steam_update_description (item, html, error))
		return FALSE;

	/* add */
	as_store_add_app (store, item);
	return TRUE;
}

static gboolean
gs_plugin_steam_update_store (GsPlugin *plugin, AsStore *store, GPtrArray *apps, GError **error)
{
	guint i;
	GHashTable *app;
	g_autoptr(GsApp) dummy = gs_app_new (NULL);

	for (i = 0; i < apps->len; i++) {
		app = g_ptr_array_index (apps, i);
		if (!gs_plugin_steam_update_store_app (plugin, store, app, error))
			return FALSE;

		/* update progress */
		gs_app_set_progress (dummy, (gdouble) i * 100.f / (gdouble) apps->len);
		gs_plugin_status_update (plugin, dummy, GS_PLUGIN_STATUS_DOWNLOADING);
	}
	return TRUE;
}

static gboolean
gs_plugin_steam_refresh (GsPlugin *plugin,
			 guint cache_age,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GPtrArray) apps = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *fn_xml = NULL;

	/* check if exists */
	fn = g_build_filename (g_get_user_data_dir (),
			       "Steam", "appcache", "appinfo.vdf", NULL);
	if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
		g_debug ("no %s, so skipping", fn);
		return TRUE;
	}

	/* test cache age */
	fn_xml = g_build_filename (g_get_user_data_dir (),
				   "app-info", "xmls", "steam.xml.gz", NULL);
	file = g_file_new_for_path (fn_xml);
	if (cache_age > 0) {
		guint tmp;
		tmp = gs_utils_get_file_age (file);
		if (tmp < cache_age) {
			g_debug ("%s is only %i seconds old, so ignoring refresh",
				 fn_xml, tmp);
			return TRUE;
		}
	}

	/* parse it */
	apps = gs_plugin_steam_parse_appinfo_file (fn, error);
	if (apps == NULL)
		return FALSE;

	/* debug */
	if (g_getenv ("GS_PLUGIN_STEAM_DEBUG") != NULL)
		gs_plugin_steam_dump_apps (apps);

	/* load existing AppStream XML */
	store = as_store_new ();
	as_store_set_origin (store, "steam");
	if (g_file_query_exists (file, cancellable)) {
		if (!as_store_from_file (store, file, NULL, cancellable, error))
			return FALSE;
	}

	/* update any new applications */
	if (!gs_plugin_steam_update_store (plugin, store, apps, error))
		return FALSE;

	/* save new file */
	return as_store_to_file (store, file,
				 AS_NODE_TO_XML_FLAG_FORMAT_INDENT |
				 AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE,
				 NULL,
				 error);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	return gs_plugin_steam_refresh (plugin, cache_age, cancellable, error);
}

static GHashTable *
gs_plugin_steam_load_app_manifest (const gchar *fn, GError **error)
{
	GHashTable *manifest = NULL;
	guint i;
	guint j;
	g_autofree gchar *data = NULL;
	g_auto(GStrv) lines = NULL;

	/* get file */
	if (!g_file_get_contents (fn, &data, NULL, error))
		return NULL;

	/* parse each line */
	manifest = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	lines = g_strsplit (data, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		gboolean is_key = TRUE;
		const gchar *tmp = lines[i];
		g_autoptr(GString) key = g_string_new ("");
		g_autoptr(GString) value = g_string_new ("");
		for (j = 0; tmp[j] != '\0'; j++) {

			/* alphanum, so either key or value */
			if (g_ascii_isalnum (tmp[j])) {
				g_string_append_c (is_key ? key : value, tmp[j]);
				continue;
			}

			/* first whitespace after the key */
			if (g_ascii_isspace (tmp[j]) && key->len > 0)
				is_key = FALSE;
		}
		if (g_getenv ("GS_PLUGIN_STEAM_DEBUG") != NULL)
			g_debug ("manifest %s=%s", key->str, value->str);
		if (key->len == 0 || value->len == 0)
			continue;
		g_hash_table_insert (manifest,
				     g_strdup (key->str),
				     g_strdup (value->str));
	}
	return manifest;
}

typedef enum {
	GS_STEAM_STATE_FLAG_INVALID		= 0,
	GS_STEAM_STATE_FLAG_UNINSTALLED		= 1 << 0,
	GS_STEAM_STATE_FLAG_UPDATE_REQUIRED	= 1 << 1,
	GS_STEAM_STATE_FLAG_FULLY_INSTALLED	= 1 << 2,
	GS_STEAM_STATE_FLAG_ENCRYPTED		= 1 << 3,
	GS_STEAM_STATE_FLAG_LOCKED		= 1 << 4,
	GS_STEAM_STATE_FLAG_FILES_MISSING	= 1 << 5,
	GS_STEAM_STATE_FLAG_APP_RUNNING		= 1 << 6,
	GS_STEAM_STATE_FLAG_FILES_CORRUPT	= 1 << 7,
	GS_STEAM_STATE_FLAG_UPDATE_RUNNING	= 1 << 8,
	GS_STEAM_STATE_FLAG_UPDATE_PAUSED	= 1 << 9,
	GS_STEAM_STATE_FLAG_UPDATE_STARTED	= 1 << 10,
	GS_STEAM_STATE_FLAG_UNINSTALLING	= 1 << 11,
	GS_STEAM_STATE_FLAG_BACKUP_RUNNING	= 1 << 12,
	/* not sure what happened here... */
	GS_STEAM_STATE_FLAG_RECONFIGURING	= 1 << 16,
	GS_STEAM_STATE_FLAG_VALIDATING		= 1 << 17,
	GS_STEAM_STATE_FLAG_ADDING_FILES	= 1 << 18,
	GS_STEAM_STATE_FLAG_PREALLOCATING	= 1 << 19,
	GS_STEAM_STATE_FLAG_DOWNLOADING		= 1 << 20,
	GS_STEAM_STATE_FLAG_STAGING		= 1 << 21,
	GS_STEAM_STATE_FLAG_COMMITTING		= 1 << 22,
	GS_STEAM_STATE_FLAG_UPDATE_STOPPING	= 1 << 23,
	GS_STEAM_STATE_FLAG_LAST
} GsSteamStateFlags;

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *gameid;
	const gchar *tmp;
	g_autofree gchar *manifest_basename = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(GHashTable) manifest = NULL;

	/* check is us */
	gameid = gs_app_get_metadata_item (app, "X-Steam-GameID");
	if (gameid == NULL)
		return TRUE;

	/* is this true? */
	gs_app_set_kind (app, AS_ID_KIND_DESKTOP);

	/* no way of knowing */
	if (gs_app_get_size_download (app) == 0)
		gs_app_set_size_download (app, GS_APP_SIZE_UNKNOWABLE);

	/* hardcoded */
	if (gs_app_get_origin_hostname (app) == NULL)
		gs_app_set_origin_hostname (app, "steampowered.com");

	/* size */
	tmp = gs_app_get_metadata_item (app, "X-Steam-Size");
	if (tmp != NULL) {
		guint64 sz;
		sz = g_ascii_strtoull (tmp, NULL, 10);
		if (sz > 0)
			gs_app_set_size_installed (app, sz);
	}

	/* check manifest */
	manifest_basename = g_strdup_printf ("appmanifest_%s.acf", gameid);
	fn = g_build_filename (g_get_user_data_dir (),
			       "Steam",
			       "steamapps",
			       manifest_basename,
			       NULL);
	if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
		/* can never have been installed */
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		return TRUE;
	}
	manifest = gs_plugin_steam_load_app_manifest (fn, error);
	if (manifest == NULL)
		return FALSE;

	/* this is better than the download size */
	tmp = g_hash_table_lookup (manifest, "SizeOnDisk");
	if (tmp != NULL) {
		guint64 sz;
		sz = g_ascii_strtoull (tmp, NULL, 10);
		if (sz > 0)
			gs_app_set_size_installed (app, sz);
	}

	/* set state */
	tmp = g_hash_table_lookup (manifest, "StateFlags");
	if (tmp != NULL) {
		guint64 state_flags;

		/* set state */
		state_flags = g_ascii_strtoull (tmp, NULL, 10);
		if (state_flags & GS_STEAM_STATE_FLAG_DOWNLOADING ||
		    state_flags & GS_STEAM_STATE_FLAG_PREALLOCATING ||
		    state_flags & GS_STEAM_STATE_FLAG_ADDING_FILES ||
		    state_flags & GS_STEAM_STATE_FLAG_COMMITTING ||
		    state_flags & GS_STEAM_STATE_FLAG_STAGING)
			gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		else if (state_flags & GS_STEAM_STATE_FLAG_UNINSTALLING)
			gs_app_set_state (app, AS_APP_STATE_REMOVING);
		else if (state_flags & GS_STEAM_STATE_FLAG_FULLY_INSTALLED)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		else if (state_flags & GS_STEAM_STATE_FLAG_UNINSTALLED)
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	}

	/* set install date */
	tmp = g_hash_table_lookup (manifest, "LastUpdated");
	if (tmp != NULL) {
		guint64 ts;
		ts = g_ascii_strtoull (tmp, NULL, 10);
		if (ts > 0)
			gs_app_set_install_date (app, ts);
	}

	return TRUE;
}

gboolean
gs_plugin_app_install (GsPlugin *plugin, GsApp *app,
		       GCancellable *cancellable, GError **error)
{
	const gchar *gameid;
	g_autofree gchar *cmdline = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* this is async as steam is a different process: FIXME: use D-Bus */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	gameid = gs_app_get_metadata_item (app, "X-Steam-GameID");
	cmdline = g_strdup_printf ("steam steam://install/%s", gameid);
	return g_spawn_command_line_sync (cmdline, NULL, NULL, NULL, error);
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin, GsApp *app,
		      GCancellable *cancellable, GError **error)
{
	const gchar *gameid;
	g_autofree gchar *cmdline = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* this is async as steam is a different process: FIXME: use D-Bus */
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	gameid = gs_app_get_metadata_item (app, "X-Steam-GameID");
	cmdline = g_strdup_printf ("steam steam://uninstall/%s", gameid);
	return g_spawn_command_line_sync (cmdline, NULL, NULL, NULL, error);
}

gboolean
gs_plugin_launch (GsPlugin *plugin, GsApp *app,
		  GCancellable *cancellable, GError **error)
{
	const gchar *gameid;
	g_autofree gchar *cmdline = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* this is async as steam is a different process: FIXME: use D-Bus */
	gameid = gs_app_get_metadata_item (app, "X-Steam-GameID");
	cmdline = g_strdup_printf ("steam steam://run/%s", gameid);
	return g_spawn_command_line_sync (cmdline, NULL, NULL, NULL, error);
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	/* just ensure there is any data, no matter how old */
	return gs_plugin_steam_refresh (plugin, G_MAXUINT, cancellable, error);
}
