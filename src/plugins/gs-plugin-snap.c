/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Canonical Ltd
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

#include <json-glib/json-glib.h>
#include <gnome-software.h>

#include "gs-snapd.h"

struct GsPluginData {
	GsAuth		*auth;
};

typedef gboolean (*AppFilterFunc)(const gchar *id, JsonObject *object, gpointer data);

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	if (!gs_snapd_exists ()) {
		g_debug ("disabling '%s' as snapd not running",
			 gs_plugin_get_name (plugin));
		gs_plugin_set_enabled (plugin, FALSE);
	}

	priv->auth = gs_auth_new ("snapd");
	gs_auth_set_provider_name (priv->auth, "Snap Store");
	gs_auth_set_provider_schema (priv->auth, "com.ubuntu.UbuntuOne.GnomeSoftware");
	gs_plugin_add_auth (plugin, priv->auth);

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "desktop-categories");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "ubuntu-reviews");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_BETTER_THAN, "packagekit");
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* load from disk */
	gs_auth_add_metadata (priv->auth, "macaroon", NULL);
	if (!gs_auth_store_load (priv->auth,
				 GS_AUTH_STORE_FLAG_USERNAME |
				 GS_AUTH_STORE_FLAG_METADATA,
				 cancellable, error))
		return FALSE;

	/* success */
	return TRUE;
}

static JsonParser *
parse_result (const gchar *response, const gchar *response_type, GError **error)
{
	g_autoptr(JsonParser) parser = NULL;
	g_autoptr(GError) error_local = NULL;

	if (response_type == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "snapd returned no content type");
		return NULL;
	}
	if (g_strcmp0 (response_type, "application/json") != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned unexpected content type %s", response_type);
		return NULL;
	}

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, response, -1, &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Unable to parse snapd response: %s",
			     error_local->message);
		return NULL;
	}
	if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser))) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "snapd response does is not a valid JSON object");
		return NULL;
	}

	return g_object_ref (parser);
}

static void
get_macaroon (GsPlugin *plugin, gchar **macaroon, gchar ***discharges)
{
	GsAuth *auth;
	gchar *serialized_macaroon;
	g_autoptr(GVariant) macaroon_variant = NULL;
	g_autoptr (GError) error_local = NULL;

	*macaroon = NULL;
	*discharges = NULL;

	auth = gs_plugin_get_auth_by_id (plugin, "snapd");
	if (auth == NULL)
		return;
	serialized_macaroon = gs_auth_get_metadata_item (auth, "macaroon");
	if (serialized_macaroon == NULL)
		return;
	macaroon_variant = g_variant_parse (G_VARIANT_TYPE ("(sas)"),
					    serialized_macaroon,
					    NULL,
					    NULL,
					    NULL);
	if (macaroon_variant == NULL)
		return;
	g_variant_get (macaroon_variant, "(s^as)", macaroon, discharges);
}

static void
refine_app (GsPlugin *plugin, GsApp *app, JsonObject *package, gboolean from_search, GCancellable *cancellable)
{
	g_autofree gchar *macaroon = NULL;
	g_auto(GStrv) discharges = NULL;
	const gchar *status, *icon_url, *launch_name = NULL;
	g_autoptr(GdkPixbuf) icon_pixbuf = NULL;
	gint64 size = -1;

	get_macaroon (plugin, &macaroon, &discharges);

	status = json_object_get_string_member (package, "status");
	if (g_strcmp0 (status, "installed") == 0 || g_strcmp0 (status, "active") == 0) {
		const gchar *update_available;

		update_available = json_object_has_member (package, "update_available") ?
			json_object_get_string_member (package, "update_available") : NULL;
		if (update_available)
			gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
		else {
			if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE)
				gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		}
	} else if (g_strcmp0 (status, "not installed") == 0 || g_strcmp0 (status, "available") == 0) {
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	}
	gs_app_set_name (app, GS_APP_QUALITY_HIGHEST,
			 json_object_get_string_member (package, "summary"));
	gs_app_set_summary (app, GS_APP_QUALITY_HIGHEST,
			    json_object_get_string_member (package, "description"));
	gs_app_set_version (app, json_object_get_string_member (package, "version"));
	if (json_object_has_member (package, "installed-size")) {
		size = json_object_get_int_member (package, "installed-size");
		if (size > 0)
			gs_app_set_size_installed (app, (guint64) size);
	}
	if (json_object_has_member (package, "download-size")) {
		size = json_object_get_int_member (package, "download-size");
		if (size > 0)
			gs_app_set_size_download (app, (guint64) size);
	}
	gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
	icon_url = json_object_get_string_member (package, "icon");
	if (g_str_has_prefix (icon_url, "/")) {
		g_autofree gchar *icon_response = NULL;
		gsize icon_response_length;

		if (gs_snapd_request ("GET", icon_url, NULL,
				      macaroon, discharges,
				      NULL, NULL,
				      NULL, &icon_response, &icon_response_length,
				      cancellable, NULL)) {
			g_autoptr(GdkPixbufLoader) loader = NULL;

			loader = gdk_pixbuf_loader_new ();
			gdk_pixbuf_loader_write (loader,
						 (guchar *) icon_response,
						 icon_response_length,
						 NULL);
			gdk_pixbuf_loader_close (loader, NULL);
			icon_pixbuf = g_object_ref (gdk_pixbuf_loader_get_pixbuf (loader));
		}
		else
			g_printerr ("Failed to get icon\n");
	}
	else {
		g_autoptr(SoupMessage) message = NULL;
		g_autoptr(GdkPixbufLoader) loader = NULL;

		message = soup_message_new (SOUP_METHOD_GET, icon_url);
		if (message != NULL) {
			soup_session_send_message (gs_plugin_get_soup_session (plugin), message);
			loader = gdk_pixbuf_loader_new ();
			gdk_pixbuf_loader_write (loader,
						 (guint8 *) message->response_body->data,
						 (gsize) message->response_body->length,
						 NULL);
			gdk_pixbuf_loader_close (loader, NULL);
			icon_pixbuf = g_object_ref (gdk_pixbuf_loader_get_pixbuf (loader));
		}
	}

	if (icon_pixbuf) {
		gs_app_set_pixbuf (app, icon_pixbuf);
	} else {
		g_autoptr(AsIcon) icon = as_icon_new ();
		as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
		as_icon_set_name (icon, "package-x-generic");
		gs_app_add_icon (app, icon);
	}

	if (!from_search) {
		JsonArray *apps;

		apps = json_object_get_array_member (package, "apps");
		if (apps && json_array_get_length (apps) > 0)
			launch_name = json_object_get_string_member (json_array_get_object_element (apps, 0), "name");

		if (launch_name)
			gs_app_set_metadata (app, "snap::launch-name", launch_name);
		else
			gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
	}
}

static gboolean
get_apps (GsPlugin *plugin,
	  const gchar *sources,
	  gchar **search_terms,
	  GsAppList *list,
	  AppFilterFunc filter_func,
	  gpointer user_data,
	  GCancellable *cancellable,
	  GError **error)
{
	g_autofree gchar *macaroon = NULL;
	g_auto(GStrv) discharges = NULL;
	guint status_code;
	GPtrArray *query_fields;
	g_autoptr (GString) path = NULL;
	g_autofree gchar *reason_phrase = NULL, *response_type = NULL, *response = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root;
	JsonArray *result;
	GList *snaps;
	GList *l;

	get_macaroon (plugin, &macaroon, &discharges);

	/* Get all the apps */
	query_fields = g_ptr_array_new_with_free_func (g_free);
	if (sources != NULL) {
		g_autofree gchar *escaped;
		escaped = soup_uri_encode (sources, NULL);
		g_ptr_array_add (query_fields, g_strdup_printf ("sources=%s", escaped));
	}
	if (search_terms != NULL && search_terms[0] != NULL) {
		g_autoptr (GString) query = NULL;
		g_autofree gchar *escaped = NULL;
		gint i;

		query = g_string_new ("q=");
		escaped = soup_uri_encode (search_terms[0], NULL);
		g_string_append (query, escaped);
		for (i = 1; search_terms[i] != NULL; i++) {
			g_autofree gchar *e = soup_uri_encode (search_terms[0], NULL);
			g_string_append_printf (query, "+%s", e);
		}
		g_ptr_array_add (query_fields, g_strdup (query->str));
		path = g_string_new ("/v2/find");
	}
	else
		path = g_string_new ("/v2/snaps");
	g_ptr_array_add (query_fields, NULL);
	if (query_fields->len > 1) {
		g_autofree gchar *fields = NULL;
		g_string_append (path, "?");
		fields = g_strjoinv ("&", (gchar **) query_fields->pdata);
		g_string_append (path, fields);
	}
	g_ptr_array_free (query_fields, TRUE);
	if (!gs_snapd_request ("GET", path->str, NULL,
			       macaroon, discharges,
			       &status_code, &reason_phrase,
			       &response_type, &response, NULL,
			       cancellable, error))
		return FALSE;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return FALSE;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return FALSE;

	root = json_node_get_object (json_parser_get_root (parser));
	result = json_object_get_array_member (root, "result");
	snaps = json_array_get_elements (result);

	for (l = snaps; l != NULL; l = l->next) {
		JsonObject *package = json_node_get_object (l->data);
		g_autoptr(GsApp) app = NULL;
		g_autofree gchar *unique_id = NULL;
		const gchar *id;

		id = json_object_get_string_member (package, "name");

		if (filter_func != NULL && !filter_func (id, package, user_data))
			continue;

		/* create a unique ID for deduplication, TODO: branch?, arch? */
		unique_id = as_utils_unique_id_build (AS_APP_SCOPE_SYSTEM,
						      AS_BUNDLE_KIND_SNAP,
						      NULL,	/* origin */
						      AS_APP_KIND_UNKNOWN,
						      id,
						      NULL,	/* arch */
						      NULL);	/* branch */
		app = gs_app_new (id);
		gs_app_set_unique_id (app, unique_id);
		gs_app_set_management_plugin (app, "snap");
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
		refine_app (plugin, app, package, TRUE, cancellable);
		gs_app_list_add (list, app);
	}

	g_list_free (snaps);

	return TRUE;
}

static gboolean
get_app (GsPlugin *plugin, GsApp *app, GCancellable *cancellable, GError **error)
{
	g_autofree gchar *macaroon = NULL;
	g_auto(GStrv) discharges = NULL;
	guint status_code;
	g_autofree gchar *path = NULL;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response = NULL;
	g_autofree gchar *response_type = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root;
	JsonObject *result;

	get_macaroon (plugin, &macaroon, &discharges);

	path = g_strdup_printf ("/v2/snaps/%s", gs_app_get_id (app));
	if (!gs_snapd_request ("GET", path, NULL,
			       macaroon, discharges,
			       &status_code, &reason_phrase,
			       &response_type, &response, NULL,
			       cancellable, error))
		return FALSE;

	if (status_code == SOUP_STATUS_NOT_FOUND)
		return TRUE;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return FALSE;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return FALSE;
	root = json_node_get_object (json_parser_get_root (parser));
	result = json_object_get_object_member (root, "result");
	if (result == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned no results for %s", gs_app_get_id (app));
		return FALSE;
	}

	refine_app (plugin, app, result, FALSE, cancellable);

	return TRUE;
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_clear_object (&priv->auth);
}

static gboolean
is_active (const gchar *id, JsonObject *object, gpointer data)
{
	const gchar *status = json_object_get_string_member (object, "status");
	return g_strcmp0 (status, "active") == 0;
}

gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	return get_apps (plugin, "local", NULL, list, is_active, NULL, cancellable, error);
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	return get_apps (plugin, NULL, values, list, NULL, values, cancellable, error);
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	/* not us */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	// Get info from snapd
	return get_app (plugin, app, cancellable, error);
}

static gboolean
send_package_action (GsPlugin *plugin,
		     GsApp *app,
		     const gchar *id,
		     const gchar *action,
		     GCancellable *cancellable,
		     GError **error)
{
	g_autofree gchar *macaroon = NULL;
	g_auto(GStrv) discharges = NULL;
	g_autofree gchar *content = NULL, *path = NULL;
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *response = NULL;
	g_autofree gchar *status = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root, *result, *task, *progress;
	JsonArray *tasks;
	GList *task_list, *l;
	gint64 done, total, task_done, task_total;
        const gchar *resource_path;
	const gchar *type;
	const gchar *change_id;

	get_macaroon (plugin, &macaroon, &discharges);

	content = g_strdup_printf ("{\"action\": \"%s\"}", action);
	path = g_strdup_printf ("/v2/snaps/%s", id);
	if (!gs_snapd_request ("POST", path, content,
			       macaroon, discharges,
			       &status_code, &reason_phrase,
			       &response_type, &response, NULL,
			       cancellable, error))
		return FALSE;

	if (status_code == SOUP_STATUS_UNAUTHORIZED) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_AUTH_REQUIRED,
				     "Requires authentication with @snapd");
		return FALSE;
	}

	if (status_code != SOUP_STATUS_ACCEPTED) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return FALSE;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return FALSE;

	root = json_node_get_object (json_parser_get_root (parser));
	type = json_object_get_string_member (root, "type");

	if (g_strcmp0 (type, "async") == 0) {
		change_id = json_object_get_string_member (root, "change");
		resource_path = g_strdup_printf ("/v2/changes/%s", change_id);

		while (TRUE) {
			g_autofree gchar *status_reason_phrase = NULL;
			g_autofree gchar *status_response_type = NULL;
			g_autofree gchar *status_response = NULL;
			g_autoptr(JsonParser) status_parser = NULL;

			/* Wait for a little bit before polling */
			g_usleep (100 * 1000);

			if (!gs_snapd_request ("GET", resource_path, NULL,
					       macaroon, discharges,
					       &status_code, &status_reason_phrase,
					       &status_response_type, &status_response, NULL,
					       cancellable, error)) {
				return FALSE;
			}

			if (status_code != SOUP_STATUS_OK) {
				g_set_error (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "snapd returned status code %u: %s",
					     status_code, status_reason_phrase);
				return FALSE;
			}

			status_parser = parse_result (status_response, status_response_type, error);
			if (status_parser == NULL)
				return FALSE;

			root = json_node_get_object (json_parser_get_root (status_parser));
			result = json_object_get_object_member (root, "result");

			g_free (status);
			status = g_strdup (json_object_get_string_member (result, "status"));

			if (g_strcmp0 (status, "Done") == 0)
				break;

			tasks = json_object_get_array_member (result, "tasks");
			task_list = json_array_get_elements (tasks);

			done = 0;
			total = 0;

			for (l = task_list; l != NULL; l = l->next) {
				task = json_node_get_object (l->data);
				progress = json_object_get_object_member (task, "progress");
				task_done = json_object_get_int_member (progress, "done");
				task_total = json_object_get_int_member (progress, "total");

				done += task_done;
				total += task_total;
			}

			if (total > 0)
				gs_app_set_progress (app, (guint) (100 * done / total));

			g_list_free (task_list);
		}
	}

	if (g_strcmp0 (status, "Done") != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd operation finished with status %s", status);
		return FALSE;
	}

	return TRUE;
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	gboolean ret;

	/* We can only install apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	ret = send_package_action (plugin, app, gs_app_get_id (app), "install", cancellable, error);
	if (!ret) {
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *launch_name;
	g_autofree gchar *binary_name = NULL;
	g_autoptr(GAppInfo) info = NULL;

	/* We can only launch apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	launch_name = gs_app_get_metadata_item (app, "snap::launch-name");
	if (!launch_name)
		return TRUE;

	if (g_strcmp0 (launch_name, gs_app_get_id (app)) == 0)
		binary_name = g_strdup_printf ("/snap/bin/%s", launch_name);
	else
		binary_name = g_strdup_printf ("/snap/bin/%s.%s", gs_app_get_id (app), launch_name);

	// FIXME: Since we don't currently know if this app needs a terminal or not we launch everything with one
	// https://bugs.launchpad.net/bugs/1595023
	info = g_app_info_create_from_commandline (binary_name, NULL, G_APP_INFO_CREATE_NEEDS_TERMINAL, error);
	if (info == NULL)
		return FALSE;

	return g_app_info_launch (info, NULL, NULL, error);
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	gboolean ret;

	/* We can only remove apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	ret = send_package_action (plugin, app, gs_app_get_id (app), "remove", cancellable, error);
	if (!ret) {
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	return TRUE;
}

gboolean
gs_plugin_auth_login (GsPlugin *plugin, GsAuth *auth,
		      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonNode) json_root = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autofree gchar *data = NULL;
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *response = NULL;
	g_autoptr(JsonObject) result = NULL;
	JsonArray *discharges;
	guint i;
	g_autoptr(GVariantBuilder) b = NULL;
	g_autoptr(GVariant) macaroon_variant = NULL;
	g_autofree gchar *serialized_macaroon = NULL;

	if (auth != priv->auth)
		return TRUE;

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "username");
	json_builder_add_string_value (builder, gs_auth_get_username (auth));
	json_builder_set_member_name (builder, "password");
	json_builder_add_string_value (builder, gs_auth_get_password (auth));
	if (gs_auth_get_pin (auth)) {
		json_builder_set_member_name (builder, "otp");
		json_builder_add_string_value (builder, gs_auth_get_pin (auth));
	}
	json_builder_end_object (builder);

	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "Failed to generate JSON request");
		return FALSE;
	}

	if (!gs_snapd_request ("POST", "/v2/login", data,
			       NULL, NULL,
			       &status_code, &reason_phrase,
			       &response_type, &response, NULL,
			       cancellable, error))
		return FALSE;

	if (status_code != SOUP_STATUS_OK) {
		g_autofree gchar *error_message = NULL;
		g_autofree gchar *error_kind = NULL;

		if (!gs_snapd_parse_error (response_type, response, &error_message, &error_kind, error))
			return FALSE;

		if (g_strcmp0 (error_kind, "two-factor-required") == 0) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_PIN_REQUIRED,
					     error_message);
		}
		else {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     error_message);
		}
		return FALSE;
	}

	if (!gs_snapd_parse_result (response_type, response, &result, error))
		return FALSE;

	if (!json_object_has_member (result, "macaroon")) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "Login response missing macaroon");
		return FALSE;
	}
	discharges = json_object_get_array_member (result, "discharges");
	b = g_variant_builder_new (G_VARIANT_TYPE ("as"));
	for (i = 0; i < json_array_get_length (discharges); i++) {
		JsonNode *node;
		node = json_array_get_element (discharges, i);
		if (!JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) != G_TYPE_STRING) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "Macaroon discharge contains unexpected value");
			return FALSE;
		}
		g_variant_builder_add (b, "s", json_node_get_string (node));
	}
	macaroon_variant = g_variant_new ("(sas)",
					  json_object_get_string_member (result, "macaroon"),
					  b);
	serialized_macaroon = g_variant_print (macaroon_variant, FALSE);
	gs_auth_add_metadata (auth, "macaroon", serialized_macaroon);

	/* store */
	if (!gs_auth_store_save (auth,
				 GS_AUTH_STORE_FLAG_USERNAME |
				 GS_AUTH_STORE_FLAG_METADATA,
				 cancellable, error))
		return FALSE;

	gs_auth_add_flags (priv->auth, GS_AUTH_FLAG_VALID);

	return TRUE;
}

gboolean
gs_plugin_auth_lost_password (GsPlugin *plugin, GsAuth *auth,
			      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (auth != priv->auth)
		return TRUE;

	// FIXME: snapd might not be using Ubuntu One accounts
	// https://bugs.launchpad.net/bugs/1598667
	g_set_error_literal (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_AUTH_INVALID,
			     "do online using @https://login.ubuntu.com/+forgot_password");
	return FALSE;
}

gboolean
gs_plugin_auth_register (GsPlugin *plugin, GsAuth *auth,
			 GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (auth != priv->auth)
		return TRUE;

	// FIXME: snapd might not be using Ubuntu One accounts
	// https://bugs.launchpad.net/bugs/1598667
	g_set_error_literal (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_AUTH_INVALID,
			     "do online using @https://login.ubuntu.com/+login");
	return FALSE;
}
