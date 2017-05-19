/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include "gs-plugin-loader-sync.h"

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GsAppList	*list;
	GPtrArray	*catlist;
	GMainContext    *context;
	GMainLoop	*loop;
	gboolean	 ret;
	GsApp		*app;
} GsPluginLoaderHelper;

static void
_job_process_finish_sync (GsPluginLoader *plugin_loader,
			  GAsyncResult *res,
			  GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_job_process_finish (plugin_loader,
							    res,
							    helper->error);
	g_main_loop_quit (helper->loop);
}

GsAppList *
gs_plugin_loader_job_process (GsPluginLoader *plugin_loader,
			      GsPluginJob *plugin_job,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoaderHelper helper;

	/* create temp object */
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	gs_plugin_loader_job_process_async (plugin_loader,
					    plugin_job,
					    cancellable,
					    (GAsyncReadyCallback) _job_process_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.list;
}

static void
_job_get_categories_finish_sync (GsPluginLoader *plugin_loader,
				 GAsyncResult *res,
				 GsPluginLoaderHelper *helper)
{
	helper->catlist = gs_plugin_loader_job_get_categories_finish (plugin_loader,
								      res,
								      helper->error);
	g_main_loop_quit (helper->loop);
}

GPtrArray *
gs_plugin_loader_job_get_categories (GsPluginLoader *plugin_loader,
				    GsPluginJob *plugin_job,
				    GCancellable *cancellable,
				    GError **error)
{
	GsPluginLoaderHelper helper;

	/* create temp object */
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	gs_plugin_loader_job_get_categories_async (plugin_loader,
						   plugin_job,
						   cancellable,
						   (GAsyncReadyCallback) _job_get_categories_finish_sync,
						   &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.catlist;
}

static void
_job_action_finish_sync (GsPluginLoader *plugin_loader,
			 GAsyncResult *res,
			 GsPluginLoaderHelper *helper)
{
	helper->ret = gs_plugin_loader_job_action_finish (plugin_loader,
							  res,
							  helper->error);
	g_main_loop_quit (helper->loop);
}

gboolean
gs_plugin_loader_job_action (GsPluginLoader *plugin_loader,
			      GsPluginJob *plugin_job,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoaderHelper helper;

	/* create temp object */
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	gs_plugin_loader_job_process_async (plugin_loader,
					    plugin_job,
					    cancellable,
					    (GAsyncReadyCallback) _job_action_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.ret;
}

static void
_job_process_app_finish_sync (GObject *source_object,
			      GAsyncResult *res,
			      gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) user_data;
	g_autoptr(GsAppList) list = NULL;

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    helper->error);
	if (list != NULL)
		helper->app = g_object_ref (gs_app_list_index (list, 0));
	g_main_loop_quit (helper->loop);
}

GsApp *
gs_plugin_loader_job_process_app (GsPluginLoader *plugin_loader,
				  GsPluginJob *plugin_job,
				  GCancellable *cancellable,
				  GError **error)
{
	GsPluginLoaderHelper helper;

	/* create temp object */
	helper.app = NULL;
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	gs_plugin_loader_job_process_async (plugin_loader,
					    plugin_job,
					    cancellable,
					    _job_process_app_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.app;
}

/* vim: set noexpandtab: */
