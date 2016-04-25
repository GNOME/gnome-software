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
	pk_task_set_only_download (priv->task, TRUE);
	pk_client_set_background (PK_CLIENT (priv->task), TRUE);
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
		g_auto(GStrv) package_ids = NULL;
		g_autoptr(PkPackageSack) sack = NULL;
		g_autoptr(PkResults) results2 = NULL;

		sack = pk_results_get_package_sack (results);
		if (pk_package_sack_get_size (sack) == 0)
			return TRUE;
		package_ids = pk_package_sack_get_ids (sack);
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
		results2 = pk_task_update_packages_sync (priv->task,
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
