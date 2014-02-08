/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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

#include <gs-plugin.h>

#include "gs-moduleset.h"

struct GsPluginPrivate {
	GsModuleset		*moduleset;
	gsize			 done_init;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "moduleset-core";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->moduleset = gs_moduleset_new ();
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->moduleset);
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"packagekit",		/* pkgname */
		NULL };
	return deps;
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	gboolean ret;
	gchar *filename;

	/* Parse the XML */
	gs_profile_start (plugin->profile, "moduleset-core::startup");
	filename = g_build_filename (DATADIR,
				     "gnome-software",
				     "moduleset-core.xml",
				     NULL);
	ret = gs_moduleset_parse_filename (plugin->priv->moduleset,
					   filename,
					   error);
	if (!ret)
		goto out;
out:
	g_free (filename);
	gs_profile_stop (plugin->profile, "moduleset-core::startup");
	return ret;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList **list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	GList *l;
	GsApp *app;
	gboolean ret = TRUE;
	gchar **pkgs = NULL;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* just mark each one as core */
	pkgs = gs_moduleset_get_by_kind (plugin->priv->moduleset,
					 GS_MODULESET_MODULE_KIND_PACKAGE);
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		for (i = 0; pkgs[i] != NULL; i++) {
			if (g_strcmp0 (pkgs[i], gs_app_get_source_default (app)) == 0) {
				gs_app_set_kind (app, GS_APP_KIND_CORE);
				break;
			}
		}
	}
out:
	g_strfreev (pkgs);
	return ret;
}
