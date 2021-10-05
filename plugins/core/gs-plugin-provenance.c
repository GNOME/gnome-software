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

/*
 * SECTION:
 * Sets the package provenance to TRUE if installed by an official
 * software source.
 */

struct GsPluginData {
	GSettings		*settings;
	GHashTable		*repos; /* gchar *name ~> NULL */
	GPtrArray		*wildcards; /* non-NULL, when have names with wildcards */
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
		/* The keys are stolen by the hash table, thus free only the array */
		g_autofree gchar **repos = NULL;
		g_hash_table_remove_all (priv->repos);
		g_clear_pointer (&priv->wildcards, g_ptr_array_unref);
		repos = gs_plugin_provenance_get_sources (plugin);
		for (guint ii = 0; repos && repos[ii]; ii++) {
			if (strchr (repos[ii], '*') ||
			    strchr (repos[ii], '?') ||
			    strchr (repos[ii], '[')) {
				if (priv->wildcards == NULL)
					priv->wildcards = g_ptr_array_new_with_free_func (g_free);
				g_ptr_array_add (priv->wildcards, g_steal_pointer (&(repos[ii])));
			} else {
				g_hash_table_insert (priv->repos, g_steal_pointer (&(repos[ii])), NULL);
			}
		}
		if (priv->wildcards != NULL)
			g_ptr_array_add (priv->wildcards, NULL);
	}
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->settings = g_settings_new ("org.gnome.software");
	priv->repos = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_signal_connect (priv->settings, "changed",
			  G_CALLBACK (gs_plugin_provenance_settings_changed_cb), plugin);
	gs_plugin_provenance_settings_changed_cb (priv->settings, "official-repos", plugin);

	/* after the package source is set */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "dummy");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "rpm-ostree");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_hash_table_unref (priv->repos);
	g_clear_pointer (&priv->wildcards, g_ptr_array_unref);
	g_object_unref (priv->settings);
}

static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GHashTable		 *repos,
	    GPtrArray		 *wildcards,
	    GCancellable         *cancellable,
	    GError              **error)
{
	const gchar *origin;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) == 0)
		return TRUE;
	if (gs_app_has_quirk (app, GS_APP_QUIRK_PROVENANCE))
		return TRUE;

	/* simple case */
	origin = gs_app_get_origin (app);
	if (origin != NULL && (g_hash_table_contains (repos, origin) ||
	    (wildcards != NULL && gs_utils_strv_fnmatch ((gchar **) wildcards->pdata, origin)))) {
		gs_app_add_quirk (app, GS_APP_QUIRK_PROVENANCE);
		return TRUE;
	}

	/* Software sources/repositories are represented as #GsApps too. Add the
	 * provenance quirk to the system-configured repositories (but not
	 * user-configured ones). */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_REPOSITORY &&
	    (g_hash_table_contains (repos, gs_app_get_id (app)) ||
	    (wildcards != NULL && gs_utils_strv_fnmatch ((gchar **) wildcards->pdata, gs_app_get_id (app))))) {
		if (gs_app_get_scope (app) != AS_COMPONENT_SCOPE_USER)
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
	if (g_hash_table_contains (repos, origin + 1) ||
	    (wildcards != NULL && gs_utils_strv_fnmatch ((gchar **) wildcards->pdata, origin + 1))) {
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
	g_autoptr(GHashTable) repos = NULL;
	g_autoptr(GPtrArray) wildcards = NULL;

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) == 0)
		return TRUE;
	repos = g_hash_table_ref (priv->repos);
	wildcards = priv->wildcards != NULL ? g_ptr_array_ref (priv->wildcards) : NULL;
	/* nothing to search */
	if (g_hash_table_size (repos) == 0)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (plugin, app, flags, repos, wildcards, cancellable, error))
			return FALSE;
	}

	return TRUE;
}
