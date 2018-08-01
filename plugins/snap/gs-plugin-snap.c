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

#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <snapd-glib/snapd-glib.h>
#include <gnome-software.h>

struct GsPluginData {
	SnapdAuthData		*auth_data;
	gchar			*store_name;
	SnapdSystemConfinement	 system_confinement;
	GsAuth			*auth;

	GMutex			 store_snaps_lock;
	GHashTable		*store_snaps;
};

static SnapdClient *
get_client (GsPlugin *plugin, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(SnapdClient) client = NULL;
	const gchar *old_user_agent;
	g_autofree gchar *user_agent = NULL;

	client = snapd_client_new ();
	snapd_client_set_allow_interaction (client, TRUE);
	old_user_agent = snapd_client_get_user_agent (client);
	user_agent = g_strdup_printf ("%s %s", gs_user_agent (), old_user_agent);
	snapd_client_set_user_agent (client, user_agent);
	snapd_client_set_auth_data (client, priv->auth_data);

	return g_steal_pointer (&client);
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	g_autoptr(SnapdClient) client = NULL;
	g_autoptr (GError) error = NULL;

	g_mutex_init (&priv->store_snaps_lock);

	client = get_client (plugin, &error);
	if (client == NULL) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	priv->store_snaps = g_hash_table_new_full (g_str_hash, g_str_equal,
						   g_free, (GDestroyNotify) g_object_unref);

	priv->auth = gs_auth_new ("snapd");
	gs_auth_set_provider_name (priv->auth, "Snap Store");
	gs_auth_set_provider_schema (priv->auth, "com.ubuntu.SnapStore.GnomeSoftware");
	gs_plugin_add_auth (plugin, priv->auth);

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "desktop-categories");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "ubuntu-reviews");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_BETTER_THAN, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "icons");

	/* Override hardcoded popular apps */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "hardcoded-popular");

	/* set name of MetaInfo file */
	gs_plugin_set_appstream_id (plugin, "org.gnome.Software.Plugin.Snap");
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_SNAP)
		gs_app_set_management_plugin (app, "snap");
}

static void
snapd_error_convert (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return;

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gio (perror))
		return;

	/* custom to this plugin */
	if (error->domain == SNAPD_ERROR) {
		switch (error->code) {
		case SNAPD_ERROR_AUTH_DATA_REQUIRED:
			error->code = GS_PLUGIN_ERROR_AUTH_REQUIRED;
			g_free (error->message);
			error->message = g_strdup ("Requires authentication with @snapd");
			break;
		case SNAPD_ERROR_TWO_FACTOR_REQUIRED:
			error->code = GS_PLUGIN_ERROR_PIN_REQUIRED;
			break;
		case SNAPD_ERROR_AUTH_DATA_INVALID:
		case SNAPD_ERROR_TWO_FACTOR_INVALID:
			error->code = GS_PLUGIN_ERROR_AUTH_INVALID;
			break;
		case SNAPD_ERROR_CONNECTION_FAILED:
		case SNAPD_ERROR_WRITE_FAILED:
		case SNAPD_ERROR_READ_FAILED:
		case SNAPD_ERROR_BAD_REQUEST:
		case SNAPD_ERROR_BAD_RESPONSE:
		case SNAPD_ERROR_PERMISSION_DENIED:
		case SNAPD_ERROR_FAILED:
		case SNAPD_ERROR_TERMS_NOT_ACCEPTED:
		case SNAPD_ERROR_PAYMENT_NOT_SETUP:
		case SNAPD_ERROR_PAYMENT_DECLINED:
		case SNAPD_ERROR_ALREADY_INSTALLED:
		case SNAPD_ERROR_NOT_INSTALLED:
		case SNAPD_ERROR_NO_UPDATE_AVAILABLE:
		case SNAPD_ERROR_PASSWORD_POLICY_ERROR:
		case SNAPD_ERROR_NEEDS_DEVMODE:
		case SNAPD_ERROR_NEEDS_CLASSIC:
		case SNAPD_ERROR_NEEDS_CLASSIC_SYSTEM:
		default:
			error->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}
	} else {
		g_warning ("can't reliably fixup error from domain %s",
			   g_quark_to_string (error->domain));
		error->code = GS_PLUGIN_ERROR_FAILED;
	}
	error->domain = GS_PLUGIN_ERROR;
}

static void
load_auth (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsAuth *auth;
	const gchar *serialized_macaroon;
	g_autoptr(GVariant) macaroon_variant = NULL;
	const gchar *macaroon;
	g_auto(GStrv) discharges = NULL;
	g_autoptr(SnapdAuthData) auth_data = NULL;

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

	g_variant_get (macaroon_variant, "(&s^as)", &macaroon, &discharges);
	g_clear_object (&priv->auth_data);
	priv->auth_data = snapd_auth_data_new (macaroon, discharges);
	gs_auth_add_flags (priv->auth, GS_AUTH_FLAG_VALID);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(SnapdClient) client = NULL;
	g_autoptr(SnapdSystemInformation) system_information = NULL;

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	system_information = snapd_client_get_system_information_sync (client, cancellable, error);
	if (system_information == NULL)
		return FALSE;
	priv->store_name = g_strdup (snapd_system_information_get_store (system_information));
	if (priv->store_name == NULL)
		priv->store_name = g_strdup (/* TRANSLATORS: default snap store name */
					     _("Snap Store"));
	priv->system_confinement = snapd_system_information_get_confinement (system_information);

	/* load from disk */
	gs_auth_add_metadata (priv->auth, "macaroon", NULL);
	if (!gs_auth_store_load (priv->auth,
				 GS_AUTH_STORE_FLAG_USERNAME |
				 GS_AUTH_STORE_FLAG_METADATA,
				 cancellable, error))
		return FALSE;
	load_auth (plugin);

	/* success */
	return TRUE;
}

static SnapdSnap *
store_snap_cache_lookup (GsPlugin *plugin, const gchar *name)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->store_snaps_lock);
	return g_hash_table_lookup (priv->store_snaps, name);
}

static void
store_snap_cache_update (GsPlugin *plugin, GPtrArray *snaps)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->store_snaps_lock);
	guint i;

	for (i = 0; i < snaps->len; i++) {
		SnapdSnap *snap = snaps->pdata[i];
		g_hash_table_insert (priv->store_snaps, g_strdup (snapd_snap_get_name (snap)), g_object_ref (snap));
	}
}

static GPtrArray *
find_snaps (GsPlugin *plugin, SnapdFindFlags flags, const gchar *section, const gchar *query, GCancellable *cancellable, GError **error)
{
	g_autoptr(SnapdClient) client = NULL;
	g_autoptr(GPtrArray) snaps = NULL;

	client = get_client (plugin, error);
	if (client == NULL)
		return NULL;

	snaps = snapd_client_find_section_sync (client, flags, section, query, NULL, cancellable, error);
	if (snaps == NULL) {
		snapd_error_convert (error);
		return NULL;
	}

	store_snap_cache_update (plugin, snaps);

	return g_steal_pointer (&snaps);
}

static GsApp *
snap_to_app (GsPlugin *plugin, SnapdSnap *snap)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GStrv common_ids;
	g_autofree gchar *appstream_id = NULL;
	g_autofree gchar *unique_id = NULL;
	GsApp *cached_app;
	g_autoptr(GsApp) app = NULL;
	SnapdConfinement confinement;

	/* Get the AppStream ID from the snap, or generate a fallback one */
	common_ids = snapd_snap_get_common_ids (snap);
	if (g_strv_length (common_ids) == 1)
		appstream_id = g_strdup (common_ids[0]);
	else
		appstream_id = g_strdup_printf ("io.snapcraft.%s-%s", snapd_snap_get_name (snap), snapd_snap_get_id (snap));

	switch (snapd_snap_get_snap_type (snap)) {
	case SNAPD_SNAP_TYPE_APP:
		unique_id = g_strdup_printf ("system/snap/*/desktop/%s/*", appstream_id);
		break;
	case SNAPD_SNAP_TYPE_KERNEL:
	case SNAPD_SNAP_TYPE_GADGET:
	case SNAPD_SNAP_TYPE_OS:
		unique_id = g_strdup_printf ("system/snap/*/runtime/%s/*", appstream_id);
		break;
        default:
	case SNAPD_SNAP_TYPE_UNKNOWN:
		unique_id = g_strdup_printf ("system/snap/*/*/%s/*", appstream_id);
		break;
	}

	cached_app = gs_plugin_cache_lookup (plugin, unique_id);
	if (cached_app == NULL) {
		app = gs_app_new (NULL);
		gs_app_set_from_unique_id (app, unique_id);
		gs_app_set_metadata (app, "snap::name", snapd_snap_get_name (snap));
		gs_plugin_cache_add (plugin, unique_id, app);
	}
	else
		app = g_object_ref (cached_app);

	gs_app_set_management_plugin (app, "snap");
	if (gs_app_get_kind (app) != AS_APP_KIND_DESKTOP)
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
	if (gs_plugin_check_distro_id (plugin, "ubuntu"))
		gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);

	confinement = snapd_snap_get_confinement (snap);
	if (confinement != SNAPD_CONFINEMENT_UNKNOWN) {
		GEnumClass *enum_class = g_type_class_ref (SNAPD_TYPE_CONFINEMENT);
		gs_app_set_metadata (app, "snap::confinement", g_enum_get_value (enum_class, confinement)->value_nick);
		g_type_class_unref (enum_class);
	}

	if (priv->system_confinement == SNAPD_SYSTEM_CONFINEMENT_STRICT && confinement == SNAPD_CONFINEMENT_STRICT)
		gs_app_add_kudo (app, GS_APP_KUDO_SANDBOXED);

	return g_steal_pointer (&app);
}

gboolean
gs_plugin_url_to_app (GsPlugin *plugin,
		      GsAppList *list,
		      const gchar *url,
		      GCancellable *cancellable,
		      GError **error)
{
	g_autofree gchar *scheme = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(GPtrArray) snaps = NULL;
	g_autoptr(GsApp) app = NULL;

	/* not us */
	scheme = gs_utils_get_url_scheme (url);
	if (g_strcmp0 (scheme, "snap") != 0)
		return TRUE;

	/* create app */
	path = gs_utils_get_url_path (url);
	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_MATCH_NAME, NULL, path, cancellable, NULL);
	if (snaps == NULL || snaps->len < 1)
		return TRUE;

	app = snap_to_app (plugin, g_ptr_array_index (snaps, 0));
	gs_app_list_add (list, app);

	return TRUE;
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_free (priv->store_name);
	g_clear_object (&priv->auth);
	g_clear_pointer (&priv->store_snaps, g_hash_table_unref);
	g_mutex_clear (&priv->store_snaps_lock);
}

static gboolean
is_banner_image (const gchar *filename)
{
	/* Check if this screenshot was uploaded as "banner.png" or "banner.jpg".
	 * The server optionally adds a 7 character suffix onto it if it would collide with
	 * an existing name, e.g. "banner_MgEy4MI.png"
	 * See https://forum.snapcraft.io/t/improve-method-for-setting-featured-snap-banner-image-in-store/
	 */
	return g_regex_match_simple ("^banner(?:_[a-zA-Z0-9]{7})?\\.(?:png|jpg)$", filename, 0, 0);
}

static gboolean
is_banner_icon_image (const gchar *filename)
{
	/* Check if this screenshot was uploaded as "banner-icon.png" or "banner-icon.jpg".
	 * The server optionally adds a 7 character suffix onto it if it would collide with
	 * an existing name, e.g. "banner-icon_Ugn6pmj.png"
	 * See https://forum.snapcraft.io/t/improve-method-for-setting-featured-snap-banner-image-in-store/
	 */
	return g_regex_match_simple ("^banner-icon(?:_[a-zA-Z0-9]{7})?\\.(?:png|jpg)$", filename, 0, 0);
}

gboolean
gs_plugin_add_featured (GsPlugin *plugin,
		        GsAppList *list,
		        GCancellable *cancellable,
		        GError **error)
{
	g_autoptr(GPtrArray) snaps = NULL;
	SnapdSnap *snap;
	g_autoptr(GsApp) app = NULL;
	GPtrArray *screenshots;
	guint i;
	const gchar *banner_url = NULL, *icon_url = NULL;
	g_autoptr(GString) background_css = NULL;
	g_autofree gchar *css = NULL;

	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_NONE, "featured", NULL, cancellable, error);

	if (snaps == NULL)
		return FALSE;

	if (snaps->len == 0)
		return TRUE;

	/* use first snap as the featured app */
	snap = snaps->pdata[0];
	app = snap_to_app (plugin, snap);

	/* if has a screenshot called 'banner.png' or 'banner-icon.png' then use them for the banner */
	screenshots = snapd_snap_get_screenshots (snap);
	for (i = 0; i < screenshots->len; i++) {
		SnapdScreenshot *screenshot = screenshots->pdata[i];
		const gchar *url;
		g_autofree gchar *filename = NULL;

		url = snapd_screenshot_get_url (screenshot);
		filename = g_path_get_basename (url);
		if (is_banner_image (filename))
			banner_url = url;
		else if (is_banner_icon_image (filename))
			icon_url = url;
	}

	background_css = g_string_new ("");
	if (icon_url != NULL)
		g_string_append_printf (background_css,
					"url('%s') left center / auto 100%% no-repeat, ",
					icon_url);
	else
		g_string_append_printf (background_css,
					"url('%s') left center / auto 100%% no-repeat, ",
					snapd_snap_get_icon (snap));
	if (banner_url != NULL)
		g_string_append_printf (background_css,
					"url('%s') center / cover no-repeat;",
					banner_url);
	else
		g_string_append_printf (background_css, "#FFFFFF;");
	css = g_strdup_printf ("border-color: #000000;\n"
			       "text-shadow: 0 1px 1px rgba(255,255,255,0.5);\n"
			       "color: #000000;\n"
			       "outline-offset: 0;\n"
			       "outline-color: alpha(#ffffff, 0.75);\n"
			       "outline-style: dashed;\n"
			       "outline-offset: 2px;\n"
			       "background: %s;",
			       background_css->str);
	gs_app_set_metadata (app, "GnomeSoftware::FeatureTile-css", css);

	gs_app_list_add (list, app);

	return TRUE;
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(GPtrArray) snaps = NULL;
	guint i;

	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_NONE, "featured", NULL, cancellable, error);
	if (snaps == NULL)
		return FALSE;

	/* skip first snap - it is used as the featured app */
	for (i = 1; i < snaps->len; i++) {
		g_autoptr(GsApp) app = snap_to_app (plugin, g_ptr_array_index (snaps, i));
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsCategory *c;
	g_autoptr(GString) id = NULL;
	const gchar *sections = NULL;

	id = g_string_new ("");
	for (c = category; c != NULL; c = gs_category_get_parent (c)) {
		if (c != category)
			g_string_prepend (id, "/");
		g_string_prepend (id, gs_category_get_id (c));
	}

	if (strcmp (id->str, "games/featured") == 0)
		sections = "games";
	else if (strcmp (id->str, "audio-video/featured") == 0)
		sections = "music;video";
	else if (strcmp (id->str, "graphics/featured") == 0)
		sections = "graphics";
	else if (strcmp (id->str, "communication/featured") == 0)
		sections = "social-networking";
	else if (strcmp (id->str, "productivity/featured") == 0)
		sections = "productivity;finance";
	else if (strcmp (id->str, "developer-tools/featured") == 0)
		sections = "developers";
	else if (strcmp (id->str, "utilities/featured") == 0)
		sections = "utilities";

	if (sections != NULL) {
		g_auto(GStrv) tokens = NULL;
		int i;

		tokens = g_strsplit (sections, ";", -1);
		for (i = 0; tokens[i] != NULL; i++) {
			g_autoptr(GPtrArray) snaps = NULL;
			guint j;

			snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_NONE, tokens[i], NULL, cancellable, error);
			if (snaps == NULL)
				return FALSE;
			for (j = 0; j < snaps->len; j++) {
				g_autoptr(GsApp) app = snap_to_app (plugin, g_ptr_array_index (snaps, j));
				gs_app_list_add (list, app);
			}
		}
	}
	return TRUE;
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

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	snaps = snapd_client_get_snaps_sync (client, SNAPD_GET_SNAPS_FLAGS_NONE, NULL, cancellable, error);
	if (snaps == NULL) {
		snapd_error_convert (error);
		return FALSE;
	}

	for (i = 0; i < snaps->len; i++) {
		SnapdSnap *snap = g_ptr_array_index (snaps, i);
		g_autoptr(GsApp) app = NULL;

		app = snap_to_app (plugin, snap);
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
	g_autofree gchar *query = NULL;
	g_autoptr(GPtrArray) snaps = NULL;
	guint i;

	query = g_strjoinv (" ", values);
	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_NONE, NULL, query, cancellable, error);
	if (snaps == NULL)
		return FALSE;

	for (i = 0; i < snaps->len; i++) {
		g_autoptr(GsApp) app = snap_to_app (plugin, g_ptr_array_index (snaps, i));
		gs_app_list_add (list, app);
	}

	return TRUE;
}

static SnapdSnap *
get_store_snap (GsPlugin *plugin, const gchar *name, GCancellable *cancellable, GError **error)
{
	SnapdSnap *snap = NULL;
	g_autoptr(GPtrArray) snaps = NULL;

	/* use cached version if available */
	snap = store_snap_cache_lookup (plugin, name);
	if (snap != NULL)
		return g_object_ref (snap);

	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_MATCH_NAME, NULL, name, cancellable, error);
	if (snaps == NULL || snaps->len < 1)
		return NULL;

	return g_object_ref (g_ptr_array_index (snaps, 0));
}

static gboolean
load_snap_icon (GsApp *app, SnapdClient *client, SnapdSnap *snap, GCancellable *cancellable)
{
	const gchar *icon_url;
	g_autoptr(SnapdIcon) icon = NULL;
	g_autoptr(GInputStream) input_stream = NULL;
	g_autoptr(GdkPixbuf) pixbuf = NULL;
	g_autoptr(GError) error = NULL;

	icon_url = snapd_snap_get_icon (snap);
	if (icon_url == NULL || strcmp (icon_url, "") == 0)
		return FALSE;

	icon = snapd_client_get_icon_sync (client, gs_app_get_metadata_item (app, "snap::name"), cancellable, &error);
	if (icon == NULL) {
		g_warning ("Failed to load snap icon: %s", error->message);
		return FALSE;
	}

	input_stream = g_memory_input_stream_new_from_bytes (snapd_icon_get_data (icon));
	pixbuf = gdk_pixbuf_new_from_stream_at_scale (input_stream, 64, 64, TRUE, cancellable, &error);
	if (pixbuf == NULL) {
		g_warning ("Failed to decode snap icon %s: %s", icon_url, error->message);
		return FALSE;
	}
	gs_app_set_pixbuf (app, pixbuf);

	return TRUE;
}

static gboolean
app_name_matches_snap_name (SnapdSnap *snap, SnapdApp *app)
{
	return g_strcmp0 (snapd_snap_get_name (snap), snapd_app_get_name (app)) == 0;
}

static SnapdApp *
get_primary_app (SnapdSnap *snap)
{
	GPtrArray *apps;
	guint i;
	SnapdApp *primary_app = NULL;

	/* Pick the "main" app from the snap.  In order of
	 * preference, we want to pick:
	 *
	 *   1. the main app, provided it has a desktop file
	 *   2. the first app with a desktop file
	 *   3. the main app
	 *   4. the first app
	 *
	 * The "main app" is one whose name matches the snap name.
	 */
	apps = snapd_snap_get_apps (snap);
	for (i = 0; i < apps->len; i++) {
		SnapdApp *app = apps->pdata[i];

		if (primary_app == NULL ||
                    (snapd_app_get_desktop_file (primary_app) == NULL && snapd_app_get_desktop_file (app) != NULL) ||
                    (!app_name_matches_snap_name (snap, primary_app) && app_name_matches_snap_name (snap, app)))
			primary_app = app;
	}

	return primary_app;
}

static gboolean
load_desktop_icon (GsApp *app, SnapdSnap *snap)
{
	GPtrArray *apps;
	guint i;

	apps = snapd_snap_get_apps (snap);
	for (i = 0; i < apps->len; i++) {
		SnapdApp *snap_app = apps->pdata[i];
		const gchar *desktop_file_path;
		g_autoptr(GKeyFile) desktop_file = NULL;
		g_autoptr(GError) error = NULL;
		g_autofree gchar *icon_value = NULL;
		g_autoptr(AsIcon) icon = NULL;

		desktop_file_path = snapd_app_get_desktop_file (snap_app);
		if (desktop_file_path == NULL)
			continue;

		desktop_file = g_key_file_new ();
		if (!g_key_file_load_from_file (desktop_file, desktop_file_path, G_KEY_FILE_NONE, &error)) {
			g_warning ("Failed to load desktop file %s: %s", desktop_file_path, error->message);
			continue;
		}

		icon_value = g_key_file_get_string (desktop_file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, &error);
		if (icon_value == NULL) {
			g_warning ("Failed to get desktop file icon %s: %s", desktop_file_path, error->message);
			continue;
		}

		icon = as_icon_new ();
		if (g_str_has_prefix (icon_value, "/")) {
			as_icon_set_kind (icon, AS_ICON_KIND_LOCAL);
			as_icon_set_filename (icon, icon_value);
		} else {
			as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
			as_icon_set_name (icon, icon_value);
		}
		gs_app_add_icon (app, icon);

		return TRUE;
	}

	return FALSE;
}

static gboolean
load_store_icon (GsApp *app, SnapdSnap *snap)
{
	const gchar *icon_url;

	icon_url = snapd_snap_get_icon (snap);
	if (icon_url == NULL)
		return FALSE;

	if (g_str_has_prefix (icon_url, "http://") || g_str_has_prefix (icon_url, "https://")) {
		g_autoptr(AsIcon) icon = as_icon_new ();
		as_icon_set_kind (icon, AS_ICON_KIND_REMOTE);
		as_icon_set_url (icon, icon_url);
		gs_app_add_icon (app, icon);
		return TRUE;
	}

	return FALSE;
}

static gboolean
load_icon (GsPlugin *plugin, SnapdClient *client, GsApp *app, const gchar *id, SnapdSnap *local_snap, SnapdSnap *store_snap, GCancellable *cancellable)
{
	if (local_snap != NULL) {
		if (load_snap_icon (app, client, local_snap, cancellable))
			return TRUE;
		if (load_desktop_icon (app, local_snap))
			return TRUE;
	}

	if (store_snap == NULL)
		store_snap = get_store_snap (plugin, gs_app_get_metadata_item (app, "snap::name"), cancellable, NULL);
	if (store_snap != NULL)
		return load_store_icon (app, store_snap);

	return FALSE;
}

static gchar *
gs_plugin_snap_get_description_safe (SnapdSnap *snap)
{
	GString *str = g_string_new (snapd_snap_get_description (snap));
	as_utils_string_replace (str, "\r", "");
	as_utils_string_replace (str, "  ", " ");
	return g_string_free (str, FALSE);
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(SnapdClient) client = NULL;
	const gchar *name;
	g_autoptr(SnapdSnap) local_snap = NULL;
	g_autoptr(SnapdSnap) store_snap = NULL;
	SnapdSnap *snap;
	const gchar *developer_name;
	g_autofree gchar *description = NULL;

	/* not us */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	/* get information from local snaps and store */
	local_snap = snapd_client_get_snap_sync (client, gs_app_get_metadata_item (app, "snap::name"), cancellable, NULL);
	if (local_snap == NULL || (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS) != 0)
		store_snap = get_store_snap (plugin, gs_app_get_metadata_item (app, "snap::name"), cancellable, NULL);
	if (local_snap == NULL && store_snap == NULL)
		return TRUE;

	if (local_snap != NULL)
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	else
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

	/* use store information for basic metadata over local information */
	snap = store_snap != NULL ? store_snap : local_snap;
	name = snapd_snap_get_title (snap);
	if (name == NULL || g_strcmp0 (name, "") == 0)
		name = snapd_snap_get_name (snap);
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, name);
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, snapd_snap_get_summary (snap));
	description = gs_plugin_snap_get_description_safe (snap);
	if (description != NULL)
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, description);
	gs_app_set_license (app, GS_APP_QUALITY_NORMAL, snapd_snap_get_license (snap));
	developer_name = snapd_snap_get_publisher_display_name (snap);
	if (developer_name == NULL)
		developer_name = snapd_snap_get_publisher_username (snap);
	gs_app_set_developer_name (app, developer_name);

	snap = local_snap != NULL ? local_snap : store_snap;
	gs_app_set_version (app, snapd_snap_get_version (snap));

	/* add information specific to installed snaps */
	if (local_snap != NULL) {
		SnapdApp *snap_app;
		GDateTime *install_date;

		install_date = snapd_snap_get_install_date (local_snap);
		gs_app_set_size_installed (app, snapd_snap_get_installed_size (local_snap));
		gs_app_set_install_date (app, install_date != NULL ? g_date_time_to_unix (install_date) : GS_APP_INSTALL_DATE_UNKNOWN);

		snap_app = get_primary_app (local_snap);
		if (snap_app != NULL) {
			gs_app_set_metadata (app, "snap::launch-name", snapd_app_get_name (snap_app));
			gs_app_set_metadata (app, "snap::launch-desktop", snapd_app_get_desktop_file (snap_app));
		} else {
			gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
		}
	}

	/* add information specific to store snaps */
	if (store_snap != NULL) {
		gs_app_set_origin (app, priv->store_name);
		gs_app_set_size_download (app, snapd_snap_get_download_size (store_snap));

		if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS && gs_app_get_screenshots (app)->len == 0) {
			GPtrArray *screenshots;
			guint i;

			screenshots = snapd_snap_get_screenshots (store_snap);
			for (i = 0; i < screenshots->len; i++) {
				SnapdScreenshot *screenshot = screenshots->pdata[i];
				const gchar *url;
				g_autofree gchar *filename = NULL;
				g_autoptr(AsScreenshot) ss = NULL;
				g_autoptr(AsImage) image = NULL;

				/* skip screenshots used for banner when app is featured */
				url = snapd_screenshot_get_url (screenshot);
				filename = g_path_get_basename (url);
				if (is_banner_image (filename) || is_banner_icon_image (filename))
					continue;

				ss = as_screenshot_new ();
				as_screenshot_set_kind (ss, AS_SCREENSHOT_KIND_NORMAL);
				image = as_image_new ();
				as_image_set_url (image, snapd_screenshot_get_url (screenshot));
				as_image_set_kind (image, AS_IMAGE_KIND_SOURCE);
				as_image_set_width (image, snapd_screenshot_get_width (screenshot));
				as_image_set_height (image, snapd_screenshot_get_height (screenshot));
				as_screenshot_add_image (ss, image);
				gs_app_add_screenshot (app, ss);
			}
		}
	}

	/* load icon if requested */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON && gs_app_get_pixbuf (app) == NULL)
		load_icon (plugin, client, app, gs_app_get_metadata_item (app, "snap::name"), local_snap, store_snap, cancellable);

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
	SnapdInstallFlags flags = SNAPD_INSTALL_FLAGS_NONE;

	/* We can only install apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (g_strcmp0 (gs_app_get_metadata_item (app, "snap::confinement"), "classic") == 0)
		flags |= SNAPD_INSTALL_FLAGS_CLASSIC;
	if (!snapd_client_install2_sync (client, flags, gs_app_get_metadata_item (app, "snap::name"), NULL, NULL, progress_cb, app, cancellable, error)) {
		gs_app_set_state_recover (app);
		snapd_error_convert (error);
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

	client = get_client (plugin, &error);
	if (client == NULL)
		return FALSE;

	if (!snapd_client_get_interfaces_sync (client, &plugs, NULL, cancellable, &error)) {
		g_warning ("Failed to check interfaces: %s", error->message);
		return FALSE;
	}

	for (i = 0; i < plugs->len; i++) {
		SnapdPlug *plug = plugs->pdata[i];
		const gchar *interface;

		// Only looks at the plugs for this snap
		if (g_strcmp0 (snapd_plug_get_snap (plug), gs_app_get_metadata_item (app, "snap::name")) != 0)
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
	const gchar *launch_desktop;
	g_autoptr(GAppInfo) info = NULL;

	/* We can only launch apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	launch_name = gs_app_get_metadata_item (app, "snap::launch-name");
	launch_desktop = gs_app_get_metadata_item (app, "snap::launch-desktop");
	if (!launch_name)
		return TRUE;

	if (launch_desktop) {
		info = (GAppInfo *)g_desktop_app_info_new_from_filename (launch_desktop);
	} else {
		g_autofree gchar *commandline = NULL;
		GAppInfoCreateFlags flags = G_APP_INFO_CREATE_NONE;

		if (g_strcmp0 (launch_name, gs_app_get_metadata_item (app, "snap::name")) == 0)
			commandline = g_strdup_printf ("snap run %s", launch_name);
		else
			commandline = g_strdup_printf ("snap run %s.%s", gs_app_get_metadata_item (app, "snap::name"), launch_name);

		if (!is_graphical (plugin, app, cancellable))
			flags |= G_APP_INFO_CREATE_NEEDS_TERMINAL;
		info = g_app_info_create_from_commandline (commandline, NULL, flags, error);
	}

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

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	if (!snapd_client_remove_sync (client, gs_app_get_metadata_item (app, "snap::name"), progress_cb, app, cancellable, error)) {
		gs_app_set_state_recover (app);
		snapd_error_convert (error);
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
	g_autoptr(SnapdClient) client = NULL;
	g_autoptr(SnapdUserInformation) user_information = NULL;
	g_autoptr(GVariant) macaroon_variant = NULL;
	g_autofree gchar *serialized_macaroon = NULL;

	if (auth != priv->auth)
		return TRUE;

	g_clear_object (&priv->auth_data);

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	user_information = snapd_client_login2_sync (client, gs_auth_get_username (auth), gs_auth_get_password (auth), gs_auth_get_pin (auth), NULL, error);
	if (user_information == NULL) {
		snapd_error_convert (error);
		return FALSE;
	}

	priv->auth_data = g_object_ref (snapd_user_information_get_auth_data (user_information));

	macaroon_variant = g_variant_new ("(s^as)",
					  snapd_auth_data_get_macaroon (priv->auth_data),
					  snapd_auth_data_get_discharges (priv->auth_data));
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
gs_plugin_auth_logout (GsPlugin *plugin, GsAuth *auth,
		       GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (auth != priv->auth)
		return TRUE;

	/* clear */
	if (!gs_auth_store_clear (auth,
				  GS_AUTH_STORE_FLAG_USERNAME |
				  GS_AUTH_STORE_FLAG_METADATA,
				  cancellable, error))
		return FALSE;

	g_clear_object (&priv->auth_data);
	gs_auth_set_flags (priv->auth, 0);
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
