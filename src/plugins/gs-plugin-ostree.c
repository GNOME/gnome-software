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

#include <ostree.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <gs-plugin.h>

#include "gs-utils.h"

struct GsPluginPrivate {
	OstreeRepo		*ostree_repo;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "ostree";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);

	/* only works on OSTree */
	if (!g_file_test ("/run/ostree-booted", G_FILE_TEST_EXISTS)) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}
}

/**
 * gs_plugin_get_conflicts:
 */
const gchar **
gs_plugin_get_conflicts (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"packagekit",
		"packagekit-history",
		"packagekit-offline",
		"packagekit-origin",
		"packagekit-proxy",
		"packagekit-refine",
		"packagekit-refresh",
		"systemd-updates",
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	if (plugin->priv->ostree_repo != NULL)
		g_object_unref (plugin->priv->ostree_repo);
}

/**
 * gs_plugin_setup:
 */
gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	/* already started */
	if (plugin->priv->ostree_repo != NULL)
		return TRUE;

	/* open */
	plugin->priv->ostree_repo = ostree_repo_new_default ();
	if (!ostree_repo_open (plugin->priv->ostree_repo, cancellable, error))
		return FALSE;
	return TRUE;
}

/**
 * gs_plugin_add_sources:
 */
gboolean
gs_plugin_add_sources (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	guint i;
	g_auto(GStrv) names = NULL;

	/* get all remotes */
	names = ostree_repo_remote_list (plugin->priv->ostree_repo, NULL);
	if (names == NULL)
		return TRUE;
	for (i = 0; names[i] != NULL; i++) {
		g_autofree gchar *url = NULL;
		g_autoptr(GsApp) app = NULL;

		/* get info */
		if (!ostree_repo_remote_get_url (plugin->priv->ostree_repo,
						 names[i], &url, error))
			return FALSE;

		/* create app */
		app = gs_app_new (names[i]);
		gs_app_set_management_plugin (app, plugin->name);
		gs_app_set_kind (app, AS_APP_KIND_SOURCE);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, url);
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST, names[i]);
	}

	return TRUE;
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
	return TRUE;
}
