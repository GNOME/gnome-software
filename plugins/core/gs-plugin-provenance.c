/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <gnome-software.h>
#include "gs-fedora-third-party.h"

/*
 * SECTION:
 * Sets the package provenance to TRUE if installed by an official
 * software source.
 */

struct GsPluginData {
	GSettings		*settings;
	gchar			**sources;
	GsFedoraThirdParty	*third_party;
};

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
	return g_settings_get_strv (priv->settings, "official-repos");
}

static void
gs_plugin_provenance_settings_changed_cb (GSettings *settings,
					  const gchar *key,
					  GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (g_strcmp0 (key, "official-repos") == 0) {
		g_strfreev (priv->sources);
		priv->sources = gs_plugin_provenance_get_sources (plugin);
	}
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->settings = g_settings_new ("org.gnome.software");
	g_signal_connect (priv->settings, "changed",
			  G_CALLBACK (gs_plugin_provenance_settings_changed_cb), plugin);
	priv->sources = gs_plugin_provenance_get_sources (plugin);
	priv->third_party = gs_fedora_third_party_new ();

	/* after the package source is set */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "dummy");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "rpm-ostree");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_strfreev (priv->sources);
	g_object_unref (priv->settings);
	g_clear_object (&priv->third_party);
}

static gboolean
is_fedora_third_party_source (GHashTable *third_party_repos,
			      GsApp *app,
			      const gchar *origin)
{
	if (origin == NULL || gs_app_get_scope (app) == AS_COMPONENT_SCOPE_USER)
		return FALSE;

	return gs_fedora_third_party_util_is_third_party_repo (third_party_repos,
							       origin,
							       gs_app_get_management_plugin (app));
}

static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GHashTable		 *third_party_repos,
	    GCancellable         *cancellable,
	    GError              **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *origin;
	gchar **sources;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) == 0)
		return TRUE;
	if (gs_app_has_quirk (app, GS_APP_QUIRK_PROVENANCE))
		return TRUE;

	sources = priv->sources;
	gs_app_remove_quirk (app, GS_APP_QUIRK_DISTRO_SAFE);

	/* simple case */
	origin = gs_app_get_origin (app);
	if (is_fedora_third_party_source (third_party_repos, app, origin)) {
		gs_app_add_quirk (app, GS_APP_QUIRK_DISTRO_SAFE);
		return TRUE;
	} else if (origin != NULL && sources != NULL && gs_utils_strv_fnmatch (sources, origin)) {
		gs_app_add_quirk (app, GS_APP_QUIRK_PROVENANCE);
		return TRUE;
	}

	/* Software sources/repositories are represented as #GsApps too. Add the
	 * provenance quirk to the system-configured repositories (but not
	 * user-configured ones). */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_REPOSITORY &&
	    sources != NULL && gs_utils_strv_fnmatch (sources, gs_app_get_id (app))) {
		if (gs_app_get_scope (app) != AS_COMPONENT_SCOPE_USER &&
		    !is_fedora_third_party_source (third_party_repos, app, gs_app_get_id (app)))
			gs_app_add_quirk (app, GS_APP_QUIRK_PROVENANCE);
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
	if (is_fedora_third_party_source (third_party_repos, app, origin + 1)) {
		gs_app_add_quirk (app, GS_APP_QUIRK_DISTRO_SAFE);
		return TRUE;
	} else if (sources != NULL && gs_utils_strv_fnmatch (sources, origin + 1)) {
		gs_app_add_quirk (app, GS_APP_QUIRK_PROVENANCE);
		return TRUE;
	}
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
	g_autoptr(GHashTable) third_party_repos = NULL;
	g_autoptr(GError) local_error = NULL;

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) == 0)
		return TRUE;

	if (!gs_fedora_third_party_list_sync (priv->third_party, &third_party_repos, cancellable, &local_error)) {
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_propagate_error (error, local_error);
			return FALSE;
		}
		if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			g_debug ("Failed to get fedora-third-party repos: %s", local_error->message);
	}

	/* nothing to search */
	if ((priv->sources == NULL || priv->sources[0] == NULL) && third_party_repos == NULL)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (plugin, app, flags, third_party_repos, cancellable, error))
			return FALSE;
	}

	return TRUE;
}
