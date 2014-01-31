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
	PkClient		*client;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "packagekit-search";
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
	pk_client_set_background (plugin->priv->client, FALSE);
	pk_client_set_interactive (plugin->priv->client, FALSE);
	pk_client_set_cache_age (plugin->priv->client, G_MAXUINT);

	/* at the moment we don't need this; or at least I can't think of
	 * a reason *right now* :) */
	gs_plugin_set_enabled (plugin, FALSE);
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
				  "packagekit-search::transaction");
	} else if (status == PK_STATUS_ENUM_FINISHED) {
		gs_profile_stop (plugin->profile,
				 "packagekit-search::transaction");
	}

	plugin_status = packagekit_status_enum_to_plugin_status (status);
	if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
		gs_plugin_status_update (plugin, NULL, plugin_status);
}

/**
 * gs_plugin_add_search:
 */
gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GList **list,
		      GCancellable *cancellable,
		      GError **error)
{
	gboolean ret = TRUE;
	PkBitfield filter;
	PkResults *results;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* do sync call */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 PK_FILTER_ENUM_NOT_COLLECTIONS,
					 -1);
	results = pk_client_search_details (plugin->priv->client,
					    filter,
					    values,
					    cancellable,
					    gs_plugin_packagekit_progress_cb, plugin,
					    error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* add results */
	ret = gs_plugin_packagekit_add_results (plugin, list, results, error);
	if (!ret)
		goto out;
out:
	if (results != NULL)
		g_object_unref (results);
	return ret;
}
