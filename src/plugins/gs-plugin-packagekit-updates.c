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
	return "packagekit-updates";
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
	plugin_status = packagekit_status_enum_to_plugin_status (status);
	if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
		gs_plugin_status_update (plugin, NULL, plugin_status);
}

/**
 * gs_plugin_add_updates:
 */
gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
	GsApp *app;
	guint i;
	PkBitfield filter;
	PkPackage *pkg;
	PkResults *results = NULL;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* do sync call */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_DOWNLOADED,
					 -1);
	results = pk_client_get_updates (PK_CLIENT (plugin->priv->task),
					 filter,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, plugin,
					 error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* add results */
	array = pk_results_get_package_array (results);
	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		app = gs_app_new (NULL);
		gs_app_set_source (app, pk_package_get_name (pkg));
		gs_app_set_update_version (app, pk_package_get_version (pkg));
		gs_app_set_management_plugin (app, "PackageKit");
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
		gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
		gs_plugin_add_app (list, app);
	}
out:
	if (results != NULL)
		g_object_unref (results);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}
