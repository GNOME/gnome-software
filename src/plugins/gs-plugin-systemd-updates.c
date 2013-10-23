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

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include <gs-plugin.h>

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "systemd-updates";
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 10.0f;
}

#define PK_PREPARED_UPDATE_FN	"/var/lib/PackageKit/prepared-update"

/**
 * gs_plugin_add_updates:
 */
gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsApp *app;
	gboolean ret;
	gchar **package_ids = NULL;
	gchar **split;
	gchar *data = NULL;
	guint i;

	/* does the file exist ? */
	if (!g_file_test (PK_PREPARED_UPDATE_FN, G_FILE_TEST_EXISTS)) {
		ret = TRUE;
		goto out;
	}

	/* get the list of packages to update */
	ret = g_file_get_contents (PK_PREPARED_UPDATE_FN, &data, NULL, error);
	if (!ret)
		goto out;

	/* add them to the new array */
	package_ids = g_strsplit (data, "\n", -1);
	for (i = 0; package_ids[i] != NULL; i++) {
		app = gs_app_new (NULL);
		gs_app_set_management_plugin (app, "PackageKit");
		gs_app_set_metadata (app,
				     "PackageKit::package-id",
				     package_ids[i]);
		split = pk_package_id_split (package_ids[i]);
		gs_app_set_source_default (app, split[PK_PACKAGE_ID_NAME]);
		gs_app_set_update_version (app, split[PK_PACKAGE_ID_VERSION]);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
		gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
		gs_plugin_add_app (list, app);
		g_strfreev (split);
	}
out:
	g_free (data);
	g_strfreev (package_ids);
	return ret;
}
