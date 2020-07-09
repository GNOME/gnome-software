/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017 Canonical Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>
#include <gnome-software.h>

#include "gs-packagekit-helper.h"
#include "packagekit-common.h"

struct GsPluginData {
	PkClient		*client;
	GMutex			 client_mutex;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	g_mutex_init (&priv->client_mutex);
	priv->client = pk_client_new ();

	pk_client_set_background (priv->client, FALSE);
	pk_client_set_cache_age (priv->client, G_MAXUINT);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_mutex_clear (&priv->client_mutex);
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
	gs_plugin_packagekit_set_packaging_format (plugin, app);
	gs_app_add_source (app, path);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);

	package_ids = g_new0 (gchar *, 2);
	package_ids[0] = g_strdup (path);

	g_mutex_lock (&priv->client_mutex);
	results = pk_client_resolve (priv->client,
				     pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_ARCH, -1),
				     package_ids,
				     cancellable,
				     gs_packagekit_helper_cb, helper,
				     error);
	g_mutex_unlock (&priv->client_mutex);

	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to resolve package_ids: ");
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	details = pk_results_get_details_array (results);

	if (packages->len >= 1) {
		g_autoptr(GHashTable) details_collection = NULL;

		if (gs_app_get_local_file (app) != NULL)
			return TRUE;

		details_collection = gs_plugin_packagekit_details_array_to_hash (details);

		gs_plugin_packagekit_resolve_packages_app (plugin, packages, app);
		gs_plugin_packagekit_refine_details_app (plugin, details_collection, app);

		gs_app_list_add (list, app);
	} else {
		g_warning ("no results returned");
	}

	return TRUE;
}
