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
 * software source. Also sets compulsory quirk when a required repository.
 */

struct GsPluginData {
	GSettings		*settings;
	GHashTable		*repos; /* gchar *name ~> guint flags */
	GPtrArray		*provenance_wildcards; /* non-NULL, when have names with wildcards */
	GPtrArray		*compulsory_wildcards; /* non-NULL, when have names with wildcards */
};

static GHashTable *
gs_plugin_provenance_remove_by_flag (GHashTable *old_repos,
				     GsAppQuirk quirk)
{
	GHashTable *new_repos = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init (&iter, old_repos);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		guint flags = GPOINTER_TO_UINT (value);
		flags = flags & (~quirk);
		if (flags != 0)
			g_hash_table_insert (new_repos, g_strdup (key), GUINT_TO_POINTER (flags));
	}
	return new_repos;
}

static void
gs_plugin_provenance_add_quirks (GsApp *app,
				 guint quirks)
{
	GsAppQuirk array[] = {
		GS_APP_QUIRK_PROVENANCE,
		GS_APP_QUIRK_COMPULSORY
	};
	for (guint ii = 0; ii < G_N_ELEMENTS (array); ii++) {
		if ((quirks & array[ii]) != 0)
			gs_app_add_quirk (app, array[ii]);
	}
}

static gchar **
gs_plugin_provenance_get_sources (GsPlugin *plugin,
				  const gchar *key)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *tmp;
	tmp = g_getenv ("GS_SELF_TEST_PROVENANCE_SOURCES");
	if (tmp != NULL) {
		if (g_strcmp0 (key, "required-repos") == 0)
			return NULL;
		g_debug ("using custom provenance sources of %s", tmp);
		return g_strsplit (tmp, ",", -1);
	}
	return g_settings_get_strv (priv->settings, key);
}

static void
gs_plugin_provenance_settings_changed_cb (GSettings *settings,
					  const gchar *key,
					  GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsAppQuirk quirk = GS_APP_QUIRK_NONE;
	GPtrArray **pwildcards = NULL;

	if (g_strcmp0 (key, "official-repos") == 0) {
		quirk = GS_APP_QUIRK_PROVENANCE;
		pwildcards = &priv->provenance_wildcards;
	} else if (g_strcmp0 (key, "required-repos") == 0) {
		quirk = GS_APP_QUIRK_COMPULSORY;
		pwildcards = &priv->compulsory_wildcards;
	}

	if (quirk != GS_APP_QUIRK_NONE) {
		/* The keys are stolen by the hash table, thus free only the array */
		g_autofree gchar **repos = NULL;
		g_autoptr(GHashTable) old_repos = priv->repos;
		g_autoptr(GPtrArray) old_wildcards = *pwildcards;
		GHashTable *new_repos = gs_plugin_provenance_remove_by_flag (old_repos, quirk);
		GPtrArray *new_wildcards = NULL;
		repos = gs_plugin_provenance_get_sources (plugin, key);
		for (guint ii = 0; repos && repos[ii]; ii++) {
			gchar *repo = g_steal_pointer (&(repos[ii]));
			if (strchr (repo, '*') ||
			    strchr (repo, '?') ||
			    strchr (repo, '[')) {
				if (new_wildcards == NULL)
					new_wildcards = g_ptr_array_new_with_free_func (g_free);
				g_ptr_array_add (new_wildcards, repo);
			} else {
				g_hash_table_insert (new_repos, repo,
					GUINT_TO_POINTER (quirk |
					GPOINTER_TO_UINT (g_hash_table_lookup (new_repos, repo))));
			}
		}
		if (new_wildcards != NULL)
			g_ptr_array_add (new_wildcards, NULL);
		priv->repos = new_repos;
		*pwildcards = new_wildcards;
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
	gs_plugin_provenance_settings_changed_cb (priv->settings, "required-repos", plugin);

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
	g_clear_pointer (&priv->provenance_wildcards, g_ptr_array_unref);
	g_clear_pointer (&priv->compulsory_wildcards, g_ptr_array_unref);
	g_object_unref (priv->settings);
}

static gboolean
gs_plugin_provenance_find_repo_flags (GHashTable *repos,
				      GPtrArray *provenance_wildcards,
				      GPtrArray *compulsory_wildcards,
				      const gchar *repo,
				      guint *out_flags)
{
	if (repo == NULL || *repo == '\0')
		return FALSE;
	*out_flags = GPOINTER_TO_UINT (g_hash_table_lookup (repos, repo));
	if (provenance_wildcards != NULL &&
	    gs_utils_strv_fnmatch ((gchar **) provenance_wildcards->pdata, repo))
		*out_flags |= GS_APP_QUIRK_PROVENANCE;
	if (compulsory_wildcards != NULL &&
	    gs_utils_strv_fnmatch ((gchar **) compulsory_wildcards->pdata, repo))
		*out_flags |= GS_APP_QUIRK_COMPULSORY;
	return *out_flags != 0;
}

static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GHashTable		 *repos,
	    GPtrArray		 *provenance_wildcards,
	    GPtrArray		 *compulsory_wildcards,
	    GCancellable         *cancellable,
	    GError              **error)
{
	const gchar *origin;
	guint quirks;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) == 0)
		return TRUE;
	if (gs_app_has_quirk (app, GS_APP_QUIRK_PROVENANCE))
		return TRUE;

	/* simple case */
	origin = gs_app_get_origin (app);
	if (gs_plugin_provenance_find_repo_flags (repos, provenance_wildcards, compulsory_wildcards, origin, &quirks)) {
		gs_plugin_provenance_add_quirks (app, quirks);
		return TRUE;
	}

	/* Software sources/repositories are represented as #GsApps too. Add the
	 * provenance quirk to the system-configured repositories (but not
	 * user-configured ones). */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_REPOSITORY &&
	    gs_plugin_provenance_find_repo_flags (repos, provenance_wildcards, compulsory_wildcards, gs_app_get_id (app), &quirks)) {
		if (gs_app_get_scope (app) != AS_COMPONENT_SCOPE_USER)
			gs_plugin_provenance_add_quirks (app, quirks);
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
	if (gs_plugin_provenance_find_repo_flags (repos, provenance_wildcards, compulsory_wildcards, origin + 1, &quirks))
		gs_plugin_provenance_add_quirks (app, quirks);

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
	g_autoptr(GPtrArray) provenance_wildcards = NULL;
	g_autoptr(GPtrArray) compulsory_wildcards = NULL;

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) == 0)
		return TRUE;
	repos = g_hash_table_ref (priv->repos);
	provenance_wildcards = priv->provenance_wildcards != NULL ? g_ptr_array_ref (priv->provenance_wildcards) : NULL;
	compulsory_wildcards = priv->compulsory_wildcards != NULL ? g_ptr_array_ref (priv->compulsory_wildcards) : NULL;
	/* nothing to search */
	if (g_hash_table_size (repos) == 0 && provenance_wildcards == NULL && compulsory_wildcards == NULL)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (plugin, app, flags, repos, provenance_wildcards, compulsory_wildcards, cancellable, error))
			return FALSE;
	}

	return TRUE;
}
