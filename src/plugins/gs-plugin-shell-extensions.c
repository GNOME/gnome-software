/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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
#include <json-glib/json-glib.h>

#include <gs-plugin.h>
#include <gs-os-release.h>
#include <gs-utils.h>

#define SHELL_EXTENSIONS_API_URI 		"https://extensions.gnome.org/"

/*
 * Things we want from the API:
 *
 *  - Screenshots
 *  - Size on disk/download
 *  - Existing review data for each extension?
 *  - A local icon for an installed shell extension
 *
 * See https://git.gnome.org/browse/extensions-web/tree/sweettooth/extensions/views.py
 * for the source to the web application.
 */

struct GsPluginPrivate {
	GDBusProxy	*proxy;
	gchar		*shell_version;
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

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "shell-extensions";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_free (plugin->priv->shell_version);
	if (plugin->priv->proxy != NULL)
		g_object_unref (plugin->priv->proxy);
}

/**
 * gs_plugin_shell_extensions_id_from_uuid:
 */
static gchar *
gs_plugin_shell_extensions_id_from_uuid (const gchar *uuid)
{
	return g_strdup_printf ("%s.shell-extension", uuid);
}

/**
 * gs_plugin_shell_extensions_add_app:
 */
static GsApp *
gs_plugin_shell_extensions_add_app (const gchar *uuid,
				    GVariantIter *iter,
				    GError **error)
{
	const gchar *tmp;
	gchar *str;
	GVariant *val;
	g_autofree gchar *id = NULL;
	g_autofree gchar *id_prefix = NULL;
	g_autoptr(AsIcon) ic = NULL;
	g_autoptr(GsApp) app = NULL;

	id = gs_plugin_shell_extensions_id_from_uuid (uuid);
	id_prefix = g_strdup_printf ("user:%s", id);
	app = gs_app_new (id_prefix);
	gs_app_set_management_plugin (app, "ShellExtensions");
	gs_app_set_metadata (app, "ShellExtensions::uuid", uuid);
	gs_app_set_kind (app, AS_APP_KIND_SHELL_EXTENSION);
	gs_app_set_license (app, GS_APP_QUALITY_NORMAL, "GPL-2.0+");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "GNOME Shell Extension");
	while (g_variant_iter_loop (iter, "{sv}", &str, &val)) {
		if (g_strcmp0 (str, "description") == 0) {
			g_autofree gchar *tmp1 = NULL;
			g_autofree gchar *tmp2 = NULL;
			tmp1 = as_markup_import (g_variant_get_string (val, NULL));
			tmp2 = as_markup_convert_simple (tmp1, error);
			if (tmp2 == NULL)
				return NULL;
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
			guint val_int = g_variant_get_double (val);
			switch (val_int) {
			case GS_PLUGIN_SHELL_EXTENSION_KIND_SYSTEM:
			case GS_PLUGIN_SHELL_EXTENSION_KIND_PER_USER:
				gs_app_set_kind (app, AS_APP_KIND_SHELL_EXTENSION);
				break;
			default:
				g_warning ("%s unknown type %i", uuid, val_int);
				break;
			}
			continue;
		}
		if (g_strcmp0 (str, "state") == 0) {
			guint val_int = g_variant_get_double (val);
			switch (val_int) {
			case GS_PLUGIN_SHELL_EXTENSION_STATE_DISABLED:
			case GS_PLUGIN_SHELL_EXTENSION_STATE_DOWNLOADING:
			case GS_PLUGIN_SHELL_EXTENSION_STATE_ENABLED:
			case GS_PLUGIN_SHELL_EXTENSION_STATE_INITIALIZED:
			case GS_PLUGIN_SHELL_EXTENSION_STATE_OUT_OF_DATE:
				gs_app_set_state (app, AS_APP_STATE_INSTALLED);
				break;
			case GS_PLUGIN_SHELL_EXTENSION_STATE_UNINSTALLED:
				gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
				break;
			case GS_PLUGIN_SHELL_EXTENSION_STATE_ERROR:
				g_warning ("%s unhandled error state", uuid);
				gs_app_set_state (app, AS_APP_STATE_INSTALLED);
				break;
			default:
				g_warning ("%s unknown state %i", uuid, val_int);
				break;
			}
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
				gs_app_set_metadata (app, "ShellExtensions::has-prefs", "");
			continue;
		}
		if (g_strcmp0 (str, "extension-id") == 0) {
			tmp = g_variant_get_string (val, NULL);
			gs_app_set_metadata (app, "ShellExtensions::extension-id", tmp);
			continue;
		}
		if (g_strcmp0 (str, "path") == 0) {
			tmp = g_variant_get_string (val, NULL);
			gs_app_set_metadata (app, "ShellExtensions::path", tmp);
			continue;
		}
	}

	/* hardcode icon */
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "application-x-addon-symbolic");
	gs_app_set_icon (app, ic);

	/* add categories */
	gs_app_add_category (app, "Addons");
	gs_app_add_category (app, "ShellExtensions");

	return g_steal_pointer (&app);
}

/**
 * gs_plugin_setup:
 */
static gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	g_autoptr(GVariant) version = NULL;

	if (plugin->priv->proxy != NULL)
		return TRUE;
	plugin->priv->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
							     G_DBUS_PROXY_FLAGS_NONE,
							     NULL,
							     "org.gnome.Shell",
							     "/org/gnome/Shell",
							     "org.gnome.Shell.Extensions",
							     cancellable,
							     error);

	/* get the GNOME Shell version */
	version = g_dbus_proxy_get_cached_property (plugin->priv->proxy,
						    "ShellVersion");
	if (version != NULL)
		plugin->priv->shell_version = g_variant_dup_string (version, NULL);
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
	GVariantIter *ext_iter;
	gchar *ext_uuid;
	g_autoptr(GVariantIter) iter = NULL;
	g_autoptr(GVariant) retval = NULL;

	/* connect to gnome-shell */
	if (!gs_plugin_setup (plugin, cancellable, error))
		return FALSE;

	/* installed */
	retval = g_dbus_proxy_call_sync (plugin->priv->proxy,
					 "ListExtensions",
					 NULL,
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 cancellable,
					 error);
	if (retval == NULL)
		return FALSE;

	/* parse each installed extension */
	g_variant_get (retval, "(a{sa{sv}})", &iter);
	while (g_variant_iter_loop (iter, "{sa{sv}}", &ext_uuid, &ext_iter)) {
		g_autoptr(GsApp) app = NULL;

		/* parse the data into an GsApp */
		app = gs_plugin_shell_extensions_add_app (ext_uuid,
							  ext_iter,
							  error);
		if (app == NULL)
			return FALSE;

		/* add to results */
		gs_plugin_add_app (list, app);
	}
	return TRUE;
}

/**
 * gs_plugin_refine:
 */
static gboolean
gs_plugin_refine_item (GsPlugin *plugin,
		       GsApp *app,
		       GError **error)
{
	/* only process this these kinds */
	if (gs_app_get_kind (app) != AS_APP_KIND_SHELL_EXTENSION)
		return TRUE;

	/* adopt any here */
	if (gs_app_get_management_plugin (app) == NULL)
		gs_app_set_management_plugin (app, "ShellExtensions");

	/* assume apps are available if they exist in AppStream metadata */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

	/* FIXME: assume these are small */
	if (gs_app_get_size (app) == 0)
		gs_app_set_size (app, 1024 * 50);

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
	GList *l;
	GsApp *app;

	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (!gs_plugin_refine_item (plugin, app, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * gs_plugin_shell_extensions_parse_version:
 */
static gboolean
gs_plugin_shell_extensions_parse_version (GsPlugin *plugin,
					  AsApp *app,
					  JsonObject *ver_map,
					  GError **error)
{
	JsonObject *json_ver = NULL;
	gint64 version;
	g_autofree gchar *shell_version = NULL;
	g_autoptr(AsRelease) release = NULL;

	/* look for version, major.minor.micro */
	if (json_object_has_member (ver_map, plugin->priv->shell_version)) {
		json_ver = json_object_get_object_member (ver_map,
							  plugin->priv->shell_version);
	}

	/* look for version, major.minor */
	if (json_ver == NULL) {
		g_auto(GStrv) ver_majmin = NULL;
		ver_majmin = g_strsplit (plugin->priv->shell_version, ".", -1);
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
			 plugin->priv->shell_version);
		return TRUE;
	}

	/* parse the version */
	version = json_object_get_int_member (json_ver, "version");
	if (version == 0) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
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

/**
 * gs_plugin_shell_extensions_parse_app:
 */
static AsApp *
gs_plugin_shell_extensions_parse_app (GsPlugin *plugin,
				      JsonObject *json_app,
				      GError **error)
{
	AsApp *app;
	JsonObject *json_ver_map;
	const gchar *tmp;
	guint64 pk;

	app = as_app_new ();
	as_app_set_kind (app, AS_APP_KIND_SHELL_EXTENSION);
	as_app_set_project_license (app, "GPL-2.0+");

	tmp = json_object_get_string_member (json_app, "description");
	if (tmp != NULL) {
		g_autofree gchar *desc = NULL;
		desc = as_markup_import (tmp);
		as_app_set_description (app, NULL, desc);
	}
	tmp = json_object_get_string_member (json_app, "name");
	if (tmp != NULL)
		as_app_set_name (app, NULL, tmp);
	tmp = json_object_get_string_member (json_app, "uuid");
	if (tmp != NULL) {
		g_autofree gchar *id = NULL;
		id = gs_plugin_shell_extensions_id_from_uuid (tmp);
		as_app_set_id (app, id);
		as_app_add_metadata (app, "ShellExtensions::uuid", tmp);
	}
	tmp = json_object_get_string_member (json_app, "link");
	if (tmp != NULL) {
		g_autofree gchar *uri = NULL;
		uri = g_build_filename (SHELL_EXTENSIONS_API_URI, tmp, NULL);
		as_app_add_url (app, AS_URL_KIND_HOMEPAGE, uri);
	}
	tmp = json_object_get_string_member (json_app, "icon");
	if (tmp != NULL) {
		g_autofree gchar *uri = NULL;
		g_autoptr(AsIcon) ic = NULL;

		/* use stock icon for generic */
		ic = as_icon_new ();
		if (g_strcmp0 (tmp, "/static/images/plugin.png") == 0) {
			as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
			as_icon_set_name (ic, "application-x-addon-symbolic");
		} else {
			uri = g_build_filename (SHELL_EXTENSIONS_API_URI, tmp, NULL);
			as_icon_set_kind (ic, AS_ICON_KIND_REMOTE);
			as_icon_set_url (ic, uri);
		}
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

	/* add a screenshot, which curiously isn't in the json */
	pk = json_object_get_int_member (json_app, "pk");
	if (1) {
		g_autoptr(AsScreenshot) ss = NULL;
		g_autoptr(AsImage) im = NULL;
		g_autofree gchar *uri = NULL;
		uri = g_strdup_printf ("%s/static/extension-data/"
				       "screenshots/"
				       "screenshot_%" G_GUINT64_FORMAT ".png",
				       SHELL_EXTENSIONS_API_URI, pk);
		im = as_image_new ();
		as_image_set_kind (im, AS_IMAGE_KIND_SOURCE);
		as_image_set_url (im, uri);
		ss = as_screenshot_new ();
		as_screenshot_set_kind (ss, AS_SCREENSHOT_KIND_DEFAULT);
		as_screenshot_add_image (ss, im);
		as_app_add_screenshot (app, ss);
	}

	/* required to match categories in gnome-software */
	as_app_add_category (app, "Addons");
	as_app_add_category (app, "ShellExtensions");

	/* we have no data :/ */
	as_app_set_comment (app, NULL, "GNOME Shell Extension");
	as_app_add_metadata (app, "ManagementPlugin", "ShellExtensions");
	return app;
}

/**
 * gs_plugin_shell_extensions_parse_apps:
 */
static GPtrArray *
gs_plugin_shell_extensions_parse_apps (GsPlugin *plugin,
				       const gchar *data,
				       gsize data_len,
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
				     GS_PLUGIN_ERROR_FAILED,
				     "server returned no data");
		return NULL;
	}

	/* parse the data and find the success */
	json_parser = json_parser_new ();
	if (!json_parser_load_from_data (json_parser, data, data_len, error))
		return NULL;
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no data root");
		return NULL;
	}
	if (json_node_get_node_type (json_root) != JSON_NODE_OBJECT) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no data object");
		return NULL;
	}
	json_item = json_node_get_object (json_root);
	if (json_item == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no data object");
		return NULL;
	}

	/* load extensions */
	apps = g_ptr_array_new ();
	json_extensions = json_object_get_member (json_item, "extensions");
	if (json_extensions == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no extensions object");
		return NULL;
	}
	json_extensions_array = json_node_get_array (json_extensions);
	if (json_extensions_array == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
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

/**
 * gs_plugin_shell_extensions_get_apps:
 */
static GPtrArray *
gs_plugin_shell_extensions_get_apps (GsPlugin *plugin,
				     guint cache_age,
				     GError **error)
{
	GPtrArray *apps;
	guint status_code;
	g_autofree gchar *cachedir = NULL;
	g_autofree gchar *cachefn = NULL;
	g_autofree gchar *data = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(GFile) cachefn_file = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* look in the cache */
	cachedir = gs_utils_get_cachedir ("extensions", error);
	if (cachedir == NULL)
		return NULL;
	cachefn = g_strdup_printf ("%s/gnome.json", cachedir);
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
	uri = g_strdup_printf ("%s/extension-query/"
			       "?shell_version=%s"
			       "&page=1&n_per_page=1000",
			       SHELL_EXTENSIONS_API_URI,
			       plugin->priv->shell_version);
	msg = soup_message_new (SOUP_METHOD_GET, uri);
	status_code = soup_session_send_message (plugin->soup_session, msg);
	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to get shell extensions: %s",
			     msg->response_body->data);
		return NULL;
	}
	apps = gs_plugin_shell_extensions_parse_apps (plugin,
						      msg->response_body->data,
						      msg->response_body->length,
						      error);
	if (apps == NULL) {
		guint len = msg->response_body->length;
		g_autofree gchar *tmp = NULL;

		/* truncate the string if long */
		if (len > 100)
			len = 100;
		tmp = g_strndup (msg->response_body->data, len);
		g_prefix_error (error, "Failed to parse '%s': ", tmp);
		return NULL;
	}

	/* save to the cache */
	if (!g_file_set_contents (cachefn,
				  msg->response_body->data,
				  msg->response_body->length,
				  error))
		return NULL;

	return apps;
}

/**
 * gs_plugin_refresh:
 */
gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	AsApp *app;
	guint i;
	g_autofree gchar *fn = NULL;
	g_autoptr(GPtrArray) apps = NULL;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GFile) file = NULL;

	/* connect to gnome-shell */
	if (!gs_plugin_setup (plugin, cancellable, error))
		return FALSE;

	/* get data */
	apps = gs_plugin_shell_extensions_get_apps (plugin, cache_age, error);
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
	fn = g_build_filename (g_get_user_data_dir (),
			       "app-info",
			       "xmls",
			       "extensions-web.xml",
			       NULL);
	if (!gs_mkdir_parent (fn, error))
		return FALSE;
	file = g_file_new_for_path (fn);
	g_debug ("saving to %s", fn);
	return as_store_to_file (store, file,
				 AS_NODE_TO_XML_FLAG_ADD_HEADER |
				 AS_NODE_TO_XML_FLAG_FORMAT_INDENT |
				 AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE,
				 cancellable,
				 error);
}

/**
 * gs_plugin_app_remove:
 */
gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *uuid;
	gboolean ret;
	g_autoptr(GVariant) retval = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "ShellExtensions") != 0)
		return TRUE;

	/* connect to gnome-shell */
	if (!gs_plugin_setup (plugin, cancellable, error))
		return FALSE;

	/* install */
	uuid = gs_app_get_metadata_item (app, "ShellExtensions::uuid");
	retval = g_dbus_proxy_call_sync (plugin->priv->proxy,
					 "UninstallExtension",
					 g_variant_new ("(s)", uuid),
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 cancellable,
					 error);
	if (retval == NULL)
		return FALSE;

	/* not sure why this would fail -- perhaps installed in /usr? */
	g_variant_get (retval, "(b)", &ret);
	if (!ret) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to uninstall %s",
			     gs_app_get_id (app));
		return FALSE;
	}

	return TRUE;
}

/**
 * gs_plugin_app_install:
 */
gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	const gchar *uuid;
	const gchar *retstr;
	g_autoptr(GVariant) retval = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "ShellExtensions") != 0)
		return TRUE;

	/* connect to gnome-shell */
	if (!gs_plugin_setup (plugin, cancellable, error))
		return FALSE;

	/* install */
	uuid = gs_app_get_metadata_item (app, "ShellExtensions::uuid");
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	retval = g_dbus_proxy_call_sync (plugin->priv->proxy,
					 "InstallRemoteExtension",
					 g_variant_new ("(s)", uuid),
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 cancellable,
					 error);
	if (retval == NULL)
		return FALSE;
	g_variant_get (retval, "(&s)", &retstr);

	/* user declined download */
	if (g_strcmp0 (retstr, "cancelled") == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_CANCELLED,
			     "extension %s download was cancelled",
			     gs_app_get_id (app));
		return FALSE;
	}
	g_debug ("shell returned: %s", retstr);
	return TRUE;
}

/**
 * gs_plugin_launch:
 */
gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *uuid;
	g_autoptr(GVariant) retval = NULL;

	/* launch both PackageKit-installed and user-installed */
	if (gs_app_get_kind (app) != AS_APP_KIND_SHELL_EXTENSION)
		return TRUE;

	/* connect to gnome-shell */
	if (!gs_plugin_setup (plugin, cancellable, error))
		return FALSE;

	/* install */
	uuid = gs_app_get_metadata_item (app, "ShellExtensions::uuid");
	if (uuid == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "no uuid set for %s",
			     gs_app_get_id (app));
		return FALSE;
	}
	retval = g_dbus_proxy_call_sync (plugin->priv->proxy,
					 "LaunchExtensionPrefs",
					 g_variant_new ("(s)", uuid),
					 G_DBUS_CALL_FLAGS_NONE,
					 -1,
					 cancellable,
					 error);
	if (retval == NULL)
		return FALSE;
	return TRUE;
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
	/* just ensure there is any data, no matter how old */
	return gs_plugin_refresh (plugin, G_MAXUINT,
				  GS_PLUGIN_REFRESH_FLAGS_NONE,
				  cancellable, error);
}
