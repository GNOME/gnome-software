/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014-2016 Richard Hughes <richard@hughsie.com>
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
#include <glib/gi18n.h>

#include <gs-plugin.h>
#include <gs-utils.h>

#include "packagekit-common.h"

/*
 * SECTION:
 * Do a PackageKit UpdatePackages(ONLY_DOWNLOAD) method on refresh and
 * also convert any package files to applications the best we can.
 */

struct GsPluginData {
	PkTask			*task;
};

/**
 * gs_plugin_get_conflicts:
 */
const gchar **
gs_plugin_get_conflicts (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"dpkg",
		NULL };
	return deps;
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->task = pk_task_new ();
	pk_client_set_interactive (PK_CLIENT (priv->task), FALSE);
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->task);
}

typedef struct {
	GsPlugin	*plugin;
	AsProfileTask	*ptask;
} ProgressData;

/**
 * gs_plugin_packagekit_progress_cb:
 **/
static void
gs_plugin_packagekit_progress_cb (PkProgress *progress,
				  PkProgressType type,
				  gpointer user_data)
{
	ProgressData *data = (ProgressData *) user_data;
	GsPlugin *plugin = data->plugin;
	GsPluginStatus plugin_status;
	PkStatusEnum status;

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;
	g_object_get (progress,
		      "status", &status,
		      NULL);

	/* profile */
	if (status == PK_STATUS_ENUM_SETUP) {
		data->ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
							"packagekit-refresh::transaction");
	} else if (status == PK_STATUS_ENUM_FINISHED) {
		g_clear_pointer (&data->ptask, as_profile_task_free);
	}

	plugin_status = packagekit_status_enum_to_plugin_status (status);
	if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
		gs_plugin_status_update (plugin, NULL, plugin_status);
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	ProgressData data;
	g_autoptr(PkResults) results = NULL;

	/* nothing to re-generate */
	if (flags == 0)
		return TRUE;

	/* cache age of 0 is user-initiated */
	pk_client_set_background (PK_CLIENT (priv->task), cache_age > 0);

	data.plugin = plugin;
	data.ptask = NULL;

	/* refresh the metadata */
	if (flags & GS_PLUGIN_REFRESH_FLAGS_METADATA ||
	    flags & GS_PLUGIN_REFRESH_FLAGS_PAYLOAD) {
		PkBitfield filter;

		filter = pk_bitfield_value (PK_FILTER_ENUM_NONE);
		pk_client_set_cache_age (PK_CLIENT (priv->task), cache_age);
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
		results = pk_client_get_updates (PK_CLIENT (priv->task),
						 filter,
						 cancellable,
						 gs_plugin_packagekit_progress_cb, &data,
						 error);
		if (results == NULL) {
			gs_plugin_packagekit_convert_gerror (error);
			return FALSE;
		}
	}

	/* download all the packages themselves */
	if (flags & GS_PLUGIN_REFRESH_FLAGS_PAYLOAD) {
		PkBitfield transaction_flags;
		g_auto(GStrv) package_ids = NULL;
		g_autoptr(PkPackageSack) sack = NULL;
		g_autoptr(PkResults) results2 = NULL;

		sack = pk_results_get_package_sack (results);
		if (pk_package_sack_get_size (sack) == 0)
			return TRUE;
		package_ids = pk_package_sack_get_ids (sack);
		transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD);
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
		results2 = pk_client_update_packages (PK_CLIENT (priv->task),
						      transaction_flags,
						      package_ids,
						      cancellable,
						      gs_plugin_packagekit_progress_cb, &data,
						      error);
		if (results2 == NULL) {
			gs_plugin_packagekit_convert_gerror (error);
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * gs_plugin_packagekit_refresh_set_text:
 *
 * The cases we have to deal with:
 *  - Single line text, so all to summary
 *  - Single line long text, so all to description
 *  - Multiple line text, so first line to summary and the rest to description
 */
static void
gs_plugin_packagekit_refresh_set_text (GsApp *app, const gchar *text)
{
	gchar *nl;
	g_autofree gchar *tmp = NULL;

	if (text == NULL || text[0] == '\0')
		return;

	/* look for newline */
	tmp = g_strdup (text);
	nl = g_strstr_len (tmp, -1, "\n");
	if (nl == NULL) {
		if (strlen (text) < 40) {
			gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, text);
			return;
		}
		gs_app_set_description (app, GS_APP_QUALITY_LOWEST, text);
		return;
	}
	*nl = '\0';
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, tmp);
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST, nl + 1);
}

/**
 * gs_plugin_packagekit_refresh_guess_app_id:
 */
static gboolean
gs_plugin_packagekit_refresh_guess_app_id (GsPlugin *plugin,
					   GsApp *app,
					   const gchar *filename,
					   GCancellable *cancellable,
					   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	PkFiles *item;
	ProgressData data;
	guint i;
	guint j;
	gchar **fns;
	g_auto(GStrv) files = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	data.plugin = plugin;
	data.ptask = NULL;

	/* get file list so we can work out ID */
	files = g_strsplit (filename, "\t", -1);
	results = pk_client_get_files_local (PK_CLIENT (priv->task),
					     files,
					     cancellable,
					     gs_plugin_packagekit_progress_cb, &data,
					     error);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;
	array = pk_results_get_files_array (results);
	if (array->len == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "no files for %s", filename);
		return FALSE;
	}

	/* find the first desktop file */
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		fns = pk_files_get_files (item);
		for (j = 0; fns[j] != NULL; j++) {
			if (g_str_has_prefix (fns[j], "/etc/yum.repos.d/") &&
			    g_str_has_suffix (fns[j], ".repo")) {
				gs_app_add_quirk (app, AS_APP_QUIRK_HAS_SOURCE);
			}
			if (g_str_has_prefix (fns[j], "/usr/share/applications/") &&
			    g_str_has_suffix (fns[j], ".desktop")) {
				g_autofree gchar *basename = g_path_get_basename (fns[j]);
				gs_app_set_id (app, basename);
				gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
				gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
				break;
			}
		}
	}
	return TRUE;
}

/**
 * gs_plugin_filename_to_app:
 */
gboolean
gs_plugin_filename_to_app (GsPlugin *plugin,
			   GList **list,
			   const gchar *filename,
			   GCancellable *cancellable,
			   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *package_id;
	PkDetails *item;
	ProgressData data;
	g_autoptr (PkResults) results = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *license_spdx = NULL;
	g_auto(GStrv) files = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GsApp) app = NULL;
	const gchar *mimetypes[] = {
		"application/x-app-package",
		"application/x-deb",
		"application/x-redhat-package-manager",
		"application/x-rpm",
		NULL };

	/* does this match any of the mimetypes we support */
	content_type = gs_utils_get_content_type (filename, cancellable, error);
	if (content_type == NULL)
		return FALSE;
	if (!g_strv_contains (mimetypes, content_type))
		return TRUE;

	data.plugin = plugin;
	data.ptask = NULL;

	/* get details */
	files = g_strsplit (filename, "\t", -1);
	pk_client_set_cache_age (PK_CLIENT (priv->task), G_MAXUINT);
	results = pk_client_get_details_local (PK_CLIENT (priv->task),
					       files,
					       cancellable,
					       gs_plugin_packagekit_progress_cb, &data,
					       error);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;

	/* get results */
	array = pk_results_get_details_array (results);
	if (array->len == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "no details for %s", filename);
		return FALSE;
	}
	if (array->len > 1) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "too many details [%i] for %s",
			     array->len, filename);
		return FALSE;
	}

	/* create application */
	item = g_ptr_array_index (array, 0);
	app = gs_app_new (NULL);
	package_id = pk_details_get_package_id (item);
	split = pk_package_id_split (package_id);
	basename = g_path_get_basename (filename);
	gs_app_set_management_plugin (app, "packagekit");
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	if (pk_details_get_summary (item))
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST,
				 pk_details_get_summary (item));
	else
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST, split[PK_PACKAGE_ID_NAME]);
	gs_app_set_version (app, split[PK_PACKAGE_ID_VERSION]);
	gs_app_set_metadata (app, "packagekit::local-filename", filename);
	gs_app_set_origin (app, basename);
	gs_app_add_source (app, split[PK_PACKAGE_ID_NAME]);
	gs_app_add_source_id (app, package_id);
	gs_plugin_packagekit_refresh_set_text (app,
					       pk_details_get_description (item));
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, pk_details_get_url (item));
	gs_app_set_size_installed (app, pk_details_get_size (item));
	gs_app_set_size_download (app, 0);
	license_spdx = as_utils_license_to_spdx (pk_details_get_license (item));
	gs_app_set_license (app, GS_APP_QUALITY_LOWEST, license_spdx);

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
