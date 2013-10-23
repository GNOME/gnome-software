/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <gs-plugin.h>

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "packagekit-offline";
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 9.5f;
}

#define PK_OFFLINE_UPDATE_RESULTS_GROUP		"PackageKit Offline Update Results"
#define PK_OFFLINE_UPDATE_RESULTS_FILENAME	"/var/lib/PackageKit/offline-update-competed"

/**
 * gs_plugin_add_updates_historical:
 */
gboolean
gs_plugin_add_updates_historical (GsPlugin *plugin,
				  GList **list,
				  GCancellable *cancellable,
				  GError **error)
{
	gboolean ret = TRUE;
	gboolean success;
	gchar **package_ids = NULL;
	gchar *packages = NULL;
	gchar **split;
	GKeyFile *key_file = NULL;
	GsApp *app;
	guint i;

	/* was any offline update attempted */
	if (!g_file_test (PK_OFFLINE_UPDATE_RESULTS_FILENAME, G_FILE_TEST_EXISTS))
		goto out;

	/* open the file */
	key_file = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file,
					 PK_OFFLINE_UPDATE_RESULTS_FILENAME,
					 G_KEY_FILE_NONE,
					 error);
	if (!ret)
		goto out;

	/* only return results if successful */
	success = g_key_file_get_boolean (key_file,
					  PK_OFFLINE_UPDATE_RESULTS_GROUP,
					  "Success",
					  NULL);
	if (!success)
		goto out;

	/* get list of package-ids */
	packages = g_key_file_get_string (key_file,
					  PK_OFFLINE_UPDATE_RESULTS_GROUP,
					  "Packages",
					  NULL);
	if (packages == NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "No 'Packages' in %s",
			     PK_OFFLINE_UPDATE_RESULTS_FILENAME);
		goto out;
	}
	package_ids = g_strsplit (packages, ",", -1);
	for (i = 0; package_ids[i] != NULL; i++) {
		app = gs_app_new (NULL);
		split = g_strsplit (package_ids[i], ";", 4);
		gs_app_set_source_default (app, split[0]);
		gs_app_set_update_version (app, split[1]);
		gs_app_set_management_plugin (app, "PackageKit");
		gs_app_add_source_id (app, package_ids[i]);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
		gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
		gs_plugin_add_app (list, app);
		g_strfreev (split);
	}
out:
	g_free (packages);
	g_strfreev (package_ids);
	if (key_file != NULL)
		g_key_file_free (key_file);
	return ret;
}
