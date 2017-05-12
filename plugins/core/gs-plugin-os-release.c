/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <gnome-software.h>

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* we might change the app-id */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "appstream");
	gs_plugin_add_flags (plugin, GS_PLUGIN_FLAGS_GLOBAL_CACHE);
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *cpe_name;
	const gchar *home_url;
	const gchar *name;
	const gchar *version;
	g_autoptr(GsOsRelease) os_release = NULL;

	/* match meta-id */
	if (g_strcmp0 (gs_app_get_id (app), "system") != 0)
		return TRUE;

	/* avoid setting again */
	if (gs_app_get_metadata_item (app, "GnomeSoftware::CpeName") != NULL)
		return TRUE;

	/* hardcoded */
	gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);

	/* get visible data */
	os_release = gs_os_release_new (error);
	if (os_release == NULL)
		return FALSE;
	cpe_name = gs_os_release_get_cpe_name (os_release);
	if (cpe_name != NULL)
		gs_app_set_metadata (app, "GnomeSoftware::CpeName", cpe_name);
	name = gs_os_release_get_name (os_release);
	if (name != NULL)
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST, name);
	version = gs_os_release_get_version_id (os_release);
	if (version != NULL)
		gs_app_set_version (app, version);

	/* use libsoup to convert a URL */
	home_url = gs_os_release_get_home_url (os_release);
	if (home_url != NULL) {
		g_autoptr(SoupURI) uri = NULL;

		/* homepage */
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, home_url);

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
				/* set the new ID and update the cache */
				gs_app_set_id (app, id);
				gs_plugin_cache_add (plugin, NULL, app);
			}
		}
	}

	/* success */
	return TRUE;
}
