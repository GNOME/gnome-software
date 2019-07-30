/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <errno.h>
#include <glib/gi18n.h>
#include <json-glib/json-glib.h>
#include <xmlb.h>

#include <gnome-software.h>

#include "gs-appstream.h"

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
	XbSilo		*silo;
	GRWLock		 silo_lock;
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

static gboolean _check_silo (GsPlugin *plugin, GCancellable *cancellable, GError **error);

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* XbSilo needs external locking as we destroy the silo and build a new
	 * one when something changes */
	g_rw_lock_init (&priv->silo_lock);

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
	if (priv->silo != NULL)
		g_object_unref (priv->silo);
	g_object_unref (priv->cached_origin);
	g_object_unref (priv->settings);
	g_rw_lock_clear (&priv->silo_lock);
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
		if (g_strcmp0 (str, "version") == 0) {
			guint val_int = (guint) g_variant_get_double (val);
			g_autofree gchar *version = g_strdup_printf ("%u", val_int);
			gs_app_set_version (app, version);
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
	if (version == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "unable to get shell version");
		return FALSE;
	}
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
	gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *uuid;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(XbNode) component = NULL;

	/* repo not enabled */
	if (!g_settings_get_boolean (priv->settings, "enable-shell-extensions-repo"))
		return TRUE;

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


	/* check silo is valid */
	if (!_check_silo (plugin, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);

	/* find the component using the UUID */
	if (uuid == NULL)
		return TRUE;

	xpath = g_strdup_printf ("components/component/custom/"
				 "value[@key='shell-extensions::uuid'][text()='%s']/../..",
				 uuid);
	component = xb_silo_query_first (priv->silo, xpath, &error_local);
	if (component == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	return gs_appstream_refine_app (plugin, app, priv->silo, component, flags, error);
}

gboolean
gs_plugin_refine_wildcard (GsPlugin *plugin,
			   GsApp *app,
			   GsAppList *list,
			   GsPluginRefineFlags refine_flags,
			   GCancellable *cancellable,
			   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *id;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	/* repo not enabled */
	if (!g_settings_get_boolean (priv->settings, "enable-shell-extensions-repo"))
		return TRUE;

	/* check silo is valid */
	if (!_check_silo (plugin, cancellable, error))
		return FALSE;

	/* not enough info to find */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);

	/* find all apps */
	xpath = g_strdup_printf ("components/component/id[text()='%s']/..", id);
	components = xb_silo_query (priv->silo, xpath, 0, &error_local);
	if (components == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(GsApp) new = NULL;
		new = gs_appstream_create_app (plugin, priv->silo, component, error);
		if (new == NULL)
			return FALSE;
		gs_app_subsume_metadata (new, app);
		if (!gs_appstream_refine_app (plugin, new, priv->silo, component,
					      refine_flags, error))
			return FALSE;
		gs_app_list_add (list, new);
	}

	/* success */
	return TRUE;
}

static gboolean
gs_plugin_shell_extensions_parse_version (GsPlugin *plugin,
					  const gchar *component_id,
					  XbBuilderNode *app,
					  JsonObject *ver_map,
					  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	JsonObject *json_ver = NULL;
	gint64 version;
	g_autofree gchar *shell_version = NULL;

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
	if (json_ver == NULL)
		return TRUE;

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
	xb_builder_node_insert_text (app, "release", NULL,
				     "version", shell_version,
				     NULL);
	return TRUE;
}



static XbBuilderNode *
gs_plugin_shell_extensions_parse_app (GsPlugin *plugin,
				      JsonObject *json_app,
				      GError **error)
{
	JsonObject *json_ver_map;
	const gchar *tmp;
	g_autofree gchar *component_id = NULL;
	g_autoptr(XbBuilderNode) app = NULL;
	g_autoptr(XbBuilderNode) categories = NULL;
	g_autoptr(XbBuilderNode) metadata = NULL;

	app = xb_builder_node_new ("component");
	xb_builder_node_set_attr (app, "kind", "shell-extension");
	xb_builder_node_insert_text (app, "project_license", "GPL-2.0+", NULL);
	categories = xb_builder_node_insert (app, "categories", NULL);
	xb_builder_node_insert_text (categories, "category", "Addon", NULL);
	xb_builder_node_insert_text (categories, "category", "ShellExtension", NULL);
	metadata = xb_builder_node_insert (app, "custom", NULL);

	tmp = json_object_get_string_member (json_app, "description");
	if (tmp != NULL) {
		g_auto(GStrv) paras = g_strsplit (tmp, "\n", -1);
		g_autoptr(XbBuilderNode) desc = xb_builder_node_insert (app, "description", NULL);
		for (guint i = 0; paras[i] != NULL; i++)
			xb_builder_node_insert_text (desc, "p", paras[i], NULL);
	}
	tmp = json_object_get_string_member (json_app, "screenshot");
	if (tmp != NULL) {
		g_autoptr(XbBuilderNode) screenshots = NULL;
		g_autoptr(XbBuilderNode) screenshot = NULL;
		g_autofree gchar *uri = NULL;
		screenshots = xb_builder_node_insert (app, "screenshots", NULL);
		screenshot = xb_builder_node_insert (screenshots, "screenshot",
						     "kind", "default",
						     NULL);
		uri = g_build_path ("/", SHELL_EXTENSIONS_API_URI, tmp, NULL);
		xb_builder_node_insert_text (screenshot, "image", uri,
					     "kind", "source",
					     NULL);
	}
	tmp = json_object_get_string_member (json_app, "name");
	if (tmp != NULL)
		xb_builder_node_insert_text (app, "name", tmp, NULL);
	tmp = json_object_get_string_member (json_app, "uuid");
	if (tmp != NULL) {
		component_id = as_utils_appstream_id_build (tmp);
		xb_builder_node_insert_text (app, "id", component_id, NULL);
		xb_builder_node_insert_text (metadata, "value", tmp,
					     "key", "shell-extensions::uuid",
					     NULL);
	}
	tmp = json_object_get_string_member (json_app, "link");
	if (tmp != NULL) {
		g_autofree gchar *uri = NULL;
		uri = g_build_filename (SHELL_EXTENSIONS_API_URI, tmp, NULL);
		xb_builder_node_insert_text (app, "url", uri,
					     "type", "homepage",
					     NULL);
	}
	tmp = json_object_get_string_member (json_app, "icon");
	if (tmp != NULL) {
		/* just use a stock icon as the remote icons are
		 * sometimes missing, poor quality and low resolution */
		xb_builder_node_insert_text (app, "icon",
					     "application-x-addon-symbolic",
					     "type", "stock",
					     NULL);
	}

	/* try to get version */
	json_ver_map = json_object_get_object_member (json_app, "shell_version_map");
	if (json_ver_map != NULL) {
		if (!gs_plugin_shell_extensions_parse_version (plugin,
							       component_id,
							       app,
							       json_ver_map,
							       error))
			return NULL;
	}

	return g_steal_pointer (&app);
}

static GInputStream *
gs_plugin_appstream_load_json_cb (XbBuilderSource *self,
				  XbBuilderSourceCtx *ctx,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);
	JsonArray *json_extensions_array;
	JsonNode *json_extensions;
	JsonNode *json_root;
	JsonObject *json_item;
	gchar *xml;
	g_autoptr(JsonParser) json_parser = NULL;
	g_autoptr(XbBuilderNode) apps = NULL;

	/* parse the data and find the success */
	json_parser = json_parser_new ();
	if (!json_parser_load_from_stream (json_parser,
					   xb_builder_source_ctx_get_stream (ctx),
					   cancellable, error)) {
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
	apps = xb_builder_node_new ("components");
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
	for (guint i = 0; i < json_array_get_length (json_extensions_array); i++) {
		JsonNode *json_extension;
		JsonObject *json_extension_obj;
		g_autoptr(XbBuilderNode) component = NULL;

		json_extension = json_array_get_element (json_extensions_array, i);
		json_extension_obj = json_node_get_object (json_extension);
		component = gs_plugin_shell_extensions_parse_app (plugin,
							    json_extension_obj,
							    error);
		if (component == NULL)
			return NULL;
		xb_builder_node_add_child (apps, component);
	}

	/* convert back to XML */
	xml = xb_builder_node_export (apps, XB_NODE_EXPORT_FLAG_ADD_HEADER, error);
	if (xml == NULL)
		return NULL;
	return g_memory_input_stream_new_from_data (xml, -1, g_free);
}

static gboolean
gs_plugin_shell_extensions_refresh (GsPlugin *plugin,
				    guint cache_age,
				    GCancellable *cancellable,
				    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

	/* get cache filename */
	fn = gs_utils_get_cache_filename ("shell-extensions",
					  "gnome.json",
					  GS_UTILS_CACHE_FLAG_WRITEABLE,
					  error);
	if (fn == NULL)
		return FALSE;

	/* check age */
	file = g_file_new_for_path (fn);
	if (g_file_query_exists (file, NULL)) {
		guint age = gs_utils_get_file_age (file);
		if (age < cache_age) {
			g_debug ("%s is only %u seconds old, ignoring", fn, age);
			return TRUE;
		}
	}

	/* download the file */
	uri = g_strdup_printf ("%s/static/extensions.json",
			       SHELL_EXTENSIONS_API_URI);
	gs_app_set_summary_missing (app_dl,
				    /* TRANSLATORS: status text when downloading */
				    _("Downloading shell extension metadata…"));
	if (!gs_plugin_download_file (plugin, app_dl, uri, fn, cancellable, error)) {
		gs_utils_error_add_origin_id (error, priv->cached_origin);
		return FALSE;
	}

	/* be explicit */
	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
	if (priv->silo != NULL)
		xb_silo_invalidate (priv->silo);

	return TRUE;
}

static gboolean
_check_silo (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *blobfn = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) blobfile = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GRWLockReaderLocker) reader_locker = NULL;
	g_autoptr(GRWLockWriterLocker) writer_locker = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(XbBuilderNode) info = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	reader_locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
	/* everything is okay */
	if (priv->silo != NULL && xb_silo_is_valid (priv->silo)) {
		g_debug ("silo already valid");
		return TRUE;
	}
	g_clear_pointer (&reader_locker, g_rw_lock_reader_locker_free);

	/* drat! silo needs regenerating */
	writer_locker = g_rw_lock_writer_locker_new (&priv->silo_lock);
	g_clear_object (&priv->silo);

	/* verbose profiling */
	if (g_getenv ("GS_XMLB_VERBOSE") != NULL) {
		xb_builder_set_profile_flags (builder,
					      XB_SILO_PROFILE_FLAG_XPATH |
					      XB_SILO_PROFILE_FLAG_DEBUG);
	}

	/* add metadata */
	info = xb_builder_node_insert (NULL, "info", NULL);
	xb_builder_node_insert_text (info, "scope", "user", NULL);
	xb_builder_source_set_info (source, info);

	/* add support for JSON files */
	fn = gs_utils_get_cache_filename ("shell-extensions",
					  "gnome.json",
					  GS_UTILS_CACHE_FLAG_WRITEABLE,
					  error);
	if (fn == NULL)
		return FALSE;
	xb_builder_source_add_adapter (source, "application/json",
				       gs_plugin_appstream_load_json_cb,
				       plugin, NULL);
	file = g_file_new_for_path (fn);
	if (!xb_builder_source_load_file (source, file,
					  XB_BUILDER_SOURCE_FLAG_WATCH_FILE,
					  cancellable,
					  error)) {
		return FALSE;
	}
	xb_builder_import_source (builder, source);

	/* create binary cache */
	blobfn = gs_utils_get_cache_filename ("shell-extensions",
					      "extensions-web.xmlb",
					      GS_UTILS_CACHE_FLAG_WRITEABLE,
					      error);
	if (blobfn == NULL)
		return FALSE;
	blobfile = g_file_new_for_path (blobfn);
	g_debug ("ensuring %s", blobfn);
	priv->silo = xb_builder_ensure (builder, blobfile,
					XB_BUILDER_COMPILE_FLAG_IGNORE_INVALID,
					NULL, &error_local);
	if (priv->silo == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to compile %s",
			     error_local->message);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
_claim_components (GsPlugin *plugin, GsAppList *list)
{
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		gs_app_set_kind (app, AS_APP_KIND_SHELL_EXTENSION);
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
				    /* TRANSLATORS: the one-line summary */
				    _("GNOME Shell Extension"));
	}
}

gboolean
gs_plugin_add_search (GsPlugin *plugin, gchar **values, GsAppList *list,
		      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GsAppList) list_tmp = gs_app_list_new ();
	if (!g_settings_get_boolean (priv->settings, "enable-shell-extensions-repo"))
		return TRUE;
	if (!_check_silo (plugin, cancellable, error))
		return FALSE;
	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
	if (!gs_appstream_search (plugin, priv->silo, (const gchar * const *) values, list_tmp,
				  cancellable, error))
		return FALSE;
	_claim_components (plugin, list_tmp);
	gs_app_list_add_list (list, list_tmp);
	return TRUE;
}

gboolean
gs_plugin_add_category_apps (GsPlugin *plugin, GsCategory *category, GsAppList *list,
			     GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	if (!g_settings_get_boolean (priv->settings, "enable-shell-extensions-repo"))
		return TRUE;
	if (!_check_silo (plugin, cancellable, error))
		return FALSE;
	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
	return gs_appstream_add_category_apps (plugin, priv->silo, category,
					       list, cancellable, error);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (!g_settings_get_boolean (priv->settings, "enable-shell-extensions-repo"))
		return TRUE;
	if (!gs_plugin_shell_extensions_refresh (plugin,
						 cache_age,
						 cancellable,
						 error))
		return FALSE;
	return _check_silo (plugin, cancellable, error);
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
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* repo not enabled */
	if (!g_settings_get_boolean (priv->settings, "enable-shell-extensions-repo"))
		return TRUE;

	/* just ensure there is any data, no matter how old */
	return gs_plugin_shell_extensions_refresh (plugin,
						   G_MAXUINT,
						   cancellable,
						   error);
}
