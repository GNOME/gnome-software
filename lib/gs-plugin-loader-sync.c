/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2012-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-plugin-loader-sync.h"

/* tiny helper to help us do the async operation */
typedef struct {
	GAsyncResult    *res;
	GMainContext    *context;
	GMainLoop	*loop;
} GsPluginLoaderHelper;

static void
_job_process_finish_sync (GsPluginLoader *plugin_loader,
			  GAsyncResult *res,
			  GsPluginLoaderHelper *helper)
{
	helper->res = g_object_ref (res);
	g_main_loop_quit (helper->loop);
}

GsAppList *
gs_plugin_loader_job_process (GsPluginLoader *plugin_loader,
			      GsPluginJob *plugin_job,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoaderHelper helper;
	GsAppList *list;

	/* create temp object */
	helper.res = NULL;
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	gs_plugin_loader_job_process_async (plugin_loader,
					    plugin_job,
					    cancellable,
					    (GAsyncReadyCallback) _job_process_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);
	list = gs_plugin_loader_job_process_finish (plugin_loader,
	                                            helper.res,
	                                            error);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);
	if (helper.res != NULL)
		g_object_unref (helper.res);

	return list;
}

static void
_job_get_categories_finish_sync (GsPluginLoader *plugin_loader,
				 GAsyncResult *res,
				 GsPluginLoaderHelper *helper)
{
	helper->res = g_object_ref (res);
	g_main_loop_quit (helper->loop);
}

GPtrArray *
gs_plugin_loader_job_get_categories (GsPluginLoader *plugin_loader,
				    GsPluginJob *plugin_job,
				    GCancellable *cancellable,
				    GError **error)
{
	GsPluginLoaderHelper helper;
	GPtrArray *catlist;

	/* create temp object */
	helper.res = NULL;
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	gs_plugin_loader_job_get_categories_async (plugin_loader,
						   plugin_job,
						   cancellable,
						   (GAsyncReadyCallback) _job_get_categories_finish_sync,
						   &helper);
	g_main_loop_run (helper.loop);
	catlist = gs_plugin_loader_job_get_categories_finish (plugin_loader,
	                                                      helper.res,
	                                                      error);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);
	if (helper.res != NULL)
		g_object_unref (helper.res);

	return catlist;
}

static void
_job_action_finish_sync (GsPluginLoader *plugin_loader,
			 GAsyncResult *res,
			 GsPluginLoaderHelper *helper)
{
	helper->res = g_object_ref (res);
	g_main_loop_quit (helper->loop);
}

gboolean
gs_plugin_loader_job_action (GsPluginLoader *plugin_loader,
			      GsPluginJob *plugin_job,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoaderHelper helper;
	gboolean ret;

	/* create temp object */
	helper.res = NULL;
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	gs_plugin_loader_job_process_async (plugin_loader,
					    plugin_job,
					    cancellable,
					    (GAsyncReadyCallback) _job_action_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);
	ret = gs_plugin_loader_job_action_finish (plugin_loader,
	                                          helper.res,
	                                          error);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);
	if (helper.res != NULL)
		g_object_unref (helper.res);

	return ret;
}

static void
_job_process_app_finish_sync (GObject *source_object,
			      GAsyncResult *res,
			      gpointer user_data)
{
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) user_data;

	helper->res = g_object_ref (res);
	g_main_loop_quit (helper->loop);
}

GsApp *
gs_plugin_loader_job_process_app (GsPluginLoader *plugin_loader,
				  GsPluginJob *plugin_job,
				  GCancellable *cancellable,
				  GError **error)
{
	GsPluginLoaderHelper helper;
	g_autoptr(GsAppList) list = NULL;
	GsApp *app = NULL;

	/* create temp object */
	helper.res = NULL;
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	gs_plugin_loader_job_process_async (plugin_loader,
					    plugin_job,
					    cancellable,
					    _job_process_app_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);
	list = gs_plugin_loader_job_process_finish (plugin_loader,
	                                            helper.res,
	                                            error);
	if (list != NULL)
		app = g_object_ref (gs_app_list_index (list, 0));

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);
	if (helper.res != NULL)
		g_object_unref (helper.res);

	return app;
}
