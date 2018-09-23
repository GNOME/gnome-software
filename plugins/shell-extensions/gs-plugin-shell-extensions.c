/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
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

#include <errno.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include <gnome-software.h>

#define SHELL_EXTENSIONS_API_URI 		"https://extensions.gnome.org/"

/*
 * Things we want from the API:
 *
 *  - Size on disk/download
 *  - Existing review data for each extension?
 *  - A local icon for an installed shell extension
 *
 * See https://git.gnome.org/browse/extensions-web/tree/sweettooth/extensions/views.py
 * for the source to the web application.
 */

struct GsPluginData {
	GDBusProxy	*proxy;
	gchar		*shell_version;
	GsApp		*cached_origin;
	GSettings	*settings;
};

typedef enum {
	GS_PLUGIN_SHELL_EXTENSION_STATE_ENABLED		= 1,
	GS_PLUGIN_SHELL_EXTENSION_STATE_DISABLED	= 2,
	GS_PLUGIN_SHELL_EXTENSION_STATE_ERROR		= 3,
	GS_PLUGIN_SHELL_EXTENSION_STATE_OUT_OF_DATE	= 4,
	GS_PLUGIN_SHELL_EXTENSION_STATE_DOWNLOADING	= 5,
	GS_PLUGIN_SHELL_EXTENSION_STATE_INITIALIZED	= 6,
	GS_PLUGIN_SHELL_EXTENSION_STATE_UNINSTALLED	= 99,
	GS_PLUGIN_SHELL_EXTENSION_STATE_LAST
} GsPluginShellExtensionState;

typedef enum {
	GS_PLUGIN_SHELL_EXTENSION_KIND_SYSTEM		= 1,
	GS_PLUGIN_SHELL_EXTENSION_KIND_PER_USER		= 2,
	GS_PLUGIN_SHELL_EXTENSION_KIND_LAST
} GsPluginShellExtensionKind;

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* add source */
	priv->cached_origin = gs_app_new (gs_plugin_get_name (plugin));
	gs_app_set_kind (priv->cached_origin, AS_APP_KIND_SOURCE);
	gs_app_set_origin_hostname (priv->cached_origin, SHELL_EXTENSIONS_API_URI);

	priv->settings = g_settings_new ("org.gnome.software");

	/* add the source to the plugin cache which allows us to match the
	 * unique ID to a GsApp when creating an event */
	gs_plugin_cache_add (plugin,
			     gs_app_get_unique_id (priv->cached_origin),
			     priv->cached_origin);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_free (priv->shell_version);
	if (priv->proxy != NULL)
		g_object_unref (priv->proxy);
	g_object_unref (priv->cached_origin);
	g_object_unref (priv->settings);
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_kind (app) == AS_APP_KIND_SHELL_EXTENSION &&
	    gs_app_get_scope (app) == AS_APP_SCOPE_USER) {
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	}
}

static AsAppState
gs_plugin_shell_extensions_convert_state (guint value)
{
	switch (value) {
	case GS_PLUGIN_SHELL_EXTENSION_STATE_DISABLED:
	case GS_PLUGIN_SHELL_EXTENSION_STATE_DOWNLOADING:
	case GS_PLUGIN_SHELL_EXTENSION_STATE_ENABLED:
	case GS_PLUGIN_SHELL_EXTENSION_STATE_ERROR:
	case GS_PLUGIN_SHELL_EXTENSION_STATE_INITIALIZED:
	case GS_PLUGIN_SHELL_EXTENSION_STATE_OUT_OF_DATE:
		return AS_APP_STATE_INSTALLED;
	case GS_PLUGIN_SHELL_EXTENSION_STATE_UNINSTALLED:
		return AS_APP_STATE_AVAILABLE;
	default:
		g_warning ("unknown state %u", value);
	}
	return AS_APP_STATE_UNKNOWN;
}

static GsApp *
gs_plugin_shell_extensions_parse_installed (GsPlugin *plugin,
                                            const gchar *uuid,
                                            GVariantIter *iter,
                                            GError **error)
{
	const gchar *tmp;
	gchar *str;
	GVariant *val;
	g_autofree gchar *id = NULL;
	g_autoptr(AsIcon) ic = NULL;
	g_autoptr(GsApp) app = NULL;

	id = as_utils_appstream_id_build (uuid);
	app = gs_app_new (id);
	gs_app_set_metadata (app, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_set_metadata (app, "shell-extensions::uuid", uuid);
	gs_app_set_kind (app, AS_APP_KIND_SHELL_EXTENSION);
	gs_app_set_license (app, GS_APP_QUALITY_NORMAL, "GPL-2.0+");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "GNOME Shell Extension");
	while (g_variant_iter_loop (iter, "{sv}", &str, &val)) {
		if (g_strcmp0 (str, "description") == 0) {
			g_autofree gchar *tmp1 = NULL;
			g_autofree gchar *tmp2 = NULL;
			tmp1 = as_markup_import (g_variant_get_string (val, NULL),
						 AS_MARKUP_CONVERT_FORMAT_SIMPLE,
						 NULL);
			tmp2 = as_markup_convert_simple (tmp1, error);
			if (tmp2 == NULL) {
				gs_utils_error_convert_appstream (error);
				return NULL;
			}
			gs_app_set_description (app, GS_APP_QUALITY_NORMAL, tmp2);
			continue;
		}
		if (g_strcmp0 (str, "name") == 0) {
			gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
					 g_variant_get_string (val, NULL));
			continue;
		}
		if (g_strcmp0 (str, "url") == 0) {
			gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
					g_variant_get_string (val, NULL));
			continue;
		}
		if (g_strcmp0 (str, "type") == 0) {
			guint val_int = (guint) g_variant_get_double (val);
			switch (val_int) {
			case GS_PLUGIN_SHELL_EXTENSION_KIND_SYSTEM:
				gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
				break;
			case GS_PLUGIN_SHELL_EXTENSION_KIND_PER_USER:
				gs_app_set_scope (app, AS_APP_SCOPE_USER);
				break;
			default:
				g_warning ("%s unknown type %u", uuid, val_int);
				break;
			}
			continue;
		}
		if (g_strcmp0 (str, "state") == 0) {
			AsAppState st;
			guint val_int = (guint) g_variant_get_double (val);
			st = gs_plugin_shell_extensions_convert_state (val_int);
			gs_app_set_state (app, st);
			continue;
		}
		if (g_strcmp0 (str, "error") == 0) {
			tmp = g_variant_get_string (val, NULL);
			if (tmp != NULL && tmp[0] != '\0') {
				g_warning ("unhandled shell error: %s", tmp);
			}
			continue;
		}
		if (g_strcmp0 (str, "hasPrefs") == 0) {
			if (g_variant_get_boolean (val))
				gs_app_set_metadata (app, "shell-extensions::has-prefs", "");
			continue;
		}
		if (g_strcmp0 (str, "extension-id") == 0) {
			tmp = g_variant_get_string (val, NULL);
			gs_app_set_metadata (app, "shell-extensions::extension-id", tmp);
			continue;
		}
		if (g_strcmp0 (str, "path") == 0) {
			tmp = g_variant_get_string (val, NULL);
			gs_app_set_metadata (app, "shell-extensions::path", tmp);
			continue;
		}
	}

	/* hardcode icon */
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "application-x-addon-symbolic");
	gs_app_add_icon (app, ic);

	/* add categories */
	gs_app_add_category (app, "Addon");
	gs_app_add_category (app, "ShellExtension");

	return g_steal_pointer (&app);
}

static void
gs_plugin_shell_extensions_changed_cb (GDBusProxy *proxy,
				       const gchar *sender_name,
				       const gchar *signal_name,
				       GVariant *parameters,
				       GsPlugin *plugin)
{
	if (g_strcmp0 (signal_name, "ExtensionStatusChanged") == 0) {
		AsAppState st;
		GsApp *app;
		const gchar *error_str;
		const gchar *uuid;
		guint state;

		/* get what changed */
		g_variant_get (parameters, "(&si&s)",
			       &uuid, &state, &error_str);

		/* find it in the cache; do we care? */
		app = gs_plugin_cache_lookup (plugin, uuid);
		if (app == NULL) {
			g_debug ("no app for changed %s", uuid);
			return;
		}

		/* set the new state in the UI */
		st = gs_plugin_shell_extensions_convert_state (state);
		gs_app_set_state (app, st);

		/* not sure what to do here */
		if (error_str != NULL && error_str[0] != '\0') {
			g_warning ("%s has error: %s",
				   gs_app_get_id (app),
				   error_str);
		}
	}
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *name_owner = NULL;
	g_autoptr(GVariant) version = NULL;

	if (priv->proxy != NULL)
		return TRUE;
	priv->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
						     G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION,
						     NULL,
						     "org.gnome.Shell",
						     "/org/gnome/Shell",
						     "org.gnome.Shell.Extensions",
						     cancellable,
						     error);
	if (priv->proxy == NULL) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	/* not running under Shell */
	name_owner = g_dbus_proxy_get_name_owner (priv->proxy);
	if (name_owner == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "gnome-shell is not running");
		return FALSE;
	}

	g_signal_connect (priv->proxy, "g-signal",
			  G_CALLBACK (gs_plugin_shell_extensions_changed_cb), plugin);

	/* get the GNOME Shell version */
	version = g_dbus_proxy_get_cached_property (priv->proxy,
						    "ShellVersion");
	if (version != NULL)
		priv->shell_version = g_variant_dup_string (version, NULL);
	return TRUE;
}

gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GVariantIter *ext_iter;
	gchar *ext_uuid;
	g_autoptr(GVariantIter) iter = NULL;
	g_autoptr(GVariant) retval = NULL;

	/* installed */
	retval = g_dbus_proxy_call_sync (priv->proxy,
					 "ListExtensions",
					 NULL,
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 cancellable,
					 error);
	if (retval == NULL) {
		gs_utils_error_convert_gdbus (error);
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	/* parse each installed extension */
	g_variant_get (retval, "(a{sa{sv}})", &iter);
	while (g_variant_iter_loop (iter, "{sa{sv}}", &ext_uuid, &ext_iter)) {
		g_autoptr(GsApp) app = NULL;

		/* search in the cache */
		app = gs_plugin_cache_lookup (plugin, ext_uuid);
		if (app != NULL) {
			gs_app_list_add (list, app);
			continue;
		}

		/* parse the data into an GsApp */
		app = gs_plugin_shell_extensions_parse_installed (plugin,
		                                                  ext_uuid,
		                                                  ext_iter,
		                                                  error);
		if (app == NULL)
			return FALSE;

		/* ignore system installed */
		if (gs_app_get_scope (app) == AS_APP_SCOPE_SYSTEM)
			continue;

		/* save in the cache */
		gs_plugin_cache_add (plugin, ext_uuid, app);

		/* add to results */
		gs_app_list_add (list, app);
	}
	return TRUE;
}

gboolean
gs_plugin_add_sources (GsPlugin *plugin,
                       GsAppList *list,
                       GCancellable *cancellable,
                       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsApp) app = NULL;

	/* create something that we can use to enable/disable */
	app = gs_app_new ("org.gnome.extensions");
	gs_app_set_kind (app, AS_APP_KIND_SOURCE);
	gs_app_set_scope (app, AS_APP_SCOPE_USER);
	if (g_settings_get_boolean (priv->settings, "enable-shell-extensions-repo"))
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	else
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST,
	                 _("GNOME Shell Extensions Repository"));
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
	                SHELL_EXTENSIONS_API_URI);
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_list_add (list, app);
	return TRUE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *uuid;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* can we get the AppStream-created app state using the cache */
	uuid = gs_app_get_metadata_item (app, "shell-extensions::uuid");
	if (uuid != NULL && gs_app_get_state (app) == AS_APP_STATE_UNKNOWN) {
		GsApp *app_cache = gs_plugin_cache_lookup (plugin, uuid);
		if (app_cache != NULL) {
			g_debug ("copy cached state for %s",
				 gs_app_get_id (app));
			gs_app_set_state (app, gs_app_get_state (app_cache));
		}
	}

	/* assume apps are available if they exist in AppStream metadata */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

	/* FIXME: assume these are small */
	if (gs_app_get_size_installed (app) == 0)
		gs_app_set_size_installed (app, 1024 * 50);
	if (gs_app_get_size_download (app) == 0)
		gs_app_set_size_download (app, GS_APP_SIZE_UNKNOWABLE);

	return TRUE;
}

static gboolean
gs_plugin_shell_extensions_parse_version (GsPlugin *plugin,
					  AsApp *app,
					  JsonObject *ver_map,
					  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	JsonObject *json_ver = NULL;
	gint64 version;
	g_autofree gchar *shell_version = NULL;
	g_autoptr(AsRelease) release = NULL;

	/* look for version, major.minor.micro */
	if (json_object_has_member (ver_map, priv->shell_version)) {
		json_ver = json_object_get_object_member (ver_map,
							  priv->shell_version);
	}

	/* look for version, major.minor */
	if (json_ver == NULL) {
		g_auto(GStrv) ver_majmin = NULL;
		ver_majmin = g_strsplit (priv->shell_version, ".", -1);
		if (g_strv_length (ver_majmin) >= 2) {
			g_autofree gchar *tmp = NULL;
			tmp = g_strdup_printf ("%s.%s", ver_majmin[0], ver_majmin[1]);
			if (json_object_has_member (ver_map, tmp))
				json_ver = json_object_get_object_member (ver_map, tmp);
		}
	}

	/* FIXME: mark as incompatible? */
	if (json_ver == NULL) {
		g_debug ("no version_map for %s: %s",
			 as_app_get_id (app),
			 priv->shell_version);
		return TRUE;
	}

	/* parse the version */
	version = json_object_get_int_member (json_ver, "version");
	if (version == 0) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no version in map!");
		return FALSE;
	}
	shell_version = g_strdup_printf ("%" G_GINT64_FORMAT, version);

	/* add a dummy release */
	release = as_release_new ();
	as_release_set_version (release, shell_version);
	as_app_add_release (app, release);
	return TRUE;
}

static AsApp *
gs_plugin_shell_extensions_parse_app (GsPlugin *plugin,
				      JsonObject *json_app,
				      GError **error)
{
	g_autoptr(AsApp) app = NULL;
	JsonObject *json_ver_map;
	const gchar *tmp;

	app = as_app_new ();
	as_app_set_kind (app, AS_APP_KIND_SHELL_EXTENSION);
	as_app_set_project_license (app, "GPL-2.0+");

	tmp = json_object_get_string_member (json_app, "description");
	if (tmp != NULL) {
		g_autofree gchar *desc = NULL;
		desc = as_markup_import (tmp, AS_MARKUP_CONVERT_FORMAT_SIMPLE, error);
		if (desc == NULL) {
			gs_utils_error_convert_appstream (error);
			return NULL;
		}
		as_app_set_description (app, NULL, desc);
	}
	tmp = json_object_get_string_member (json_app, "screenshot");
	if (tmp != NULL) {
		g_autoptr(AsScreenshot) ss = NULL;
		g_autoptr(AsImage) im = NULL;
		g_autofree gchar *uri = NULL;
		uri = g_build_path ("/", SHELL_EXTENSIONS_API_URI, tmp, NULL);
		im = as_image_new ();
		as_image_set_kind (im, AS_IMAGE_KIND_SOURCE);
		as_image_set_url (im, uri);
		ss = as_screenshot_new ();
		as_screenshot_set_kind (ss, AS_SCREENSHOT_KIND_DEFAULT);
		as_screenshot_add_image (ss, im);
		as_app_add_screenshot (app, ss);
	}
	tmp = json_object_get_string_member (json_app, "name");
	if (tmp != NULL)
		as_app_set_name (app, NULL, tmp);
	tmp = json_object_get_string_member (json_app, "uuid");
	if (tmp != NULL) {
		g_autofree gchar *id = NULL;
		id = as_utils_appstream_id_build (tmp);
		as_app_set_id (app, id);
		as_app_add_metadata (app, "shell-extensions::uuid", tmp);
	}
	tmp = json_object_get_string_member (json_app, "link");
	if (tmp != NULL) {
		g_autofree gchar *uri = NULL;
		uri = g_build_filename (SHELL_EXTENSIONS_API_URI, tmp, NULL);
		as_app_add_url (app, AS_URL_KIND_HOMEPAGE, uri);
	}
	tmp = json_object_get_string_member (json_app, "icon");
	if (tmp != NULL) {
		g_autoptr(AsIcon) ic = NULL;
		/* just use a stock icon as the remote icons are
		 * sometimes missing, poor quality and low resolution */
		ic = as_icon_new ();
		as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
		as_icon_set_name (ic, "application-x-addon-symbolic");
		as_app_add_icon (app, ic);
	}

	/* try to get version */
	json_ver_map = json_object_get_object_member (json_app, "shell_version_map");
	if (json_ver_map != NULL) {
		if (!gs_plugin_shell_extensions_parse_version (plugin,
							       app,
							       json_ver_map,
							       error))
			return NULL;
	}

	/* we have no data :/ */
	as_app_add_metadata (app, "GnomeSoftware::Plugin",
			     gs_plugin_get_name (plugin));
	as_app_add_metadata (app, "GnomeSoftware::OriginHostnameUrl",
			     SHELL_EXTENSIONS_API_URI);

	return g_steal_pointer (&app);
}

static GPtrArray *
gs_plugin_shell_extensions_parse_apps (GsPlugin *plugin,
				       const gchar *data,
				       gssize data_len,
				       GError **error)
{
	GPtrArray *apps;
	JsonArray *json_extensions_array;
	JsonNode *json_extensions;
	JsonNode *json_root;
	JsonObject *json_item;
	guint i;
	g_autoptr(JsonParser) json_parser = NULL;

	/* nothing */
	if (data == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "server returned no data");
		return NULL;
	}

	/* parse the data and find the success */
	json_parser = json_parser_new ();
	if (!json_parser_load_from_data (json_parser, data, data_len, error)) {
		gs_utils_error_convert_json_glib (error);
		return NULL;
	}
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no data root");
		return NULL;
	}
	if (json_node_get_node_type (json_root) != JSON_NODE_OBJECT) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no data object");
		return NULL;
	}
	json_item = json_node_get_object (json_root);
	if (json_item == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no data object");
		return NULL;
	}

	/* load extensions */
	apps = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	json_extensions = json_object_get_member (json_item, "extensions");
	if (json_extensions == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no extensions object");
		return NULL;
	}
	json_extensions_array = json_node_get_array (json_extensions);
	if (json_extensions_array == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no extensions array");
		return NULL;
	}

	/* parse each app */
	for (i = 0; i < json_array_get_length (json_extensions_array); i++) {
		AsApp *app;
		JsonNode *json_extension;
		JsonObject *json_extension_obj;
		json_extension = json_array_get_element (json_extensions_array, i);
		json_extension_obj = json_node_get_object (json_extension);
		app = gs_plugin_shell_extensions_parse_app (plugin,
							    json_extension_obj,
							    error);
		if (app == NULL)
			return NULL;
		g_ptr_array_add (apps, app);
	}

	return apps;
}

static GPtrArray *
gs_plugin_shell_extensions_get_apps (GsPlugin *plugin,
				     guint cache_age,
				     GCancellable *cancellable,
				     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GPtrArray *apps;
	g_autofree gchar *cachefn = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(GFile) cachefn_file = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

	/* look in the cache */
	cachefn = gs_utils_get_cache_filename ("shell-extensions",
					       "gnome.json",
					       GS_UTILS_CACHE_FLAG_WRITEABLE,
					       error);
	if (cachefn == NULL)
		return NULL;
	cachefn_file = g_file_new_for_path (cachefn);
	if (gs_utils_get_file_age (cachefn_file) < cache_age) {
		g_autofree gchar *json_data = NULL;
		if (!g_file_get_contents (cachefn, &json_data, NULL, error))
			return NULL;
		g_debug ("got cached extension data from %s", cachefn);
		return gs_plugin_shell_extensions_parse_apps (plugin,
							      json_data,
							      -1, error);
	}

	/* create the GET data */
	uri = g_strdup_printf ("%s/static/extensions.json",
			       SHELL_EXTENSIONS_API_URI);
	gs_app_set_summary_missing (app_dl,
				    /* TRANSLATORS: status text when downloading */
				    _("Downloading shell extension metadata…"));
	data = gs_plugin_download_data (plugin, app_dl, uri, cancellable, error);
	if (data == NULL) {
		gs_utils_error_add_unique_id (error, priv->cached_origin);
		return NULL;
	}
	apps = gs_plugin_shell_extensions_parse_apps (plugin,
						      g_bytes_get_data (data, NULL),
						      (gssize) g_bytes_get_size (data),
						      error);
	if (apps == NULL) {
		gsize len = g_bytes_get_size (data);
		g_autofree gchar *tmp = NULL;

		/* truncate the string if long */
		if (len > 100)
			len = 100;
		tmp = g_strndup (g_bytes_get_data (data, NULL), len);
		g_prefix_error (error, "Failed to parse '%s': ", tmp);
		return NULL;
	}

	/* save to the cache */
	if (!g_file_set_contents (cachefn,
				  g_bytes_get_data (data, NULL),
				  (guint) g_bytes_get_size (data),
				  error))
		return NULL;

	return apps;
}

static gboolean
gs_plugin_shell_extensions_refresh (GsPlugin *plugin,
				    guint cache_age,
				    GCancellable *cancellable,
				    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsApp *app;
	gboolean repo_enabled;
	const gchar *fn_test;
	guint i;
	g_autofree gchar *fn = NULL;
	g_autoptr(GPtrArray) apps = NULL;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GFile) file = NULL;

	/* check age */
	fn_test = g_getenv ("GS_SELF_TEST_SHELL_EXTENSIONS_XML_FN");
	if (fn_test != NULL) {
		fn = g_strdup (fn_test);
	} else {
		fn = g_build_filename (g_get_user_data_dir (),
				       "app-info",
				       "xmls",
				       "extensions-web.xml",
				       NULL);
	}

	/* remove old appstream data if the repo is disabled */
	repo_enabled = g_settings_get_boolean (priv->settings, "enable-shell-extensions-repo");
	if (!repo_enabled) {
		g_unlink (fn);
		return TRUE;
	}

	file = g_file_new_for_path (fn);
	if (g_file_query_exists (file, NULL)) {
		guint age = gs_utils_get_file_age (file);
		if (age < cache_age) {
			g_debug ("%s is only %u seconds old, ignoring", fn, age);
			return TRUE;
		}
	}

	/* get data */
	apps = gs_plugin_shell_extensions_get_apps (plugin,
						    cache_age,
						    cancellable,
						    error);
	if (apps == NULL)
		return FALSE;

	/* add to local store */
	store = as_store_new ();
	as_store_set_origin (store, "extensions-web");
	for (i = 0; i < apps->len; i++) {
		app = g_ptr_array_index (apps, i);
		g_debug ("adding to local store %s", as_app_get_id (app));
		as_store_add_app (store, app);
	}

	/* save to disk */
	if (!gs_mkdir_parent (fn, error))
		return FALSE;
	g_debug ("saving to %s", fn);
	return as_store_to_file (store, file,
				 AS_NODE_TO_XML_FLAG_ADD_HEADER |
				 AS_NODE_TO_XML_FLAG_FORMAT_INDENT |
				 AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE,
				 cancellable,
				 error);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	return gs_plugin_shell_extensions_refresh (plugin,
						   cache_age,
						   cancellable,
						   error);
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *uuid;
	gboolean ret;
	g_autoptr(GVariant) retval = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* disable repository */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE) {
		gs_app_set_state (app, AS_APP_STATE_REMOVING);
		g_settings_set_boolean (priv->settings, "enable-shell-extensions-repo", FALSE);
		/* remove appstream data */
		ret = gs_plugin_shell_extensions_refresh (plugin,
		                                          G_MAXUINT,
		                                          cancellable,
		                                          error);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		return ret;
	}

	/* remove */
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	uuid = gs_app_get_metadata_item (app, "shell-extensions::uuid");
	retval = g_dbus_proxy_call_sync (priv->proxy,
					 "UninstallExtension",
					 g_variant_new ("(s)", uuid),
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 cancellable,
					 error);
	if (retval == NULL) {
		gs_utils_error_convert_gio (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* not sure why this would fail -- perhaps installed in /usr? */
	g_variant_get (retval, "(b)", &ret);
	if (!ret) {
		gs_app_set_state_recover (app);
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "failed to uninstall %s",
			     gs_app_get_id (app));
		return FALSE;
	}

	/* state is not known: we don't know if we can re-install this app */
	gs_app_set_state (app, AS_APP_STATE_UNKNOWN);

	return TRUE;
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *uuid;
	const gchar *retstr;
	g_autoptr(GVariant) retval = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* enable repository */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE) {
		gboolean ret;

		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		g_settings_set_boolean (priv->settings, "enable-shell-extensions-repo", TRUE);
		/* refresh metadata */
		ret = gs_plugin_shell_extensions_refresh (plugin,
		                                          G_MAXUINT,
		                                          cancellable,
		                                          error);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		return ret;
	}

	/* install */
	uuid = gs_app_get_metadata_item (app, "shell-extensions::uuid");
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	retval = g_dbus_proxy_call_sync (priv->proxy,
					 "InstallRemoteExtension",
					 g_variant_new ("(s)", uuid),
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 cancellable,
					 error);
	if (retval == NULL) {
		gs_app_set_state_recover (app);
		return FALSE;
	}
	g_variant_get (retval, "(&s)", &retstr);

	/* user declined download */
	if (g_strcmp0 (retstr, "cancelled") == 0) {
		gs_app_set_state_recover (app);
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_CANCELLED,
			     "extension %s download was cancelled",
			     gs_app_get_id (app));
		return FALSE;
	}
	g_debug ("shell returned: %s", retstr);

	/* state is known */
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *uuid = NULL;
	g_autoptr(GVariant) retval = NULL;

	/* launch both PackageKit-installed and user-installed */
	if (gs_app_get_kind (app) != AS_APP_KIND_SHELL_EXTENSION)
		return TRUE;

	uuid = g_strdup (gs_app_get_metadata_item (app, "shell-extensions::uuid"));
	if (uuid == NULL) {
		const gchar *suffix = ".shell-extension";
		const gchar *id = gs_app_get_id (app);
		/* PackageKit-installed extension ID generated by appstream-builder */
		if (g_str_has_suffix (id, suffix))
			uuid = g_strndup (id, strlen (id) - strlen (suffix));
	}
	if (uuid == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "no uuid set for %s",
			     gs_app_get_id (app));
		return FALSE;
	}
	/* launch */
	retval = g_dbus_proxy_call_sync (priv->proxy,
					 "LaunchExtensionPrefs",
					 g_variant_new ("(s)", uuid),
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 cancellable,
					 error);
	if (retval == NULL) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	/* just ensure there is any data, no matter how old */
	return gs_plugin_shell_extensions_refresh (plugin,
						   G_MAXUINT,
						   cancellable,
						   error);
}
