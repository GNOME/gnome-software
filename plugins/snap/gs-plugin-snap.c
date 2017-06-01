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

#include <snapd-glib/snapd-glib.h>
#include <gnome-software.h>

struct GsPluginData {
	GsAuth		*auth;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	g_autoptr(SnapdClient) client = NULL;
	g_autoptr (GError) error = NULL;

	client = snapd_client_new ();
	if (!snapd_client_connect_sync (client, NULL, &error)) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	priv->auth = gs_auth_new ("snapd");
	gs_auth_set_provider_name (priv->auth, "Snap Store");
	gs_auth_set_provider_schema (priv->auth, "com.ubuntu.UbuntuOne.GnomeSoftware");
	gs_plugin_add_auth (plugin, priv->auth);

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "desktop-categories");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "ubuntu-reviews");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_BETTER_THAN, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "icons");

	/* set name of MetaInfo file */
	gs_plugin_set_appstream_id (plugin, "org.gnome.Software.Plugin.Snap");
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

static SnapdAuthData *
get_auth (GsPlugin *plugin)
{
	GsAuth *auth;
	const gchar *serialized_macaroon;
	g_autoptr(GVariant) macaroon_variant = NULL;
	const gchar *macaroon;
	g_auto(GStrv) discharges = NULL;
	g_autoptr (GError) error_local = NULL;

	auth = gs_plugin_get_auth_by_id (plugin, "snapd");
	if (auth == NULL)
		return NULL;
	serialized_macaroon = gs_auth_get_metadata_item (auth, "macaroon");
	if (serialized_macaroon == NULL)
		return NULL;
	macaroon_variant = g_variant_parse (G_VARIANT_TYPE ("(sas)"),
					    serialized_macaroon,
					    NULL,
					    NULL,
					    NULL);
	if (macaroon_variant == NULL)
		return NULL;
	g_variant_get (macaroon_variant, "(&s^as)", &macaroon, &discharges);

	return snapd_auth_data_new (macaroon, discharges);
}

static SnapdClient *
get_client (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	g_autoptr(SnapdClient) client = NULL;
	g_autoptr(SnapdAuthData) auth_data = NULL;

	client = snapd_client_new ();
	if (!snapd_client_connect_sync (client, cancellable, error))
		return NULL;
	auth_data = get_auth (plugin);
	snapd_client_set_auth_data (client, auth_data);

	return g_steal_pointer (&client);
}

static gboolean
gs_plugin_snap_set_app_pixbuf_from_data (GsApp *app, const gchar *buf, gsize count, GError **error)
{
	g_autoptr(GdkPixbufLoader) loader = NULL;
	g_autoptr(GError) error_local = NULL;

	loader = gdk_pixbuf_loader_new ();
	if (!gdk_pixbuf_loader_write (loader, buf, count, &error_local)) {
		g_debug ("icon_data[%" G_GSIZE_FORMAT "]=%s", count, buf);
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to write: %s",
			     error_local->message);
		return FALSE;
	}
	if (!gdk_pixbuf_loader_close (loader, &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to close: %s",
			     error_local->message);
		return FALSE;
	}
	gs_app_set_pixbuf (app, gdk_pixbuf_loader_get_pixbuf (loader));
	return TRUE;
}

static gboolean
refine_icon (GsPlugin *plugin, GsApp *app, GCancellable *cancellable, GError **error)
{
	const gchar *icon_url;

	icon_url = gs_app_get_metadata_item (app, "snap::icon");
	if (icon_url == NULL || g_strcmp0 (icon_url, "") == 0) {
		g_autoptr(AsIcon) icon = as_icon_new ();
		as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
		as_icon_set_name (icon, "package-x-generic");
		gs_app_add_icon (app, icon);
		return TRUE;
	}

	/* icon is optional, either loaded from snapd or from a URL */
	if (g_str_has_prefix (icon_url, "/")) {
		g_autoptr(SnapdClient) client = NULL;
		g_autoptr(SnapdIcon) icon = NULL;

		client = get_client (plugin, cancellable, error);
		if (client == NULL)
			return FALSE;

		icon = snapd_client_get_icon_sync (client, gs_app_get_id (app), cancellable, error);
		if (icon == NULL)
			return FALSE;

		if (!gs_plugin_snap_set_app_pixbuf_from_data (app,
							      g_bytes_get_data (snapd_icon_get_data (icon), NULL),
							      g_bytes_get_size (snapd_icon_get_data (icon)),
							      error)) {
			g_prefix_error (error, "Failed to load %s: ", icon_url);
			return FALSE;
		}
	} else {
		g_autoptr(SoupMessage) message = NULL;
		g_autoptr(GdkPixbufLoader) loader = NULL;
		message = soup_message_new (SOUP_METHOD_GET, icon_url);
		if (message == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "Failed to parse icon URL: %s",
				     icon_url);
			return FALSE;
		}
		soup_session_send_message (gs_plugin_get_soup_session (plugin), message);
		if (!gs_plugin_snap_set_app_pixbuf_from_data (app,
					(const gchar *) message->response_body->data,
					message->response_body->length,
					error)) {
			g_prefix_error (error, "Failed to load %s: ", icon_url);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
gs_plugin_snap_refine_app (GsPlugin *plugin,
			   GsApp *app,
			   GsPluginRefineFlags flags,
			   SnapdSnap *snap,
			   gboolean from_search,
			   GCancellable *cancellable,
			   GError **error)
{
	const gchar *icon_url, *launch_name = NULL;
	const gchar *origin;
	g_autofree gchar *origin_hostname = NULL;
	g_autoptr(GdkPixbuf) icon_pixbuf = NULL;
	GPtrArray *screenshots;

	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	switch (snapd_snap_get_status (snap)) {
	case SNAPD_SNAP_STATUS_INSTALLED:
	case SNAPD_SNAP_STATUS_ACTIVE:
		if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE)
			gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		break;
	case SNAPD_SNAP_STATUS_AVAILABLE:
	case SNAPD_SNAP_STATUS_PRICED:
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		break;
	default:
		g_warning ("Ignoring snap with unknown state");
		return TRUE;
	}
	gs_app_set_name (app, GS_APP_QUALITY_HIGHEST, snapd_snap_get_name (snap));
	gs_app_set_summary (app, GS_APP_QUALITY_HIGHEST, snapd_snap_get_summary (snap));
	gs_app_set_description (app, GS_APP_QUALITY_HIGHEST, snapd_snap_get_description (snap));
	gs_app_set_version (app, snapd_snap_get_version (snap));
	if (snapd_snap_get_installed_size (snap) > 0)
		gs_app_set_size_installed (app, snapd_snap_get_installed_size (snap));
	if (snapd_snap_get_download_size (snap) > 0)
		gs_app_set_size_download (app, snapd_snap_get_download_size (snap));
	if (snapd_snap_get_install_date (snap) != NULL)
		gs_app_set_install_date (app, g_date_time_to_unix (snapd_snap_get_install_date (snap)));
	if (gs_plugin_check_distro_id (plugin, "ubuntu"))
		gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
	if (snapd_snap_get_confinement (snap) == SNAPD_CONFINEMENT_STRICT)
		gs_app_add_kudo (app, GS_APP_KUDO_SANDBOXED);

	icon_url = snapd_snap_get_icon (snap);
	if (icon_url != NULL)
		gs_app_set_metadata (app, "snap::icon", icon_url);

	/* assume the origin is where the icon is coming from */
	if (icon_url != NULL && g_strcmp0 (icon_url, "") != 0 && !g_str_has_prefix (icon_url, "/"))
		origin_hostname = g_strdup (icon_url);

        if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON) {
		if (!refine_icon (plugin, app, cancellable, error))
			return FALSE;
	}

	screenshots = snapd_snap_get_screenshots (snap);
	if (screenshots != NULL && screenshots->len > 0) {
		guint i;

		for (i = 0; i < screenshots->len; i++) {
			SnapdScreenshot *screenshot = screenshots->pdata[i];
			g_autoptr(AsScreenshot) ss = NULL;
			g_autoptr(AsImage) image = NULL;

			ss = as_screenshot_new ();
			as_screenshot_set_kind (ss, AS_SCREENSHOT_KIND_NORMAL);
			image = as_image_new ();
			as_image_set_url (image, snapd_screenshot_get_url (screenshot));
			as_image_set_kind (image, AS_IMAGE_KIND_SOURCE);
			as_image_set_width (image, snapd_screenshot_get_width (screenshot));
			as_image_set_height (image, snapd_screenshot_get_height (screenshot));
			as_screenshot_add_image (ss, image);
			gs_app_add_screenshot (app, ss);

			/* fall back to the screenshot */
			if (origin_hostname == NULL)
				origin_hostname = g_strdup (snapd_screenshot_get_url (screenshot));
		}
	}

	/* set the application origin */
	if (gs_app_get_origin_hostname (app) == NULL) {
		/* from the snap store */
		origin = snapd_snap_get_developer (snap);
		if (g_strcmp0 (origin, "canonical") == 0)
			gs_app_set_origin_hostname (app, "myapps.developer.ubuntu.com");
		else if (origin_hostname != NULL)
			gs_app_set_origin_hostname (app, origin_hostname);
	}

	if (!from_search) {
		GPtrArray *apps;

		apps = snapd_snap_get_apps (snap);
		if (apps->len > 0)
			launch_name = snapd_app_get_name (apps->pdata[0]);

		if (launch_name != NULL)
			gs_app_set_metadata (app, "snap::launch-name", launch_name);
		else
			gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
	}
	return TRUE;
}

gboolean
gs_plugin_url_to_app (GsPlugin *plugin,
		      GsAppList *list,
		      const gchar *url,
		      GCancellable *cancellable,
		      GError **error)
{
	g_autofree gchar *scheme = NULL;
	g_autoptr(SnapdClient) client = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(GPtrArray) snaps = NULL;
	SnapdSnap *snap;
	g_autoptr(GsApp) app = NULL;

	/* not us */
	scheme = gs_utils_get_url_scheme (url);
	if (g_strcmp0 (scheme, "snap") != 0)
		return TRUE;

	/* create app */
	path = gs_utils_get_url_path (url);
	client = get_client (plugin, cancellable, error);
	if (client == NULL)
		return FALSE;
	snaps = snapd_client_find_sync (client, SNAPD_FIND_FLAGS_MATCH_NAME, path, NULL, cancellable, NULL);
	if (snaps == NULL || snaps->len < 1)
		return TRUE;

	snap = snaps->pdata[0];
	app = gs_app_new (snapd_snap_get_name (snap));
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_SNAP);
	gs_app_set_management_plugin (app, "snap");
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
	if (!gs_plugin_snap_refine_app (plugin, app, GS_PLUGIN_REFINE_FLAGS_DEFAULT, snap, TRUE, cancellable, error))
		return FALSE;
	gs_app_list_add (list, app);

	return TRUE;
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
	g_autoptr(SnapdClient) client = NULL;
	g_autoptr(GPtrArray) snaps = NULL;
	guint i;

	client = get_client (plugin, cancellable, error);
	if (client == NULL)
		return FALSE;
	snaps = snapd_client_list_sync (client, cancellable, error);
	if (snaps == NULL)
		return FALSE;

	for (i = 0; i < snaps->len; i++) {
		SnapdSnap *snap = snaps->pdata[i];
		g_autoptr(GsApp) app = NULL;

		if (snapd_snap_get_status (snap) != SNAPD_SNAP_STATUS_ACTIVE)
			continue;

		/* create a unique ID for deduplication, TODO: branch? */
		app = gs_app_new (snapd_snap_get_name (snap));
		gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_SNAP);
		gs_app_set_management_plugin (app, "snap");
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
		if (!gs_plugin_snap_refine_app (plugin, app, GS_PLUGIN_REFINE_FLAGS_DEFAULT, snap, TRUE, cancellable, error))
			return FALSE;
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
	g_autoptr(SnapdClient) client = NULL;
	g_autofree gchar *query = NULL;
	g_autoptr(GPtrArray) snaps = NULL;
	guint i;

	client = get_client (plugin, cancellable, error);
	if (client == NULL)
		return FALSE;
	query = g_strjoinv (" ", values);
	snaps = snapd_client_find_sync (client, SNAPD_FIND_FLAGS_NONE, query, NULL, cancellable, error);
	if (snaps == NULL)
		return FALSE;

	for (i = 0; i < snaps->len; i++) {
		SnapdSnap *snap = snaps->pdata[i];
		g_autoptr(GsApp) app = NULL;

		/* create a unique ID for deduplication, TODO: branch? */
		app = gs_app_new (snapd_snap_get_name (snap));
		gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_SNAP);
		gs_app_set_management_plugin (app, "snap");
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
		if (!gs_plugin_snap_refine_app (plugin, app, GS_PLUGIN_REFINE_FLAGS_DEFAULT, snap, TRUE, cancellable, error))
			return FALSE;
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
	g_autoptr(SnapdClient) client = NULL;
	const gchar *id;
	g_autoptr(SnapdSnap) snap = NULL;

	/* not us */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	client = get_client (plugin, cancellable, error);
	if (client == NULL)
		return FALSE;
	id = gs_app_get_id (app);
	if (id == NULL)
		id = gs_app_get_source_default (app);
	snap = snapd_client_list_one_sync (client, id, cancellable, error);
	if (snap == NULL)
		return FALSE;
	if (!gs_plugin_snap_refine_app (plugin, app, flags, snap, FALSE, cancellable, error))
		return FALSE;

	return TRUE;
}

static void
progress_cb (SnapdClient *client, SnapdChange *change, gpointer deprecated, gpointer user_data)
{
	GsApp *app = user_data;
	GPtrArray *tasks;
	guint i;
	gint64 done = 0, total = 0;

	tasks = snapd_change_get_tasks (change);
	for (i = 0; i < tasks->len; i++) {
		SnapdTask *task = tasks->pdata[i];
		done += snapd_task_get_progress_done (task);
		total += snapd_task_get_progress_total (task);
	}

	gs_app_set_progress (app, (guint) (100 * done / total));
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(SnapdClient) client = NULL;

	/* We can only install apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	client = get_client (plugin, cancellable, error);
	if (client == NULL)
		return FALSE;
	if (!snapd_client_install_sync (client, gs_app_get_id (app), NULL, progress_cb, app, cancellable, error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

// Check if an app is graphical by checking if it uses a known GUI interface.
// This doesn't necessarily mean that every binary uses this interfaces, but is probably true.
// https://bugs.launchpad.net/bugs/1595023
static gboolean
is_graphical (GsPlugin *plugin, GsApp *app, GCancellable *cancellable)
{
	g_autoptr(SnapdClient) client = NULL;
	g_autoptr(GPtrArray) plugs = NULL;
	guint i;
	g_autoptr(GError) error = NULL;

	client = get_client (plugin, cancellable, &error);
	if (client == NULL) {
		g_warning ("Failed to make snapd client: %s", error->message);
		return FALSE;
	}
	if (!snapd_client_get_interfaces_sync (client, &plugs, NULL, cancellable, &error)) {
		g_warning ("Failed to check interfaces: %s", error->message);
		return FALSE;
	}

	for (i = 0; i < plugs->len; i++) {
		SnapdPlug *plug = plugs->pdata[i];
		const gchar *interface;

		// Only looks at the plugs for this snap
		if (g_strcmp0 (snapd_plug_get_snap (plug), gs_app_get_id (app)) != 0)
			continue;

		interface = snapd_plug_get_interface (plug);
		if (interface == NULL)
			continue;

		if (g_strcmp0 (interface, "unity7") == 0 || g_strcmp0 (interface, "x11") == 0 || g_strcmp0 (interface, "mir") == 0)
			return TRUE;
	}

	return FALSE;
}

gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *launch_name;
	g_autofree gchar *binary_name = NULL;
	GAppInfoCreateFlags flags = G_APP_INFO_CREATE_NONE;
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

	if (!is_graphical (plugin, app, cancellable))
		flags |= G_APP_INFO_CREATE_NEEDS_TERMINAL;
	info = g_app_info_create_from_commandline (binary_name, NULL, flags, error);
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
	g_autoptr(SnapdClient) client = NULL;

	/* We can only remove apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	client = get_client (plugin, cancellable, error);
	if (client == NULL)
		return FALSE;
	if (!snapd_client_remove_sync (client, gs_app_get_id (app), progress_cb, app, cancellable, error)) {
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
			g_dbus_error_strip_remote_error (local_error);
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
