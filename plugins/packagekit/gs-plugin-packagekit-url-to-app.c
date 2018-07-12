/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Canonical Ltd
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

struct GsPluginData {
	PkClient		*client;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->client = pk_client_new ();

	pk_client_set_background (priv->client, FALSE);
	pk_client_set_cache_age (priv->client, G_MAXUINT);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->client);
}

gboolean
gs_plugin_url_to_app (GsPlugin *plugin,
		      GsAppList *list,
		      const gchar *url,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	g_autofree gchar *scheme = NULL;
	g_autofree gchar *path = NULL;
	const gchar *id = NULL;
	const gchar * const *id_like = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;
	g_autoptr(GPtrArray) packages = NULL;
	g_autoptr(GPtrArray) details = NULL;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);

	path = gs_utils_get_url_path (url);

	/* only do this for apt:// on debian or debian-like distros */
	os_release = gs_os_release_new (error);
	if (os_release == NULL) {
		g_prefix_error (error, "failed to determine OS information:");
                return FALSE;
	} else  {
		id = gs_os_release_get_id (os_release);
		id_like = gs_os_release_get_id_like (os_release);
		scheme = gs_utils_get_url_scheme (url);
		if (!(g_strcmp0 (scheme, "apt") == 0 &&
		     (g_strcmp0 (id, "debian") == 0 ||
		      g_strv_contains (id_like, "debian")))) {
			return TRUE;
		}
	}

	app = gs_app_new (NULL);
	gs_app_add_source (app, path);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);

	package_ids = g_new0 (gchar *, 2);
	package_ids[0] = g_strdup (path);

	results = pk_client_resolve (priv->client,
				     pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_ARCH, -1),
				     package_ids,
				     cancellable,
				     gs_packagekit_helper_cb, helper,
				     error);

	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to resolve package_ids: ");
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	details = pk_results_get_details_array (results);

	if (packages->len >= 1) {
		if (gs_app_get_local_file (app) != NULL)
			return TRUE;

		gs_plugin_packagekit_resolve_packages_app (plugin, packages, app);
		gs_plugin_packagekit_refine_details_app (plugin, details, app);

		gs_app_list_add (list, app);
	} else {
		g_warning ("no results returned");
	}

	return TRUE;
}
