/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Matthias Klumpp <mak@debian.org>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
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
	return g_settings_get_strv (priv->settings, "free-repos");
}

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
		url = g_settings_get_string (priv->settings, "free-repos-url");
		if (url == NULL)
			return g_strdup ("LicenseRef-free");
	}
	return g_strdup_printf ("LicenseRef-free=%s", url);
}

static void
gs_plugin_provenance_license_changed_cb (GSettings *settings,
					 const gchar *key,
					 GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (g_strcmp0 (key, "free-repos") == 0) {
		g_strfreev (priv->sources);
		priv->sources = gs_plugin_provenance_license_get_sources (plugin);
	}
	if (g_strcmp0 (key, "free-repos-url") == 0) {
		g_free (priv->license_id);
		priv->license_id = gs_plugin_provenance_license_get_id (plugin);
	}
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->settings = g_settings_new ("org.gnome.software");
	g_signal_connect (priv->settings, "changed",
			  G_CALLBACK (gs_plugin_provenance_license_changed_cb), plugin);
	priv->sources = gs_plugin_provenance_license_get_sources (plugin);
	priv->license_id = gs_plugin_provenance_license_get_id (plugin);

	/* need this set */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "provenance");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_strfreev (priv->sources);
	g_free (priv->license_id);
	g_object_unref (priv->settings);
}

static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GCancellable         *cancellable,
	    GError              **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *origin;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) == 0)
		return TRUE;

	/* no provenance */
	if (!gs_app_has_quirk (app, GS_APP_QUIRK_PROVENANCE))
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

gboolean
gs_plugin_refine (GsPlugin             *plugin,
		  GsAppList            *list,
		  GsPluginRefineFlags   flags,
		  GCancellable         *cancellable,
		  GError              **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) == 0)
		return TRUE;
	/* nothing to search */
	if (priv->sources == NULL || priv->sources[0] == NULL)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (plugin, app, flags, cancellable, error))
			return FALSE;
	}

	return TRUE;
}
