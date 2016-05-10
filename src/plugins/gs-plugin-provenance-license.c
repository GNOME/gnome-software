/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Matthias Klumpp <mak@debian.org>
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

/*
 * SECTION:
 * Marks the application as Free Software if it comes from an origin
 * that is recognized as being DFSGish-free.
 */

struct GsPluginData {
	GSettings		*settings;
	gchar			**sources;
	gchar			*license_id;
};

/**
 * gs_plugin_provenance_license_get_sources:
 */
static gchar **
gs_plugin_provenance_license_get_sources (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *tmp;

	tmp = g_getenv ("GS_SELF_TEST_PROVENANCE_LICENSE_SOURCES");
	if (tmp != NULL) {
		g_debug ("using custom provenance_license sources of %s", tmp);
		return g_strsplit (tmp, ",", -1);
	}
	return g_settings_get_strv (priv->settings, "free-sources");
}

/**
 * gs_plugin_provenance_license_get_id:
 */
static gchar *
gs_plugin_provenance_license_get_id (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *tmp;
	g_autofree gchar *url = NULL;

	tmp = g_getenv ("GS_SELF_TEST_PROVENANCE_LICENSE_URL");
	if (tmp != NULL) {
		g_debug ("using custom license generic sources of %s", tmp);
		url = g_strdup (tmp);
	} else {
		url = g_settings_get_string (priv->settings, "free-sources-url");
		if (url == NULL)
			return g_strdup ("LicenseRef-free");
	}
	return g_strdup_printf ("LicenseRef-free=%s", url);
}

/**
 * gs_plugin_provenance_license_changed_cb:
 */
static void
gs_plugin_provenance_license_changed_cb (GSettings *settings,
					 const gchar *key,
					 GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (g_strcmp0 (key, "free-sources") == 0) {
		g_strfreev (priv->sources);
		priv->sources = gs_plugin_provenance_license_get_sources (plugin);
	}
	if (g_strcmp0 (key, "free-sources-url") == 0) {
		g_free (priv->license_id);
		priv->license_id = gs_plugin_provenance_license_get_id (plugin);
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
			  G_CALLBACK (gs_plugin_provenance_license_changed_cb), plugin);
	priv->sources = gs_plugin_provenance_license_get_sources (plugin);
	priv->license_id = gs_plugin_provenance_license_get_id (plugin);
}

/**
 * gs_plugin_order_after:
 */
const gchar **
gs_plugin_order_after (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"provenance",
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
	g_free (priv->license_id);
	g_object_unref (priv->settings);
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

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) == 0)
		return TRUE;

	/* no provenance */
	if (!gs_app_has_quirk (app, AS_APP_QUIRK_PROVENANCE))
		return TRUE;

	/* nothing to search */
	if (priv->sources == NULL || priv->sources[0] == NULL)
		return TRUE;

	/* simple case */
	origin = gs_app_get_origin (app);
	if (origin != NULL && gs_utils_strv_fnmatch (priv->sources, origin))
		gs_app_set_license (app, GS_APP_QUALITY_NORMAL, priv->license_id);

	return TRUE;
}
