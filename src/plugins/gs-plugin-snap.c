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
#include <snapd-glib/snapd-glib.h>
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

static void
get_macaroon (GsPlugin *plugin, gchar **macaroon, gchar ***discharges)
{
	GsAuth *auth;
	const gchar *serialized_macaroon;
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
		g_autofree gchar *icon_data = NULL;
		gsize icon_data_length;
		g_autoptr(GError) error = NULL;

		icon_data = gs_snapd_get_resource (macaroon, discharges, icon_url, &icon_data_length, cancellable, &error);
		if (icon_data != NULL) {
			g_autoptr(GdkPixbufLoader) loader = NULL;

			loader = gdk_pixbuf_loader_new ();
			gdk_pixbuf_loader_write (loader,
						 (guchar *) icon_data,
						 icon_data_length,
						 NULL);
			gdk_pixbuf_loader_close (loader, NULL);
			icon_pixbuf = g_object_ref (gdk_pixbuf_loader_get_pixbuf (loader));
		}
		else
			g_printerr ("Failed to get icon: %s\n", error->message);
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

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_clear_object (&priv->auth);
}

gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autofree gchar *macaroon = NULL;
	g_auto(GStrv) discharges = NULL;
	g_autoptr(JsonArray) result = NULL;
	guint i;

	get_macaroon (plugin, &macaroon, &discharges);
	result = gs_snapd_list (macaroon, discharges, cancellable, error);
	if (result == NULL)
		return FALSE;

	for (i = 0; i < json_array_get_length (result); i++) {
		JsonObject *package = json_array_get_object_element (result, i);
		g_autoptr(GsApp) app = NULL;
		const gchar *status, *name;

		status = json_object_get_string_member (package, "status");
		if (g_strcmp0 (status, "active") != 0)
			continue;

		/* create a unique ID for deduplication, TODO: branch? */
		name = json_object_get_string_member (package, "name");
		app = gs_app_new (name);
		gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_SNAP);
		gs_app_set_management_plugin (app, "snap");
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
		refine_app (plugin, app, package, TRUE, cancellable);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	g_autofree gchar *macaroon = NULL;
	g_auto(GStrv) discharges = NULL;
	g_autoptr(JsonArray) result = NULL;
	guint i;

	get_macaroon (plugin, &macaroon, &discharges);
	result = gs_snapd_find (macaroon, discharges, values, cancellable, error);
	if (result == NULL)
		return FALSE;

	for (i = 0; i < json_array_get_length (result); i++) {
		JsonObject *package = json_array_get_object_element (result, i);
		g_autoptr(GsApp) app = NULL;
		const gchar *name;

		/* create a unique ID for deduplication, TODO: branch? */
		name = json_object_get_string_member (package, "name");
		app = gs_app_new (name);
		gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_SNAP);
		gs_app_set_management_plugin (app, "snap");
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
		refine_app (plugin, app, package, TRUE, cancellable);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	g_autofree gchar *macaroon = NULL;
	g_auto(GStrv) discharges = NULL;
	g_autoptr(JsonObject) result = NULL;

	/* not us */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	get_macaroon (plugin, &macaroon, &discharges);

	result = gs_snapd_list_one (macaroon, discharges, gs_app_get_id (app), cancellable, error);
	if (result == NULL)
		return FALSE;
	refine_app (plugin, app, result, FALSE, cancellable);

	return TRUE;
}

static void
progress_cb (JsonObject *result, gpointer user_data)
{
	GsApp *app = user_data;
	JsonArray *tasks;
	GList *task_list, *l;
	gint64 done = 0, total = 0;

	tasks = json_object_get_array_member (result, "tasks");
	task_list = json_array_get_elements (tasks);

	for (l = task_list; l != NULL; l = l->next) {
		JsonObject *task, *progress;
		gint64 task_done, task_total;

		task = json_node_get_object (l->data);
		progress = json_object_get_object_member (task, "progress");
		task_done = json_object_get_int_member (progress, "done");
		task_total = json_object_get_int_member (progress, "total");

		done += task_done;
		total += task_total;
	}

	gs_app_set_progress (app, (guint) (100 * done / total));

	g_list_free (task_list);
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autofree gchar *macaroon = NULL;
	g_auto(GStrv) discharges = NULL;

	/* We can only install apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	get_macaroon (plugin, &macaroon, &discharges);

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	get_macaroon (plugin, &macaroon, &discharges);
	if (!gs_snapd_install (macaroon, discharges, gs_app_get_id (app), progress_cb, app, cancellable, error)) {
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
	g_autofree gchar *macaroon = NULL;
	g_auto(GStrv) discharges = NULL;

	/* We can only remove apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	get_macaroon (plugin, &macaroon, &discharges);

	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	if (!gs_snapd_remove (macaroon, discharges, gs_app_get_id (app), progress_cb, app, cancellable, error)) {
		gs_app_set_state_recover (app);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
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
	g_autoptr(SnapdAuthData) auth_data = NULL;
	g_autoptr(GVariant) macaroon_variant = NULL;
	g_autofree gchar *serialized_macaroon = NULL;
	g_autoptr(GError) local_error = NULL;

	if (auth != priv->auth)
		return TRUE;

	auth_data = snapd_login_sync (gs_auth_get_username (auth), gs_auth_get_password (auth), gs_auth_get_pin (auth), NULL, &local_error);
	if (auth_data == NULL) {
		if (g_error_matches (local_error, SNAPD_ERROR, SNAPD_ERROR_TWO_FACTOR_REQUIRED)) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_PIN_REQUIRED,
					     local_error->message);
		} else if (g_error_matches (local_error, SNAPD_ERROR, SNAPD_ERROR_AUTH_DATA_INVALID) ||
			   g_error_matches (local_error, SNAPD_ERROR, SNAPD_ERROR_TWO_FACTOR_INVALID)) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_AUTH_INVALID,
					     local_error->message);
		} else {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     local_error->message);
		}
		return FALSE;
	}

	macaroon_variant = g_variant_new ("(s^as)",
					  snapd_auth_data_get_macaroon (auth_data),
					  snapd_auth_data_get_discharges (auth_data));
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
