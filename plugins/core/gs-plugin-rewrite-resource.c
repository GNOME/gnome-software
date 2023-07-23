/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gnome-software.h>

#include "gs-plugin-rewrite-resource.h"

/*
 * SECTION:
 * Rewrites CSS metadata for apps to refer to locally downloaded resources.
 *
 * This plugin rewrites the CSS of apps to refer to locally cached resources,
 * rather than HTTP/HTTPS URIs for images (for example).
 *
 * FIXME: Eventually this should move into the refine plugin job, as it needs
 * to execute after all other refine jobs (in order to see all the URIs which
 * they produce).
 */

struct _GsPluginRewriteResource
{
	GsPlugin	parent;
};

G_DEFINE_TYPE (GsPluginRewriteResource, gs_plugin_rewrite_resource, GS_TYPE_PLUGIN)

static void
gs_plugin_rewrite_resource_init (GsPluginRewriteResource *self)
{
	/* let appstream add metadata first */
	gs_plugin_add_rule (GS_PLUGIN (self), GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

typedef struct {
	GError *saved_error;  /* (owned) (nullable) */
	guint n_pending_ops;

#ifdef HAVE_SYSPROF
	gint64 begin_time_nsec;
#endif
} RewriteResourcesData;

static void
rewrite_resources_data_free (RewriteResourcesData *data)
{
	g_assert (data->saved_error == NULL);
	g_assert (data->n_pending_ops == 0);

	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RewriteResourcesData, rewrite_resources_data_free)

typedef struct {
	GTask *task;  /* (owned) (not nullable) */
	GsApp *app;  /* (owned) (not nullable) */
	const gchar *key;  /* (not nullable) */
} OpData;

static void
op_data_free (OpData *data)
{
	g_clear_object (&data->task);
	g_clear_object (&data->app);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (OpData, op_data_free)

static void rewrite_resource_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);
static void finish_op (GTask  *task,
                       GError *error);

static void
gs_rewrite_resources_async (GsAppList           *list,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
	g_autoptr(GTask) task = NULL;
	RewriteResourcesData *data;
	g_autoptr(RewriteResourcesData) data_owned = NULL;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (NULL, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_rewrite_resources_async);

	data = data_owned = g_new0 (RewriteResourcesData, 1);
	data->n_pending_ops = 1;  /* count setup as an operation */

	g_task_set_task_data (task, g_steal_pointer (&data_owned), (GDestroyNotify) rewrite_resources_data_free);

#ifdef HAVE_SYSPROF
	data->begin_time_nsec = SYSPROF_CAPTURE_CURRENT_TIME;
#endif

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		const gchar *keys[] = {
			"GnomeSoftware::FeatureTile-css",
			"GnomeSoftware::UpgradeBanner-css",
			NULL
		};

		/* Handle cancellation */
		if (g_cancellable_set_error_if_cancelled (cancellable, &local_error))
			break;

		/* rewrite URIs */
		for (gsize j = 0; keys[j] != NULL; j++) {
			const gchar *css = gs_app_get_metadata_item (app, keys[j]);
			g_autoptr(OpData) op_data = NULL;

			if (css == NULL)
				continue;

			op_data = g_new0 (OpData, 1);
			op_data->task = g_object_ref (task);
			op_data->app = g_object_ref (app);
			op_data->key = keys[j];

			data->n_pending_ops++;
			gs_download_rewrite_resource_async (css,
							    cancellable,
							    rewrite_resource_cb,
							    g_steal_pointer (&op_data));
		}
	}

	finish_op (task, g_steal_pointer (&local_error));
}

static void
rewrite_resource_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
	g_autoptr(OpData) op_data = g_steal_pointer (&user_data);
	GTask *task = op_data->task;
	g_autoptr(GError) local_error = NULL;
	const gchar *css_old;
	g_autofree gchar *css_new = NULL;

	css_new = gs_download_rewrite_resource_finish (result, &local_error);

	/* Successfully rewritten? */
	css_old = gs_app_get_metadata_item (op_data->app, op_data->key);

	if (css_new != NULL && g_strcmp0 (css_old, css_new) != 0) {
		gs_app_set_metadata (op_data->app, op_data->key, NULL);
		gs_app_set_metadata (op_data->app, op_data->key, css_new);
	}

	finish_op (task, g_steal_pointer (&local_error));
}

/* @error is (transfer full) if non-%NULL */
static void
finish_op (GTask  *task,
           GError *error)
{
	g_autoptr(GError) error_owned = g_steal_pointer (&error);
	RewriteResourcesData *data = g_task_get_task_data (task);

	if (error_owned != NULL && data->saved_error == NULL)
		data->saved_error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while rewriting resources: %s", error_owned->message);

	g_assert (data->n_pending_ops > 0);
	data->n_pending_ops--;

	if (data->n_pending_ops > 0)
		return;

	if (data->saved_error != NULL) {
		g_task_return_error (task, g_steal_pointer (&data->saved_error));
		return;
	}

	g_assert (data->saved_error == NULL);
	g_assert (data->n_pending_ops == 0);

	/* success */
	g_task_return_boolean (task, TRUE);

	GS_PROFILER_ADD_MARK (RewriteResources,
			      data->begin_time_nsec,
			      "RewriteResources",
			      NULL);
}

static gboolean
gs_rewrite_resources_finish (GAsyncResult  *result,
                             GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_rewrite_resource_refine_async (GsPlugin            *plugin,
                                         GsAppList           *list,
                                         GsPluginRefineFlags  flags,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
	GsPluginRewriteResource *self = GS_PLUGIN_REWRITE_RESOURCE (plugin);
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_refine_data_new_task (plugin, list, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_rewrite_resource_refine_async);

	gs_rewrite_resources_async (list, cancellable, rewrite_resources_cb, g_steal_pointer (&task));
}

static void
rewrite_resources_cb (GObject      *source_object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
	g_autoptr(GError) local_error = NULL;

	if (!gs_rewrite_resources_finish (result, &local_error))
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_rewrite_resource_refine_finish (GsPlugin      *plugin,
                                          GAsyncResult  *result,
                                          GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_rewrite_resource_class_init (GsPluginRewriteResourceClass *klass)
{
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	plugin_class->refine_async = gs_plugin_rewrite_resource_refine_async;
	plugin_class->refine_finish = gs_plugin_rewrite_resource_refine_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_REWRITE_RESOURCE;
}
