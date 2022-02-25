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

#include "gs-app.h"
#include "gs-app-collation.h"
#include "gs-app-private.h"
#include "gs-app-list-private.h"
#include "gs-enums.h"
#include "gs-plugin-job-private.h"
#include "gs-plugin-job-refine.h"
#include "gs-utils.h"

struct _GsPluginJobRefine
{
	GsPluginJob parent;

	/* Input data. */
	GsAppList *app_list;  /* (owned) */
	GsPluginRefineFlags flags;

	/* Output data. */
	GsAppList *result_list;  /* (owned) (nullable) */
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

static gint
review_score_sort_cb (gconstpointer a, gconstpointer b)
{
	AsReview *ra = *((AsReview **) a);
	AsReview *rb = *((AsReview **) b);
	if (as_review_get_priority (ra) < as_review_get_priority (rb))
		return 1;
	if (as_review_get_priority (ra) > as_review_get_priority (rb))
		return -1;
	return 0;
}

static gboolean
app_is_non_wildcard (GsApp *app, gpointer user_data)
{
	return !gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
}

static void plugin_refine_cb (GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data);

static void
plugin_refine_cb (GObject      *source_object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
	GAsyncResult **result_out = user_data;

	g_assert (*result_out == NULL);
	*result_out = g_object_ref (result);
	g_main_context_wakeup (g_main_context_get_thread_default ());
}

static gboolean
run_refine_internal (GsPluginJobRefine    *self,
                     GsPluginLoader       *plugin_loader,
                     GsAppList            *list,
                     GsPluginRefineFlags   flags,
                     GCancellable         *cancellable,
                     GError              **error)
{
	GsOdrsProvider *odrs_provider;
	GsOdrsProviderRefineFlags odrs_refine_flags = 0;
	GPtrArray *plugins;  /* (element-type GsPlugin) */

	/* try to adopt each application with a plugin */
	gs_plugin_loader_run_adopt (plugin_loader, list);

	/* run each plugin */
	plugins = gs_plugin_loader_get_plugins (plugin_loader);

	for (guint i = 0; i < plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (plugins, i);
		GsPluginClass *plugin_class = GS_PLUGIN_GET_CLASS (plugin);
		g_autoptr(GAsyncResult) refine_result = NULL;

		if (!gs_plugin_get_enabled (plugin))
			continue;
		if (plugin_class->refine_async == NULL)
			continue;

		/* run the batched plugin symbol */
		plugin_class->refine_async (plugin, list, flags,
					    cancellable, plugin_refine_cb, &refine_result);

		/* FIXME: Make this sync until the calling function is rearranged
		 * to be async. */
		while (refine_result == NULL)
			g_main_context_iteration (g_main_context_get_thread_default (), TRUE);

		if (!plugin_class->refine_finish (plugin, refine_result, error))
			return FALSE;

		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* Add ODRS data if needed */
	odrs_provider = gs_plugin_loader_get_odrs_provider (plugin_loader);

	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS)
		odrs_refine_flags |= GS_ODRS_PROVIDER_REFINE_FLAGS_GET_REVIEWS;
	if (flags & (GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS |
		     GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING))
		odrs_refine_flags |= GS_ODRS_PROVIDER_REFINE_FLAGS_GET_RATINGS;

	if (odrs_provider != NULL && odrs_refine_flags != 0) {
		g_autoptr(GAsyncResult) odrs_refine_result = NULL;

		gs_odrs_provider_refine_async (odrs_provider, list, odrs_refine_flags,
					       cancellable, plugin_refine_cb, &odrs_refine_result);

		/* FIXME: Make this sync until the calling function is rearranged
		 * to be async. */
		while (odrs_refine_result == NULL)
			g_main_context_iteration (g_main_context_get_thread_default (), TRUE);

		if (!gs_odrs_provider_refine_finish (odrs_provider, odrs_refine_result, error))
			return FALSE;
	}

	/* filter any wildcard apps left in the list */
	gs_app_list_filter (list, app_is_non_wildcard, NULL);

	/* ensure these are sorted by score */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS) {
		GPtrArray *reviews;
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			reviews = gs_app_get_reviews (app);
			g_ptr_array_sort (reviews, review_score_sort_cb);
		}
	}

	/* refine addons one layer deep */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS) {
		g_autoptr(GsAppList) addons_list = gs_app_list_new ();
		GsPluginRefineFlags addons_flags = flags;

		addons_flags &= ~(GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS |
				  GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS |
				  GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS);

		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			GsAppList *addons = gs_app_get_addons (app);
			for (guint j = 0; j < gs_app_list_length (addons); j++) {
				GsApp *addon = gs_app_list_index (addons, j);
				g_debug ("refining app %s addon %s",
					 gs_app_get_id (app),
					 gs_app_get_id (addon));
				gs_app_list_add (addons_list, addon);
			}
		}

		if (gs_app_list_length (addons_list) > 0 && addons_flags != 0) {
			if (!run_refine_internal (self, plugin_loader,
						  addons_list, addons_flags,
						  cancellable, error)) {
				return FALSE;
			}
		}
	}

	/* also do runtime */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME) {
		g_autoptr(GsAppList) runtimes_list = gs_app_list_new ();
		GsPluginRefineFlags runtimes_flags = flags;

		runtimes_flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME;

		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			GsApp *runtime = gs_app_get_runtime (app);

			if (runtime != NULL)
				gs_app_list_add (runtimes_list, runtime);
		}

		if (gs_app_list_length (runtimes_list) > 0 && runtimes_flags != 0) {
			if (!run_refine_internal (self, plugin_loader,
						  runtimes_list, runtimes_flags,
						  cancellable, error)) {
				return FALSE;
			}
		}
	}

	/* also do related packages one layer deep */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED) {
		g_autoptr(GsAppList) related_list = gs_app_list_new ();
		GsPluginRefineFlags related_flags = flags;

		related_flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED;

		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			GsAppList *related = gs_app_get_related (app);
			for (guint j = 0; j < gs_app_list_length (related); j++) {
				GsApp *app2 = gs_app_list_index (related, j);
				g_debug ("refining related: %s[%s]",
					 gs_app_get_id (app2),
					 gs_app_get_source_default (app2));
				gs_app_list_add (related_list, app2);
			}
		}

		if (gs_app_list_length (related_list) > 0 && related_flags != 0) {
			if (!run_refine_internal (self, plugin_loader,
						  related_list, related_flags,
						  cancellable, error)) {
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

static gboolean
app_thaw_notify_idle (gpointer data)
{
	GsApp *app = GS_APP (data);
	g_object_thaw_notify (G_OBJECT (app));
	g_object_unref (app);
	return G_SOURCE_REMOVE;
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
	g_task_set_source_tag (task, gs_plugin_job_refine_run_async);

	/* Operate on a copy of the input list so we don’t modify it when
	 * resolving wildcards. */
	result_list = gs_app_list_copy (self->app_list);

	/* nothing to do */
	if (self->flags == 0) {
		g_debug ("no refine flags set for transaction");
		goto results;
	}
	if (gs_app_list_length (result_list) == 0)
		goto results;

	/* freeze all apps */
	for (guint i = 0; i < gs_app_list_length (self->app_list); i++) {
		GsApp *app = gs_app_list_index (self->app_list, i);
		g_object_freeze_notify (G_OBJECT (app));
	}

	/* first pass */
	if (run_refine_internal (self, plugin_loader, result_list, self->flags, cancellable, &local_error)) {
		/* remove any addons that have the same source as the parent app */
		for (guint i = 0; i < gs_app_list_length (result_list); i++) {
			g_autoptr(GPtrArray) to_remove = g_ptr_array_new ();
			GsApp *app = gs_app_list_index (result_list, i);
			GsAppList *addons = gs_app_get_addons (app);

			/* find any apps with the same source */
			const gchar *pkgname_parent = gs_app_get_source_default (app);
			if (pkgname_parent == NULL)
				continue;
			for (guint j = 0; j < gs_app_list_length (addons); j++) {
				GsApp *addon = gs_app_list_index (addons, j);
				if (g_strcmp0 (gs_app_get_source_default (addon),
					       pkgname_parent) == 0) {
					g_debug ("%s has the same pkgname of %s as %s",
						 gs_app_get_unique_id (app),
						 pkgname_parent,
						 gs_app_get_unique_id (addon));
					g_ptr_array_add (to_remove, addon);
				}
			}

			/* remove any addons with the same source */
			for (guint j = 0; j < to_remove->len; j++) {
				GsApp *addon = g_ptr_array_index (to_remove, j);
				gs_app_remove_addon (app, addon);
			}
		}
	}

	/* now emit all the changed signals */
	for (guint i = 0; i < gs_app_list_length (self->app_list); i++) {
		GsApp *app = gs_app_list_index (self->app_list, i);
		g_idle_add (app_thaw_notify_idle, g_object_ref (app));
	}

	/* Delayed error handling. */
	if (local_error != NULL) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

results:
	/* Internal calls to #GsPluginJobRefine may want to do their own
	 * filtering, typically if the refine is being done as part of another
	 * plugin job. If so, only filter to remove wildcards. Wildcards should
	 * always be removed, as they should have been resolved as part of the
	 * refine; any remaining wildcards will never be resolved.
	 *
	 * If the flag is not specified, filter by a variety of indicators of
	 * what a ‘valid’ app is. */
	if (self->flags & GS_PLUGIN_REFINE_FLAGS_DISABLE_FILTERING)
		gs_app_list_filter (result_list, app_is_non_wildcard, NULL);
	else
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
