/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
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
 * It uses a worker thread to download the resources.
 *
 * FIXME: Eventually this should move into the refine plugin job, as it needs
 * to execute after all other refine jobs (in order to see all the URIs which
 * they produce).
 */

struct _GsPluginRewriteResource
{
	GsPlugin	parent;

	GsWorkerThread	*worker;  /* (owned) */
};

G_DEFINE_TYPE (GsPluginRewriteResource, gs_plugin_rewrite_resource, GS_TYPE_PLUGIN)

#define assert_in_worker(self) \
	g_assert (gs_worker_thread_is_in_worker_context (self->worker))

static void
gs_plugin_rewrite_resource_init (GsPluginRewriteResource *self)
{
	/* let appstream add metadata first */
	gs_plugin_add_rule (GS_PLUGIN (self), GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

static void
gs_plugin_rewrite_resource_dispose (GObject *object)
{
	GsPluginRewriteResource *self = GS_PLUGIN_REWRITE_RESOURCE (object);

	g_clear_object (&self->worker);

	G_OBJECT_CLASS (gs_plugin_rewrite_resource_parent_class)->dispose (object);
}

static void
gs_plugin_rewrite_resource_setup_async (GsPlugin            *plugin,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
	GsPluginRewriteResource *self = GS_PLUGIN_REWRITE_RESOURCE (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_rewrite_resource_setup_async);

	/* Start up a worker thread to process all the plugin’s function calls. */
	self->worker = gs_worker_thread_new ("gs-plugin-rewrite-resource");

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_rewrite_resource_setup_finish (GsPlugin      *plugin,
                                         GAsyncResult  *result,
                                         GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void shutdown_cb (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data);

static void
gs_plugin_rewrite_resource_shutdown_async (GsPlugin            *plugin,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
	GsPluginRewriteResource *self = GS_PLUGIN_REWRITE_RESOURCE (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_rewrite_resource_shutdown_async);

	/* Stop the worker thread. */
	gs_worker_thread_shutdown_async (self->worker, cancellable, shutdown_cb, g_steal_pointer (&task));
}

static void
shutdown_cb (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	GsPluginRewriteResource *self = g_task_get_source_object (task);
	g_autoptr(GsWorkerThread) worker = NULL;
	g_autoptr(GError) local_error = NULL;

	worker = g_steal_pointer (&self->worker);

	if (!gs_worker_thread_shutdown_finish (worker, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_rewrite_resource_shutdown_finish (GsPlugin      *plugin,
                                            GAsyncResult  *result,
                                            GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
refine_app (GsPluginRewriteResource  *self,
            GsApp                    *app,
            GsPluginRefineFlags       flags,
            GCancellable             *cancellable,
            GError                  **error)
{
	const gchar *keys[] = {
		"GnomeSoftware::FeatureTile-css",
		"GnomeSoftware::UpgradeBanner-css",
		NULL };

	assert_in_worker (self);

	/* rewrite URIs */
	for (guint i = 0; keys[i] != NULL; i++) {
		const gchar *css = gs_app_get_metadata_item (app, keys[i]);
		if (css != NULL) {
			g_autofree gchar *css_new = NULL;
			g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (GS_PLUGIN (self)));
			gs_app_set_summary_missing (app_dl,
						    /* TRANSLATORS: status text when downloading */
						    _("Downloading featured images…"));
			css_new = gs_plugin_download_rewrite_resource (GS_PLUGIN (self),
								       app,
								       css,
								       cancellable,
								       error);
			if (css_new == NULL)
				return FALSE;
			if (g_strcmp0 (css, css_new) != 0) {
				gs_app_set_metadata (app, keys[i], NULL);
				gs_app_set_metadata (app, keys[i], css_new);
			}
		}
	}
	return TRUE;
}

static void refine_thread_cb (GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable);

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
	gboolean interactive = gs_plugin_has_flags (GS_PLUGIN (self), GS_PLUGIN_FLAGS_INTERACTIVE);

	task = gs_plugin_refine_data_new_task (plugin, list, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_rewrite_resource_refine_async);

	/* Queue a job for the refine. */
	gs_worker_thread_queue (self->worker, interactive ? G_PRIORITY_DEFAULT : G_PRIORITY_LOW,
				refine_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
refine_thread_cb (GTask        *task,
                  gpointer      source_object,
                  gpointer      task_data,
                  GCancellable *cancellable)
{
	GsPluginRewriteResource *self = GS_PLUGIN_REWRITE_RESOURCE (source_object);
	GsPluginRefineData *data = task_data;
	GsAppList *list = data->list;
	GsPluginRefineFlags flags = data->flags;
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		if (!refine_app (self, app, flags, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

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
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_rewrite_resource_dispose;

	plugin_class->setup_async = gs_plugin_rewrite_resource_setup_async;
	plugin_class->setup_finish = gs_plugin_rewrite_resource_setup_finish;
	plugin_class->shutdown_async = gs_plugin_rewrite_resource_shutdown_async;
	plugin_class->shutdown_finish = gs_plugin_rewrite_resource_shutdown_finish;
	plugin_class->refine_async = gs_plugin_rewrite_resource_refine_async;
	plugin_class->refine_finish = gs_plugin_rewrite_resource_refine_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_REWRITE_RESOURCE;
}
