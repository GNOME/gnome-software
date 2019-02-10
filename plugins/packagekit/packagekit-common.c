/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <packagekit-glib2/packagekit.h>

#include <gnome-software.h>

#include "packagekit-common.h"

GsPluginStatus
packagekit_status_enum_to_plugin_status (PkStatusEnum status)
{
	GsPluginStatus plugin_status = GS_PLUGIN_STATUS_UNKNOWN;

	switch (status) {
	case PK_STATUS_ENUM_SETUP:
	case PK_STATUS_ENUM_CANCEL:
	case PK_STATUS_ENUM_FINISHED:
	case PK_STATUS_ENUM_UNKNOWN:
		break;
	case PK_STATUS_ENUM_WAIT:
	case PK_STATUS_ENUM_WAITING_FOR_LOCK:
	case PK_STATUS_ENUM_WAITING_FOR_AUTH:
		plugin_status = GS_PLUGIN_STATUS_WAITING;
		break;
	case PK_STATUS_ENUM_LOADING_CACHE:
	case PK_STATUS_ENUM_TEST_COMMIT:
	case PK_STATUS_ENUM_RUNNING:
	case PK_STATUS_ENUM_SIG_CHECK:
	case PK_STATUS_ENUM_REFRESH_CACHE:
		plugin_status = GS_PLUGIN_STATUS_SETUP;
		break;
	case PK_STATUS_ENUM_DOWNLOAD:
	case PK_STATUS_ENUM_DOWNLOAD_REPOSITORY:
	case PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST:
	case PK_STATUS_ENUM_DOWNLOAD_FILELIST:
	case PK_STATUS_ENUM_DOWNLOAD_CHANGELOG:
	case PK_STATUS_ENUM_DOWNLOAD_GROUP:
	case PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO:
		plugin_status = GS_PLUGIN_STATUS_DOWNLOADING;
		break;
	case PK_STATUS_ENUM_INSTALL:
	case PK_STATUS_ENUM_UPDATE:
		plugin_status = GS_PLUGIN_STATUS_INSTALLING;
		break;
	case PK_STATUS_ENUM_CLEANUP:
	case PK_STATUS_ENUM_REMOVE:
		plugin_status = GS_PLUGIN_STATUS_REMOVING;
		break;
	case PK_STATUS_ENUM_REQUEST:
	case PK_STATUS_ENUM_QUERY:
	case PK_STATUS_ENUM_INFO:
	case PK_STATUS_ENUM_DEP_RESOLVE:
		plugin_status = GS_PLUGIN_STATUS_QUERYING;
		break;
	default:
		g_warning ("no mapping for %s",
			   pk_status_enum_to_string (status));
		break;
	}
	return plugin_status;
}

gboolean
gs_plugin_packagekit_error_convert (GError **error)
{
	GError *error_tmp;

	if (error == NULL)
		return FALSE;

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gio (error))
		return TRUE;

	/* not set */
	error_tmp = *error;
	if (error_tmp == NULL)
		return FALSE;

	/* already correct */
	if (error_tmp->domain == GS_PLUGIN_ERROR)
		return TRUE;

	/* get a local version */
	if (error_tmp->domain != PK_CLIENT_ERROR)
		return FALSE;

	/* daemon errors */
	if (error_tmp->code <= 0xff) {
		switch (error_tmp->code) {
		case PK_CLIENT_ERROR_CANNOT_START_DAEMON:
		case PK_CLIENT_ERROR_INVALID_FILE:
		case PK_CLIENT_ERROR_NOT_SUPPORTED:
			error_tmp->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
			break;
		/* this is working around a bug in libpackagekit-glib */
		case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
			error_tmp->code = GS_PLUGIN_ERROR_CANCELLED;
			break;
		default:
			error_tmp->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}

	/* backend errors */
	} else {
		switch (error_tmp->code - 0xff) {
		case PK_ERROR_ENUM_INVALID_PACKAGE_FILE:
		case PK_ERROR_ENUM_NOT_SUPPORTED:
		case PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED:
			error_tmp->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
			break;
		case PK_ERROR_ENUM_NO_CACHE:
		case PK_ERROR_ENUM_NO_NETWORK:
			error_tmp->code = GS_PLUGIN_ERROR_NO_NETWORK;
			break;
		case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
		case PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY:
		case PK_ERROR_ENUM_CANNOT_FETCH_SOURCES:
			error_tmp->code = GS_PLUGIN_ERROR_DOWNLOAD_FAILED;
			break;
		case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
		case PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED:
		case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
		case PK_ERROR_ENUM_GPG_FAILURE:
		case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
		case PK_ERROR_ENUM_NO_LICENSE_AGREEMENT:
		case PK_ERROR_ENUM_NOT_AUTHORIZED:
		case PK_ERROR_ENUM_RESTRICTED_DOWNLOAD:
			error_tmp->code = GS_PLUGIN_ERROR_NO_SECURITY;
			break;
		case PK_ERROR_ENUM_NO_SPACE_ON_DEVICE:
			error_tmp->code = GS_PLUGIN_ERROR_NO_SPACE;
			break;
		case PK_ERROR_ENUM_CANCELLED_PRIORITY:
		case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
			error_tmp->code = GS_PLUGIN_ERROR_CANCELLED;
			break;
		default:
			error_tmp->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}
	}
	error_tmp->domain = GS_PLUGIN_ERROR;
	return TRUE;
}

gboolean
gs_plugin_packagekit_results_valid (PkResults *results, GError **error)
{
	g_autoptr(PkError) error_code = NULL;

	/* method failed? */
	if (results == NULL) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_set_error_literal (error,
				     PK_CLIENT_ERROR,
				     pk_error_get_code (error_code),
				     pk_error_get_details (error_code));
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}

	/* all good */
	return TRUE;
}

gboolean
gs_plugin_packagekit_add_results (GsPlugin *plugin,
				  GsAppList *list,
				  PkResults *results,
				  GError **error)
{
	const gchar *package_id;
	guint i;
	PkPackage *package;
	g_autoptr(GHashTable) installed = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(GPtrArray) array_filtered = NULL;
	g_autoptr(GPtrArray) array = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "failed to get-packages: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		return FALSE;
	}

	/* add all installed packages to a hash */
	installed = g_hash_table_new (g_str_hash, g_str_equal);
	array = pk_results_get_package_array (results);
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		if (pk_package_get_info (package) != PK_INFO_ENUM_INSTALLED)
			continue;
		g_hash_table_insert (installed,
				     (const gpointer) pk_package_get_name (package),
				     (const gpointer) pk_package_get_id (package));
	}

	/* if the search returns more than one package with the same name,
	 * ignore everything with that name except the installed package */
	array_filtered = g_ptr_array_new ();
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		package_id = g_hash_table_lookup (installed, pk_package_get_name (package));
		if (pk_package_get_info (package) == PK_INFO_ENUM_INSTALLED || package_id == NULL) {
			g_ptr_array_add (array_filtered, package);
		} else {
			g_debug ("ignoring available %s as installed %s also reported",
				 pk_package_get_id (package), package_id);
		}
	}

	/* process packages */
	for (i = 0; i < array_filtered->len; i++) {
		g_autoptr(GsApp) app = NULL;
		package = g_ptr_array_index (array_filtered, i);

		app = gs_plugin_cache_lookup (plugin, pk_package_get_id (package));
		if (app == NULL) {
			app = gs_app_new (NULL);
			gs_plugin_packagekit_set_packaging_format (plugin, app);
			gs_app_add_source (app, pk_package_get_name (package));
			gs_app_add_source_id (app, pk_package_get_id (package));
			gs_plugin_cache_add (plugin, pk_package_get_id (package), app);
		}
		gs_app_set_name (app,
				 GS_APP_QUALITY_LOWEST,
				 pk_package_get_name (package));
		gs_app_set_summary (app,
				    GS_APP_QUALITY_LOWEST,
				    pk_package_get_summary (package));
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_set_management_plugin (app, "packagekit");
		gs_app_set_version (app, pk_package_get_version (package));
		switch (pk_package_get_info (package)) {
		case PK_INFO_ENUM_INSTALLED:
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
			break;
		case PK_INFO_ENUM_AVAILABLE:
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
			break;
		case PK_INFO_ENUM_INSTALLING:
		case PK_INFO_ENUM_UPDATING:
		case PK_INFO_ENUM_DOWNGRADING:
		case PK_INFO_ENUM_OBSOLETING:
		case PK_INFO_ENUM_UNTRUSTED:
			break;
		case PK_INFO_ENUM_UNAVAILABLE:
		case PK_INFO_ENUM_REMOVING:
			gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);
			break;
		default:
			gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
			g_warning ("unknown info state of %s",
				   pk_info_enum_to_string (pk_package_get_info (package)));
		}
		gs_app_set_kind (app, AS_APP_KIND_GENERIC);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_list_add (list, app);
	}
	return TRUE;
}

void
gs_plugin_packagekit_resolve_packages_app (GsPlugin *plugin,
					   GPtrArray *packages,
					   GsApp *app)
{
	GPtrArray *sources;
	PkPackage *package;
	const gchar *pkgname;
	guint i, j;
	guint number_available = 0;
	guint number_installed = 0;

	/* find any packages that match the package name */
	number_installed = 0;
	number_available = 0;
	sources = gs_app_get_sources (app);
	for (j = 0; j < sources->len; j++) {
		pkgname = g_ptr_array_index (sources, j);
		for (i = 0; i < packages->len; i++) {
			package = g_ptr_array_index (packages, i);
			if (g_strcmp0 (pk_package_get_name (package), pkgname) == 0) {
				gs_plugin_packagekit_set_metadata_from_package (plugin, app, package);
				switch (pk_package_get_info (package)) {
				case PK_INFO_ENUM_INSTALLED:
					number_installed++;
					break;
				case PK_INFO_ENUM_AVAILABLE:
					number_available++;
					break;
				case PK_INFO_ENUM_UNAVAILABLE:
					number_available++;
					break;
				default:
					/* should we expect anything else? */
					break;
				}
			}
		}
	}

	/* if *all* the source packages for the app are installed then the
	 * application is considered completely installed */
	if (number_installed == sources->len && number_available == 0) {
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	} else if (number_installed + number_available == sources->len) {
		/* if all the source packages are installed and all the rest
		 * of the packages are available then the app is available */
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	} else if (number_installed + number_available > sources->len) {
		/* we have more packages returned than source packages */
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	} else if (number_installed + number_available < sources->len) {
		g_autofree gchar *tmp = NULL;
		/* we have less packages returned than source packages */
		tmp = gs_app_to_string (app);
		g_debug ("Failed to find all packages for:\n%s", tmp);
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
	}
}

void
gs_plugin_packagekit_set_metadata_from_package (GsPlugin *plugin,
                                                GsApp *app,
                                                PkPackage *package)
{
	const gchar *data;

	gs_plugin_packagekit_set_packaging_format (plugin, app);
	gs_app_set_management_plugin (app, "packagekit");
	gs_app_add_source (app, pk_package_get_name (package));
	gs_app_add_source_id (app, pk_package_get_id (package));

	/* set origin */
	if (gs_app_get_origin (app) == NULL) {
		data = pk_package_get_data (package);
		if (g_str_has_prefix (data, "installed:"))
			data += 10;
		gs_app_set_origin (app, data);
	}

	/* set unavailable state */
	if (pk_package_get_info (package) == PK_INFO_ENUM_UNAVAILABLE) {
		gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);
		if (gs_app_get_size_installed (app) == 0)
			gs_app_set_size_installed (app, GS_APP_SIZE_UNKNOWABLE);
		if (gs_app_get_size_download (app) == 0)
			gs_app_set_size_download (app, GS_APP_SIZE_UNKNOWABLE);
	}
	if (gs_app_get_version (app) == NULL)
		gs_app_set_version (app, pk_package_get_version (package));
	gs_app_set_name (app,
			 GS_APP_QUALITY_LOWEST,
			 pk_package_get_name (package));
	gs_app_set_summary (app,
			    GS_APP_QUALITY_LOWEST,
			    pk_package_get_summary (package));
}

/*
 * gs_pk_compare_ids:
 *
 * Do not compare the repo. Some backends do not append the origin.
 */
static gboolean
gs_pk_compare_ids (const gchar *package_id1, const gchar *package_id2)
{
	gboolean ret;
	g_auto(GStrv) split1 = NULL;
	g_auto(GStrv) split2 = NULL;

	split1 = pk_package_id_split (package_id1);
	if (split1 == NULL)
		return FALSE;
	split2 = pk_package_id_split (package_id2);
	if (split2 == NULL)
		return FALSE;
	ret = (g_strcmp0 (split1[PK_PACKAGE_ID_NAME],
			  split2[PK_PACKAGE_ID_NAME]) == 0 &&
	       g_strcmp0 (split1[PK_PACKAGE_ID_VERSION],
			  split2[PK_PACKAGE_ID_VERSION]) == 0 &&
	       g_strcmp0 (split1[PK_PACKAGE_ID_ARCH],
			  split2[PK_PACKAGE_ID_ARCH]) == 0);
	return ret;
}


void
gs_plugin_packagekit_refine_details_app (GsPlugin *plugin,
					 GPtrArray *array,
					 GsApp *app)
{
	GPtrArray *source_ids;
	PkDetails *details;
	const gchar *package_id;
	guint i;
	guint j;
	guint64 size = 0;

	source_ids = gs_app_get_source_ids (app);
	for (j = 0; j < source_ids->len; j++) {
		package_id = g_ptr_array_index (source_ids, j);
		for (i = 0; i < array->len; i++) {
			g_autofree gchar *desc = NULL;
			/* right package? */
			details = g_ptr_array_index (array, i);
			if (!gs_pk_compare_ids (package_id,
						pk_details_get_package_id (details))) {
				continue;
			}
			if (gs_app_get_license (app) == NULL) {
				g_autofree gchar *license_spdx = NULL;
				license_spdx = as_utils_license_to_spdx (pk_details_get_license (details));
				if (license_spdx != NULL) {
					gs_app_set_license (app,
							    GS_APP_QUALITY_LOWEST,
							    license_spdx);
				}
			}
			if (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE) == NULL) {
				gs_app_set_url (app,
						AS_URL_KIND_HOMEPAGE,
						pk_details_get_url (details));
			}
			if (gs_app_get_description (app) == NULL) {
				gs_app_set_description (app,
				                        GS_APP_QUALITY_LOWEST,
				                        pk_details_get_description (details));
			}
			size += pk_details_get_size (details);
			break;
		}
	}

	/* the size is the size of all sources */
	if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE) {
		if (size > 0 && gs_app_get_size_installed (app) == 0)
			gs_app_set_size_installed (app, size);
		if (size > 0 && gs_app_get_size_download (app) == 0)
			gs_app_set_size_download (app, size);
	} else if (gs_app_is_installed (app)) {
		if (gs_app_get_size_download (app) == 0)
			gs_app_set_size_download (app, GS_APP_SIZE_UNKNOWABLE);
		if (size > 0 && gs_app_get_size_installed (app) == 0)
			gs_app_set_size_installed (app, size);
	} else {
		if (gs_app_get_size_installed (app) == 0)
			gs_app_set_size_installed (app, GS_APP_SIZE_UNKNOWABLE);
		if (size > 0 && gs_app_get_size_download (app) == 0)
			gs_app_set_size_download (app, size);
	}
}

void
gs_plugin_packagekit_set_packaging_format (GsPlugin *plugin, GsApp *app)
{
	if (gs_plugin_check_distro_id (plugin, "fedora") ||
	    gs_plugin_check_distro_id (plugin, "rhel")) {
		gs_app_set_metadata (app, "GnomeSoftware::PackagingFormat", "RPM");
	} else if (gs_plugin_check_distro_id (plugin, "debian") ||
	           gs_plugin_check_distro_id (plugin, "ubuntu")) {
		gs_app_set_metadata (app, "GnomeSoftware::PackagingFormat", "deb");
	}
}
