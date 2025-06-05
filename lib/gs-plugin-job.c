/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <glib.h>

#include "gs-enums.h"
#include "gs-plugin-private.h"
#include "gs-plugin-job-private.h"

typedef struct
{
	gint64			 time_created;
	GCancellable		*cancellable; /* (nullable) (owned) */
} GsPluginJobPrivate;

typedef enum {
	SIGNAL_COMPLETED,
	SIGNAL_EVENT,
	SIGNAL_LAST
} GsPluginJobSignal;

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsPluginJob, gs_plugin_job, G_TYPE_OBJECT)

gchar *
gs_plugin_job_to_string (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	GString *str = g_string_new (NULL);
	gint64 time_now = g_get_monotonic_time ();
	const gchar *job_type_name = G_OBJECT_TYPE_NAME (self);

	g_string_append (str, "running ");

	if (job_type_name != NULL && g_str_has_prefix (job_type_name, "GsPluginJob"))
		g_string_append_printf (str, "%s job", job_type_name + strlen ("GsPluginJob"));
	else
		g_string_append_printf (str, "%s", job_type_name);

	if (time_now - priv->time_created > 1000) {
		g_string_append_printf (str, ", elapsed time since creation %" G_GINT64_FORMAT "ms",
					(time_now - priv->time_created) / 1000);
	}
	return g_string_free (str, FALSE);
}

gboolean
gs_plugin_job_get_interactive (GsPluginJob *self)
{
	GsPluginJobClass *klass;
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), FALSE);
	klass = GS_PLUGIN_JOB_GET_CLASS (self);
	if (klass->get_interactive == NULL)
		return FALSE;
	return klass->get_interactive (self);
}

static void
gs_plugin_job_finalize (GObject *obj)
{
	GsPluginJob *self = GS_PLUGIN_JOB (obj);
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);

	g_clear_object (&priv->cancellable);

	G_OBJECT_CLASS (gs_plugin_job_parent_class)->finalize (obj);
}

static void
gs_plugin_job_class_init (GsPluginJobClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gs_plugin_job_finalize;

	/**
	 * GsPluginJob::completed:
	 *
	 * Emitted when the job is completed, but before it is finalized.
	 *
	 * Since: 44
	 */
	signals[SIGNAL_COMPLETED] =
		g_signal_new ("completed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 0);

	/**
	 * GsPluginJob::event:
	 *
	 * Emitted when an event happens while running the job.
	 *
	 * This typically means that a plugin has encountered an error.
	 *
	 * Since: 49
	 */
	signals[SIGNAL_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2, GS_TYPE_PLUGIN, GS_TYPE_PLUGIN_EVENT);
}

static void
gs_plugin_job_init (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);

	priv->time_created = g_get_monotonic_time ();
}

/**
 * gs_plugin_job_run_async:
 * @self: a #GsPluginJob
 * @plugin_loader: plugin loader to provide the plugins to run the job against
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: callback to call once the job is finished
 * @user_data: data to pass to @callback
 *
 * Asynchronously run the job.
 *
 * This stores a reference to @cancellable so that gs_plugin_job_cancel() can be
 * used to asynchronously cancel the job from another thread.
 *
 * Since: 49
 */
void
gs_plugin_job_run_async (GsPluginJob         *self,
                         GsPluginLoader      *plugin_loader,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	GsPluginJobClass *job_class;

	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	job_class = GS_PLUGIN_JOB_GET_CLASS (self);
	g_assert (job_class->run_async != NULL);

	/* Store a reference to the cancellable for later use by gs_plugin_job_cancel() */
	g_set_object (&priv->cancellable, cancellable);

	job_class->run_async (self, plugin_loader, cancellable, callback, user_data);
}

/**
 * gs_plugin_job_run_finish:
 * @self: a #GsPluginJob
 * @result: result of the asynchronous operation
 * @error: return location for a #GError, or %NULL
 *
 * Finish an asynchronous plugin job started with gs_plugin_job_run_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 49
 */
gboolean
gs_plugin_job_run_finish (GsPluginJob   *self,
                          GAsyncResult  *result,
                          GError       **error)
{
	GsPluginJobClass *job_class;

	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	job_class = GS_PLUGIN_JOB_GET_CLASS (self);
	g_assert (job_class->run_finish != NULL);

	return job_class->run_finish (self, result, error);
}

/**
 * gs_plugin_job_cancel:
 * @self: a #GsPluginJob
 *
 * Cancel the plugin job.
 *
 * This will cancel the #GCancellable passed to gs_plugin_job_run_async().
 *
 * Since: 45
 */
void
gs_plugin_job_cancel (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);

	g_return_if_fail (GS_IS_PLUGIN_JOB (self));

	g_cancellable_cancel (priv->cancellable);
}

/**
 * gs_plugin_job_emit_event:
 * @self: a plugin job
 * @plugin: (nullable) (transfer none): plugin which reported the event, or
 *   `NULL` if the event is not associated to a specific plugin
 * @event: (transfer none): event being reported
 *
 * Emit an event from the plugin job.
 *
 * This is typically used to report errors while running the job, and it allows
 * multiple errors to be reported and for the job to continue after those
 * errors. Returning a #GError would not allow that.
 *
 * @plugin may be `NULL` if the event is not associated with a specific plugin.
 * It will typically be non-`NULL`, though, as most events come from
 * plugin-specific code.
 *
 * Since: 49
 */
void
gs_plugin_job_emit_event (GsPluginJob   *self,
                          GsPlugin      *plugin,
                          GsPluginEvent *event)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_return_if_fail (plugin == NULL || GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_PLUGIN_EVENT (event));

	g_signal_emit (self, signals[SIGNAL_EVENT], 0, plugin, event);
}
