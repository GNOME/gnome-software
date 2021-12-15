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

typedef struct {
	/* Track pending operations. */
	guint n_pending_operations;
	gboolean completed;
	GError *error;  /* (nullable) (owned) */
} RefineData;

static void
refine_data_free (RefineData *data)
{
	g_assert (data->n_pending_operations == 0);
	g_assert (data->completed);

	g_clear_error (&data->error);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RefineData, refine_data_free)

static GTask *
refine_task_add_operation (GTask *refine_task)
{
	RefineData *data = g_task_get_task_data (refine_task);

	g_assert (!data->completed);
	data->n_pending_operations++;

	return g_object_ref (refine_task);
}

static void
refine_task_complete_operation (GTask *refine_task)
{
	RefineData *data = g_task_get_task_data (refine_task);

	g_assert (data->n_pending_operations > 0);
	data->n_pending_operations--;

	/* Have all operations completed? */
	if (data->n_pending_operations == 0) {
		g_assert (!data->completed);
		data->completed = TRUE;

		if (data->error != NULL)
			g_task_return_error (refine_task, g_steal_pointer (&data->error));
		else
			g_task_return_boolean (refine_task, TRUE);
	}
}

static void
refine_task_complete_operation_with_error (GTask  *refine_task,
					   GError *error  /* (transfer full) */)
{
	RefineData *data = g_task_get_task_data (refine_task);
	g_autoptr(GError) owned_error = g_steal_pointer (&error);

	/* Multiple operations might fail. Just take the first error. */
	if (data->error == NULL)
		data->error = g_steal_pointer (&owned_error);

	refine_task_complete_operation (refine_task);
}

typedef struct {
	GTask *refine_task;  /* (owned) (not nullable) */
	GsApp *app;  /* (owned) (not nullable) */
	gchar *filename;  /* (owned) (not nullable) */
	GsPackagekitHelper *progress_data;  /* (owned) (not nullable) */
} SearchFilesData;

static void
search_files_data_free (SearchFilesData *data)
{
	g_free (data->filename);
	g_clear_object (&data->app);
	g_clear_object (&data->refine_task);
	g_clear_object (&data->progress_data);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SearchFilesData, search_files_data_free)

/* The @progress_data is referenced purely so it stays alive for the duration of
 * the async operation. It has separately been passed in as the closure for the
 * progress callback, which will be called zero or more times during the
 * operation. */
static SearchFilesData *
search_files_data_new_operation (GTask              *refine_task,
                                 GsApp              *app,
                                 const gchar        *filename,
                                 GsPackagekitHelper *progress_data)
{
	g_autoptr(SearchFilesData) data = g_new0 (SearchFilesData, 1);
	data->refine_task = refine_task_add_operation (refine_task);
	data->app = g_object_ref (app);
	data->filename = g_strdup (filename);
	data->progress_data = g_object_ref (progress_data);

	return g_steal_pointer (&data);
}

static void search_files_cb (GObject      *source_object,
                             GAsyncResult *result,
                             gpointer      user_data);

static void
gs_plugin_packagekit_refine_repos_refine_async (GsPlugin            *plugin,
                                                GsAppList           *list,
                                                GsPluginRefineFlags  flags,
                                                GCancellable        *cancellable,
                                                GAsyncReadyCallback  callback,
                                                gpointer             user_data)
{
	GsPluginPackagekitRefineRepos *self = GS_PLUGIN_PACKAGEKIT_REFINE_REPOS (plugin);
	g_autoptr(GTask) task = NULL;
	g_autoptr(RefineData) data = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_packagekit_refine_repos_refine_async);
	data = g_new0 (RefineData, 1);
	data->n_pending_operations = 1;  /* to prevent the task being completed before all operations have been started */
	g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) refine_data_free);

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		const gchar *filename;
		const gchar *to_array[] = { NULL, NULL };
		g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);

		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		if (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY)
			continue;
		if (!gs_app_has_management_plugin (app, plugin))
			continue;
		filename = gs_app_get_metadata_item (app, "repos::repo-filename");
		if (filename == NULL)
			continue;

		/* set the source package name for an installed .repo file */
		to_array[0] = filename;
		gs_packagekit_helper_add_app (helper, app);
		g_mutex_lock (&self->client_mutex);
		pk_client_set_interactive (self->client, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
		pk_client_search_files_async (self->client,
					      pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
					      (gchar **) to_array,
					      cancellable,
					      gs_packagekit_helper_cb, helper,
					      search_files_cb,
					      search_files_data_new_operation (task, app, filename, helper));
		g_mutex_unlock (&self->client_mutex);
	}

	/* Mark the operation to set up all the other operations as completed.
	 * The @refine_task will now be completed once all the async operations
	 * have completed, and the task callback invoked. */
	refine_task_complete_operation (task);
}

static void
search_files_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	PkClient *client = PK_CLIENT (source_object);
	g_autoptr(SearchFilesData) search_files_data = g_steal_pointer (&user_data);
	GTask *refine_task = search_files_data->refine_task;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) packages = NULL;
	g_autoptr(GError) local_error = NULL;

	results = pk_client_generic_finish (client, result, &local_error);

	if (!gs_plugin_packagekit_results_valid (results, &local_error)) {
		g_prefix_error (&local_error, "failed to search file %s: ", search_files_data->filename);
		refine_task_complete_operation_with_error (refine_task, g_steal_pointer (&local_error));
		return;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (packages->len == 1) {
		PkPackage *package = g_ptr_array_index (packages, 0);
		gs_app_add_source_id (search_files_data->app, pk_package_get_id (package));
	} else {
		g_debug ("failed to find one package for repo %s, %s, [%u]",
			 gs_app_get_id (search_files_data->app), search_files_data->filename, packages->len);
	}

	refine_task_complete_operation (refine_task);
}

static gboolean
gs_plugin_packagekit_refine_repos_refine_finish (GsPlugin      *plugin,
                                                 GAsyncResult  *result,
                                                 GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_packagekit_refine_repos_class_init (GsPluginPackagekitRefineReposClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_packagekit_refine_repos_dispose;
	object_class->finalize = gs_plugin_packagekit_refine_repos_finalize;

	plugin_class->refine_async = gs_plugin_packagekit_refine_repos_refine_async;
	plugin_class->refine_finish = gs_plugin_packagekit_refine_repos_refine_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_PACKAGEKIT_REFINE_REPOS;
}
