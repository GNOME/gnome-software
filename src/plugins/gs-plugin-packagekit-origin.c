/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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
#include <gnome-software.h>

#include "packagekit-common.h"

/*
 * SECTION:
 * Uses the system PackageKit instance to return convert short origins like
 * 'fedora-updates' into longer summaries for the UI.
 *
 * Requires:    | [origin]
 * Refines:     | [origin-ui]
 */

struct GsPluginData {
	PkClient		*client;
	GHashTable		*sources;
};

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->client = pk_client_new ();
	pk_client_set_background (priv->client, FALSE);
	pk_client_set_interactive (priv->client, FALSE);
	pk_client_set_cache_age (priv->client, G_MAXUINT);
	priv->sources = g_hash_table_new_full (g_str_hash,
						       g_str_equal,
						       g_free,
						       g_free);

	/* need origin */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit-refine");
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_hash_table_unref (priv->sources);
	g_object_unref (priv->client);
}

/**
 * gs_plugin_packagekit_origin_ensure_sources:
 **/
static gboolean
gs_plugin_packagekit_origin_ensure_sources (GsPlugin *plugin,
					    GCancellable *cancellable,
					    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	PkRepoDetail *rd;
	guint i;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* already done */
	if (g_hash_table_size (priv->sources) > 0)
		return TRUE;

	/* ask PK for the repo details */
	results = pk_client_get_repo_list (priv->client,
					   pk_bitfield_from_enums (PK_FILTER_ENUM_NONE, -1),
					   cancellable,
					   NULL, plugin,
					   error);
	if (!gs_plugin_packagekit_results_valid (results, error))
		return FALSE;
	array = pk_results_get_repo_detail_array (results);
	for (i = 0; i < array->len; i++) {
		rd = g_ptr_array_index (array, i);
		g_hash_table_insert (priv->sources,
				     g_strdup (pk_repo_detail_get_id (rd)),
				     g_strdup (pk_repo_detail_get_description (rd)));
	}
	return TRUE;
}

/**
 * gs_plugin_refine_app:
 */
gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *origin_id;
	const gchar *origin_ui;

	/* only run when required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) == 0)
		return TRUE;

	if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
		return TRUE;
	if (gs_app_get_origin (app) == NULL)
		return TRUE;
	if (gs_app_get_origin_ui (app) != NULL)
		return TRUE;

	/* this is for libsolv */
	origin_id = gs_app_get_origin (app);
	if (g_strcmp0 (origin_id, "@commandline") == 0) {
		gs_app_set_origin_ui (app, "User");
		return TRUE;
	}

	/* this is fedora specific */
	if (g_str_has_prefix (origin_id, "koji-override-")) {
		gs_app_set_origin_ui (app, "Koji");
		return TRUE;
	}

	/* ensure set up */
	if (!gs_plugin_packagekit_origin_ensure_sources (plugin, cancellable, error))
		return FALSE;

	/* set new value */
	origin_ui = g_hash_table_lookup (priv->sources, origin_id);
	if (origin_ui != NULL)
		gs_app_set_origin_ui (app, origin_ui);
	return TRUE;
}
