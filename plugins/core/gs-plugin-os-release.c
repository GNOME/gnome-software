/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <gnome-software.h>

struct GsPluginData {
	GsApp			*app_system;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->app_system = gs_app_new ("system");
	gs_app_set_kind (priv->app_system, AS_APP_KIND_OS_UPGRADE);
	gs_app_set_state (priv->app_system, AS_APP_STATE_INSTALLED);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->app_system);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *cpe_name;
	const gchar *home_url;
	const gchar *name;
	const gchar *version;
	g_autoptr(GsOsRelease) os_release = NULL;

	/* parse os-release, wherever it may be */
	os_release = gs_os_release_new (error);
	if (os_release == NULL)
		return FALSE;
	cpe_name = gs_os_release_get_cpe_name (os_release);
	if (cpe_name != NULL)
		gs_app_set_metadata (priv->app_system, "GnomeSoftware::CpeName", cpe_name);
	name = gs_os_release_get_name (os_release);
	if (name != NULL)
		gs_app_set_name (priv->app_system, GS_APP_QUALITY_LOWEST, name);
	version = gs_os_release_get_version_id (os_release);
	if (version != NULL)
		gs_app_set_version (priv->app_system, version);

	/* use libsoup to convert a URL */
	home_url = gs_os_release_get_home_url (os_release);
	if (home_url != NULL) {
		g_autoptr(SoupURI) uri = NULL;

		/* homepage */
		gs_app_set_url (priv->app_system, AS_URL_KIND_HOMEPAGE, home_url);

		/* build ID from the reverse-DNS URL and the name version */
		uri = soup_uri_new (home_url);
		if (uri != NULL) {
			g_auto(GStrv) split = NULL;
			const gchar *home_host = soup_uri_get_host (uri);
			split = g_strsplit_set (home_host, ".", -1);
			if (g_strv_length (split) >= 2) {
				g_autofree gchar *id = NULL;
				id = g_strdup_printf ("%s.%s.%s-%s",
						      split[1],
						      split[0],
						      name,
						      version);
				gs_app_set_id (priv->app_system, id);
			}
		}
	}

	/* success */
	return TRUE;
}

gboolean
gs_plugin_refine_wildcard (GsPlugin *plugin,
			   GsApp *app,
			   GsAppList *list,
			   GsPluginRefineFlags flags,
			   GCancellable *cancellable,
			   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* match meta-id */
	if (g_strcmp0 (gs_app_get_id (app), "system") == 0) {
		/* copy over interesting metadata */
		if (gs_app_get_install_date (app) != 0 &&
		    gs_app_get_install_date (priv->app_system) == 0) {
			gs_app_set_install_date (priv->app_system,
			                         gs_app_get_install_date (app));
		}

		gs_app_list_add (list, priv->app_system);
		return TRUE;
	}

	/* success */
	return TRUE;
}
