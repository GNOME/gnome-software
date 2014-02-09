/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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
	return "moduleset-popular";
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
gs_plugin_startup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	gboolean ret;
	gchar *filename;

	/* Parse the XML */
	gs_profile_start (plugin->profile, "moduleset-popular::startup");
	filename = g_build_filename (DATADIR,
				     "gnome-software",
				     "popular-apps.xml",
				     NULL);
	ret = gs_moduleset_parse_filename (plugin->priv->moduleset,
					   filename,
					   error);
	if (!ret)
		goto out;
out:
	g_free (filename);
	gs_profile_stop (plugin->profile, "moduleset-popular::startup");
	return ret;
}

/**
 * gs_plugin_add_popular:
 */
gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsApp *app;
	gboolean ret = TRUE;
	gchar **apps = NULL;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, cancellable, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	if (g_getenv ("GNOME_SOFTWARE_POPULAR")) {
		apps = g_strsplit (g_getenv ("GNOME_SOFTWARE_POPULAR"), ",", 0);
	} else {
		apps = gs_moduleset_get_by_kind (plugin->priv->moduleset,
						 GS_MODULESET_MODULE_KIND_APPLICATION);
	}

	/* just add all */
	for (i = 0; apps[i]; i++) {
		app = gs_app_new (apps[i]);
		gs_plugin_add_app (list, app);
		g_object_unref (app);
	}
out:
	g_strfreev (apps);
	return ret;
}

/* vim: set noexpandtab: */
