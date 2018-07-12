/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
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

#include <packagekit-glib2/packagekit.h>
#include <gnome-software.h>

#include "gs-packagekit-helper.h"
#include "packagekit-common.h"

/*
 * SECTION:
 * Uses the system PackageKit instance to return convert repo filenames to
 * package-ids.
 *
 * Requires:    | [repos::repo-filename]
 * Refines:     | [source-id]
 */

struct GsPluginData {
	PkClient	*client;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->client = pk_client_new ();
	pk_client_set_background (priv->client, FALSE);
	pk_client_set_cache_age (priv->client, G_MAXUINT);

	/* need repos::repo-filename */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "repos");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->client);
}

static gboolean
gs_plugin_packagekit_refine_repo_from_filename (GsPlugin *plugin,
                                                GsApp *app,
                                                const gchar *filename,
                                                GCancellable *cancellable,
                                                GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *to_array[] = { NULL, NULL };
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) packages = NULL;

	to_array[0] = filename;
	gs_packagekit_helper_add_app (helper, app);
	results = pk_client_search_files (priv->client,
	                                  pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
	                                  (gchar **) to_array,
	                                  cancellable,
	                                  gs_packagekit_helper_cb, helper,
	                                  error);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to search file %s: ", filename);
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (packages->len == 1) {
		PkPackage *package = g_ptr_array_index (packages, 0);
		gs_app_add_source_id (app, pk_package_get_id (package));
	} else {
		g_debug ("failed to find one package for repo %s, %s, [%u]",
		         gs_app_get_id (app), filename, packages->len);
	}
	return TRUE;
}

gboolean
gs_plugin_refine (GsPlugin *plugin,
                  GsAppList *list,
                  GsPluginRefineFlags flags,
                  GCancellable *cancellable,
                  GError **error)
{
	g_autoptr(AsProfileTask) ptask = NULL;

	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
	                                  "packagekit-refine-repos[repo-filename->id]");
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		const gchar *fn;
		if (gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX))
			continue;
		if (gs_app_get_kind (app) != AS_APP_KIND_SOURCE)
			continue;
		if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
			continue;
		fn = gs_app_get_metadata_item (app, "repos::repo-filename");
		if (fn == NULL)
			continue;
		/* set the source package name for an installed .repo file */
		if (!gs_plugin_packagekit_refine_repo_from_filename (plugin,
		                                                     app,
		                                                     fn,
		                                                     cancellable,
		                                                     error))
			return FALSE;
	}

	/* success */
	return TRUE;
}
