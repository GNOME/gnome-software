/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-plugin-job-refine
 * @short_description: A plugin job to refine #GsApps and add more data
 *
 * #GsPluginJobRefine is a #GsPluginJob representing a refine operation.
 *
 * It’s used to query and add more data to a set of #GsApps. The data to be set
 * is controlled by the #GsPluginRefineFlags, and is looked up for all the apps
 * in a #GsAppList by the loaded plugins.
 *
 * This class is a wrapper around #GsPluginClass.refine_async, calling it for
 * all loaded plugins, with some additional refinements done on the results.
 *
 * In particular, if an app in the #GsAppList has %GS_APP_QUIRK_IS_WILDCARD,
 * refining it will replace it with zero or more non-wildcard #GsApps in the
 * #GsAppList, all of which are candidates for what the wildcard represents.
 * For example, they may have the same ID as the wildcard, or match its name.
 * Refining is the canonical process for resolving wildcards.
 *
 * This means that the #GsAppList at the end of the refine operation may not
 * match the #GsAppList passed in as input. Retrieve the final #GsAppList using
 * gs_plugin_job_refine_get_result_list(). The #GsAppList which was passed
 * into the job will not be modified.
 *
 * See also: #GsPluginClass.refine_async
 * Since: 42
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include "gs-enums.h"
#include "gs-plugin-job-private.h"
#include "gs-plugin-job-refine.h"
#include "gs-utils.h"

struct _GsPluginJobRefine
{
	GsPluginJob parent;

	GsAppList *app_list;  /* (owned) */
	GsAppList *result_list;  /* (owned) (nullable) */
	GsPluginRefineFlags flags;
};

G_DEFINE_TYPE (GsPluginJobRefine, gs_plugin_job_refine, GS_TYPE_PLUGIN_JOB)

typedef enum {
	PROP_APP_LIST = 1,
	PROP_FLAGS,
} GsPluginJobRefineProperty;

static GParamSpec *props[PROP_FLAGS + 1] = { NULL, };

static void
gs_plugin_job_refine_dispose (GObject *object)
{
	GsPluginJobRefine *self = GS_PLUGIN_JOB_REFINE (object);

	g_clear_object (&self->app_list);
	g_clear_object (&self->result_list);

	G_OBJECT_CLASS (gs_plugin_job_refine_parent_class)->dispose (object);
}

static void
gs_plugin_job_refine_constructed (GObject *object)
{
	GsPluginJobRefine *self = GS_PLUGIN_JOB_REFINE (object);

	G_OBJECT_CLASS (gs_plugin_job_refine_parent_class)->constructed (object);

	/* FIXME: the plugins should specify this, rather than hardcoding */
	if (self->flags & (GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_UI |
			   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME))
		self->flags |= GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN;
	if (self->flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE)
		self->flags |= GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME;
}

static void
gs_plugin_job_refine_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	GsPluginJobRefine *self = GS_PLUGIN_JOB_REFINE (object);

	switch ((GsPluginJobRefineProperty) prop_id) {
	case PROP_APP_LIST:
		g_value_set_object (value, self->app_list);
		break;
	case PROP_FLAGS:
		g_value_set_flags (value, self->flags);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_job_refine_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	GsPluginJobRefine *self = GS_PLUGIN_JOB_REFINE (object);

	switch ((GsPluginJobRefineProperty) prop_id) {
	case PROP_APP_LIST:
		/* Construct only. */
		g_assert (self->app_list == NULL);
		self->app_list = g_value_dup_object (value);
		g_object_notify_by_pspec (object, props[PROP_APP_LIST]);
		break;
	case PROP_FLAGS:
		/* Construct only. */
		g_assert (self->flags == 0);
		self->flags = g_value_get_flags (value);
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
	GsPluginJobRefine *self = GS_PLUGIN_JOB_REFINE (user_data);

	return gs_plugin_loader_app_is_valid (app, self->flags);
}

static void
gs_plugin_job_refine_run_async (GsPluginJob         *job,
                                GsPluginLoader      *plugin_loader,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
	GsPluginJobRefine *self = GS_PLUGIN_JOB_REFINE (job);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *job_debug = NULL;
	g_autoptr(GsAppList) result_list = NULL;

	/* check required args */
	task = g_task_new (job, cancellable, callback, user_data);
	g_task_set_name (task, G_STRFUNC);

	/* Operate on a copy of the input list so we don’t modify it when
	 * resolving wildcards. */
	result_list = gs_app_list_copy (self->app_list);

	/* run refine() on each one if required */
	if (self->flags != 0) {
		if (!gs_plugin_loader_run_refine (helper, result_list, cancellable, &local_error)) {
			gs_utils_error_convert_gio (&local_error);
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	} else {
		g_debug ("no refine flags set for transaction");
	}

	gs_app_list_filter (result_list, app_is_valid_filter, self);

	/* show elapsed time */
	job_debug = gs_plugin_job_to_string (job);
	g_debug ("%s", job_debug);

	/* success */
	g_set_object (&self->result_list, result_list);
	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_job_refine_run_finish (GsPluginJob   *self,
                                 GAsyncResult  *result,
                                 GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_job_refine_class_init (GsPluginJobRefineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginJobClass *job_class = GS_PLUGIN_JOB_CLASS (klass);

	object_class->dispose = gs_plugin_job_refine_dispose;
	object_class->constructed = gs_plugin_job_refine_constructed;
	object_class->get_property = gs_plugin_job_refine_get_property;
	object_class->set_property = gs_plugin_job_refine_set_property;

	job_class->run_async = gs_plugin_job_refine_run_async;
	job_class->run_finish = gs_plugin_job_refine_run_finish;

	/**
	 * GsPluginJobRefine:app-list:
	 *
	 * List of #GsApps to refine.
	 *
	 * This will not change during the course of the operation.
	 *
	 * Since: 42
	 */
	props[PROP_APP_LIST] =
		g_param_spec_object ("app-list", "App List",
				     "List of GsApps to refine.",
				     GS_TYPE_APP_LIST,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginJobRefine:flags:
	 *
	 * Flags to control what to refine.
	 *
	 * Since: 42
	 */
	props[PROP_FLAGS] =
		g_param_spec_flags ("flags", "Flags",
				    "Flags to control what to refine.",
				     GS_TYPE_PLUGIN_REFINE_FLAGS, GS_PLUGIN_REFINE_FLAGS_NONE,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
gs_plugin_job_refine_init (GsPluginJobRefine *self)
{
}

/**
 * gs_plugin_job_refine_new:
 * @app_list: the list of #GsApps to refine
 * @flags: flags to affect what is refined
 *
 * Create a new #GsPluginJobRefine for refining the given @app_list.
 *
 * Returns: (transfer full): a new #GsPluginJobRefine
 * Since: 42
 */
GsPluginJob *
gs_plugin_job_refine_new (GsAppList           *app_list,
                          GsPluginRefineFlags  flags)
{
	return g_object_new (GS_TYPE_PLUGIN_JOB_REFINE,
			     "app-list", app_list,
			     "flags", flags,
			     NULL);
}

/**
 * gs_plugin_job_refine_new_for_app:
 * @app: the #GsApp to refine
 * @flags: flags to affect what is refined
 *
 * Create a new #GsPluginJobRefine for refining the given @app.
 *
 * Returns: (transfer full): a new #GsPluginJobRefine
 * Since: 42
 */
GsPluginJob *
gs_plugin_job_refine_new_for_app (GsApp               *app,
                                  GsPluginRefineFlags  flags)
{
	g_autoptr(GsAppList) list = gs_app_list_new ();
	gs_app_list_add (list, app);

	return gs_plugin_job_refine_new (list, flags);
}

/**
 * gs_plugin_job_refine_get_result_list:
 * @self: a #GsPluginJobRefine
 *
 * Get the full list of refined #GsApps. This includes apps created in place of
 * wildcards, if wildcards were provided in the #GsAppList passed to
 * gs_plugin_job_refine_new().
 *
 * If this is called before the job is complete, %NULL will be returned.
 *
 * Returns: (transfer none) (nullable): the job results, or %NULL on error
 *   or if called before the job has completed
 * Since: 42
 */
GsAppList *
gs_plugin_job_refine_get_result_list (GsPluginJobRefine *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB_REFINE (self), NULL);

	return self->result_list;
}
