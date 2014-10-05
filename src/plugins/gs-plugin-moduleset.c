/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include <glib/gi18n.h>

#include <gs-plugin.h>
#include <gs-category.h>

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
	return "moduleset";
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",		/* requires id */
		"packagekit",		/* pkgname */
		NULL };
	return deps;
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
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GError **error)
{
	gboolean ret;

	/* Parse the XML */
	gs_profile_start (plugin->profile, "moduleset::startup");
	ret = gs_moduleset_parse_path (plugin->priv->moduleset,
				       GS_MODULESETDIR,
				       error);
	if (!ret)
		goto out;
out:
	gs_profile_stop (plugin->profile, "moduleset::startup");
	return ret;
}

/**
 * gs_plugin_add_popular:
 */
gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GList **list,
		       const gchar *category,
		       const gchar *category_exclude,
		       GCancellable *cancellable,
		       GError **error)
{
	GsApp *app;
	gboolean ret = TRUE;
	gchar **apps = NULL;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	if (g_getenv ("GNOME_SOFTWARE_POPULAR")) {
		apps = g_strsplit (g_getenv ("GNOME_SOFTWARE_POPULAR"), ",", 0);
	} else {
		apps = gs_moduleset_get_modules (plugin->priv->moduleset,
						 GS_MODULESET_MODULE_KIND_APPLICATION,
						 "popular",
						 category);
	}

	/* just add all */
	for (i = 0; apps[i]; i++) {
		app = gs_app_new (apps[i]);
		gs_plugin_add_app (list, app);
		gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
		g_object_unref (app);
	}
out:
	g_strfreev (apps);
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
	gchar **apps = NULL;
	gchar **pkgs = NULL;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	/* just mark each one as core */
	apps = gs_moduleset_get_modules (plugin->priv->moduleset,
					 GS_MODULESET_MODULE_KIND_APPLICATION,
					 "system",
					 NULL);
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		for (i = 0; apps[i] != NULL; i++) {
			if (g_strcmp0 (apps[i], gs_app_get_id (app)) == 0) {
				gs_app_set_kind (app, GS_APP_KIND_SYSTEM);
				break;
			}
		}
	}

	/* just mark each one as core */
	pkgs = gs_moduleset_get_modules (plugin->priv->moduleset,
					 GS_MODULESET_MODULE_KIND_PACKAGE,
					 "core",
					 NULL);
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
	g_strfreev (apps);
	g_strfreev (pkgs);
	return ret;
}

/* vim: set noexpandtab: */
