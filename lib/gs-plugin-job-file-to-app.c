/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-plugin-job-file-to-app
 * @short_description: A plugin job on an app
 *
 * #GsPluginJobFileToApp is a #GsPluginJob representing an operation to
 * convert a #GFile into a #GsApp.
 *
 * This class is a wrapper around #GsPluginClass.file_to_app_async
 * calling it for all loaded plugins, with #GsPluginJobRefine used to refine the
 * results using the given set of refine flags.
 *
 * Retrieve the resulting #GsAppList using
 * gs_plugin_job_file_to_app_get_result_list().
 *
 * Since: 47
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>

#include "gs-app.h"
#include "gs-app-list-private.h"
#include "gs-enums.h"
#include "gs-plugin-job.h"
#include "gs-plugin-job-file-to-app.h"
#include "gs-plugin-job-private.h"
#include "gs-plugin-job-refine.h"
#include "gs-plugin-types.h"

struct _GsPluginJobFileToApp
{
	GsPluginJob parent;

	/* Input arguments. */
	GFile *file;  /* (owned) (not nullable) */
	GsPluginRefineRequireFlags require_flags;
	GsPluginFileToAppFlags flags;

	/* In-progress data. */
	GError *saved_error;  /* (owned) (nullable) */
	guint n_pending_ops;
	gboolean did_refine;
	GsAppList *in_progress_list;  /* (owned) (nullable) */

	/* Results. */
	GsAppList *result_list;  /* (owned) (nullable) */
};

G_DEFINE_TYPE (GsPluginJobFileToApp, gs_plugin_job_file_to_app, GS_TYPE_PLUGIN_JOB)

typedef enum {
	PROP_REFINE_REQUIRE_FLAGS = 1,
	PROP_FLAGS,
	PROP_FILE,
} GsPluginJobFileToAppProperty;

static GParamSpec *props[PROP_FILE + 1] = { NULL, };

static void
gs_plugin_job_file_to_app_dispose (GObject *object)
{
	GsPluginJobFileToApp *self = GS_PLUGIN_JOB_FILE_TO_APP (object);

	g_assert (self->saved_error == NULL);
	g_assert (self->n_pending_ops == 0);

	g_clear_object (&self->file);
	g_clear_object (&self->result_list);
	g_clear_object (&self->in_progress_list);

	G_OBJECT_CLASS (gs_plugin_job_file_to_app_parent_class)->dispose (object);
}

static void
gs_plugin_job_file_to_app_get_property (GObject    *object,
					guint       prop_id,
					GValue     *value,
					GParamSpec *pspec)
{
	GsPluginJobFileToApp *self = GS_PLUGIN_JOB_FILE_TO_APP (object);

	switch ((GsPluginJobFileToAppProperty) prop_id) {
	case PROP_REFINE_REQUIRE_FLAGS:
		g_value_set_flags (value, self->require_flags);
		break;
	case PROP_FLAGS:
		g_value_set_flags (value, self->flags);
		break;
	case PROP_FILE:
		g_value_set_object (value, self->file);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_job_file_to_app_set_property (GObject      *object,
					guint         prop_id,
					const GValue *value,
					GParamSpec   *pspec)
{
	GsPluginJobFileToApp *self = GS_PLUGIN_JOB_FILE_TO_APP (object);

	switch ((GsPluginJobFileToAppProperty) prop_id) {
	case PROP_REFINE_REQUIRE_FLAGS:
		/* Construct only. */
		g_assert (self->require_flags == 0);
		self->require_flags = g_value_get_flags (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	case PROP_FLAGS:
		/* Construct only. */
		g_assert (self->flags == 0);
		self->flags = g_value_get_flags (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	case PROP_FILE:
		/* Construct only. */
		g_assert (self->file == NULL);
		self->file = g_value_dup_object (value);
		g_assert (self->file != NULL);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
gs_plugin_job_file_to_app_get_interactive (GsPluginJob *job)
{
	GsPluginJobFileToApp *self = GS_PLUGIN_JOB_FILE_TO_APP (job);
	return (self->flags & GS_PLUGIN_FILE_TO_APP_FLAGS_INTERACTIVE) != 0;
}

static void plugin_event_cb (GsPlugin      *plugin,
                             GsPluginEvent *event,
                             void          *user_data);
static void plugin_app_func_cb (GObject      *source_object,
				GAsyncResult *result,
				gpointer      user_data);
static void finish_op (GTask *task,
		       GsAppList *list,
                       GError *error);
static void refine_job_finished_cb (GObject      *source_object,
                                    GAsyncResult *result,
                                    gpointer      user_data);
static void finish_refine_op (GTask     *task,
                              GsAppList *list,
                              GError    *error);

static void
gs_plugin_job_file_to_app_run_async (GsPluginJob         *job,
				     GsPluginLoader      *plugin_loader,
				     GCancellable        *cancellable,
				     GAsyncReadyCallback  callback,
				     gpointer             user_data)
{
	GsPluginJobFileToApp *self = GS_PLUGIN_JOB_FILE_TO_APP (job);
	g_autoptr(GTask) task = NULL;
	GPtrArray *plugins;  /* (element-type GsPlugin) */
	gboolean anything_ran = FALSE;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (job, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_job_file_to_app_run_async);
	g_task_set_task_data (task, g_object_ref (plugin_loader), (GDestroyNotify) g_object_unref);

	/* run each plugin, keeping a counter of pending operations which is
	 * initialised to 1 until all the operations are started */
	self->n_pending_ops = 1;
	plugins = gs_plugin_loader_get_plugins (plugin_loader);

	for (guint i = 0; i < plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (plugins, i);
		GsPluginClass *plugin_class = GS_PLUGIN_GET_CLASS (plugin);

		if (!gs_plugin_get_enabled (plugin))
			continue;
		if (plugin_class->file_to_app_async == NULL)
			continue;

		/* at least one plugin supports this vfunc */
		anything_ran = TRUE;

		/* Handle cancellation */
		if (g_cancellable_set_error_if_cancelled (cancellable, &local_error))
			break;

		/* run the plugin */
		self->n_pending_ops++;
		plugin_class->file_to_app_async (plugin, self->file, self->flags, plugin_event_cb, task, cancellable, plugin_app_func_cb, g_object_ref (task));
	}

	if (!anything_ran) {
		g_set_error_literal (&local_error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no plugin could handle converting file to app");
	}

	finish_op (task, NULL, g_steal_pointer (&local_error));
}

static void
plugin_event_cb (GsPlugin      *plugin,
                 GsPluginEvent *event,
                 void          *user_data)
{
	GTask *task = G_TASK (user_data);
	GsPluginJob *plugin_job = g_task_get_source_object (task);

	gs_plugin_job_emit_event (plugin_job, plugin, event);
}

static void
plugin_app_func_cb (GObject      *source_object,
		    GAsyncResult *result,
		    gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	GsPluginClass *plugin_class = GS_PLUGIN_GET_CLASS (plugin);
	g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) local_error = NULL;

	list = plugin_class->file_to_app_finish (plugin, result, &local_error);

	g_assert (list != NULL || local_error != NULL);

	finish_op (task, list, g_steal_pointer (&local_error));
}

/* @error is (transfer full) if non-%NULL */
static void
finish_op (GTask  *task,
	   GsAppList *list,
           GError *error)
{
	GsPluginJobFileToApp *self = g_task_get_source_object (task);
	GsPluginLoader *plugin_loader = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);

	if (error_owned != NULL && self->saved_error == NULL)
		self->saved_error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while converting file to app: %s", error_owned->message);

	g_assert (self->n_pending_ops > 0);
	self->n_pending_ops--;

	if (list != NULL) {
		if (self->in_progress_list == NULL)
			self->in_progress_list = gs_app_list_new ();
		gs_app_list_add_list (self->in_progress_list, list);
	}

	if (self->n_pending_ops > 0)
		return;

	/* Once all the file-to-app operations are complete, refine the results. */
	if (self->in_progress_list != NULL) {
		if (self->require_flags != GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE) {
			g_autoptr(GsPluginJob) refine_job = NULL;
			GsPluginRefineFlags job_flags;

			/* to not have filtered out repositories */
			job_flags = GS_PLUGIN_REFINE_FLAGS_DISABLE_FILTERING;

			refine_job = gs_plugin_job_refine_new (self->in_progress_list, job_flags, self->require_flags);
			gs_plugin_loader_job_process_async (plugin_loader, refine_job, cancellable,
							    refine_job_finished_cb, g_object_ref (task));
			return;
		}
	}

	/* Fall through without refining. */
	finish_refine_op (task, self->in_progress_list, NULL);
}

static void
refine_job_finished_cb (GObject *source_object,
			GAsyncResult *result,
			gpointer user_data)
{
	g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
	g_autoptr(GsPluginJobRefine) refine_job = NULL;
	g_autoptr(GError) local_error = NULL;

	gs_plugin_loader_job_process_finish (GS_PLUGIN_LOADER (source_object), result, (GsPluginJob **) &refine_job, &local_error);
	g_prefix_error_literal (&local_error, "Failed to refine file-to-app apps:");

	finish_refine_op (task, gs_plugin_job_refine_get_result_list (refine_job), g_steal_pointer (&local_error));
}

/* @error is (transfer full) if non-%NULL */
static void
finish_refine_op (GTask     *task,
                  GsAppList *list,
                  GError    *error)
{
	GsPluginJobFileToApp *self = g_task_get_source_object (task);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);
	g_autofree gchar *job_debug = NULL;

	if (error_owned != NULL && self->saved_error == NULL)
		self->saved_error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while converting file to app: %s", error_owned->message);

	g_clear_object (&self->result_list);
	self->result_list = (list != NULL) ? g_object_ref (list) : NULL;

	/* only allow one result */
	if (self->saved_error == NULL) {
		if (self->result_list == NULL ||
		    gs_app_list_length (self->result_list) == 0) {
			g_autofree gchar *str = gs_plugin_job_to_string (GS_PLUGIN_JOB (self));
			g_set_error (&self->saved_error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no application was created for %s", str);
		} else if (gs_app_list_length (self->result_list) > 1) {
			g_autofree gchar *str = gs_plugin_job_to_string (GS_PLUGIN_JOB (self));
			g_debug ("expected one, but received %u apps for %s", gs_app_list_length (self->result_list), str);
		}

		/* Ensure the icon is set on all the apps. */
		if (self->result_list != NULL) {
			for (guint i = 0; i < gs_app_list_length (self->result_list); i++) {
				GsApp *app = gs_app_list_index (self->result_list, i);

				if (!gs_app_has_icons (app)) {
					g_autoptr(GIcon) ic = NULL;
					const gchar *icon_name;
					if (gs_app_has_quirk (app, GS_APP_QUIRK_LOCAL_HAS_REPOSITORY))
						icon_name = "x-package-repository";
					else
						icon_name = "system-component-application";
					ic = g_themed_icon_new (icon_name);
					gs_app_add_icon (app, ic);
				}
			}
		}
	}

	/* show elapsed time */
	job_debug = gs_plugin_job_to_string (GS_PLUGIN_JOB (self));
	g_debug ("%s", job_debug);

	if (self->saved_error != NULL)
		g_task_return_error (task, g_steal_pointer (&self->saved_error));
	else
		g_task_return_boolean (task, TRUE);
	g_signal_emit_by_name (G_OBJECT (self), "completed");
}

static gboolean
gs_plugin_job_file_to_app_run_finish (GsPluginJob   *self,
				     GAsyncResult  *result,
				     GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_job_file_to_app_class_init (GsPluginJobFileToAppClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginJobClass *job_class = GS_PLUGIN_JOB_CLASS (klass);

	object_class->dispose = gs_plugin_job_file_to_app_dispose;
	object_class->get_property = gs_plugin_job_file_to_app_get_property;
	object_class->set_property = gs_plugin_job_file_to_app_set_property;

	job_class->get_interactive = gs_plugin_job_file_to_app_get_interactive;
	job_class->run_async = gs_plugin_job_file_to_app_run_async;
	job_class->run_finish = gs_plugin_job_file_to_app_run_finish;

	/**
	 * GsPluginJobFileToApp:file: (not nullable)
	 *
	 * A #GFile to convert to a #GsApp.
	 *
	 * Since: 47
	 */
	props[PROP_FILE] =
		g_param_spec_object ("file", "File",
				     "A #GFile to convert to a #GsApp.",
				     G_TYPE_FILE,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginJobFileToApp:refine-require-flags:
	 *
	 * Flags to specify how to refine the returned apps.
	 *
	 * Since: 49
	 */
	props[PROP_REFINE_REQUIRE_FLAGS] =
		g_param_spec_flags ("refine-require-flags", "Refine Flags",
				    "Flags to specify how to refine the returned apps.",
				    GS_TYPE_PLUGIN_REFINE_REQUIRE_FLAGS, GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginJobFileToApp:flags:
	 *
	 * Flags affecting how the operation runs.
	 *
	 * Since: 47
	 */
	props[PROP_FLAGS] =
		g_param_spec_flags ("flags", "Flags",
				    "Flags affecting how the operation runs.",
				    GS_TYPE_PLUGIN_FILE_TO_APP_FLAGS,
				    GS_PLUGIN_FILE_TO_APP_FLAGS_NONE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
gs_plugin_job_file_to_app_init (GsPluginJobFileToApp *self)
{
}

/**
 * gs_plugin_job_file_to_app_new:
 * @file: (not nullable) (transfer none): an #GFile to run the operation on
 * @flags: flags affecting how the operation runs
 * @require_flags: flags to affect how the results are refined
 *
 * Create a new #GsPluginJobFileToApp to convert the given @file.
 *
 * Returns: (transfer full): a new #GsPluginJobFileToApp
 * Since: 49
 */
GsPluginJob *
gs_plugin_job_file_to_app_new (GFile                      *file,
                               GsPluginFileToAppFlags      flags,
                               GsPluginRefineRequireFlags  require_flags)
{
	g_return_val_if_fail (G_IS_FILE (file), NULL);

	return g_object_new (GS_TYPE_PLUGIN_JOB_FILE_TO_APP,
			     "file", file,
			     "refine-require-flags", require_flags,
			     "flags", flags,
			     NULL);
}

/**
 * gs_plugin_job_file_to_app_get_result_list:
 * @self: a #GsPluginJobFileToApp
 *
 * Get the list of apps converted from the given file.
 *
 * If this is called before the job is complete, %NULL will be returned.
 *
 * Returns: (transfer none) (nullable): the job results, or %NULL on error
 *   or if called before the job has completed
 *
 * Since: 47
 */
GsAppList *
gs_plugin_job_file_to_app_get_result_list (GsPluginJobFileToApp *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB_FILE_TO_APP (self), NULL);

	return self->result_list;
}
