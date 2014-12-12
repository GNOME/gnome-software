/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#include "packagekit-common.h"

struct GsPluginPrivate {
	PkTask			*task;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "packagekit-refresh";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->task = pk_task_new ();
	pk_client_set_background (PK_CLIENT (plugin->priv->task), FALSE);
	pk_client_set_interactive (PK_CLIENT (plugin->priv->task), FALSE);
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->task);
}

/**
 * gs_plugin_packagekit_progress_cb:
 **/
static void
gs_plugin_packagekit_progress_cb (PkProgress *progress,
				  PkProgressType type,
				  gpointer user_data)
{
	GsPluginStatus plugin_status;
	PkStatusEnum status;
	GsPlugin *plugin = GS_PLUGIN (user_data);

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;
	g_object_get (progress,
		      "status", &status,
		      NULL);

	/* profile */
	if (status == PK_STATUS_ENUM_SETUP) {
		gs_profile_start (plugin->profile,
				  "packagekit-refresh::transaction");
	} else if (status == PK_STATUS_ENUM_FINISHED) {
		gs_profile_stop (plugin->profile,
				 "packagekit-refresh::transaction");
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
	PkBitfield filter;
	PkBitfield transaction_flags;
	_cleanup_strv_free_ gchar **package_ids = NULL;
	_cleanup_object_unref_ PkPackageSack *sack = NULL;
	_cleanup_object_unref_ PkResults *results2 = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	/* not us */
	if ((flags & GS_PLUGIN_REFRESH_FLAGS_UPDATES) == 0)
		return TRUE;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* do sync call */
	filter = pk_bitfield_value (PK_FILTER_ENUM_NONE);
	pk_client_set_cache_age (PK_CLIENT (plugin->priv->task), cache_age);
	results = pk_client_get_updates (PK_CLIENT (plugin->priv->task),
					 filter,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, plugin,
					 error);
	if (results == NULL)
		return FALSE;

	/* download all the updates */
	sack = pk_results_get_package_sack (results);
	if (pk_package_sack_get_size (sack) == 0)
		return TRUE;
	package_ids = pk_package_sack_get_ids (sack);
	transaction_flags = pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD);
	results2 = pk_client_update_packages (PK_CLIENT (plugin->priv->task),
					      transaction_flags,
					      package_ids,
					      cancellable,
					      gs_plugin_packagekit_progress_cb, plugin,
					      error);
	return results2 != NULL;
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
	_cleanup_free_ gchar *tmp = NULL;

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
 * gs_plugin_filename_to_app:
 */
gboolean
gs_plugin_filename_to_app (GsPlugin *plugin,
			   GList **list,
			   const gchar *filename,
			   GCancellable *cancellable,
			   GError **error)
{
	const gchar *package_id;
	PkDetails *item;
	PkResults *results = NULL;
	_cleanup_free_ gchar *basename = NULL;
	_cleanup_strv_free_ gchar **files = NULL;
	_cleanup_strv_free_ gchar **split = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;
	_cleanup_object_unref_ GsApp *app = NULL;

	/* get details */
	files = g_strsplit (filename, "\t", -1);
	pk_client_set_cache_age (PK_CLIENT (plugin->priv->task), G_MAXUINT);
#if PK_CHECK_VERSION(0,9,1)
	results = pk_client_get_details_local (PK_CLIENT (plugin->priv->task),
					       files,
					       cancellable,
					       gs_plugin_packagekit_progress_cb, plugin,
					       error);
#else
	g_set_error_literal (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "GetDetailsLocal() not supported");
#endif
	if (results == NULL)
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
	gs_app_set_management_plugin (app, "PackageKit");
	gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
#if PK_CHECK_VERSION(0,9,1)
	if (pk_details_get_summary (item))
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST,
				 pk_details_get_summary (item));
	else
#endif
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST, split[PK_PACKAGE_ID_NAME]);
	gs_app_set_version (app, split[PK_PACKAGE_ID_VERSION]);
	gs_app_set_metadata (app, "PackageKit::local-filename", filename);
	gs_app_set_origin (app, basename);
	gs_app_add_source (app, split[PK_PACKAGE_ID_NAME]);
	gs_app_add_source_id (app, package_id);
	gs_plugin_packagekit_refresh_set_text (app,
					       pk_details_get_description (item));
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, pk_details_get_url (item));
	gs_app_set_size (app, pk_details_get_size (item));
	gs_app_set_licence (app, pk_details_get_license (item));
	gs_plugin_add_app (list, app);
	return TRUE;
}
