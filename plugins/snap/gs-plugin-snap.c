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

typedef struct {
	SnapdSnap *snap;
	gboolean full_details;
} CacheEntry;

static CacheEntry *
cache_entry_new (SnapdSnap *snap, gboolean full_details)
{
	CacheEntry *entry = g_slice_new (CacheEntry);
	entry->snap = g_object_ref (snap);
	entry->full_details = full_details;
	return entry;
}

static void
cache_entry_free (CacheEntry *entry)
{
	g_object_unref (entry->snap);
	g_slice_free (CacheEntry, entry);
}

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
						   g_free, (GDestroyNotify) cache_entry_free);

	priv->auth = gs_auth_new ("snapd");
	gs_auth_set_provider_name (priv->auth, "Snap Store");
	gs_auth_set_provider_schema (priv->auth, "com.ubuntu.SnapStore.GnomeSoftware");
	gs_plugin_add_auth (plugin, priv->auth);

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "desktop-categories");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "ubuntu-reviews");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_BETTER_THAN, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "icons");

	/* Override hardcoded popular apps */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "hardcoded-popular");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "hardcoded-featured");

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
		case SNAPD_ERROR_PAYMENT_NOT_SETUP:
			error->code = GS_PLUGIN_ERROR_PURCHASE_NOT_SETUP;
			g_free (error->message);
			error->message = g_strdup ("do online using @https://my.ubuntu.com/payment/edit");
			break;
		case SNAPD_ERROR_PAYMENT_DECLINED:
			error->code = GS_PLUGIN_ERROR_PURCHASE_DECLINED;
			break;
		case SNAPD_ERROR_AUTH_CANCELLED:
			error->code = GS_PLUGIN_ERROR_CANCELLED;
			break;
		case SNAPD_ERROR_CONNECTION_FAILED:
		case SNAPD_ERROR_WRITE_FAILED:
		case SNAPD_ERROR_READ_FAILED:
		case SNAPD_ERROR_BAD_REQUEST:
		case SNAPD_ERROR_BAD_RESPONSE:
		case SNAPD_ERROR_PERMISSION_DENIED:
		case SNAPD_ERROR_FAILED:
		case SNAPD_ERROR_TERMS_NOT_ACCEPTED:
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
store_snap_cache_lookup (GsPlugin *plugin, const gchar *name, gboolean need_details)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	CacheEntry *entry;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->store_snaps_lock);

	entry = g_hash_table_lookup (priv->store_snaps, name);
	if (entry == NULL)
		return NULL;

	if (need_details && !entry->full_details)
		return NULL;

	return g_object_ref (entry->snap);
}

static void
store_snap_cache_update (GsPlugin *plugin, GPtrArray *snaps, gboolean full_details)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->store_snaps_lock);
	guint i;

	for (i = 0; i < snaps->len; i++) {
		SnapdSnap *snap = snaps->pdata[i];
		g_hash_table_insert (priv->store_snaps, g_strdup (snapd_snap_get_name (snap)), cache_entry_new (snap, full_details));
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

	store_snap_cache_update (plugin, snaps, flags & SNAPD_FIND_FLAGS_MATCH_NAME);

	return g_steal_pointer (&snaps);
}

static GsApp *
snap_to_app (GsPlugin *plugin, SnapdSnap *snap)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GStrv common_ids;
	g_autofree gchar *appstream_id = NULL;
	g_autofree gchar *unique_id = NULL;
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

	app = gs_plugin_cache_lookup (plugin, unique_id);
	if (app == NULL) {
		app = gs_app_new (NULL);
		gs_app_set_from_unique_id (app, unique_id);
		gs_app_set_metadata (app, "snap::name", snapd_snap_get_name (snap));
		gs_plugin_cache_add (plugin, unique_id, app);
	}

	gs_app_set_metadata (app, "snap::id", snapd_snap_get_id (snap));
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
	else
		gs_app_remove_kudo (app, GS_APP_KUDO_SANDBOXED);

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
	g_autofree gchar *channel_name = NULL;

	/* not us */
	scheme = gs_utils_get_url_scheme (url);
	if (g_strcmp0 (scheme, "snap") != 0)
		return TRUE;

	/* create app */
	path = gs_utils_get_url_path (url);
	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_SCOPE_WIDE | SNAPD_FIND_FLAGS_MATCH_NAME, NULL, path, cancellable, NULL);
	if (snaps == NULL || snaps->len < 1)
		return TRUE;

	app = snap_to_app (plugin, g_ptr_array_index (snaps, 0));
	channel_name = gs_utils_get_url_query_param (url, "channel");
	if (channel_name != NULL)
		gs_app_set_metadata (app, "snap::channel", channel_name);
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

static gboolean
remove_cb (GsApp *app, gpointer user_data)
{
	return FALSE;
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

	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_SCOPE_WIDE, "featured", NULL, cancellable, error);

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

	/* replace any other featured apps with our one */
	gs_app_list_filter (list, remove_cb, NULL);
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

	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_SCOPE_WIDE, "featured", NULL, cancellable, error);
	if (snaps == NULL)
		return FALSE;

	/* replace any other popular apps with our one */
	gs_app_list_filter (list, remove_cb, NULL);

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

			snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_SCOPE_WIDE, tokens[i], NULL, cancellable, error);
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
	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_SCOPE_WIDE, NULL, query, cancellable, error);
	if (snaps == NULL)
		return FALSE;

	for (i = 0; i < snaps->len; i++) {
		g_autoptr(GsApp) app = snap_to_app (plugin, g_ptr_array_index (snaps, i));
		gs_app_set_match_value (app, snaps->len - i);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

static SnapdSnap *
get_store_snap (GsPlugin *plugin, const gchar *name, gboolean need_details, GCancellable *cancellable, GError **error)
{
	SnapdSnap *snap = NULL;
	g_autoptr(GPtrArray) snaps = NULL;

	/* use cached version if available */
	snap = store_snap_cache_lookup (plugin, name, need_details);
	if (snap != NULL)
		return g_object_ref (snap);

	snaps = find_snaps (plugin, SNAPD_FIND_FLAGS_SCOPE_WIDE | SNAPD_FIND_FLAGS_MATCH_NAME, NULL, name, cancellable, error);
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
	g_autoptr(GdkPixbuf) pixbuf = NULL;
	g_autoptr(GError) error = NULL;

	if (local_snap != NULL) {
		if (load_snap_icon (app, client, local_snap, cancellable))
			return TRUE;
		if (load_desktop_icon (app, local_snap))
			return TRUE;
	}

	if (store_snap == NULL)
		store_snap = get_store_snap (plugin, gs_app_get_metadata_item (app, "snap::name"), FALSE, cancellable, NULL);
	if (store_snap != NULL) {
		if (load_store_icon (app, store_snap))
			return TRUE;
	}

	/* Default to built-in icon */
	pixbuf = gdk_pixbuf_new_from_resource_at_scale ("/org/gnome/Software/Snap/default-snap-icon.svg", 64, 64, TRUE, &error);
	if (pixbuf != NULL) {
		gs_app_set_pixbuf (app, pixbuf);
		return TRUE;
	}

	g_warning ("Failed to load built-in icon: %s", error->message);
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

static void
add_channel (GsApp *app, const gchar *name, const gchar *version)
{
	g_autoptr(GsChannel) c = NULL;

	c = gs_channel_new (name, version);
	gs_app_add_channel (app, c);
}

static int
compare_branch_names (gconstpointer a, gconstpointer b)
{
	SnapdChannel *channel_a = *((SnapdChannel **) a);
	SnapdChannel *channel_b = *((SnapdChannel **) b);
	return g_strcmp0 (snapd_channel_get_name (channel_a), snapd_channel_get_name (channel_b));
}

static void
refine_channels (GsApp *app, SnapdSnap *snap)
{
	gchar **tracks;
	guint i;

	/* already refined... */
	if (gs_app_get_channels (app)->len > 0)
		return;

	tracks = snapd_snap_get_tracks (snap);
	for (i = 0; tracks[i] != NULL; i++) {
		const gchar *track = tracks[i];
		const gchar *risks[] = {"stable", "candidate", "beta", "edge", NULL};
		const gchar *last_version = NULL;
		guint j;

		for (j = 0; risks[j] != NULL; j++) {
			const gchar *risk = risks[j];
			GPtrArray *channels;
			g_autofree gchar *name = NULL;
			const gchar *version = NULL;
			guint k;
			g_autoptr(GPtrArray) branches = NULL;

			channels = snapd_snap_get_channels (snap);

			if (strcmp (track, "latest") == 0)
				name = g_strdup (risk);
			else
				name = g_strdup_printf ("%s/%s", track, risk);
			for (k = 0; k < channels->len; k++) {
				SnapdChannel *channel = channels->pdata[k];
				if (strcmp (snapd_channel_get_name (channel), name) == 0) {
					version = snapd_channel_get_version (channel);
					break;
				}
			}
			if (version == NULL)
				version = last_version;
			add_channel (app, name, version);

			/* add any branches for this track/risk */
			branches = g_ptr_array_new ();
			for (k = 0; k < channels->len; k++) {
				SnapdChannel *c = channels->pdata[k];
				if (snapd_channel_get_branch (c) != NULL &&
				    g_strcmp0 (snapd_channel_get_track (c), track) == 0 &&
				    g_strcmp0 (snapd_channel_get_risk (c), risk) == 0)
					g_ptr_array_add (branches, c);
			}
			g_ptr_array_sort (branches, compare_branch_names);
			for (k = 0; k < branches->len; k++) {
				SnapdChannel *c = branches->pdata[k];
				add_channel (app, snapd_channel_get_name (c), snapd_channel_get_version (c));
			}

			last_version = version;
		}
	}
}

static gboolean
set_active_channel (GsApp *app, SnapdChannel *channel)
{
	GPtrArray *channels = gs_app_get_channels (app);
	guint i;

	for (i = 0; i < channels->len; i++) {
		GsChannel *c = g_ptr_array_index (channels, i);
		if (g_strcmp0 (gs_channel_get_name (c), snapd_channel_get_name (channel)) == 0) {
			gs_app_set_active_channel (app, c);
			return TRUE;
		}
	}

	return FALSE;
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
	const gchar *name, *tracking_channel = NULL, *store_version = NULL;
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
	/* Need to do full lookup when channel information required
	 * https://forum.snapcraft.io/t/channel-maps-list-is-empty-when-using-v1-snaps-search-as-opposed-to-using-v2-snaps-details */
	if (local_snap == NULL || (flags & (GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS | GS_PLUGIN_REFINE_FLAGS_REQUIRE_CHANNELS)) != 0)
		store_snap = get_store_snap (plugin, gs_app_get_metadata_item (app, "snap::name"), (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_CHANNELS) != 0, cancellable, NULL);
	if (local_snap == NULL && store_snap == NULL)
		return TRUE;

	/* get channel information */
	if (store_snap != NULL && flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_CHANNELS)
		refine_channels (app, store_snap);

	/* set channel being tracked */
	if (local_snap != NULL)
		tracking_channel = snapd_snap_get_tracking_channel (local_snap);
	else
		tracking_channel = gs_app_get_metadata_item (app, "snap::channel");
	if (store_snap != NULL && tracking_channel != NULL) {
		SnapdChannel *c = NULL;

		c = snapd_snap_match_channel (store_snap, tracking_channel);
		if (c != NULL)
			set_active_channel (app, c);
	}

	/* get latest upstream version */
	if (store_snap != NULL) {
		GsChannel *channel = gs_app_get_active_channel (app);
		if (channel != NULL)
			store_version = gs_channel_get_version (channel);
		else
			store_version = snapd_snap_get_version (store_snap);
	}

	gs_app_set_update_version (app, NULL);
	if (local_snap != NULL) {
		if (store_version != NULL && g_strcmp0 (store_version, snapd_snap_get_version (local_snap)) != 0) {
			gs_app_set_update_version (app, store_version);
			gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
		}
		else {
			// Workaround it not being valid to switch from updatable to installed (e.g. if you switch channels)
			if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE_LIVE)
				gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		}
	}
	else {
		if (store_snap != NULL && snapd_snap_get_status (store_snap) == SNAPD_SNAP_STATUS_PRICED) {
			if (g_getenv ("GNOME_SOFTWARE_SHOW_PAID") == NULL) {
				g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED, "Paid snaps not supported");
				return FALSE;
			}

			gs_app_set_state (app, AS_APP_STATE_PURCHASABLE);
		}
		else
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	}

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
	if (snapd_snap_get_publisher_validation (snap) == SNAPD_PUBLISHER_VALIDATION_VERIFIED)
		gs_app_add_quirk (app, AS_APP_QUIRK_DEVELOPER_VERIFIED);

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
		GPtrArray *prices;

		gs_app_set_origin (app, priv->store_name);

		prices = snapd_snap_get_prices (store_snap);
		if (prices->len > 0) {
			SnapdPrice *price = prices->pdata[0];
			gs_app_set_price (app, snapd_price_get_amount (price), snapd_price_get_currency (price));
		}

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

	if (gs_app_get_permissions (app)->len == 0) {
		g_autoptr(GPtrArray) plugs = NULL;
		g_autoptr(GPtrArray) slots = NULL;
		guint i;

		if (!snapd_client_get_interfaces_sync (client, &plugs, &slots, cancellable, error))
			return FALSE;
		for (i = 0; i < plugs->len; i++) {
			SnapdPlug *plug = plugs->pdata[i];
			const gchar *interface_name, *label;
			g_autoptr(GsPermission) permission = NULL;
			SnapdConnection *connection = NULL;
			guint j;

			/* skip if not relating to this snap */
			if (g_strcmp0 (snapd_plug_get_snap (plug), gs_app_get_metadata_item (app, "snap::name")) != 0)
				continue;

			interface_name = snapd_plug_get_interface (plug);
			if (strcmp (interface_name, "account-control") == 0) {
				label = _("Add user accounts and change passwords");
			} else if (strcmp (interface_name, "alsa") == 0) {
				label = _("Play and record sound");
			} else if (strcmp (interface_name, "avahi-observe") == 0) {
				label = _("Detect network devices using mDNS/DNS-SD (Bonjour/zeroconf)");
			} else if (strcmp (interface_name, "bluetooth-control") == 0) {
				label = _("Access bluetooth hardware directly");
			} else if (strcmp (interface_name, "bluez") == 0) {
				label = _("Use bluetooth devices");
			} else if (strcmp (interface_name, "camera") == 0) {
				label = _("Use your camera");
			} else if (strcmp (interface_name, "cups-control") == 0) {
				label = _("Print documents");
			} else if (strcmp (interface_name, "joystick") == 0) {
				label = _("Use any connected joystick");
			} else if (strcmp (interface_name, "docker") == 0) {
				label = _("Allow connecting to the Docker service");
			} else if (strcmp (interface_name, "firewall-control") == 0) {
				label = _("Configure network firewall");
			} else if (strcmp (interface_name, "fuse-support") == 0) {
				label = _("Setup and use privileged FUSE filesystems");
			} else if (strcmp (interface_name, "fwupd") == 0) {
				label = _("Update firmware on this device");
			} else if (strcmp (interface_name, "hardware-observe") == 0) {
				label = _("Access hardware information");
			} else if (strcmp (interface_name, "hardware-random-control") == 0) {
				label = _("Provide entropy to hardware random number generator");
			} else if (strcmp (interface_name, "hardware-random-observe") == 0) {
				label = _("Use hardware-generated random numbers");
			} else if (strcmp (interface_name, "home") == 0) {
				label = _("Access files in your home folder");
			} else if (strcmp (interface_name, "libvirt") == 0) {
				label = _("Access libvirt service");
			} else if (strcmp (interface_name, "locale-control") == 0) {
				label = _("Change system language and region settings");
			} else if (strcmp (interface_name, "location-control") == 0) {
				label = _("Change location settings and providers");
			} else if (strcmp (interface_name, "location-observe") == 0) {
				label = _("Access your location");
			} else if (strcmp (interface_name, "log-observe") == 0) {
				label = _("Read system and application logs");
			} else if (strcmp (interface_name, "lxd") == 0) {
				label = _("Access LXD service");
			//} else if (strcmp (interface_name, "media-hub") == 0) {
			//	label = _("access the media-hub service");
			} else if (strcmp (interface_name, "modem-manager") == 0) {
				label = _("Use and configure modems");
			} else if (strcmp (interface_name, "mount-observe") == 0) {
				label = _("Read system mount information and disk quotas");
			} else if (strcmp (interface_name, "mpris") == 0) {
				label = _("Control music and video players");
			} else if (strcmp (interface_name, "network-control") == 0) {
				label = _("Change low-level network settings");
			} else if (strcmp (interface_name, "network-manager") == 0) {
				label = _("Access the NetworkManager service to read and change network settings");
			} else if (strcmp (interface_name, "network-observe") == 0) {
				label = _("Read access to network settings");
			} else if (strcmp (interface_name, "network-setup-control") == 0) {
				label = _("Change network settings");
			} else if (strcmp (interface_name, "network-setup-observe") == 0) {
				label = _("Read network settings");
			} else if (strcmp (interface_name, "ofono") == 0) {
				label = _("Access the ofono service to read and change network settings for mobile telephony");
			} else if (strcmp (interface_name, "openvtswitch") == 0) {
				label = _("Control Open vSwitch hardware");
			} else if (strcmp (interface_name, "optical-drive") == 0) {
				label = _("Read from CD/DVD");
			} else if (strcmp (interface_name, "password-manager-service") == 0) {
				label = _("Read, add, change, or remove saved passwords");
			} else if (strcmp (interface_name, "ppp") == 0) {
				label = _("Access pppd and ppp devices for configuring Point-to-Point Protocol connections");
			} else if (strcmp (interface_name, "process-control") == 0) {
				label = _("Pause or end any process on the system");
			} else if (strcmp (interface_name, "pulseaudio") == 0) {
				label = _("Play and record sound");
			} else if (strcmp (interface_name, "raw-usb") == 0) {
				label = _("Access USB hardware directly");
			} else if (strcmp (interface_name, "removable-media") == 0) {
				label = _("Read/write files on removable storage devices");
			} else if (strcmp (interface_name, "screen-inhibit-control") == 0) {
				label = _("Prevent screen sleep/lock");
			} else if (strcmp (interface_name, "serial-port") == 0) {
				label = _("Access serial port hardware");
			} else if (strcmp (interface_name, "shutdown") == 0) {
				label = _("Restart or power off the device");
			} else if (strcmp (interface_name, "snapd-control") == 0) {
				label = _("Install, remove and configure software");
			} else if (strcmp (interface_name, "storage-framework-service") == 0) {
				label = _("Access Storage Framework service");
			} else if (strcmp (interface_name, "system-observe") == 0) {
				label = _("Read process and system information");
			} else if (strcmp (interface_name, "system-trace") == 0) {
				label = _("Monitor and control any running program");
			} else if (strcmp (interface_name, "time-control") == 0) {
				label = _("Change the date and time");
			} else if (strcmp (interface_name, "timeserver-control") == 0) {
				label = _("Change time server settings");
			} else if (strcmp (interface_name, "timezone-control") == 0) {
				label = _("Change the time zone");
			} else if (strcmp (interface_name, "udisks2") == 0) {
				label = _("Access the UDisks2 service for configuring disks and removable media");
			} else if (strcmp (interface_name, "unity8-calendar") == 0) {
				label = _("Read/change shared calendar events in Ubuntu Unity 8");
			} else if (strcmp (interface_name, "unity8-contacts") == 0) {
				label = _("Read/change shared contacts in Ubuntu Unity 8");
			} else if (strcmp (interface_name, "upower-observe") == 0) {
				label = _("Access energy usage data");
			} else {
				g_debug ("Skipping plug with interface %s", interface_name);
				continue;
			}
			/* map interfaces to known permissions */
			permission = gs_permission_new (label);
			gs_permission_add_metadata (permission, "snap::plug", snapd_plug_get_name (plug));

			if (snapd_plug_get_connections (plug)->len > 0)
				connection = g_ptr_array_index (snapd_plug_get_connections (plug), 0);
			for (j = 0; j < slots->len; j++) {
				SnapdSlot *slot = slots->pdata[j];
				g_autoptr(GsPermissionValue) value = NULL;
				g_autofree gchar *value_label = NULL;

				/* skip slots we can't connect to */
				if (g_strcmp0 (snapd_plug_get_interface (plug), snapd_slot_get_interface (slot)) != 0)
					continue;

				if (strcmp (snapd_slot_get_snap (slot), "core") == 0)
					value_label = g_strdup_printf (":%s", snapd_slot_get_name (slot));
				else
					value_label = g_strdup_printf ("%s:%s", snapd_slot_get_snap (slot), snapd_slot_get_name (slot));
				value = gs_permission_value_new (value_label);
				gs_permission_value_add_metadata (value, "snap::snap", snapd_slot_get_snap (slot));
				gs_permission_value_add_metadata (value, "snap::slot", snapd_slot_get_name (slot));
				gs_permission_add_value (permission, value);

				if (connection != NULL &&
				    g_strcmp0 (snapd_slot_get_snap (slot), snapd_connection_get_snap (connection)) == 0 &&
				    g_strcmp0 (snapd_slot_get_name (slot), snapd_connection_get_name (connection)) == 0)
					gs_permission_set_value (permission, value);
			}
			gs_app_add_permission (app, permission);
		}
	}

	return TRUE;
}

gboolean
gs_plugin_app_purchase (GsPlugin *plugin,
			GsApp *app,
			GsPrice *price,
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(SnapdClient) client = NULL;
	const gchar *id;

	/* We can only purchase apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	gs_app_set_state (app, AS_APP_STATE_PURCHASING);

	if (!snapd_client_check_buy_sync (client, cancellable, error)) {
		gs_app_set_state_recover (app);
		snapd_error_convert (error);
		return FALSE;
	}

	id = gs_app_get_metadata_item (app, "snap::id");
	if (!snapd_client_buy_sync (client, id, gs_price_get_amount (price), gs_price_get_currency (price), cancellable, error)) {
		gs_app_set_state_recover (app);
		snapd_error_convert (error);
		return FALSE;
	}

	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

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
	const gchar *channel = NULL;

	/* We can only install apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	if (gs_app_get_active_channel (app) != NULL)
		channel = gs_channel_get_name (gs_app_get_active_channel (app));

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (g_strcmp0 (gs_app_get_metadata_item (app, "snap::confinement"), "classic") == 0)
		flags |= SNAPD_INSTALL_FLAGS_CLASSIC;
	if (!snapd_client_install2_sync (client, flags, gs_app_get_metadata_item (app, "snap::name"), channel, NULL, progress_cb, app, cancellable, error)) {
		gs_app_set_state_recover (app);
		snapd_error_convert (error);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

gboolean
gs_plugin_update_app (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	g_autoptr(SnapdClient) client = NULL;

	/* We can only install apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!snapd_client_refresh_sync (client, gs_app_get_metadata_item (app, "snap::name"), NULL, progress_cb, app, cancellable, error)) {
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
gs_plugin_app_switch_channel (GsPlugin *plugin,
			      GsApp *app,
			      GsChannel *channel,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autoptr(SnapdClient) client = NULL;

	/* We can only modify apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	if (!snapd_client_switch_sync (client, gs_app_get_metadata_item (app, "snap::name"), gs_channel_get_name (channel), progress_cb, app, cancellable, error)) {
		snapd_error_convert (error);
		return FALSE;
	}

	return TRUE;
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
gs_plugin_app_set_permission (GsPlugin *plugin,
			      GsApp *app,
			      GsPermission *permission,
			      GsPermissionValue *value,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autoptr(SnapdClient) client = NULL;
	const gchar *plug_snap, *plug_name;

	/* We can set permissions on apps we know of */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "snap") != 0)
		return TRUE;

	client = get_client (plugin, error);
	if (client == NULL)
		return FALSE;

	plug_snap = gs_app_get_metadata_item (app, "snap::name");
	plug_name = gs_permission_get_metadata_item (permission, "snap::plug");

	if (value != NULL) {
		const gchar *slot_snap, *slot_name;

		slot_snap = gs_permission_value_get_metadata_item (value, "snap::snap");
		slot_name = gs_permission_value_get_metadata_item (value, "snap::slot");
		if (!snapd_client_connect_interface_sync (client,
							  plug_snap,
							  plug_name,
							  slot_snap,
							  slot_name,
							  NULL, NULL,
							  cancellable, error))
			return FALSE;
	} else {
		if (!snapd_client_disconnect_interface_sync (client,
							     plug_snap,
							     plug_name,
							     "",
							     "",
							     NULL, NULL,
							     cancellable, error))
			return FALSE;
	}

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
