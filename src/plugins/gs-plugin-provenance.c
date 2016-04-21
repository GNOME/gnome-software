/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
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

#include <fnmatch.h>

#include <gs-plugin.h>

/*
 * SECTION:
 * Sets the package provanance to TRUE if installed by an official
 * software source.
 */

struct GsPluginData {
	GSettings		*settings;
	gchar			**sources;
};

/**
 * gs_plugin_provenance_get_sources:
 */
static gchar **
gs_plugin_provenance_get_sources (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *tmp;
	tmp = g_getenv ("GS_SELF_TEST_PROVENANCE_SOURCES");
	if (tmp != NULL) {
		g_debug ("using custom provenance sources of %s", tmp);
		return g_strsplit (tmp, ",", -1);
	}
	return g_settings_get_strv (priv->settings, "official-sources");
}

/**
 * gs_plugin_provenance_settings_changed_cb:
 */
static void
gs_plugin_provenance_settings_changed_cb (GSettings *settings,
					  const gchar *key,
					  GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (g_strcmp0 (key, "official-sources") == 0) {
		g_strfreev (priv->sources);
		priv->sources = gs_plugin_provenance_get_sources (plugin);
	}
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->settings = g_settings_new ("org.gnome.software");
	g_signal_connect (priv->settings, "changed",
			  G_CALLBACK (gs_plugin_provenance_settings_changed_cb), plugin);
	priv->sources = gs_plugin_provenance_get_sources (plugin);
}

/**
 * gs_plugin_order_after:
 */
const gchar **
gs_plugin_order_after (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"packagekit-refine",	/* after the package source is set */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_strfreev (priv->sources);
	g_object_unref (priv->settings);
}

/**
 * gs_utils_strv_fnmatch:
 */
static gboolean
gs_utils_strv_fnmatch (gchar **strv, const gchar *str)
{
	guint i;

	/* empty */
	if (strv == NULL)
		return FALSE;

	/* look at each one */
	for (i = 0; strv[i] != NULL; i++) {
		if (fnmatch (strv[i], str, 0) == 0)
			return TRUE;
	}
	return FALSE;
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
	const gchar *origin;
	gchar **sources;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) == 0)
		return TRUE;
	if (gs_app_has_quirk (app, AS_APP_QUIRK_PROVENANCE))
		return TRUE;

	/* nothing to search */
	sources = priv->sources;
	if (sources == NULL || sources[0] == NULL) {
		gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
		return TRUE;
	}

	/* simple case */
	origin = gs_app_get_origin (app);
	if (origin != NULL && gs_utils_strv_fnmatch (sources, origin)) {
		gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
		return TRUE;
	}

	/* this only works for packages */
	origin = gs_app_get_source_id_default (app);
	if (origin == NULL)
		return TRUE;
	origin = g_strrstr (origin, ";");
	if (origin == NULL)
		return TRUE;
	if (g_str_has_prefix (origin + 1, "installed:"))
		origin += 10;
	if (gs_utils_strv_fnmatch (sources, origin + 1)) {
		gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
		return TRUE;
	}
	return TRUE;
}
