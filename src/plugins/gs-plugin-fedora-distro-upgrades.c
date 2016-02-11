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

#include <errno.h>
#include <json-glib/json-glib.h>

#include <gs-plugin.h>
#include <gs-os-release.h>
#include <gs-utils.h>

#define FEDORA_PKGDB_COLLECTIONS_API_URI "https://admin.fedoraproject.org/pkgdb/api/collections/"

#ifndef JsonParser_autoptr
G_DEFINE_AUTOPTR_CLEANUP_FUNC(JsonParser, g_object_unref)
#endif

struct GsPluginPrivate {
	GPtrArray	*distros;
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
	if (plugin->priv->distros != NULL)
		g_ptr_array_unref (plugin->priv->distros);
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

		errno = 0;
		version = g_ascii_strtoull (version_str, &endptr, 10);
		if (errno != 0 || endptr == version_str || version > G_MAXUINT)
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
	g_autofree gchar *os_name = NULL;
	g_autofree gchar *os_version_str = NULL;
	g_autoptr(GPtrArray) distros = NULL;
	g_autoptr(SoupMessage) msg = NULL;
	gchar *endptr = NULL;
	guint i;
	guint status_code;
	guint64 os_version;

	os_name = gs_os_release_get_name (error);
	if (os_name == NULL)
		return FALSE;
	os_version_str = gs_os_release_get_version_id (error);
	if (os_version_str == NULL)
		return FALSE;

	errno = 0;
	os_version = g_ascii_strtoull (os_version_str, &endptr, 10);
	if (errno != 0 || endptr == os_version_str || os_version > G_MAXUINT) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed parse VERSION_ID: %s", os_version_str);
		return FALSE;
	}

	/* create the GET data */
	msg = soup_message_new (SOUP_METHOD_GET, FEDORA_PKGDB_COLLECTIONS_API_URI);

	/* set sync request */
	status_code = soup_session_send_message (plugin->soup_session, msg);
	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to download distro upgrade data: %s",
			     soup_status_get_phrase (status_code));
		return FALSE;
	}

	g_clear_pointer (&plugin->priv->distros, g_ptr_array_unref);
	plugin->priv->distros = parse_pkgdb_collections_data (msg->response_body->data,
	                                                      msg->response_body->length,
	                                                      error);
	for (i = 0; i < plugin->priv->distros->len; i++) {
		DistroInfo *distro_info = g_ptr_array_index (plugin->priv->distros, i);
		g_autofree gchar *app_id = NULL;
		g_autofree gchar *app_version = NULL;
		g_autoptr(GsApp) app = NULL;

		/* only interested in upgrades to the same distro */
		if (g_strcmp0 (distro_info->name, os_name) != 0)
			continue;
		/* only interested in newer versions */
		if (distro_info->version <= os_version)
			continue;
		/* only interested in non-devel distros */
		if (distro_info->status == DISTRO_STATUS_DEVEL)
			continue;

		app_id = g_strdup_printf ("org.fedoraproject.release-%d.upgrade", distro_info->version);
		app_version = g_strdup_printf ("%d", distro_info->version);

		/* create */
		app = gs_app_new (app_id);
		gs_app_set_kind (app, GS_APP_KIND_DISTRO_UPGRADE);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST, distro_info->name);
		gs_app_set_version (app, app_version);
		gs_plugin_add_app (list, app);
	}

	return TRUE;
}
