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

struct GsPluginPrivate {
	PkTask			*task;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "packagekit";
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
	g_object_set (plugin->priv->task,
		      "background", FALSE,
		      NULL);
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 10.0f;
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
 * gs_plugin_add_search:
 */
gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      const gchar *value,
		      GList *list,
		      GError **error)
{
	return TRUE;
}

/**
 * gs_plugin_packagekit_add_results:
 */
static gboolean
gs_plugin_packagekit_add_results (GsPlugin *plugin, GList **list, PkResults *results)
{
	GPtrArray *array = NULL;
	GsApp *app;
	guint i;
	PkError *error_code = NULL;
	PkPackage *package;

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get-packages: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	array = pk_results_get_package_array (results);
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		app = gs_app_new (pk_package_get_id (package));
		gs_app_set_metadata (app,
				     "package-id",
				     pk_package_get_id (package));
		gs_app_set_metadata (app,
				     "package-name",
				     pk_package_get_name (package));
		gs_app_set_metadata (app,
				     "package-summary",
				     pk_package_get_summary (package));
		gs_app_set_version (app, pk_package_get_version (package));
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
		gs_plugin_add_app (list, app);
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	return TRUE;
}

/**
 * gs_plugin_add_installed:
 */
gboolean
gs_plugin_add_installed (GsPlugin *plugin, GList **list, GError **error)
{
	gboolean ret = TRUE;
	PkBitfield filter;
	PkResults *results;

	/* do sync call */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
					 PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 -1);
	results = pk_client_get_packages (PK_CLIENT(plugin->priv->task),
					  filter,
					  NULL, NULL,
					  plugin->cancellable,
					  error);
	if (results == NULL)
		goto out;

	/* add results */
	ret = gs_plugin_packagekit_add_results (plugin, list, results);
	if (!ret)
		goto out;
out:
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_add_updates:
 */
gboolean
gs_plugin_add_updates (GsPlugin *plugin, GList **list, GError **error)
{
	gboolean ret = TRUE;
	PkBitfield filter;
	PkResults *results;

	/* do sync call */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH, -1);
	results = pk_client_get_updates (PK_CLIENT(plugin->priv->task),
					 filter,
					 NULL, NULL,
					 plugin->cancellable,
					 error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* add results */
	ret = gs_plugin_packagekit_add_results (plugin, list, results);
out:
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * gs_plugin_app_install:
 */
gboolean
gs_plugin_app_install (GsPlugin *plugin, GsApp *app, GError **error)
{
	const gchar *package_id;
	const gchar *to_array[] = { NULL, NULL };
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	PkResults *results = NULL;

	package_id = gs_app_get_metadata_item (app, "package-id");
	if (package_id == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "installing not supported");
		goto out;
	}
	to_array[0] = package_id;
	results = pk_task_install_packages_sync (plugin->priv->task,
						 (gchar **) to_array,
						 plugin->cancellable,
						 NULL, NULL,
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
			     "failed to remove packages: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}
