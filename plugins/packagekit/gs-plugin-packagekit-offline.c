/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>

#include <gnome-software.h>

#include "packagekit-common.h"

/*
 * SECTION:
 * This adds historical updates to the application history.
 *
 * Note: when this is cleared by one user is is unavailable for all
 * other users.
 */

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
	guint64 mtime;
	guint i;
	g_autoptr(GPtrArray) package_array = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(PkResults) results = NULL;
	PkExitEnum exit_code;

	/* get the results */
	results = pk_offline_get_results (&error_local);
	if (results == NULL) {
		/* was any offline update attempted */
		if (g_error_matches (error_local,
		                     PK_OFFLINE_ERROR,
		                     PK_OFFLINE_ERROR_NO_DATA)) {
			return TRUE;
		}

		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_INVALID_FORMAT,
		             "Failed to get offline update results: %s",
		             error_local->message);
		return FALSE;
	}

	/* get the mtime of the results */
	mtime = pk_offline_get_results_mtime (error);
	if (mtime == 0)
		return FALSE;

	/* only return results if successful */
	exit_code = pk_results_get_exit_code (results);
	if (exit_code != PK_EXIT_ENUM_SUCCESS) {
		g_autoptr(PkError) error_code = NULL;

		error_code = pk_results_get_error_code (results);
		if (error_code == NULL) {
			g_set_error (error,
			             GS_PLUGIN_ERROR,
			             GS_PLUGIN_ERROR_FAILED,
			             "Offline update failed without error_code set");
			return FALSE;
		}

		return gs_plugin_packagekit_convert_error (error,
		                                           pk_error_get_code (error_code),
		                                           pk_error_get_details (error_code));
	}

	/* distro upgrade? */
	if (pk_results_get_role (results) == PK_ROLE_ENUM_UPGRADE_SYSTEM) {
		g_autoptr(GsApp) app = NULL;

		app = gs_app_new (NULL);
		gs_app_set_from_unique_id (app, "*/*/*/*/system/*");
		gs_app_set_management_plugin (app, "packagekit");
		gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_set_install_date (app, mtime);
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_list_add (list, app);

		return TRUE;
	}

	/* get list of package-ids */
	package_array = pk_results_get_package_array (results);
	for (i = 0; i < package_array->len; i++) {
		PkPackage *pkg = g_ptr_array_index (package_array, i);
		const gchar *package_id;
		g_autofree gchar *tmp = NULL;
		g_autoptr(GsApp) app = NULL;
		g_auto(GStrv) split = NULL;

		app = gs_app_new (NULL);
		package_id = pk_package_get_id (pkg);
		split = g_strsplit (package_id, ";", 4);
		gs_plugin_packagekit_set_packaging_format (plugin, app);
		gs_app_add_source (app, split[0]);
		gs_app_set_update_version (app, split[1]);
		gs_app_set_management_plugin (app, "packagekit");
		gs_app_add_source_id (app, package_id);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
		gs_app_set_kind (app, AS_APP_KIND_GENERIC);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_set_install_date (app, mtime);
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_list_add (list, app);
	}
	return TRUE;
}
