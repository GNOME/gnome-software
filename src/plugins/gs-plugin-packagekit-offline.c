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

#include <gnome-software.h>

/*
 * SECTION:
 * This adds historical updates to the application history.
 *
 * Note: when this is cleared by one user is is unavailable for all
 * other users.
 */

#define PK_OFFLINE_UPDATE_RESULTS_GROUP		"PackageKit Offline Update Results"
#define PK_OFFLINE_UPDATE_RESULTS_FILENAME	"/var/lib/PackageKit/offline-update-competed"

static gboolean
gs_plugin_packagekit_convert_error (GError **error,
				    PkErrorEnum error_enum,
				    const gchar *details)
{
	switch (error_enum) {
	case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
	case PK_ERROR_ENUM_NO_CACHE:
	case PK_ERROR_ENUM_NO_NETWORK:
	case PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY:
	case PK_ERROR_ENUM_CANNOT_FETCH_SOURCES:
	case PK_ERROR_ENUM_UNFINISHED_TRANSACTION:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NO_NETWORK,
				     details);
		break;
	case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
	case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
	case PK_ERROR_ENUM_GPG_FAILURE:
	case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
	case PK_ERROR_ENUM_PACKAGE_CORRUPT:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NO_SECURITY,
				     details);
		break;
	case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_CANCELLED,
				     details);
		break;
	case PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE:
	case PK_ERROR_ENUM_UPDATE_NOT_FOUND:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     details);
		break;
	case PK_ERROR_ENUM_NO_SPACE_ON_DEVICE:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NO_SPACE,
				     details);
		break;
	default:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     details);
		break;
	}
	return FALSE;
}

gboolean
gs_plugin_add_updates_historical (GsPlugin *plugin,
				  GsAppList *list,
				  GCancellable *cancellable,
				  GError **error)
{
	gboolean ret;
	guint64 mtime;
	guint i;
	g_auto(GStrv) package_ids = NULL;
	g_autofree gchar *packages = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(GKeyFile) key_file = NULL;

	/* was any offline update attempted */
	if (!g_file_test (PK_OFFLINE_UPDATE_RESULTS_FILENAME, G_FILE_TEST_EXISTS))
		return TRUE;

	/* get the mtime of the results */
	file = g_file_new_for_path (PK_OFFLINE_UPDATE_RESULTS_FILENAME);
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_TIME_MODIFIED,
				  G_FILE_QUERY_INFO_NONE,
				  cancellable,
				  error);
	if (info == NULL)
		return FALSE;
	mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);

	/* open the file */
	key_file = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file,
					 PK_OFFLINE_UPDATE_RESULTS_FILENAME,
					 G_KEY_FILE_NONE,
					 error);
	if (!ret)
		return FALSE;

	/* only return results if successful */
	ret = g_key_file_get_boolean (key_file,
				      PK_OFFLINE_UPDATE_RESULTS_GROUP,
				      "Success",
				      NULL);
	if (!ret) {
		g_autofree gchar *code = NULL;
		g_autofree gchar *details = NULL;
		code = g_key_file_get_string (key_file,
					      PK_OFFLINE_UPDATE_RESULTS_GROUP,
					      "ErrorCode",
					      error);
		if (code == NULL)
			return FALSE;
		details = g_key_file_get_string (key_file,
						 PK_OFFLINE_UPDATE_RESULTS_GROUP,
						 "ErrorDetails",
						 error);
		if (details == NULL)
			return FALSE;
		return gs_plugin_packagekit_convert_error (error,
							   pk_error_enum_from_string (code),
							   details);
	}

	/* get list of package-ids */
	packages = g_key_file_get_string (key_file,
					  PK_OFFLINE_UPDATE_RESULTS_GROUP,
					  "Packages",
					  NULL);
	if (packages == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "No 'Packages' in %s",
			     PK_OFFLINE_UPDATE_RESULTS_FILENAME);
		return FALSE;
	}
	package_ids = g_strsplit (packages, ",", -1);
	for (i = 0; package_ids[i] != NULL; i++) {
		g_autoptr(GsApp) app = NULL;
		g_auto(GStrv) split = NULL;
		app = gs_app_new (NULL);
		split = g_strsplit (package_ids[i], ";", 4);
		gs_app_add_source (app, split[0]);
		gs_app_set_update_version (app, split[1]);
		gs_app_set_management_plugin (app, "packagekit");
		gs_app_add_source_id (app, package_ids[i]);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
		gs_app_set_kind (app, AS_APP_KIND_GENERIC);
		gs_app_set_install_date (app, mtime);
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_list_add (list, app);
	}
	return TRUE;
}
