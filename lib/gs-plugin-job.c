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
	GsPluginRefineFlags	 refine_flags;
	GsPluginRefineRequireFlags refine_require_flags;
	GsAppListFilterFlags	 dedupe_flags;
	gboolean		 propagate_error;
	GsPluginAction		 action;
	gint64			 time_created;
	GCancellable		*cancellable;
} GsPluginJobPrivate;

enum {
	PROP_0,
	PROP_ACTION,
	PROP_REFINE_FLAGS,
	PROP_REFINE_REQUIRE_FLAGS,
	PROP_DEDUPE_FLAGS,
	PROP_PROPAGATE_ERROR,
	PROP_LAST
};

typedef enum {
	SIGNAL_COMPLETED,
	SIGNAL_LAST
} GsPluginJobSignal;

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (GsPluginJob, gs_plugin_job, G_TYPE_OBJECT)

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
	if (priv->dedupe_flags > 0)
		g_string_append_printf (str, " with dedupe-flags=%" G_GUINT64_FORMAT, priv->dedupe_flags);
	if (priv->refine_flags > 0) {
		g_autofree gchar *tmp = gs_plugin_refine_flags_to_string (priv->refine_flags);
		g_string_append_printf (str, " with refine-flags=%s", tmp);
	}
	if (priv->refine_require_flags > 0) {
		g_autofree gchar *tmp = gs_plugin_refine_require_flags_to_string (priv->refine_require_flags);
		g_string_append_printf (str, " with refine-require-flags=%s", tmp);
	}
	if (priv->propagate_error)
		g_string_append_printf (str, " with propagate-error=True");

	if (time_now - priv->time_created > 1000) {
		g_string_append_printf (str, ", elapsed time since creation %" G_GINT64_FORMAT "ms",
					(time_now - priv->time_created) / 1000);
	}
	return g_string_free (str, FALSE);
}

void
gs_plugin_job_set_refine_flags (GsPluginJob *self, GsPluginRefineFlags refine_flags)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->refine_flags = refine_flags;
}

void
gs_plugin_job_set_refine_require_flags (GsPluginJob *self, GsPluginRefineRequireFlags refine_require_flags)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->refine_require_flags = refine_require_flags;
}

void
gs_plugin_job_set_dedupe_flags (GsPluginJob *self, GsAppListFilterFlags dedupe_flags)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->dedupe_flags = dedupe_flags;
}

GsPluginRefineFlags
gs_plugin_job_get_refine_flags (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), GS_PLUGIN_REFINE_FLAGS_NONE);
	return priv->refine_flags;
}

GsPluginRefineRequireFlags
gs_plugin_job_get_refine_require_flags (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE);
	return priv->refine_require_flags;
}

GsAppListFilterFlags
gs_plugin_job_get_dedupe_flags (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), GS_APP_LIST_FILTER_FLAG_NONE);
	return priv->dedupe_flags;
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
gs_plugin_job_set_propagate_error (GsPluginJob *self, gboolean propagate_error)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->propagate_error = propagate_error;
}

gboolean
gs_plugin_job_get_propagate_error (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), FALSE);
	return priv->propagate_error;
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
	case PROP_REFINE_FLAGS:
		g_value_set_flags (value, priv->refine_flags);
		break;
	case PROP_REFINE_REQUIRE_FLAGS:
		g_value_set_flags (value, priv->refine_require_flags);
		break;
	case PROP_DEDUPE_FLAGS:
		g_value_set_flags (value, priv->dedupe_flags);
		break;
	case PROP_PROPAGATE_ERROR:
		g_value_set_boolean (value, priv->propagate_error);
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
	case PROP_REFINE_FLAGS:
		gs_plugin_job_set_refine_flags (self, g_value_get_flags (value));
		break;
	case PROP_REFINE_REQUIRE_FLAGS:
		gs_plugin_job_set_refine_require_flags (self, g_value_get_flags (value));
		break;
	case PROP_DEDUPE_FLAGS:
		gs_plugin_job_set_dedupe_flags (self, g_value_get_flags (value));
		break;
	case PROP_PROPAGATE_ERROR:
		gs_plugin_job_set_propagate_error (self, g_value_get_boolean (value));
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

	pspec = g_param_spec_flags ("refine-flags", NULL, NULL,
				    GS_TYPE_PLUGIN_REFINE_FLAGS, GS_PLUGIN_REFINE_FLAGS_NONE,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REFINE_FLAGS, pspec);

	pspec = g_param_spec_flags ("refine-require-flags", NULL, NULL,
				    GS_TYPE_PLUGIN_REFINE_REQUIRE_FLAGS, GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REFINE_REQUIRE_FLAGS, pspec);

	pspec = g_param_spec_flags ("dedupe-flags", NULL, NULL,
				    GS_TYPE_APP_LIST_FILTER_FLAGS, GS_APP_LIST_FILTER_FLAG_NONE,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DEDUPE_FLAGS, pspec);

	pspec = g_param_spec_boolean ("propagate-error", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PROPAGATE_ERROR, pspec);

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

	priv->refine_flags = GS_PLUGIN_REFINE_FLAGS_NONE;
	priv->refine_require_flags = GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE;
	priv->dedupe_flags = GS_APP_LIST_FILTER_FLAG_KEY_ID |
			     GS_APP_LIST_FILTER_FLAG_KEY_SOURCE |
			     GS_APP_LIST_FILTER_FLAG_KEY_VERSION;
	priv->time_created = g_get_monotonic_time ();
}

/**
 * gs_plugin_job_set_cancellable:
 * @self: a #GsPluginJob
 * @cancellable: (nullable) (transfer none): the cancellable to use
 *
 * Sets the #GCancellable which can be used with gs_plugin_job_cancel() to
 * cancel the job.
 *
 * FIXME: This is only needed because #GsPluginLoader implements cancellation
 * outside of the #GsPluginJob for old-style jobs. Once all #GsPluginJob
 * subclasses implement `run_async()`, the #GCancellable passed to that can be
 * stored internally in #GsPluginJob and cancelled from gs_plugin_job_cancel().
 * Then this method will be removed.
 *
 * Since: 45
 */
void
gs_plugin_job_set_cancellable (GsPluginJob *self,
			       GCancellable *cancellable)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);

	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	g_set_object (&priv->cancellable, cancellable);
}

/**
 * gs_plugin_job_cancel:
 * @self: a #GsPluginJob
 *
 * Cancel the plugin job.
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
