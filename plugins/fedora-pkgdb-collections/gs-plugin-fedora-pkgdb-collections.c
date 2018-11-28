/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2018 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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
#include <json-glib/json-glib.h>
#include <gnome-software.h>

#define FEDORA_PKGDB_COLLECTIONS_API_URI "https://admin.fedoraproject.org/pkgdb/api/collections/"

struct GsPluginData {
	gchar		*cachefn;
	GFileMonitor	*cachefn_monitor;
	gchar		*os_name;
	guint64		 os_version;
	GsApp		*cached_origin;
	GSettings	*settings;
	gboolean	 is_valid;
	GPtrArray	*distros;
	GMutex		 mutex;
};

typedef enum {
	PKGDB_ITEM_STATUS_ACTIVE,
	PKGDB_ITEM_STATUS_DEVEL,
	PKGDB_ITEM_STATUS_EOL,
	PKGDB_ITEM_STATUS_LAST
} PkgdbItemStatus;

typedef struct {
	gchar			*name;
	PkgdbItemStatus		 status;
	guint			 version;
} PkgdbItem;

static void
_pkgdb_item_free (PkgdbItem *item)
{
	g_free (item->name);
	g_slice_free (PkgdbItem, item);
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	g_mutex_init (&priv->mutex);

	/* check that we are running on Fedora */
	if (!gs_plugin_check_distro_id (plugin, "fedora")) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as we're not Fedora", gs_plugin_get_name (plugin));
		return;
	}
	priv->distros = g_ptr_array_new_with_free_func ((GDestroyNotify) _pkgdb_item_free);
	priv->settings = g_settings_new ("org.gnome.software");

	/* require the GnomeSoftware::CpeName metadata */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "os-release");

	/* old name */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "fedora-distro-upgrades");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->cachefn_monitor != NULL)
		g_object_unref (priv->cachefn_monitor);
	if (priv->cached_origin != NULL)
		g_object_unref (priv->cached_origin);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	if (priv->distros != NULL)
		g_ptr_array_unref (priv->distros);
	g_free (priv->os_name);
	g_free (priv->cachefn);
	g_mutex_clear (&priv->mutex);
}

static void
_file_changed_cb (GFileMonitor *monitor,
		  GFile *file, GFile *other_file,
		  GFileMonitorEvent event_type,
		  gpointer user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* only reload the update list if the plugin is NOT running itself
	 * and the time since it ran is greater than 5 seconds (inotify FTW) */
	if (gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_RUNNING_SELF)) {
		g_debug ("no notify as plugin %s active", gs_plugin_get_name (plugin));
		return;
	}
	if (gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_RECENT)) {
		g_debug ("no notify as plugin %s recently active", gs_plugin_get_name (plugin));
		return;
	}

	g_debug ("cache file changed, so reloading upgrades list");
	gs_plugin_updates_changed (plugin);
	priv->is_valid = FALSE;
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *verstr = NULL;
	gchar *endptr = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	/* get the file to cache */
	priv->cachefn = gs_utils_get_cache_filename ("fedora-pkgdb-collections",
						     "fedora.json",
						     GS_UTILS_CACHE_FLAG_WRITEABLE,
						     error);
	if (priv->cachefn == NULL)
		return FALSE;

	/* watch this in case it is changed by the user */
	file = g_file_new_for_path (priv->cachefn);
	priv->cachefn_monitor = g_file_monitor (file,
						G_FILE_MONITOR_NONE,
						cancellable,
						error);
	if (priv->cachefn_monitor == NULL)
		return FALSE;
	g_signal_connect (priv->cachefn_monitor, "changed",
			  G_CALLBACK (_file_changed_cb), plugin);

	/* read os-release for the current versions */
	os_release = gs_os_release_new (error);
	if (os_release == NULL)
		return FALSE;
	priv->os_name = g_strdup (gs_os_release_get_name (os_release));
	if (priv->os_name == NULL)
		return FALSE;
	verstr = gs_os_release_get_version_id (os_release);
	if (verstr == NULL)
		return FALSE;

	/* parse the version */
	priv->os_version = g_ascii_strtoull (verstr, &endptr, 10);
	if (endptr == verstr || priv->os_version > G_MAXUINT) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "Failed parse VERSION_ID: %s", verstr);
		return FALSE;
	}

	/* add source */
	priv->cached_origin = gs_app_new (gs_plugin_get_name (plugin));
	gs_app_set_kind (priv->cached_origin, AS_APP_KIND_SOURCE);
	gs_app_set_origin_hostname (priv->cached_origin,
				    FEDORA_PKGDB_COLLECTIONS_API_URI);

	/* add the source to the plugin cache which allows us to match the
	 * unique ID to a GsApp when creating an event */
	gs_plugin_cache_add (plugin,
			     gs_app_get_unique_id (priv->cached_origin),
			     priv->cached_origin);

	/* success */
	return TRUE;
}

static gboolean
_refresh_cache (GsPlugin *plugin,
		guint cache_age,
		GCancellable *cancellable,
		GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

	/* check cache age */
	if (cache_age > 0) {
		g_autoptr(GFile) file = g_file_new_for_path (priv->cachefn);
		guint tmp = gs_utils_get_file_age (file);
		if (tmp < cache_age) {
			g_debug ("%s is only %u seconds old",
				 priv->cachefn, tmp);
			return TRUE;
		}
	}

	/* download new file */
	gs_app_set_summary_missing (app_dl,
				    /* TRANSLATORS: status text when downloading */
				    _("Downloading upgrade information…"));
	if (!gs_plugin_download_file (plugin, app_dl,
				      FEDORA_PKGDB_COLLECTIONS_API_URI,
				      priv->cachefn,
				      cancellable,
				      error)) {
		gs_utils_error_add_origin_id (error, priv->cached_origin);
		return FALSE;
	}

	/* success */
	priv->is_valid = FALSE;
	return TRUE;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
	return _refresh_cache (plugin, cache_age, cancellable, error);
}

static gchar *
_get_upgrade_css_background (guint version)
{
	g_autofree gchar *filename1 = NULL;
	g_autofree gchar *filename2 = NULL;

	filename1 = g_strdup_printf ("/usr/share/backgrounds/f%u/default/standard/f%u.png", version, version);
	if (g_file_test (filename1, G_FILE_TEST_EXISTS))
		return g_strdup_printf ("url('%s')", filename1);

	filename2 = g_strdup_printf ("/usr/share/gnome-software/backgrounds/f%u.png", version);
	if (g_file_test (filename2, G_FILE_TEST_EXISTS))
		return g_strdup_printf ("url('%s')", filename2);

	/* fall back to solid colour */
	return g_strdup_printf ("#151E65");
}

static gint
_sort_items_cb (gconstpointer a, gconstpointer b)
{
	PkgdbItem *item_a = *((PkgdbItem **) a);
	PkgdbItem *item_b = *((PkgdbItem **) b);

	if (item_a->version > item_b->version)
		return 1;
	if (item_a->version < item_b->version)
		return -1;
	return 0;
}

static GsApp *
_create_upgrade_from_info (GsPlugin *plugin, PkgdbItem *item)
{
	GsApp *app;
	g_autofree gchar *app_id = NULL;
	g_autofree gchar *app_version = NULL;
	g_autofree gchar *background = NULL;
	g_autofree gchar *cache_key = NULL;
	g_autofree gchar *css = NULL;
	g_autofree gchar *url = NULL;
	g_autoptr(AsIcon) ic = NULL;

	/* search in the cache */
	cache_key = g_strdup_printf ("release-%u", item->version);
	app = gs_plugin_cache_lookup (plugin, cache_key);
	if (app != NULL)
		return app;

	app_id = g_strdup_printf ("org.fedoraproject.Fedora-%u", item->version);
	app_version = g_strdup_printf ("%u", item->version);

	/* icon from disk */
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_LOCAL);
	as_icon_set_filename (ic, "/usr/share/pixmaps/fedora-logo-sprite.png");

	/* create */
	app = gs_app_new (app_id);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, item->name);
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
			    /* TRANSLATORS: this is a title for Fedora distro upgrades */
			    _("Upgrade your Fedora system to the latest features and improvements."));
	gs_app_set_version (app, app_version);
	gs_app_set_size_installed (app, 1024 * 1024 * 1024); /* estimate */
	gs_app_set_size_download (app, 256 * 1024 * 1024); /* estimate */
	gs_app_set_license (app, GS_APP_QUALITY_LOWEST, "LicenseRef-free");
	gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);
	gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
	gs_app_add_icon (app, ic);

	/* show a Fedora magazine article for the release */
	url = g_strdup_printf ("https://fedoramagazine.org/whats-new-fedora-%u-workstation",
			       item->version);
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, url);

	/* use a fancy background */
	background = _get_upgrade_css_background (item->version);
	css = g_strdup_printf ("background: %s;"
			       "background-position: center;"
			       "background-size: cover;",
			       background);
	gs_app_set_metadata (app, "GnomeSoftware::UpgradeBanner-css", css);

	/* save in the cache */
	gs_plugin_cache_add (plugin, cache_key, app);

	/* success */
	return app;
}

static gboolean
_is_valid_upgrade (GsPlugin *plugin, PkgdbItem *item)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* only interested in upgrades to the same distro */
	if (g_strcmp0 (item->name, priv->os_name) != 0)
		return FALSE;

	/* only interested in newer versions, but not more than N+2 */
	if (item->version <= priv->os_version ||
	    item->version > priv->os_version + 2)
		return FALSE;

	/* only interested in non-devel distros */
	if (!g_settings_get_boolean (priv->settings, "show-upgrade-prerelease")) {
		if (item->status == PKGDB_ITEM_STATUS_DEVEL)
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
_ensure_cache (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	JsonArray *collections;
	JsonObject *root;
	gsize len;
	g_autofree gchar *data = NULL;
	g_autoptr(JsonParser) parser = NULL;

	/* already done */
	if (priv->is_valid)
		return TRUE;

	/* just ensure there is any data, no matter how old */
	if (!_refresh_cache (plugin, G_MAXUINT, cancellable, error))
		return FALSE;

	/* get cached file */
	if (!g_file_get_contents (priv->cachefn, &data, &len, error)) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	/* parse data */
	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, data, len, error))
		return FALSE;

	root = json_node_get_object (json_parser_get_root (parser));
	if (root == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "no root object");
		return FALSE;
	}

	collections = json_object_get_array_member (root, "collections");
	if (collections == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "no collections object");
		return FALSE;
	}

	g_ptr_array_set_size (priv->distros, 0);
	for (guint i = 0; i < json_array_get_length (collections); i++) {
		PkgdbItem *item;
		JsonObject *collection;
		PkgdbItemStatus status;
		const gchar *name;
		const gchar *status_str;
		const gchar *version_str;
		gchar *endptr = NULL;
		guint64 version;

		collection = json_array_get_object_element (collections, i);
		if (collection == NULL)
			continue;

		name = json_object_get_string_member (collection, "name");
		if (name == NULL)
			continue;

		status_str = json_object_get_string_member (collection, "status");
		if (status_str == NULL)
			continue;

		if (g_strcmp0 (status_str, "Active") == 0)
			status = PKGDB_ITEM_STATUS_ACTIVE;
		else if (g_strcmp0 (status_str, "Under Development") == 0)
			status = PKGDB_ITEM_STATUS_DEVEL;
		else if (g_strcmp0 (status_str, "EOL") == 0)
			status = PKGDB_ITEM_STATUS_EOL;
		else
			continue;

		version_str = json_object_get_string_member (collection, "version");
		if (version_str == NULL)
			continue;

		version = g_ascii_strtoull (version_str, &endptr, 10);
		if (endptr == version_str || version > G_MAXUINT)
			continue;

		/* add item */
		item = g_slice_new0 (PkgdbItem);
		item->name = g_strdup (name);
		item->status = status;
		item->version = (guint) version;
		g_ptr_array_add (priv->distros, item);
	}

	/* ensure in correct order */
	g_ptr_array_sort (priv->distros, _sort_items_cb);

	/* success */
	priv->is_valid = TRUE;
	return TRUE;
}

static PkgdbItem *
_get_item_by_cpe_name (GsPlugin *plugin, const gchar *cpe_name)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	guint64 version;
	g_auto(GStrv) split = NULL;

	/* split up 'cpe:/o:fedoraproject:fedora:26' to sections */
	split = g_strsplit (cpe_name, ":", -1);
	if (g_strv_length (split) < 5) {
		g_warning ("CPE invalid format: %s", cpe_name);
		return NULL;
	}

	/* find the correct collection */
	version = g_ascii_strtoull (split[4], NULL, 10);
	if (version == 0) {
		g_warning ("failed to parse CPE version: %s", split[4]);
		return NULL;
	}
	for (guint i = 0; i < priv->distros->len; i++) {
		PkgdbItem *item = g_ptr_array_index (priv->distros, i);
		if (g_ascii_strcasecmp (item->name, split[3]) == 0 &&
		    item->version == version)
			return item;
	}
	return NULL;
}

gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
			       GsAppList *list,
			       GCancellable *cancellable,
			       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	/* ensure valid data is loaded */
	if (!_ensure_cache (plugin, cancellable, error))
		return FALSE;

	/* are any distros upgradable */
	for (guint i = 0; i < priv->distros->len; i++) {
		PkgdbItem *item = g_ptr_array_index (priv->distros, i);
		if (_is_valid_upgrade (plugin, item)) {
			g_autoptr(GsApp) app = NULL;
			app = _create_upgrade_from_info (plugin, item);
			gs_app_list_add (list, app);
		}
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	PkgdbItem *item;
	const gchar *cpe_name;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	/* not for us */
	if (gs_app_get_kind (app) != AS_APP_KIND_OS_UPGRADE)
		return TRUE;

	/* not enough metadata */
	cpe_name = gs_app_get_metadata_item (app, "GnomeSoftware::CpeName");
	if (cpe_name == NULL)
		return TRUE;

	/* ensure valid data is loaded */
	if (!_ensure_cache (plugin, cancellable, error))
		return FALSE;

	/* find item */
	item = _get_item_by_cpe_name (plugin, cpe_name);
	if (item == NULL) {
		g_warning ("did not find %s", cpe_name);
		return TRUE;
	}

	/* fix the state */
	switch (item->status) {
	case PKGDB_ITEM_STATUS_ACTIVE:
	case PKGDB_ITEM_STATUS_DEVEL:
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
		break;
	case PKGDB_ITEM_STATUS_EOL:
		gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);
		break;
	default:
		break;
	}

	return TRUE;
}
