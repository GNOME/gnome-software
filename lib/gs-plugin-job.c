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
	GsPluginAction		 action;
	gint64			 time_created;
	GCancellable		*cancellable; /* (nullable) (owned) */
} GsPluginJobPrivate;

enum {
	PROP_0,
	PROP_ACTION,
	PROP_LAST
};

typedef enum {
	SIGNAL_COMPLETED,
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
	g_string_append (str, "running ");
	if (priv->action != GS_PLUGIN_ACTION_UNKNOWN) {
		g_string_append_printf (str, "%s", gs_plugin_action_to_string (priv->action));
	} else {
		const gchar *job_type_name = G_OBJECT_TYPE_NAME (self);
		if (job_type_name != NULL && g_str_has_prefix (job_type_name, "GsPluginJob"))
			g_string_append_printf (str, "%s job", job_type_name + strlen ("GsPluginJob"));
		else
			g_string_append_printf (str, "%s", job_type_name);
	}

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

void
gs_plugin_job_set_action (GsPluginJob *self, GsPluginAction action)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->action = action;
}

GsPluginAction
gs_plugin_job_get_action (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), GS_PLUGIN_ACTION_UNKNOWN);
	return priv->action;
}

/* FIXME: Find the :app property of the derived class. This will be removed
 * when the remains of the old threading API are removed. */
static gboolean
gs_plugin_job_subclass_has_app_property (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), FALSE);

	return (g_object_class_find_property (G_OBJECT_GET_CLASS (self), "app") != NULL);
}

void
gs_plugin_job_set_app (GsPluginJob *self, GsApp *app)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));

	if (!gs_plugin_job_subclass_has_app_property (self))
		return;

	g_object_set (G_OBJECT (self), "app", app, NULL);
}

GsApp *
gs_plugin_job_get_app (GsPluginJob *self)
{
	g_autoptr(GsApp) app = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);

	if (!gs_plugin_job_subclass_has_app_property (self))
		return NULL;

	g_object_get (G_OBJECT (self), "app", &app, NULL);

	/* Don’t steal the reference, let the additional reference be dropped
	 * because gs_plugin_job_get_app() is (transfer none). The GsPluginJob
	 * still holds one. */
	return app;
}

static void
gs_plugin_job_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsPluginJob *self = GS_PLUGIN_JOB (obj);
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);

	switch (prop_id) {
	case PROP_ACTION:
		g_value_set_enum (value, priv->action);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_job_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsPluginJob *self = GS_PLUGIN_JOB (obj);

	switch (prop_id) {
	case PROP_ACTION:
		gs_plugin_job_set_action (self, g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
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
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_plugin_job_finalize;
	object_class->get_property = gs_plugin_job_get_property;
	object_class->set_property = gs_plugin_job_set_property;

	pspec = g_param_spec_enum ("action", NULL, NULL,
				   GS_TYPE_PLUGIN_ACTION, GS_PLUGIN_ACTION_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ACTION, pspec);

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
