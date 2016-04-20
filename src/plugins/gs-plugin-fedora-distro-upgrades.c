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

#include <gs-plugin.h>
#include <gs-os-release.h>
#include <gs-utils.h>

#define FEDORA_PKGDB_COLLECTIONS_API_URI "https://admin.fedoraproject.org/pkgdb/api/collections/"

struct GsPluginPrivate {
	gchar		*cachefn;
	GFileMonitor	*cachefn_monitor;
	gchar		*os_name;
	guint64		 os_version;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "fedora-distro-upgrades";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	/* check that we are running on Fedora */
	if (!gs_plugin_check_distro_id (plugin, "fedora")) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as we're not Fedora", plugin->name);
		return;
	}
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	if (plugin->priv->cachefn_monitor != NULL)
		g_object_unref (plugin->priv->cachefn_monitor);
	g_free (plugin->priv->os_name);
	g_free (plugin->priv->cachefn);
}

/**
 * gs_plugin_fedora_distro_upgrades_changed_cb:
 */
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
	if (plugin->flags & GS_PLUGIN_FLAGS_RUNNING_SELF) {
		g_debug ("no notify as plugin %s active", plugin->name);
		return;
	}
	if (plugin->flags & GS_PLUGIN_FLAGS_RECENT) {
		g_debug ("no notify as plugin %s recently active", plugin->name);
		return;
	}
	g_debug ("cache file changed, so reloading upgrades list");
	gs_plugin_updates_changed (plugin);
}

/**
 * gs_plugin_setup:
 */
gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	gchar *endptr = NULL;
	g_autofree gchar *cachedir = NULL;
	g_autofree gchar *verstr = NULL;
	g_autoptr(GFile) file = NULL;

	/* create the cachedir */
	cachedir = gs_utils_get_cachedir ("upgrades", error);
	if (cachedir == NULL)
		return FALSE;
	plugin->priv->cachefn = g_build_filename (cachedir, "fedora.json", NULL);

	/* watch this in case it is changed by the user */
	file = g_file_new_for_path (plugin->priv->cachefn);
	plugin->priv->cachefn_monitor = g_file_monitor (file,
							G_FILE_MONITOR_NONE,
							cancellable,
							error);
	if (plugin->priv->cachefn_monitor == NULL)
		return FALSE;
	g_signal_connect (plugin->priv->cachefn_monitor, "changed",
			  G_CALLBACK (gs_plugin_fedora_distro_upgrades_changed_cb), plugin);

	/* read os-release for the current versions */
	plugin->priv->os_name = gs_os_release_get_name (error);
	if (plugin->priv->os_name == NULL)
		return FALSE;
	verstr = gs_os_release_get_version_id (error);
	if (verstr == NULL)
		return FALSE;

	/* parse the version */
	plugin->priv->os_version = g_ascii_strtoull (verstr, &endptr, 10);
	if (endptr == verstr || plugin->priv->os_version > G_MAXUINT) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed parse VERSION_ID: %s", verstr);
		return FALSE;
	}

	/* success */
	return TRUE;
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
	/* only for update metadata, no stored state other than setup() */
	if ((flags & GS_PLUGIN_REFRESH_FLAGS_METADATA) == 0)
		return TRUE;

	/* check cache age */
	if (cache_age > 0) {
		guint tmp;
		g_autoptr(GFile) file = g_file_new_for_path (plugin->priv->cachefn);
		tmp = gs_utils_get_file_age (file);
		if (tmp < cache_age) {
			g_debug ("%s is only %i seconds old",
				 plugin->priv->cachefn, tmp);
			return TRUE;
		}
	}

	/* download new file */
	return gs_plugin_download_file (plugin, NULL,
					FEDORA_PKGDB_COLLECTIONS_API_URI,
					plugin->priv->cachefn,
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

/**
 * gs_plugin_add_distro_upgrades:
 */
gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
			       GList **list,
			       GCancellable *cancellable,
			       GError **error)
{
	gsize len;
	guint i;
	g_autofree gchar *data = NULL;
	g_autoptr(GPtrArray) distros = NULL;

	/* get cached file */
	if (!g_file_get_contents (plugin->priv->cachefn, &data, &len, error))
		return FALSE;

	/* parse data */
	distros = parse_pkgdb_collections_data (data, len, error);
	if (distros == NULL)
		return FALSE;
	for (i = 0; i < distros->len; i++) {
		DistroInfo *distro_info = g_ptr_array_index (distros, i);
		g_autofree gchar *app_id = NULL;
		g_autofree gchar *app_version = NULL;
		g_autofree gchar *url = NULL;
		g_autofree gchar *css = NULL;
		g_autoptr(GsApp) app = NULL;

		/* only interested in upgrades to the same distro */
		if (g_strcmp0 (distro_info->name, plugin->priv->os_name) != 0)
			continue;

		/* only interested in newer versions */
		if (distro_info->version <= plugin->priv->os_version)
			continue;

		/* only interested in non-devel distros */
		if (distro_info->status == DISTRO_STATUS_DEVEL)
			continue;

		app_id = g_strdup_printf ("org.fedoraproject.release-%d.upgrade",
					  distro_info->version);
		app_version = g_strdup_printf ("%d", distro_info->version);

		/* create */
		app = gs_app_new (app_id);
		gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST, distro_info->name);
		gs_app_set_version (app, app_version);
		gs_app_set_management_plugin (app, "packagekit");

		/* just use the release notes */
		url = g_strdup_printf ("https://docs.fedoraproject.org/en-US/"
				       "Fedora/%i/html/Release_Notes/",
				       distro_info->version);
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, url);

		/* use a fancy background */
		css = g_strdup_printf ("background: url('/usr/share/backgrounds/f%i/default/standard/f%i.png');"
				       "background-position: center;"
				       "background-size: cover;",
				       distro_info->version,
				       distro_info->version);
		gs_app_set_metadata (app, "GnomeSoftware::UpgradeBanner-css", css);

		gs_app_list_add (list, app);
	}

	return TRUE;
}
