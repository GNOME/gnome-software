/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2022, 2023 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-job-manager
 * @short_description: A manager to track ongoing #GsPluginJobs
 *
 * #GsJobManager tracks ongoing #GsPluginJobs and the #GsApps they are
 * affecting.
 *
 * This makes it possible to track all the jobs ongoing in gnome-software, or
 * in a particular backend, or for a particular app at any time.
 *
 * See also: #GsPluginJob
 * Since: 44
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include "gs-enums.h"
#include "gs-plugin-job.h"
#include "gs-plugin-job-update-apps.h"
#include "gs-plugin-types.h"
#include "gs-utils.h"

struct _GsJobManager
{
	GObject parent;

	GPtrArray *jobs;  /* (owned) (element-type GsPluginJob) (not nullable) */
};

G_DEFINE_TYPE (GsJobManager, gs_job_manager, G_TYPE_OBJECT)

static void
gs_job_manager_dispose (GObject *object)
{
	GsJobManager *self = GS_JOB_MANAGER (object);

	/* All jobs should have completed or been cancelled by now. */
	g_assert (self->jobs->len == 0);

	G_OBJECT_CLASS (gs_job_manager_parent_class)->dispose (object);
}

static void
gs_job_manager_finalize (GObject *object)
{
	GsJobManager *self = GS_JOB_MANAGER (object);

	g_clear_pointer (&self->jobs, g_ptr_array_unref);

	G_OBJECT_CLASS (gs_job_manager_parent_class)->finalize (object);
}

static void
gs_job_manager_class_init (GsJobManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_job_manager_dispose;
	object_class->finalize = gs_job_manager_finalize;
}

static void
gs_job_manager_init (GsJobManager *self)
{
	self->jobs = g_ptr_array_new_with_free_func (g_object_unref);
}

/**
 * gs_job_manager_new:
 *
 * Create a new #GsJobManager for tracking pending jobs.
 *
 * Returns: (transfer full): a new #GsJobManager
 * Since: 44
 */
GsJobManager *
gs_job_manager_new (void)
{
	return g_object_new (GS_TYPE_JOB_MANAGER, NULL);
}

static void
job_completed_cb (GsPluginJob *job,
		  gpointer user_data)
{
	GsJobManager *self = GS_JOB_MANAGER (user_data);

	gs_job_manager_remove_job (self, job);
}

/**
 * gs_job_manager_add_job:
 * @self: a #GsJobManager
 * @job: a #GsPluginJob to add
 *
 * Add @job to the set of jobs tracked by the #GsJobManager.
 *
 * If @job is already tracked by the job manager, this function is a no-op.
 *
 * Returns: %TRUE if @job was added to the manager, %FALSE if it was already
 *   tracked
 * Since: 44
 */
gboolean
gs_job_manager_add_job (GsJobManager *self,
                        GsPluginJob  *job)
{
	g_return_val_if_fail (GS_IS_JOB_MANAGER (self), FALSE);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (job), FALSE);

	if (g_ptr_array_find (self->jobs, job, NULL))
		return FALSE;

	g_ptr_array_add (self->jobs, g_object_ref (job));
	g_signal_connect (job, "completed", G_CALLBACK (job_completed_cb), self);

	return TRUE;
}

/**
 * gs_job_manager_remove_job:
 * @self: a #GsJobManager
 * @job: a #GsPluginJob to remove
 *
 * Remove @job from the set of jobs tracked by the #GsJobManager.
 *
 * If @job is not already tracked by the job manager, this function is a no-op.
 *
 * Returns: %TRUE if @job was removed from the manager, %FALSE if it was not
 *   already tracked
 * Since: 44
 */
gboolean
gs_job_manager_remove_job (GsJobManager *self,
                           GsPluginJob  *job)
{
	g_return_val_if_fail (GS_IS_JOB_MANAGER (self), FALSE);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (job), FALSE);

	if (g_ptr_array_remove_fast (self->jobs, job)) {
		g_signal_handlers_disconnect_by_func (job, job_completed_cb, self);
		return TRUE;
	}

	return FALSE;
}

static gboolean
job_contains_app (GsPluginJob *job,
                  GsApp       *app)
{
	GsAppList *apps = NULL;

	/* FIXME: This could be improved in future by making GsPluginJob subclasses
	 * implement an interface to query which apps they are acting on. */
	if (GS_IS_PLUGIN_JOB_UPDATE_APPS (job))
		apps = gs_plugin_job_update_apps_get_apps (GS_PLUGIN_JOB_UPDATE_APPS (job));

	if (apps == NULL)
		return FALSE;

	return (gs_app_list_lookup (apps, gs_app_get_unique_id (app)) != NULL);
}

/**
 * gs_job_manager_get_pending_jobs_for_app:
 * @self: a #GsJobManager
 * @app: app to get pending jobs for
 *
 * Find the jobs which are ongoing for the given @app.
 *
 * Returns: (element-type GsPluginJob) (transfer container): zero or more
 *   ongoing jobs
 * Since: 44
 */
GPtrArray *
gs_job_manager_get_pending_jobs_for_app (GsJobManager *self,
                                         GsApp        *app)
{
	g_autoptr(GPtrArray) jobs_for_app = NULL;

	g_return_val_if_fail (GS_IS_JOB_MANAGER (self), NULL);
	g_return_val_if_fail (GS_IS_APP (app), NULL);

	jobs_for_app = g_ptr_array_new_with_free_func (g_object_unref);

	for (gsize i = 0; i < self->jobs->len; i++) {
		GsPluginJob *job = g_ptr_array_index (self->jobs, i);

		if (job_contains_app (job, app))
			g_ptr_array_add (jobs_for_app, g_object_ref (job));
	}

	return g_steal_pointer (&jobs_for_app);
}

/**
 * gs_job_manager_app_has_pending_job_type:
 * @self: a #GsJobManager
 * @app: app to query for pending jobs for
 * @pending_job_type: %GS_TYPE_PLUGIN_JOB or one of its subtypes
 *
 * Query whether there is at least one job of type @pending_job_type ongoing for
 * @app.
 *
 * Returns: %TRUE if there is at least one job ongoing for @app, %FALSE
 *   otherwise
 * Since: 44
 */
gboolean
gs_job_manager_app_has_pending_job_type (GsJobManager *self,
                                         GsApp        *app,
                                         GType         pending_job_type)
{
	g_return_val_if_fail (GS_IS_JOB_MANAGER (self), FALSE);
	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	g_return_val_if_fail (g_type_is_a (pending_job_type, GS_TYPE_PLUGIN_JOB), FALSE);

	for (gsize i = 0; i < self->jobs->len; i++) {
		GsPluginJob *job = g_ptr_array_index (self->jobs, i);

		if (g_type_is_a (G_OBJECT_TYPE (job), pending_job_type) &&
		    job_contains_app (job, app))
			return TRUE;
	}

	return FALSE;
}
