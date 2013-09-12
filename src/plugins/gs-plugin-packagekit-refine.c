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
#include <glib/gi18n.h>

struct GsPluginPrivate {
	PkClient		*client;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "packagekit-refine";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->client = pk_client_new ();
	g_object_set (plugin->priv->client,
		      "background", FALSE,
		      NULL);
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 150.0f;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->client);
}

/**
 * gs_plugin_packagekit_progress_cb:
 **/
static void
gs_plugin_packagekit_progress_cb (PkProgress *progress,
				  PkProgressType type,
				  gpointer user_data)
{
	GsPluginStatus plugin_status = GS_PLUGIN_STATUS_UNKNOWN;
	PkStatusEnum status;
	GsPlugin *plugin = GS_PLUGIN (user_data);

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;
	g_object_get (progress,
		      "status", &status,
		      NULL);

	/* set label */
	switch (status) {
	case PK_STATUS_ENUM_SETUP:
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
	if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
		gs_plugin_status_update (plugin, NULL, plugin_status);
}

static gboolean
gs_plugin_packagekit_refine_packages (GsPlugin *plugin,
				      GList *list,
				      GCancellable *cancellable,
				      GError **error)
{
	const gchar *pkgname;
	gboolean ret = TRUE;
	const gchar **package_ids;
	GList *l;
	GPtrArray *array = NULL;
	GPtrArray *packages = NULL;
	GsApp *app;
	guint cnt = 0;
	guint i = 0;
	guint size;
	PkError *error_code = NULL;
	PkPackage *package;
	PkResults *results = NULL;

	size = g_list_length (list);
	package_ids = g_new0 (const gchar *, size + 1);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		pkgname = gs_app_get_source (app);
		package_ids[i++] = pkgname;
	}

	/* resolve them all at once */
	results = pk_client_resolve (plugin->priv->client,
				     pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_ARCH, -1),
				     (gchar **) package_ids,
				     cancellable,
				     gs_plugin_packagekit_progress_cb, plugin,
				     error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to resolve: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		pkgname = gs_app_get_source (app);

		/* find any packages that match the package name */
		cnt = 0;
		for (i = 0; i < packages->len; i++) {
			package = g_ptr_array_index (packages, i);
			if (g_strcmp0 (pk_package_get_name (package), pkgname) == 0) {
				gs_app_set_management_plugin (app, "PackageKit");
				gs_app_set_metadata (app, "PackageKit::package-id", pk_package_get_id (package));
				if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN) {
					gs_app_set_state (app,
							  pk_package_get_info (package) == PK_INFO_ENUM_INSTALLED ?
							  GS_APP_STATE_INSTALLED :
							  GS_APP_STATE_AVAILABLE);
				}
				if (gs_app_get_version (app) == NULL)
					gs_app_set_version (app, pk_package_get_version (package));
				cnt++;
			}
		}
		if (cnt == 0) {
			g_warning ("Failed to find any package for %s, %s",
				   gs_app_get_id (app), pkgname);
		} else if (cnt > 1) {
			g_warning ("found duplicate packages for %s, %s, [%d]",
				   gs_app_get_id (app), pkgname, cnt);
		}
	}
out:
	g_free (package_ids);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

static gboolean
gs_plugin_packagekit_refine_from_desktop (GsPlugin *plugin,
					  GsApp	 *app,
					  const gchar *filename,
					  GCancellable *cancellable,
					  GError **error)
{
	const gchar *to_array[] = { NULL, NULL };
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
	GPtrArray *packages = NULL;
	PkError *error_code = NULL;
	PkPackage *package;
	PkResults *results = NULL;

	to_array[0] = filename;
	results = pk_client_search_files (plugin->priv->client,
					  pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
					  (gchar **) to_array,
					  cancellable,
					  gs_plugin_packagekit_progress_cb, plugin,
					  error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to search files: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (packages->len == 1) {
		package = g_ptr_array_index (packages, 0);
		gs_app_set_metadata (app, "PackageKit::package-id", pk_package_get_id (package));
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		gs_app_set_management_plugin (app, "PackageKit");
	} else {
		g_warning ("Failed to find one package for %s, %s, [%d]",
			   gs_app_get_id (app), filename, packages->len);
	}
out:
	if (packages != NULL)
		g_ptr_array_unref (packages);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_refine:
 */
static gboolean
gs_plugin_packagekit_refine_updatedetails (GsPlugin *plugin,
					   GList *list,
					   GCancellable *cancellable,
					   GError **error)
{
	const gchar *package_id;
	gboolean ret = TRUE;
	const gchar **package_ids;
	GList *l;
	GPtrArray *array = NULL;
	GsApp *app;
	guint i = 0;
	guint size;
	PkResults *results = NULL;
	PkUpdateDetail *update_detail;

	size = g_list_length (list);
	package_ids = g_new0 (const gchar *, size + 1);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		package_id = gs_app_get_metadata_item (app, "PackageKit::package-id");
		package_ids[i++] = package_id;
	}

	/* get any update details */
	results = pk_client_get_update_detail (plugin->priv->client,
					       (gchar **) package_ids,
					       cancellable,
					       gs_plugin_packagekit_progress_cb, plugin,
					       error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* set the update details for the update */
	array = pk_results_get_update_detail_array (results);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		package_id = gs_app_get_metadata_item (app, "PackageKit::package-id");
		for (i = 0; i < array->len; i++) {
			/* right package? */
			update_detail = g_ptr_array_index (array, i);
			if (g_strcmp0 (package_id, pk_update_detail_get_package_id (update_detail)) != 0)
				continue;
			gs_app_set_update_details (app, pk_update_detail_get_update_text (update_detail));
			break;
		}
		if (gs_app_get_update_details (app) == NULL) {
			/* TRANSLATORS: this is where update details either are
			 * no longer available or were never provided in the first place */
			gs_app_set_update_details (app, _("No update details were provided"));
		}
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	g_free (package_ids);
	return ret;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GCancellable *cancellable,
		  GError **error)
{
	gboolean ret = TRUE;
	GList *l;
	GsApp *app;
	const gchar *tmp;
	GList *resolve_all = NULL;
	GList *updatedetails_all = NULL;

	/* can we resolve in one go? */
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_metadata_item (app, "PackageKit::package-id") != NULL)
			continue;
		tmp = gs_app_get_source (app);
		if (tmp != NULL)
			resolve_all = g_list_prepend (resolve_all, app);
	}
	if (resolve_all != NULL) {
		ret = gs_plugin_packagekit_refine_packages (plugin,
							    resolve_all,
							    cancellable,
							    error);
	}

	/* add any missing ratings data */
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_metadata_item (app, "PackageKit::package-id") != NULL)
			continue;
		tmp = gs_app_get_metadata_item (app, "DataDir::desktop-filename");
		if (tmp == NULL)
			continue;
		ret = gs_plugin_packagekit_refine_from_desktop (plugin,
								app,
								tmp,
								cancellable,
								error);
		if (!ret)
			goto out;
	}

	/* any update details missing? */
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_state (app) != GS_APP_STATE_UPDATABLE)
			continue;
		if (gs_app_get_update_details (app) != NULL)
			continue;
		if (gs_app_get_metadata_item (app, "PackageKit::package-id") == NULL)
			continue;
		updatedetails_all = g_list_prepend (updatedetails_all, app);
	}
	if (updatedetails_all != NULL) {
		ret = gs_plugin_packagekit_refine_updatedetails (plugin,
								 updatedetails_all,
								 cancellable,
								 error);
	}
out:
	g_list_free (resolve_all);
	g_list_free (updatedetails_all);
	return ret;
}
