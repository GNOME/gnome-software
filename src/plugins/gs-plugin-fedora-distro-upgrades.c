/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
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

#define FEDORA_PKGDB_COLLECTIONS_API_URI "https://admin.fedoraproject.org/pkgdb/api/collections/"

struct GsPluginData {
	gchar		*cachefn;
	GFileMonitor	*cachefn_monitor;
	gchar		*os_name;
	guint64		 os_version;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	/* check that we are running on Fedora */
	if (!gs_plugin_check_distro_id (plugin, "fedora")) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as we're not Fedora", gs_plugin_get_name (plugin));
		return;
	}
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->cachefn_monitor != NULL)
		g_object_unref (priv->cachefn_monitor);
	g_free (priv->os_name);
	g_free (priv->cachefn);
}

static void
gs_plugin_fedora_distro_upgrades_changed_cb (GFileMonitor *monitor,
					     GFile *file,
					     GFile *other_file,
					     GFileMonitorEvent event_type,
					     gpointer user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);

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
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *verstr = NULL;
	gchar *endptr = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;

	/* get the file to cache */
	priv->cachefn = gs_utils_get_cache_filename ("upgrades",
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
			  G_CALLBACK (gs_plugin_fedora_distro_upgrades_changed_cb), plugin);

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
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed parse VERSION_ID: %s", verstr);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
gs_plugin_fedora_distro_upgrades_refresh (GsPlugin *plugin,
					  guint cache_age,
					  GCancellable *cancellable,
					  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	/* check cache age */
	if (cache_age > 0) {
		guint tmp;
		g_autoptr(GFile) file = g_file_new_for_path (priv->cachefn);
		tmp = gs_utils_get_file_age (file);
		if (tmp < cache_age) {
			g_debug ("%s is only %u seconds old",
				 priv->cachefn, tmp);
			return TRUE;
		}
	}

	/* download new file */
	return gs_plugin_download_file (plugin, NULL,
					FEDORA_PKGDB_COLLECTIONS_API_URI,
					priv->cachefn,
					cancellable,
					error);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	/* only for update metadata, no stored state other than setup() */
	if ((flags & GS_PLUGIN_REFRESH_FLAGS_METADATA) == 0)
		return TRUE;
	return gs_plugin_fedora_distro_upgrades_refresh (plugin,
							 cache_age,
							 cancellable,
							 error);
}

typedef enum {
	DISTRO_STATUS_ACTIVE,
	DISTRO_STATUS_DEVEL,
	DISTRO_STATUS_EOL,
	DISTRO_STATUS_LAST
} DistroStatus;

typedef struct {
	gchar		*name;
	DistroStatus	 status;
	guint		 version;
} DistroInfo;

static void
distro_info_free (DistroInfo *distro_info)
{
	g_free (distro_info->name);
	g_slice_free (DistroInfo, distro_info);
}

static GPtrArray *
parse_pkgdb_collections_data (const gchar *data,
                              gssize length,
                              GError **error)
{
	g_autoptr(JsonParser) parser = NULL;
	GPtrArray *distros = NULL;
	JsonArray *collections;
	JsonObject *root;
	gboolean ret;
	guint i;

	parser = json_parser_new ();

	ret = json_parser_load_from_data (parser, data, length, error);
	if (!ret)
		return NULL;

	root = json_node_get_object (json_parser_get_root (parser));
	if (root == NULL) {
		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_FAILED,
		             "no root object");
		return NULL;
	}

	collections = json_object_get_array_member (root, "collections");
	if (collections == NULL) {
		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_FAILED,
		             "no collections object");
		return NULL;
	}

	distros = g_ptr_array_new_with_free_func ((GDestroyNotify) distro_info_free);
	for (i = 0; i < json_array_get_length (collections); i++) {
		DistroInfo *distro_info;
		JsonObject *item;
		DistroStatus status;
		const gchar *name;
		const gchar *status_str;
		const gchar *version_str;
		gchar *endptr = NULL;
		guint64 version;

		item = json_array_get_object_element (collections, i);
		if (item == NULL)
			continue;

		name = json_object_get_string_member (item, "name");
		if (name == NULL)
			continue;

		status_str = json_object_get_string_member (item, "status");
		if (status_str == NULL)
			continue;

		if (g_strcmp0 (status_str, "Active") == 0)
			status = DISTRO_STATUS_ACTIVE;
		else if (g_strcmp0 (status_str, "Under Development") == 0)
			status = DISTRO_STATUS_DEVEL;
		else if (g_strcmp0 (status_str, "EOL") == 0)
			status = DISTRO_STATUS_EOL;
		else
			continue;

		version_str = json_object_get_string_member (item, "version");
		if (version_str == NULL)
			continue;

		version = g_ascii_strtoull (version_str, &endptr, 10);
		if (endptr == version_str || version > G_MAXUINT)
			continue;

		distro_info = g_slice_new0 (DistroInfo);
		distro_info->name = g_strdup (name);
		distro_info->status = status;
		distro_info->version = (guint) version;

		g_ptr_array_add (distros, distro_info);
	}

	return distros;
}

static gchar *
get_upgrade_css_background (guint version)
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

gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
			       GsAppList *list,
			       GCancellable *cancellable,
			       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	gsize len;
	guint i;
	g_autofree gchar *data = NULL;
	g_autoptr(GPtrArray) distros = NULL;
	g_autoptr(GSettings) settings = NULL;

	/* just ensure there is any data, no matter how old */
	if (!gs_plugin_fedora_distro_upgrades_refresh (plugin,
						       G_MAXUINT,
						       cancellable,
						       error))
		return FALSE;

	/* get cached file */
	if (!g_file_get_contents (priv->cachefn, &data, &len, error))
		return FALSE;

	/* parse data */
	settings = g_settings_new ("org.gnome.software");
	distros = parse_pkgdb_collections_data (data, (gssize) len, error);
	if (distros == NULL)
		return FALSE;
	for (i = 0; i < distros->len; i++) {
		DistroInfo *distro_info = g_ptr_array_index (distros, i);
		g_autofree gchar *app_id = NULL;
		g_autofree gchar *app_version = NULL;
		g_autofree gchar *background = NULL;
		g_autofree gchar *cache_key = NULL;
		g_autofree gchar *url = NULL;
		g_autofree gchar *css = NULL;
		g_autoptr(GsApp) app = NULL;
		g_autoptr(AsIcon) ic = NULL;

		/* only interested in upgrades to the same distro */
		if (g_strcmp0 (distro_info->name, priv->os_name) != 0)
			continue;

		/* only interested in newer versions */
		if (distro_info->version <= priv->os_version)
			continue;

		/* only interested in non-devel distros */
		if (!g_settings_get_boolean (settings, "show-upgrade-prerelease")) {
			if (distro_info->status != DISTRO_STATUS_ACTIVE)
				continue;
		}

		/* search in the cache */
		cache_key = g_strdup_printf ("release-%u", distro_info->version);
		app = gs_plugin_cache_lookup (plugin, cache_key);
		if (app != NULL) {
			gs_app_list_add (list, app);
			continue;
		}

		app_id = g_strdup_printf ("org.fedoraproject.release-%u.upgrade",
					  distro_info->version);
		app_version = g_strdup_printf ("%u", distro_info->version);

		/* icon from disk */
		ic = as_icon_new ();
		as_icon_set_kind (ic, AS_ICON_KIND_LOCAL);
		as_icon_set_filename (ic, "/usr/share/pixmaps/fedora-logo-sprite.png");

		/* create */
		app = gs_app_new (app_id);
		gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST, distro_info->name);
		gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
				    "A major upgrade, with new features "
				    "and added polish.");
		gs_app_set_description (app, GS_APP_QUALITY_LOWEST,
					"Fedora Workstation is a polished, "
					"easy to use operating system for "
					"laptop and desktop computers, with a "
					"complete set of tools for developers "
					"and makers of all kinds.");
		gs_app_set_version (app, app_version);
		gs_app_set_size_installed (app, 1024 * 1024 * 1024); /* estimate */
		gs_app_set_size_download (app, 256 * 1024 * 1024); /* estimate */
		gs_app_set_license (app, GS_APP_QUALITY_LOWEST, "LicenseRef-free");
		gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);
		gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
		gs_app_set_origin_ui (app, distro_info->name);
		gs_app_add_icon (app, ic);
		gs_app_set_management_plugin (app, "packagekit");

		/* show a Fedora magazine article for the release */
		url = g_strdup_printf ("https://fedoramagazine.org/whats-new-fedora-%u-workstation",
		                       distro_info->version);
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, url);

		/* use a fancy background */
		background = get_upgrade_css_background (distro_info->version);
		css = g_strdup_printf ("background: %s;"
				       "background-position: center;"
				       "background-size: cover;",
				       background);
		gs_app_set_metadata (app, "GnomeSoftware::UpgradeBanner-css", css);

		gs_app_list_add (list, app);

		/* save in the cache */
		gs_plugin_cache_add (plugin, cache_key, app);
	}

	return TRUE;
}
