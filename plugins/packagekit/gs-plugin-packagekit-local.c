/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <string.h>

#include <packagekit-glib2/packagekit.h>
#include <gnome-software.h>

#include "packagekit-common.h"
#include "gs-packagekit-helper.h"

struct GsPluginData {
	PkTask			*task;
	GMutex			 task_mutex;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	g_mutex_init (&priv->task_mutex);
	priv->task = pk_task_new ();
	pk_client_set_background (PK_CLIENT (priv->task), FALSE);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_mutex_clear (&priv->task_mutex);
	g_object_unref (priv->task);
}

static gboolean
gs_plugin_packagekit_refresh_guess_app_id (GsPlugin *plugin,
					   GsApp *app,
					   const gchar *filename,
					   GCancellable *cancellable,
					   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_auto(GStrv) files = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GString) basename_best = g_string_new (NULL);

	/* get file list so we can work out ID */
	files = g_strsplit (filename, "\t", -1);
	gs_packagekit_helper_add_app (helper, app);
	g_mutex_lock (&priv->task_mutex);
	results = pk_client_get_files_local (PK_CLIENT (priv->task),
					     files,
					     cancellable,
					     gs_packagekit_helper_cb, helper,
					     error);
	g_mutex_unlock (&priv->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		gs_utils_error_add_origin_id (error, app);
		return FALSE;
	}
	array = pk_results_get_files_array (results);
	if (array->len == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "no files for %s", filename);
		return FALSE;
	}

	/* find the smallest length desktop file, on the logic that
	 * ${app}.desktop is going to be better than ${app}-${action}.desktop */
	for (guint i = 0; i < array->len; i++) {
		PkFiles *item = g_ptr_array_index (array, i);
		gchar **fns = pk_files_get_files (item);
		for (guint j = 0; fns[j] != NULL; j++) {
			if (g_str_has_prefix (fns[j], "/etc/yum.repos.d/") &&
			    g_str_has_suffix (fns[j], ".repo")) {
				gs_app_add_quirk (app, GS_APP_QUIRK_HAS_SOURCE);
			}
			if (g_str_has_prefix (fns[j], "/usr/share/applications/") &&
			    g_str_has_suffix (fns[j], ".desktop")) {
				g_autofree gchar *basename = g_path_get_basename (fns[j]);
				if (basename_best->len == 0 ||
				    strlen (basename) < basename_best->len)
					g_string_assign (basename_best, basename);
			}
		}
	}
	if (basename_best->len > 0) {
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
		gs_app_set_id (app, basename_best->str);
	}

	return TRUE;
}

static void
add_quirks_from_package_name (GsApp *app, const gchar *package_name)
{
	/* these packages don't have a .repo file in their file lists, but
	 * instead install one through rpm scripts / cron job */
	const gchar *packages_with_repos[] = {
		"google-chrome-stable",
		"google-earth-pro-stable",
		"google-talkplugin",
		NULL };

	if (g_strv_contains (packages_with_repos, package_name))
		gs_app_add_quirk (app, GS_APP_QUIRK_HAS_SOURCE);
}

static gboolean
gs_plugin_packagekit_local_check_installed (GsPlugin *plugin,
					    GsApp *app,
					    GCancellable *cancellable,
					    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	PkBitfield filter;
	const gchar *names[] = { gs_app_get_source_default (app), NULL };
	g_autoptr(GPtrArray) packages = NULL;
	g_autoptr(PkResults) results = NULL;

	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_INSTALLED,
					 -1);
	results = pk_client_resolve (PK_CLIENT (priv->task), filter, (gchar **) names,
				     cancellable, NULL, NULL, error);
	if (results == NULL)
		return FALSE;
	packages = pk_results_get_package_array (results);
	if (packages->len > 0) {
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		for (guint i = 0; i < packages->len; i++){
			PkPackage *pkg = g_ptr_array_index (packages, i);
			gs_app_add_source_id (app, pk_package_get_id (pkg));
		}
	}
	return TRUE;
}

gboolean
gs_plugin_file_to_app (GsPlugin *plugin,
		       GsAppList *list,
		       GFile *file,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *package_id;
	PkDetails *item;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *license_spdx = NULL;
	g_auto(GStrv) files = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GsApp) app = NULL;
	const gchar *mimetypes[] = {
		"application/x-app-package",
		"application/x-deb",
		"application/vnd.debian.binary-package",
		"application/x-redhat-package-manager",
		"application/x-rpm",
		NULL };

	/* does this match any of the mimetypes we support */
	content_type = gs_utils_get_content_type (file, cancellable, error);
	if (content_type == NULL)
		return FALSE;
	if (!g_strv_contains (mimetypes, content_type))
		return TRUE;

	/* get details */
	filename = g_file_get_path (file);
	files = g_strsplit (filename, "\t", -1);
	g_mutex_lock (&priv->task_mutex);
	pk_client_set_cache_age (PK_CLIENT (priv->task), G_MAXUINT);
	results = pk_client_get_details_local (PK_CLIENT (priv->task),
					       files,
					       cancellable,
					       gs_packagekit_helper_cb, helper,
					       error);
	g_mutex_unlock (&priv->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;

	/* get results */
	array = pk_results_get_details_array (results);
	if (array->len == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "no details for %s", filename);
		return FALSE;
	}
	if (array->len > 1) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "too many details [%u] for %s",
			     array->len, filename);
		return FALSE;
	}

	/* create application */
	item = g_ptr_array_index (array, 0);
	app = gs_app_new (NULL);
	gs_plugin_packagekit_set_packaging_format (plugin, app);
	gs_app_set_metadata (app, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));
	package_id = pk_details_get_package_id (item);
	split = pk_package_id_split (package_id);
	if (split == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "invalid package-id: %s", package_id);
		return FALSE;
	}
	gs_app_set_management_plugin (app, "packagekit");
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, split[PK_PACKAGE_ID_NAME]);
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
			    pk_details_get_summary (item));
	gs_app_set_version (app, split[PK_PACKAGE_ID_VERSION]);
	gs_app_add_source (app, split[PK_PACKAGE_ID_NAME]);
	gs_app_add_source_id (app, package_id);
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST,
				pk_details_get_description (item));
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, pk_details_get_url (item));
	gs_app_set_size_installed (app, pk_details_get_size (item));
	gs_app_set_size_download (app, 0);
	license_spdx = as_utils_license_to_spdx (pk_details_get_license (item));
	gs_app_set_license (app, GS_APP_QUALITY_LOWEST, license_spdx);
	add_quirks_from_package_name (app, split[PK_PACKAGE_ID_NAME]);

	/* is already installed? */
	if (!gs_plugin_packagekit_local_check_installed (plugin,
							 app,
							 cancellable,
							 error))
		return FALSE;

	/* look for a desktop file so we can use a valid application id */
	if (!gs_plugin_packagekit_refresh_guess_app_id (plugin,
							app,
							filename,
							cancellable,
							error))
		return FALSE;

	gs_app_list_add (list, app);
	return TRUE;
}
