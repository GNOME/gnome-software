/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-plugin-job-get-langpacks
 * @short_description: A plugin job on an app
 *
 * #GsPluginJobGetLangpacks is a #GsPluginJob representing an operation to
 * list language packs, as per given language code or locale, e.g. "ja" or "ja_JP".
 *
 * This class is a wrapper around #GsPluginClass.get_langpacks_async
 * calling it for all loaded plugins.
 *
 * Retrieve the resulting #GsAppList using
 * gs_plugin_job_get_langpacks_get_result_list().
 *
 * Since: 47
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>

#include "gs-app.h"
#include "gs-enums.h"
#include "gs-plugin-job.h"
#include "gs-plugin-job-get-langpacks.h"
#include "gs-plugin-job-private.h"
#include "gs-plugin-job-refine.h"
#include "gs-plugin-types.h"

struct _GsPluginJobGetLangpacks
{
	GsPluginJob parent;

	/* Input arguments. */
	gchar *locale;  /* (owned) (not nullable) */
	GsPluginGetLangpacksFlags flags;

	/* In-progress data. */
	GError *saved_error;  /* (owned) (nullable) */
	guint n_pending_ops;
	gboolean did_refine;
	GsAppList *in_progress_list;  /* (owned) (nullable) */

	/* Results. */
	GsAppList *result_list;  /* (owned) (nullable) */
};

G_DEFINE_TYPE (GsPluginJobGetLangpacks, gs_plugin_job_get_langpacks, GS_TYPE_PLUGIN_JOB)

typedef enum {
	PROP_FLAGS = 1,
	PROP_LOCALE,
} GsPluginJobGetLangpacksProperty;

static GParamSpec *props[PROP_LOCALE + 1] = { NULL, };

static void
gs_plugin_job_get_langpacks_dispose (GObject *object)
{
	GsPluginJobGetLangpacks *self = GS_PLUGIN_JOB_GET_LANGPACKS (object);

	g_assert (self->saved_error == NULL);
	g_assert (self->n_pending_ops == 0);

	g_clear_pointer (&self->locale, g_free);
	g_clear_object (&self->result_list);
	g_clear_object (&self->in_progress_list);

	G_OBJECT_CLASS (gs_plugin_job_get_langpacks_parent_class)->dispose (object);
}

static void
gs_plugin_job_get_langpacks_get_property (GObject    *object,
					  guint       prop_id,
					  GValue     *value,
					  GParamSpec *pspec)
{
	GsPluginJobGetLangpacks *self = GS_PLUGIN_JOB_GET_LANGPACKS (object);

	switch ((GsPluginJobGetLangpacksProperty) prop_id) {
	case PROP_FLAGS:
		g_value_set_flags (value, self->flags);
		break;
	case PROP_LOCALE:
		g_value_set_string (value, self->locale);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_job_get_langpacks_set_property (GObject      *object,
					  guint         prop_id,
					  const GValue *value,
					  GParamSpec   *pspec)
{
	GsPluginJobGetLangpacks *self = GS_PLUGIN_JOB_GET_LANGPACKS (object);

	switch ((GsPluginJobGetLangpacksProperty) prop_id) {
	case PROP_FLAGS:
		/* Construct only. */
		g_assert (self->flags == 0);
		self->flags = g_value_get_flags (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	case PROP_LOCALE:
		/* Construct only. */
		g_assert (self->locale == NULL);
		self->locale = g_value_dup_string (value);
		g_assert (self->locale != NULL);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gboolean
gs_plugin_job_get_langpacks_get_interactive (GsPluginJob *job)
{
	GsPluginJobGetLangpacks *self = GS_PLUGIN_JOB_GET_LANGPACKS (job);
	return (self->flags & GS_PLUGIN_GET_LANGPACKS_FLAGS_INTERACTIVE) != 0;
}

static void plugin_app_func_cb (GObject      *source_object,
				GAsyncResult *result,
				gpointer      user_data);
static void finish_op (GTask *task,
		       GsAppList *list,
                       GError *error);

static void
gs_plugin_job_get_langpacks_run_async (GsPluginJob         *job,
				       GsPluginLoader      *plugin_loader,
				       GCancellable        *cancellable,
				       GAsyncReadyCallback  callback,
				       gpointer             user_data)
{
	GsPluginJobGetLangpacks *self = GS_PLUGIN_JOB_GET_LANGPACKS (job);
	g_autoptr(GTask) task = NULL;
	GPtrArray *plugins;  /* (element-type GsPlugin) */
	gboolean anything_ran = FALSE;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (job, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_job_get_langpacks_run_async);
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
		if (plugin_class->get_langpacks_async == NULL)
			continue;

		/* at least one plugin supports this vfunc */
		anything_ran = TRUE;

		/* Handle cancellation */
		if (g_cancellable_set_error_if_cancelled (cancellable, &local_error))
			break;

		/* run the plugin */
		self->n_pending_ops++;
		plugin_class->get_langpacks_async (plugin, self->locale, self->flags, cancellable, plugin_app_func_cb, g_object_ref (task));
	}

	if (!anything_ran)
		g_debug ("no plugin could handle get-langpacks operation");

	finish_op (task, NULL, g_steal_pointer (&local_error));
}

static void
plugin_app_func_cb (GObject      *source_object,
		    GAsyncResult *result,
		    gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	GsPluginClass *plugin_class = GS_PLUGIN_GET_CLASS (plugin);
	g_autoptr(GTask) task = G_TASK (user_data);
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) local_error = NULL;

	list = plugin_class->get_langpacks_finish (plugin, result, &local_error);
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);

	g_assert (list != NULL || local_error != NULL);

	finish_op (task, list, g_steal_pointer (&local_error));
}

static void
refine_job_finished_cb (GObject *source_object,
			GAsyncResult *result,
			gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) local_error = NULL;
	list = gs_plugin_loader_job_process_finish (GS_PLUGIN_LOADER (source_object), result, &local_error);
	g_prefix_error_literal (&local_error, "failed to refine get-langpacks apps:");
	finish_op (task, list, g_steal_pointer (&local_error));
}

/* @error is (transfer full) if non-%NULL */
static void
finish_op (GTask  *task,
	   GsAppList *list,
           GError *error)
{
	GsPluginJobGetLangpacks *self = g_task_get_source_object (task);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);
	g_autofree gchar *job_debug = NULL;

	if (error_owned != NULL && self->saved_error == NULL)
		self->saved_error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while get-langpacks: %s", error_owned->message);

	g_assert (self->n_pending_ops > 0);
	self->n_pending_ops--;

	if (list != NULL) {
		if (self->did_refine) {
			g_set_object (&self->in_progress_list, list);
		} else {
			if (self->in_progress_list == NULL)
				self->in_progress_list = gs_app_list_new ();
			gs_app_list_add_list (self->in_progress_list, list);
		}
	}

	if (self->n_pending_ops > 0)
		return;

	if (!self->did_refine && self->in_progress_list != NULL) {
		GsPluginRefineFlags refine_flags = gs_plugin_job_get_refine_flags (GS_PLUGIN_JOB (self));
		if (refine_flags != GS_PLUGIN_REFINE_FLAGS_NONE) {
			g_autoptr(GsPluginJob) refine_job = NULL;
			GsPluginLoader *plugin_loader = g_task_get_task_data (task);
			GsPluginRefineJobFlags job_flags = gs_plugin_job_get_refine_job_flags (GS_PLUGIN_JOB (self));
			self->did_refine = TRUE;
			self->n_pending_ops++;

			refine_job = gs_plugin_job_refine_new (self->in_progress_list, job_flags, refine_flags);
			gs_plugin_loader_job_process_async (plugin_loader, refine_job, g_task_get_cancellable (task),
							    refine_job_finished_cb, g_object_ref (task));
			return;
		}
	}

	g_clear_object (&self->result_list);
	self->result_list = g_steal_pointer (&self->in_progress_list);

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
gs_plugin_job_get_langpacks_run_finish (GsPluginJob   *self,
					GAsyncResult  *result,
					GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_job_get_langpacks_class_init (GsPluginJobGetLangpacksClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginJobClass *job_class = GS_PLUGIN_JOB_CLASS (klass);

	object_class->dispose = gs_plugin_job_get_langpacks_dispose;
	object_class->get_property = gs_plugin_job_get_langpacks_get_property;
	object_class->set_property = gs_plugin_job_get_langpacks_set_property;

	job_class->get_interactive = gs_plugin_job_get_langpacks_get_interactive;
	job_class->run_async = gs_plugin_job_get_langpacks_run_async;
	job_class->run_finish = gs_plugin_job_get_langpacks_run_finish;

	/**
	 * GsPluginJobGetLangpacks:locale: (not nullable)
	 *
	 * A locale to run the operation on.
	 *
	 * Since: 47
	 */
	props[PROP_LOCALE] =
		g_param_spec_string ("locale", "Locale",
				     "A locale to run the operation on.",
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginJobGetLangpacks:flags:
	 *
	 * Flags affecting how the operation runs.
	 *
	 * Since: 47
	 */
	props[PROP_FLAGS] =
		g_param_spec_flags ("flags", "Flags",
				    "Flags affecting how the operation runs.",
				    GS_TYPE_PLUGIN_GET_LANGPACKS_FLAGS,
				    GS_PLUGIN_GET_LANGPACKS_FLAGS_NONE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
gs_plugin_job_get_langpacks_init (GsPluginJobGetLangpacks *self)
{
}

/**
 * gs_plugin_job_get_langpacks_new:
 * @locale: (not nullable): a locale to run the operation on
 * @flags: flags affecting how the operation runs
 *
 * Create a new #GsPluginJobGetLangpacks to get packages for the given @locale.
 *
 * Returns: (transfer full): a new #GsPluginJobGetLangpacks
 * Since: 47
 */
GsPluginJob *
gs_plugin_job_get_langpacks_new (const gchar		  *locale,
				 GsPluginGetLangpacksFlags flags)
{
	g_return_val_if_fail (locale != NULL, NULL);

	return g_object_new (GS_TYPE_PLUGIN_JOB_GET_LANGPACKS,
			     "locale", locale,
			     "flags", flags,
			     NULL);
}

/**
 * gs_plugin_job_get_langpacks_get_result_list:
 * @self: a #GsPluginJobGetLangpacks
 *
 * Get the list of packages for the given locale.
 *
 * If this is called before the job is complete, %NULL will be returned.
 *
 * Returns: (transfer none) (nullable): the job results, or %NULL on error
 *   or if called before the job has completed
 *
 * Since: 47
 */
GsAppList *
gs_plugin_job_get_langpacks_get_result_list (GsPluginJobGetLangpacks *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB_GET_LANGPACKS (self), NULL);

	return self->result_list;
}
