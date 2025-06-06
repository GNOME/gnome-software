/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-plugin-job-trigger-upgrade
 * @short_description: A plugin job on an app
 *
 * #GsPluginJobTriggerUpgrade is a #GsPluginJob to trigger the distribution
 * upgrade to be installed on next boot.
 *
 * This class is a wrapper around #GsPluginClass.trigger_upgrade_async
 * calling it for all loaded plugins.
 *
 * Since: 47
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>

#include "gs-app.h"
#include "gs-enums.h"
#include "gs-plugin-job.h"
#include "gs-plugin-job-trigger-upgrade.h"
#include "gs-plugin-job-private.h"
#include "gs-plugin-types.h"

struct _GsPluginJobTriggerUpgrade
{
	GsPluginJob parent;

	/* Input arguments. */
	GsApp *app;  /* (owned) (not nullable) */
	GsPluginTriggerUpgradeFlags flags;

	/* In-progress data. */
	GError *saved_error;  /* (owned) (nullable) */
	guint n_pending_ops;
};

G_DEFINE_TYPE (GsPluginJobTriggerUpgrade, gs_plugin_job_trigger_upgrade, GS_TYPE_PLUGIN_JOB)

typedef enum {
	PROP_FLAGS = 1,
	PROP_APP,
} GsPluginJobTriggerUpgradeProperty;

static GParamSpec *props[PROP_APP + 1] = { NULL, };

static void
gs_plugin_job_trigger_upgrade_dispose (GObject *object)
{
	GsPluginJobTriggerUpgrade *self = GS_PLUGIN_JOB_TRIGGER_UPGRADE (object);

	g_assert (self->saved_error == NULL);
	g_assert (self->n_pending_ops == 0);

	g_clear_object (&self->app);

	G_OBJECT_CLASS (gs_plugin_job_trigger_upgrade_parent_class)->dispose (object);
}

static void
gs_plugin_job_trigger_upgrade_get_property (GObject *object,
					    guint prop_id,
					    GValue *value,
					    GParamSpec *pspec)
{
	GsPluginJobTriggerUpgrade *self = GS_PLUGIN_JOB_TRIGGER_UPGRADE (object);

	switch ((GsPluginJobTriggerUpgradeProperty) prop_id) {
	case PROP_FLAGS:
		g_value_set_flags (value, self->flags);
		break;
	case PROP_APP:
		g_value_set_object (value, self->app);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_job_trigger_upgrade_set_property (GObject *object,
					    guint prop_id,
					    const GValue *value,
					    GParamSpec *pspec)
{
	GsPluginJobTriggerUpgrade *self = GS_PLUGIN_JOB_TRIGGER_UPGRADE (object);

	switch ((GsPluginJobTriggerUpgradeProperty) prop_id) {
	case PROP_FLAGS:
		/* Construct only. */
		g_assert (self->flags == 0);
		self->flags = g_value_get_flags (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	case PROP_APP:
		/* Construct only. */
		g_assert (self->app == NULL);
		self->app = g_value_dup_object (value);
		g_assert (self->app != NULL);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
gs_plugin_job_trigger_upgrade_get_interactive (GsPluginJob *job)
{
	GsPluginJobTriggerUpgrade *self = GS_PLUGIN_JOB_TRIGGER_UPGRADE (job);
	return (self->flags & GS_PLUGIN_TRIGGER_UPGRADE_FLAGS_INTERACTIVE) != 0;
}

static void plugin_app_func_cb (GObject      *source_object,
				GAsyncResult *result,
				gpointer      user_data);
static void finish_op (GTask  *task,
                       GError *error);

static void
gs_plugin_job_trigger_upgrade_run_async (GsPluginJob         *job,
					 GsPluginLoader      *plugin_loader,
					 GCancellable        *cancellable,
					 GAsyncReadyCallback  callback,
					 gpointer             user_data)
{
	GsPluginJobTriggerUpgrade *self = GS_PLUGIN_JOB_TRIGGER_UPGRADE (job);
	g_autoptr(GTask) task = NULL;
	GPtrArray *plugins;  /* (element-type GsPlugin) */
	gboolean anything_ran = FALSE;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (job, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_job_trigger_upgrade_run_async);
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
		if (plugin_class->trigger_upgrade_async == NULL)
			continue;

		/* at least one plugin supports this vfunc */
		anything_ran = TRUE;

		/* Handle cancellation */
		if (g_cancellable_set_error_if_cancelled (cancellable, &local_error))
			break;

		/* run the plugin */
		self->n_pending_ops++;
		plugin_class->trigger_upgrade_async (plugin, self->app, self->flags, cancellable, plugin_app_func_cb, g_object_ref (task));
	}

	if (!anything_ran) {
		g_set_error_literal (&local_error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no plugin could handle triggering a distribution upgrade");
	}

	finish_op (task, g_steal_pointer (&local_error));
}

static void
plugin_app_func_cb (GObject      *source_object,
		    GAsyncResult *result,
		    gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	GsPluginClass *plugin_class = GS_PLUGIN_GET_CLASS (plugin);
	g_autoptr(GTask) task = G_TASK (user_data);
	gboolean success;
	g_autoptr(GError) local_error = NULL;

	success = plugin_class->trigger_upgrade_finish (plugin, result, &local_error);

	g_assert (success || local_error != NULL);

	finish_op (task, g_steal_pointer (&local_error));
}

/* @error is (transfer full) if non-%NULL */
static void
finish_op (GTask  *task,
           GError *error)
{
	GsPluginJobTriggerUpgrade *self = g_task_get_source_object (task);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);
	g_autofree gchar *job_debug = NULL;

	if (error_owned != NULL && self->saved_error == NULL)
		self->saved_error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while triggering upgrade: %s", error_owned->message);

	g_assert (self->n_pending_ops > 0);
	self->n_pending_ops--;

	if (self->n_pending_ops > 0)
		return;

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
gs_plugin_job_trigger_upgrade_run_finish (GsPluginJob   *self,
					  GAsyncResult  *result,
					  GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_job_trigger_upgrade_class_init (GsPluginJobTriggerUpgradeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginJobClass *job_class = GS_PLUGIN_JOB_CLASS (klass);

	object_class->dispose = gs_plugin_job_trigger_upgrade_dispose;
	object_class->get_property = gs_plugin_job_trigger_upgrade_get_property;
	object_class->set_property = gs_plugin_job_trigger_upgrade_set_property;

	job_class->get_interactive = gs_plugin_job_trigger_upgrade_get_interactive;
	job_class->run_async = gs_plugin_job_trigger_upgrade_run_async;
	job_class->run_finish = gs_plugin_job_trigger_upgrade_run_finish;

	/**
	 * GsPluginJobTriggerUpgrade:app:
	 *
	 * A #GsApp describing the app to run the operation on.
	 *
	 * Since: 47
	 */
	props[PROP_APP] =
		g_param_spec_object ("app", "App",
				     "A #GsApp describing the app to run the operation on.",
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginJobTriggerUpgrade:flags:
	 *
	 * Flags affecting how the operation runs.
	 *
	 * Since: 47
	 */
	props[PROP_FLAGS] =
		g_param_spec_flags ("flags", "Flags",
				    "Flags affecting how the operation runs.",
				    GS_TYPE_PLUGIN_TRIGGER_UPGRADE_FLAGS,
				    GS_PLUGIN_TRIGGER_UPGRADE_FLAGS_NONE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
gs_plugin_job_trigger_upgrade_init (GsPluginJobTriggerUpgrade *self)
{
}

/**
 * gs_plugin_job_trigger_upgrade_new:
 * @app: (not nullable) (transfer none): an app to run the operation on
 * @flags: flags affecting how the operation runs
 *
 * Create a new #GsPluginJobTriggerUpgrade to trigger an upgrade of the system
 * represented by @app.
 *
 * Returns: (transfer full): a new #GsPluginJobTriggerUpgrade
 * Since: 47
 */
GsPluginJob *
gs_plugin_job_trigger_upgrade_new (GsApp *app,
				   GsPluginTriggerUpgradeFlags flags)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);

	return g_object_new (GS_TYPE_PLUGIN_JOB_TRIGGER_UPGRADE,
			     "app", app,
			     "flags", flags,
			     NULL);
}

/**
 * gs_plugin_job_trigger_upgrade_get_app:
 * @self: a #GsPluginJobTriggerUpgrade
 *
 * Get the app being upgraded by this #GsPluginJobTriggerUpgrade.
 *
 * Typically this is a #GsApp representing the whole system.
 *
 * Returns: (transfer none) (not nullable): app being upgraded
 * Since: 49
 */
GsApp *
gs_plugin_job_trigger_upgrade_get_app (GsPluginJobTriggerUpgrade *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB_TRIGGER_UPGRADE (self), NULL);

	return self->app;
}
