/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>
#include <gnome-software.h>

#include "gs-packagekit-helper.h"
#include "packagekit-common.h"

#include "gs-plugin-packagekit-refine-repos.h"

/*
 * SECTION:
 * Uses the system PackageKit instance to return convert repo filenames to
 * package-ids.
 *
 * Requires:    | [repos::repo-filename]
 * Refines:     | [source-id]
 */

struct _GsPluginPackagekitRefineRepos {
	GsPlugin	 parent;

	PkClient	*client;
	GMutex		 client_mutex;
};

G_DEFINE_TYPE (GsPluginPackagekitRefineRepos, gs_plugin_packagekit_refine_repos, GS_TYPE_PLUGIN)

static void
gs_plugin_packagekit_refine_repos_init (GsPluginPackagekitRefineRepos *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	g_mutex_init (&self->client_mutex);
	self->client = pk_client_new ();
	pk_client_set_background (self->client, FALSE);
	pk_client_set_cache_age (self->client, G_MAXUINT);
	pk_client_set_interactive (self->client, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* need repos::repo-filename */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "repos");
}

static void
gs_plugin_packagekit_refine_repos_dispose (GObject *object)
{
	GsPluginPackagekitRefineRepos *self = GS_PLUGIN_PACKAGEKIT_REFINE_REPOS (object);

	g_clear_object (&self->client);

	G_OBJECT_CLASS (gs_plugin_packagekit_refine_repos_parent_class)->dispose (object);
}

static void
gs_plugin_packagekit_refine_repos_finalize (GObject *object)
{
	GsPluginPackagekitRefineRepos *self = GS_PLUGIN_PACKAGEKIT_REFINE_REPOS (object);

	g_mutex_clear (&self->client_mutex);

	G_OBJECT_CLASS (gs_plugin_packagekit_refine_repos_parent_class)->finalize (object);
}

static gboolean
gs_plugin_packagekit_refine_repo_from_filename (GsPluginPackagekitRefineRepos  *self,
                                                GsApp                          *app,
                                                const gchar                    *filename,
                                                GCancellable                   *cancellable,
                                                GError                        **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	const gchar *to_array[] = { NULL, NULL };
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) packages = NULL;

	to_array[0] = filename;
	gs_packagekit_helper_add_app (helper, app);
	g_mutex_lock (&self->client_mutex);
	pk_client_set_interactive (self->client, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_search_files (self->client,
	                                  pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
	                                  (gchar **) to_array,
	                                  cancellable,
	                                  gs_packagekit_helper_cb, helper,
	                                  error);
	g_mutex_unlock (&self->client_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to search file %s: ", filename);
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (packages->len == 1) {
		PkPackage *package = g_ptr_array_index (packages, 0);
		gs_app_add_source_id (app, pk_package_get_id (package));
	} else {
		g_debug ("failed to find one package for repo %s, %s, [%u]",
		         gs_app_get_id (app), filename, packages->len);
	}
	return TRUE;
}

gboolean
gs_plugin_refine (GsPlugin *plugin,
                  GsAppList *list,
                  GsPluginRefineFlags flags,
                  GCancellable *cancellable,
                  GError **error)
{
	GsPluginPackagekitRefineRepos *self = GS_PLUGIN_PACKAGEKIT_REFINE_REPOS (plugin);

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		const gchar *fn;
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		if (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY)
			continue;
		if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
			continue;
		fn = gs_app_get_metadata_item (app, "repos::repo-filename");
		if (fn == NULL)
			continue;
		/* set the source package name for an installed .repo file */
		if (!gs_plugin_packagekit_refine_repo_from_filename (self,
		                                                     app,
		                                                     fn,
		                                                     cancellable,
		                                                     error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
gs_plugin_packagekit_refine_repos_class_init (GsPluginPackagekitRefineReposClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_plugin_packagekit_refine_repos_dispose;
	object_class->finalize = gs_plugin_packagekit_refine_repos_finalize;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_PACKAGEKIT_REFINE_REPOS;
}
