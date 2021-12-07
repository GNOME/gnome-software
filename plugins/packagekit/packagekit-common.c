/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
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

	if (*error != NULL)
		g_dbus_error_strip_remote_error (*error);

	/* these are allowed for low-level errors */
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
		#if PK_CHECK_VERSION(1, 2, 4)
		case PK_CLIENT_ERROR_DECLINED_INTERACTION:
			error_tmp->code = GS_PLUGIN_ERROR_CANCELLED;
			break;
		#else
		case PK_CLIENT_ERROR_FAILED:
			/* The text is not localized on the PackageKit side and it uses a generic error code
			 * FIXME: This can be dropped when we depend on a
			 * PackageKit version which includes https://github.com/PackageKit/PackageKit/pull/497 */
			if (g_strcmp0 (error_tmp->message, "user declined interaction") == 0)
				error_tmp->code = GS_PLUGIN_ERROR_CANCELLED;
			else
				error_tmp->code = GS_PLUGIN_ERROR_FAILED;
			break;
		#endif
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
		GsAppState state = GS_APP_STATE_UNKNOWN;
		package = g_ptr_array_index (array_filtered, i);

		app = gs_plugin_cache_lookup (plugin, pk_package_get_id (package));
		if (app == NULL) {
			app = gs_app_new (NULL);
			gs_plugin_packagekit_set_packaging_format (plugin, app);
			gs_app_set_management_plugin (app, plugin);
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
		gs_app_set_version (app, pk_package_get_version (package));
		switch (pk_package_get_info (package)) {
		case PK_INFO_ENUM_INSTALLED:
			state = GS_APP_STATE_INSTALLED;
			break;
		case PK_INFO_ENUM_AVAILABLE:
			state = GS_APP_STATE_AVAILABLE;
			break;
		case PK_INFO_ENUM_INSTALLING:
		case PK_INFO_ENUM_UPDATING:
		case PK_INFO_ENUM_DOWNGRADING:
		case PK_INFO_ENUM_OBSOLETING:
		case PK_INFO_ENUM_UNTRUSTED:
			break;
		case PK_INFO_ENUM_UNAVAILABLE:
		case PK_INFO_ENUM_REMOVING:
			state =  GS_APP_STATE_UNAVAILABLE;
			break;
		default:
			g_warning ("unknown info state of %s",
				   pk_info_enum_to_string (pk_package_get_info (package)));
		}
		if (state != GS_APP_STATE_UNKNOWN && gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, state);
		if (gs_app_get_kind (app) == AS_COMPONENT_KIND_UNKNOWN)
			gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
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
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, GS_APP_STATE_INSTALLED);
	} else if (number_installed + number_available == sources->len) {
		/* if all the source packages are installed and all the rest
		 * of the packages are available then the app is available */
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	} else if (number_installed + number_available > sources->len) {
		/* we have more packages returned than source packages */
		gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
	} else if (number_installed + number_available < sources->len) {
		g_autofree gchar *tmp = NULL;
		/* we have less packages returned than source packages */
		tmp = gs_app_to_string (app);
		g_debug ("Failed to find all packages for:\n%s", tmp);
		gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
	}
}

void
gs_plugin_packagekit_set_metadata_from_package (GsPlugin *plugin,
                                                GsApp *app,
                                                PkPackage *package)
{
	const gchar *data;

	gs_plugin_packagekit_set_packaging_format (plugin, app);
	gs_app_set_management_plugin (app, plugin);
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
		gs_app_set_state (app, GS_APP_STATE_UNAVAILABLE);
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

/* Hash functions which compare PkPackageIds on NAME, VERSION and ARCH, but not DATA.
 * This is because some backends do not append the origin.
 *
 * Borrowing some implementation details from pk-package-id.c, a package
 * ID is a semicolon-separated list of NAME;[VERSION];[ARCH];[DATA],
 * so a comparison which ignores DATA is just a strncmp() up to and
 * including the final semicolon.
 *
 * Doing it this way means zero allocations, which allows the hash and
 * equality functions to be fast. This is important when dealing with
 * large refine() package lists.
 *
 * The hash and equality functions assume that the IDs they are passed are
 * valid. */
static guint
package_id_hash (gconstpointer key)
{
	const gchar *package_id = key;
	gchar *no_data;
	gsize i, last_semicolon = 0;

	/* find the last semicolon, which starts the DATA section */
	for (i = 0; package_id[i] != '\0'; i++) {
		if (package_id[i] == ';')
			last_semicolon = i;
	}

	/* exit early if the DATA section was empty */
	if (last_semicolon + 1 == i)
		return g_str_hash (package_id);

	/* extract up to (and including) the last semicolon into a local string */
	no_data = g_alloca (last_semicolon + 2);
	memcpy (no_data, package_id, last_semicolon + 1);
	no_data[last_semicolon + 1] = '\0';

	return g_str_hash (no_data);
}

static gboolean
package_id_equal (gconstpointer a,
                  gconstpointer b)
{
	const gchar *package_id_a = a;
	const gchar *package_id_b = b;
	gsize i, n_semicolons = 0;

	/* compare up to and including the last semicolon */
	for (i = 0; package_id_a[i] != '\0' && package_id_b[i] != '\0'; i++) {
		if (package_id_a[i] != package_id_b[i])
			return FALSE;
		if (package_id_a[i] == ';')
			n_semicolons++;
		if (n_semicolons == 4)
			return TRUE;
	}

	return package_id_a[i] == package_id_b[i];
}

GHashTable *
gs_plugin_packagekit_details_array_to_hash (GPtrArray *array)
{
	g_autoptr(GHashTable) details_collection = NULL;

	details_collection = g_hash_table_new_full (package_id_hash, package_id_equal,
						    NULL, NULL);

	for (gsize i = 0; i < array->len; i++) {
		PkDetails *details = g_ptr_array_index (array, i);
		g_hash_table_insert (details_collection,
				     (void *) pk_details_get_package_id (details),
				     details);
	}

	return g_steal_pointer (&details_collection);
}

void
gs_plugin_packagekit_refine_details_app (GsPlugin *plugin,
					 GHashTable *details_collection,
					 GsApp *app)
{
	GPtrArray *source_ids;
	PkDetails *details;
	const gchar *package_id;
	guint j;
	guint64 download_size = 0, install_size = 0;

	/* @source_ids can have as many as 200 elements (google-noto); typically
	 * it has 1 or 2
	 *
	 * @details_collection is typically a large list of apps in the
	 * repository, on the order of 400 or 700 apps */
	source_ids = gs_app_get_source_ids (app);
	for (j = 0; j < source_ids->len; j++) {
		#ifdef HAVE_PK_DETAILS_GET_DOWNLOAD_SIZE
		guint64 download_sz;
		#endif
		package_id = g_ptr_array_index (source_ids, j);
		details = g_hash_table_lookup (details_collection, package_id);
		if (details == NULL)
			continue;

		if (gs_app_get_license (app) == NULL) {
			g_autofree gchar *license_spdx = NULL;
			license_spdx = as_license_to_spdx_id (pk_details_get_license (details));
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
		install_size += pk_details_get_size (details);
		#ifdef HAVE_PK_DETAILS_GET_DOWNLOAD_SIZE
		download_sz = pk_details_get_download_size (details);
		if (download_sz != G_MAXUINT64)
			download_size += download_sz;
		#endif
	}

	#ifndef HAVE_PK_DETAILS_GET_DOWNLOAD_SIZE
	download_size = install_size;
	#endif

	/* the size is the size of all sources */
	if (gs_app_get_state (app) == GS_APP_STATE_UPDATABLE) {
		if (install_size > 0 && gs_app_get_size_installed (app) == 0)
			gs_app_set_size_installed (app, install_size);
		if (download_size > 0 && gs_app_get_size_download (app) == 0)
			gs_app_set_size_download (app, download_size);
	} else if (gs_app_is_installed (app)) {
		if (gs_app_get_size_download (app) == 0)
			gs_app_set_size_download (app, GS_APP_SIZE_UNKNOWABLE);
		if (install_size > 0 && gs_app_get_size_installed (app) == 0)
			gs_app_set_size_installed (app, install_size);
	} else {
		if (gs_app_get_size_installed (app) == 0)
			gs_app_set_size_installed (app, install_size > 0 ? install_size : GS_APP_SIZE_UNKNOWABLE);
		if (download_size > 0 && gs_app_get_size_download (app) == 0)
			gs_app_set_size_download (app, download_size);
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

gboolean
gs_plugin_packagekit_is_packagekit_app (GsApp *app,
					GsPlugin *plugin)
{
	g_autoptr(GsPlugin) mngmt_plugin = NULL;

	if (gs_app_has_management_plugin (app, plugin))
		return TRUE;

	mngmt_plugin = gs_app_dup_management_plugin (app);

	return mngmt_plugin != NULL &&
	       g_strcmp0 (gs_plugin_get_name (mngmt_plugin), "packagekit") == 0;
}
