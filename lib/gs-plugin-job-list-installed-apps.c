/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021, 2022 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-plugin-job-list-installed-apps
 * @short_description: A plugin job to list installed #GsApps
 *
 * #GsPluginJobListInstalledApps is a #GsPluginJob representing an operation to
 * list installed apps from all plugins.
 *
 * The set of apps returned by this operation can be controlled with the
 * #GsPluginJobListInstalledApps:refine-flags,
 * #GsPluginJobListInstalledApps:max-results and
 * #GsPluginJobListInstalledApps:dedupe-flags properties. If `refine-flags` is
 * set, all results will be refined using the given set of refine flags (see
 * #GsPluginJobRefine). `max-results` and `dedupe-flags` are used to limit the
 * set of results.
 *
 * This class is a wrapper around #GsPluginClass.list_installed_apps_async,
 * calling it for all loaded plugins, with some additional filtering and
 * done on the results and #GsPluginJobRefine used to refine them.
 *
 * Retrieve the resulting #GsAppList using
 * gs_plugin_job_list_installed_apps_get_result_list().
 *
 * See also: #GsPluginClass.list_installed_apps_async
 * Since: 42
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include "gs-app.h"
#include "gs-enums.h"
#include "gs-plugin-job-private.h"
#include "gs-plugin-job-list-installed-apps.h"
#include "gs-plugin-job-refine.h"
#include "gs-utils.h"

struct _GsPluginJobListInstalledApps
{
	GsPluginJob parent;

	/* Input arguments. */
	GsPluginRefineFlags refine_flags;
	guint max_results;
	GsAppListFilterFlags dedupe_flags;

	/* In-progress data. */
	GsAppList *merged_list;  /* (owned) (nullable) */
	GError *saved_error;  /* (owned) (nullable) */
	guint n_pending_ops;

	/* Results. */
	GsAppList *result_list;  /* (owned) (nullable) */
};

G_DEFINE_TYPE (GsPluginJobListInstalledApps, gs_plugin_job_list_installed_apps, GS_TYPE_PLUGIN_JOB)

typedef enum {
	PROP_REFINE_FLAGS = 1,
	PROP_MAX_RESULTS,
	PROP_DEDUPE_FLAGS,
} GsPluginJobListInstalledAppsProperty;

static GParamSpec *props[PROP_DEDUPE_FLAGS + 1] = { NULL, };

static void
gs_plugin_job_list_installed_apps_dispose (GObject *object)
{
	GsPluginJobListInstalledApps *self = GS_PLUGIN_JOB_LIST_INSTALLED_APPS (object);

	g_assert (self->merged_list == NULL);
	g_assert (self->saved_error == NULL);
	g_assert (self->n_pending_ops == 0);

	g_clear_object (&self->result_list);

	G_OBJECT_CLASS (gs_plugin_job_list_installed_apps_parent_class)->dispose (object);
}

static void
gs_plugin_job_list_installed_apps_get_property (GObject    *object,
                                                guint       prop_id,
                                                GValue     *value,
                                                GParamSpec *pspec)
{
	GsPluginJobListInstalledApps *self = GS_PLUGIN_JOB_LIST_INSTALLED_APPS (object);

	switch ((GsPluginJobListInstalledAppsProperty) prop_id) {
	case PROP_REFINE_FLAGS:
		g_value_set_flags (value, self->refine_flags);
		break;
	case PROP_MAX_RESULTS:
		g_value_set_uint (value, self->max_results);
		break;
	case PROP_DEDUPE_FLAGS:
		g_value_set_flags (value, self->dedupe_flags);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_job_list_installed_apps_set_property (GObject      *object,
                                                guint         prop_id,
                                                const GValue *value,
                                                GParamSpec   *pspec)
{
	GsPluginJobListInstalledApps *self = GS_PLUGIN_JOB_LIST_INSTALLED_APPS (object);

	switch ((GsPluginJobListInstalledAppsProperty) prop_id) {
	case PROP_REFINE_FLAGS:
		/* Construct only. */
		g_assert (self->refine_flags == 0);
		self->refine_flags = g_value_get_flags (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	case PROP_MAX_RESULTS:
		/* Construct only. */
		g_assert (self->max_results == 0);
		self->max_results = g_value_get_uint (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	case PROP_DEDUPE_FLAGS:
		/* Construct only. */
		g_assert (self->dedupe_flags == 0);
		self->dedupe_flags = g_value_get_flags (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
app_is_valid_filter (GsApp    *app,
                     gpointer  user_data)
{
	GsPluginJobListInstalledApps *self = GS_PLUGIN_JOB_LIST_INSTALLED_APPS (user_data);

	return gs_plugin_loader_app_is_valid (app, self->refine_flags);
}

static gboolean
app_is_valid_installed (GsApp    *app,
                        gpointer  user_data)
{
	/* even without AppData, show things in progress */
	switch (gs_app_get_state (app)) {
	case GS_APP_STATE_INSTALLING:
	case GS_APP_STATE_REMOVING:
		return TRUE;
		break;
	default:
		break;
	}

	switch (gs_app_get_kind (app)) {
	case AS_COMPONENT_KIND_OPERATING_SYSTEM:
	case AS_COMPONENT_KIND_CODEC:
	case AS_COMPONENT_KIND_FONT:
		g_debug ("app invalid as %s: %s",
			 as_component_kind_to_string (gs_app_get_kind (app)),
			 gs_app_get_unique_id (app));
		return FALSE;
		break;
	default:
		break;
	}

	/* sanity check */
	if (!gs_app_is_installed (app)) {
		g_autofree gchar *tmp = gs_app_to_string (app);
		g_warning ("ignoring non-installed app %s", tmp);
		return FALSE;
	}

	return TRUE;
}

static void
sorted_truncation (GsPluginJobListInstalledApps *self,
                   GsAppList                    *list)
{
	GsAppListSortFunc sort_func;
	gpointer sort_func_data;

	g_assert (list != NULL);

	/* unset */
	if (self->max_results == 0)
		return;

	/* already small enough */
	if (gs_app_list_length (list) <= self->max_results)
		return;

	/* nothing set */
	g_debug ("truncating results to %u from %u",
		 self->max_results, gs_app_list_length (list));
	sort_func = gs_plugin_job_get_sort_func (GS_PLUGIN_JOB (self), &sort_func_data);
	if (sort_func == NULL) {
		GsPluginAction action = gs_plugin_job_get_action (GS_PLUGIN_JOB (self));
		g_debug ("no ->sort_func() set for %s, using random!",
			 gs_plugin_action_to_string (action));
		gs_app_list_randomize (list);
	} else {
		gs_app_list_sort (list, sort_func, sort_func_data);
	}
	gs_app_list_truncate (list, self->max_results);
}


static void
sorted_truncation_again (GsPluginJobListInstalledApps *self,
                         GsAppList                    *list)
{
	GsAppListSortFunc sort_func;
	gpointer sort_func_data;

	g_assert (list != NULL);

	/* unset */
	sort_func = gs_plugin_job_get_sort_func (GS_PLUGIN_JOB (self), &sort_func_data);
	if (sort_func == NULL)
		return;
	gs_app_list_sort (list, sort_func, sort_func_data);
}

static void plugin_list_installed_apps_cb (GObject      *source_object,
                                           GAsyncResult *result,
                                           gpointer      user_data);
static void finish_op (GTask *task);
static void refine_cb (GObject      *source_object,
                       GAsyncResult *result,
                       gpointer      user_data);
static void finish_task (GTask     *task,
                         GsAppList *merged_list);

static void
gs_plugin_job_list_installed_apps_run_async (GsPluginJob         *job,
                                             GsPluginLoader      *plugin_loader,
                                             GCancellable        *cancellable,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data)
{
	GsPluginJobListInstalledApps *self = GS_PLUGIN_JOB_LIST_INSTALLED_APPS (job);
	g_autoptr(GTask) task = NULL;
	GPtrArray *plugins;  /* (element-type GsPlugin) */
	gboolean anything_ran = FALSE;

	/* check required args */
	task = g_task_new (job, cancellable, callback, user_data);
	g_task_set_name (task, G_STRFUNC);
	g_task_set_task_data (task, g_object_ref (plugin_loader), (GDestroyNotify) g_object_unref);

	/* run each plugin, keeping a counter of pending operations which is
	 * initialised to 1 until all the operations are started */
	self->n_pending_ops = 1;
	self->merged_list = gs_app_list_new ();
	plugins = gs_plugin_loader_get_plugins (plugin_loader);

	for (guint i = 0; i < plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (plugins, i);
		GsPluginClass *plugin_class = GS_PLUGIN_GET_CLASS (plugin);

		if (!gs_plugin_get_enabled (plugin))
			continue;
		if (plugin_class->list_installed_apps_async == NULL)
			continue;

		/* at least one plugin supports this vfunc */
		anything_ran = TRUE;

		/* run the plugin */
		self->n_pending_ops++;
		plugin_class->list_installed_apps_async (plugin, cancellable, plugin_list_installed_apps_cb, g_object_ref (task));
	}

	/* some functions are really required for proper operation */
	if (!anything_ran) {
		g_set_error_literal (&self->saved_error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no plugin could handle listing installed apps");
	}

	finish_op (task);
}

static void
plugin_list_installed_apps_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	GsPluginClass *plugin_class = GS_PLUGIN_GET_CLASS (plugin);
	g_autoptr(GTask) task = G_TASK (user_data);
	GsPluginJobListInstalledApps *self = g_task_get_source_object (task);
	g_autoptr(GsAppList) plugin_apps = NULL;
	g_autoptr(GError) local_error = NULL;

	plugin_apps = plugin_class->list_installed_apps_finish (plugin, result, &local_error);
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);

	if (plugin_apps != NULL) {
		gs_app_list_add_list (self->merged_list, plugin_apps);
	} else {
		gs_utils_error_convert_gio (&local_error);

		if (self->saved_error == NULL)
			self->saved_error = g_steal_pointer (&local_error);
	}

	finish_op (task);
}

static void
finish_op (GTask *task)
{
	GsPluginJobListInstalledApps *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	GsPluginLoader *plugin_loader = g_task_get_task_data (task);
	g_autoptr(GsAppList) merged_list = NULL;
	g_autoptr(GError) saved_error = NULL;

	g_assert (self->n_pending_ops > 0);
	self->n_pending_ops--;

	if (self->n_pending_ops > 0)
		return;

	/* Get the results of the parallel ops. */
	merged_list = g_steal_pointer (&self->merged_list);
	saved_error = g_steal_pointer (&self->saved_error);

	if (saved_error != NULL) {
		g_task_return_error (task, g_steal_pointer (&saved_error));
		return;
	}

	/* filter to reduce to a sane set */
	sorted_truncation (self, merged_list);

	/* run refine() on each one if required */
	if (self->refine_flags != 0 &&
	    merged_list != NULL &&
	    gs_app_list_length (merged_list) > 0) {
		g_autoptr(GsPluginJob) refine_job = NULL;
		g_autoptr(GAsyncResult) refine_result = NULL;
		g_autoptr(GsAppList) new_list = NULL;

		refine_job = gs_plugin_job_refine_new (merged_list, self->refine_flags | GS_PLUGIN_REFINE_FLAGS_DISABLE_FILTERING);
		gs_plugin_loader_job_process_async (plugin_loader, refine_job,
						    cancellable,
						    refine_cb,
						    g_object_ref (task));
	} else {
		g_debug ("no refine flags set for transaction");
		finish_task (task, merged_list);
	}
}

static void
refine_cb (GObject      *source_object,
           GAsyncResult *result,
           gpointer      user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GsAppList) new_list = NULL;
	g_autoptr(GError) local_error = NULL;

	new_list = gs_plugin_loader_job_process_finish (plugin_loader, result, &local_error);
	if (new_list == NULL) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	finish_task (task, new_list);
}

static void
finish_task (GTask     *task,
             GsAppList *merged_list)
{
	GsPluginJobListInstalledApps *self = g_task_get_source_object (task);
	g_autofree gchar *job_debug = NULL;

	/* filter package list */
	gs_app_list_filter (merged_list, app_is_valid_filter, self);
	gs_app_list_filter (merged_list, app_is_valid_installed, self);

	/* filter duplicates with priority, taking into account the source name
	 * & version, so we combine available updates with the installed app */
	if (self->dedupe_flags != GS_APP_LIST_FILTER_FLAG_NONE)
		gs_app_list_filter_duplicates (merged_list, self->dedupe_flags);

	/* sort these again as the refine may have added useful metadata */
	sorted_truncation_again (self, merged_list);

	/* show elapsed time */
	job_debug = gs_plugin_job_to_string (GS_PLUGIN_JOB (self));
	g_debug ("%s", job_debug);

	/* Check the intermediate working values are all cleared. */
	g_assert (self->merged_list == NULL);
	g_assert (self->saved_error == NULL);
	g_assert (self->n_pending_ops == 0);

	/* success */
	g_set_object (&self->result_list, merged_list);
	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_job_list_installed_apps_run_finish (GsPluginJob   *self,
                                              GAsyncResult  *result,
                                              GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_job_list_installed_apps_class_init (GsPluginJobListInstalledAppsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginJobClass *job_class = GS_PLUGIN_JOB_CLASS (klass);

	object_class->dispose = gs_plugin_job_list_installed_apps_dispose;
	object_class->get_property = gs_plugin_job_list_installed_apps_get_property;
	object_class->set_property = gs_plugin_job_list_installed_apps_set_property;

	job_class->run_async = gs_plugin_job_list_installed_apps_run_async;
	job_class->run_finish = gs_plugin_job_list_installed_apps_run_finish;

	/**
	 * GsPluginJobListInstalledApps:refine-flags:
	 *
	 * Flags to specify how to refine the returned apps, if at all.
	 *
	 * Since: 42
	 */
	props[PROP_REFINE_FLAGS] =
		g_param_spec_flags ("refine-flags", "Refine Flags",
				    "Flags to specify how to refine the returned apps, if at all.",
				     GS_TYPE_PLUGIN_REFINE_FLAGS, GS_PLUGIN_REFINE_FLAGS_NONE,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginJobListInstalledApps:max-results:
	 *
	 * Maximum number of results to return, or 0 for no limit.
	 *
	 * Since: 42
	 */
	props[PROP_MAX_RESULTS] =
		g_param_spec_uint ("max-results", "Max Results",
				   "Maximum number of results to return, or 0 for no limit.",
				    0, G_MAXUINT, 0,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginJobListInstalledApps:dedupe-flags:
	 *
	 * Flags to specify how to deduplicate the returned apps, if at all.
	 *
	 * Since: 42
	 */
	props[PROP_DEDUPE_FLAGS] =
		g_param_spec_flags ("dedupe-flags", "Dedupe Flags",
				    "Flags to specify how to deduplicate the returned apps, if at all.",
				     GS_TYPE_APP_LIST_FILTER_FLAGS, GS_APP_LIST_FILTER_FLAG_NONE,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
gs_plugin_job_list_installed_apps_init (GsPluginJobListInstalledApps *self)
{
}

/**
 * gs_plugin_job_list_installed_apps_new:
 * @refine_flags: flags to affect how the results are refined, or
 *   %GS_PLUGIN_REFINE_FLAGS_NONE to skip refining them
 * @max_results: maximum number of results to return, or `0` to not limit the
 *   results
 * @dedupe_flags: flags to control deduplicating the results
 *
 * Create a new #GsPluginJobListInstalledApps for listing the installed apps.
 *
 * Returns: (transfer full): a new #GsPluginJobListInstalledApps
 * Since: 42
 */
GsPluginJob *
gs_plugin_job_list_installed_apps_new (GsPluginRefineFlags  refine_flags,
                                       guint                max_results,
                                       GsAppListFilterFlags dedupe_flags)
{
	return g_object_new (GS_TYPE_PLUGIN_JOB_LIST_INSTALLED_APPS,
			     "refine-flags", refine_flags,
			     "max-results", max_results,
			     "dedupe-flags", dedupe_flags,
			     NULL);
}

/**
 * gs_plugin_job_list_installed_apps_get_result_list:
 * @self: a #GsPluginJobListInstalledApps
 *
 * Get the full list of installed #GsApps.
 *
 * If this is called before the job is complete, %NULL will be returned.
 *
 * Returns: (transfer none) (nullable): the job results, or %NULL on error
 *   or if called before the job has completed
 * Since: 42
 */
GsAppList *
gs_plugin_job_list_installed_apps_get_result_list (GsPluginJobListInstalledApps *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB_LIST_INSTALLED_APPS (self), NULL);

	return self->result_list;
}
