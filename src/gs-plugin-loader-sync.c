/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2014 Richard Hughes <richard@hughsie.com>
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

/**
 * gs_plugin_loader_get_app_by_id:
 */
GsApp *
gs_plugin_loader_get_app_by_id (GsPluginLoader *plugin_loader,
				const gchar *id,
				GsPluginRefineFlags flags,
				GCancellable *cancellable,
				GError **error)
{
	GsApp *app;
	gboolean ret;

	app = gs_app_new (id);
	app = gs_plugin_loader_dedupe (plugin_loader, app);
	ret = gs_plugin_loader_app_refine (plugin_loader, app, flags,
					   cancellable, error);
	if (!ret)
		g_clear_object (&app);
	return app;
}

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GList		*list;
	GMainContext    *context;
	GMainLoop	*loop;
	gboolean	 ret;
	GsApp		*app;
} GsPluginLoaderHelper;

static void
gs_plugin_loader_get_installed_finish_sync (GsPluginLoader *plugin_loader,
					    GAsyncResult *res,
					    GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_get_installed_finish (plugin_loader,
							      res,
							      helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_get_installed:
 **/
GList *
gs_plugin_loader_get_installed (GsPluginLoader *plugin_loader,
				GsPluginRefineFlags flags,
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
	gs_plugin_loader_get_installed_async (plugin_loader,
					      flags,
					      cancellable,
					      (GAsyncReadyCallback) gs_plugin_loader_get_installed_finish_sync,
					      &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.list;
}

static void
gs_plugin_loader_search_finish_sync (GsPluginLoader *plugin_loader,
				     GAsyncResult *res,
				     GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_search_finish (plugin_loader,
						       res,
						       helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_search:
 **/
GList *
gs_plugin_loader_search (GsPluginLoader *plugin_loader,
			 const gchar *value,
			 GsPluginRefineFlags flags,
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
	gs_plugin_loader_search_async (plugin_loader,
				       value,
				       flags,
				       cancellable,
				       (GAsyncReadyCallback) gs_plugin_loader_search_finish_sync,
				       &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.list;
}

static void
gs_plugin_loader_get_updates_finish_sync (GsPluginLoader *plugin_loader,
					  GAsyncResult *res,
					  GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_get_updates_finish (plugin_loader,
							    res,
							    helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_get_updates:
 **/
GList *
gs_plugin_loader_get_updates (GsPluginLoader *plugin_loader,
			      GsPluginRefineFlags flags,
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
	gs_plugin_loader_get_updates_async (plugin_loader,
					    flags,
					    cancellable,
					    (GAsyncReadyCallback) gs_plugin_loader_get_updates_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.list;
}

static void
gs_plugin_loader_get_popular_finish_sync (GsPluginLoader *plugin_loader,
					  GAsyncResult *res,
					  GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_get_popular_finish (plugin_loader,
							    res,
							    helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_get_popular:
 **/
GList *
gs_plugin_loader_get_popular (GsPluginLoader *plugin_loader,
			      GsPluginRefineFlags flags,
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
	gs_plugin_loader_get_popular_async (plugin_loader,
					    flags,
					    cancellable,
					    (GAsyncReadyCallback) gs_plugin_loader_get_popular_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.list;
}

static void
gs_plugin_loader_get_featured_finish_sync (GsPluginLoader *plugin_loader,
					  GAsyncResult *res,
					  GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_get_featured_finish (plugin_loader,
							     res,
							     helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_get_featured:
 **/
GList *
gs_plugin_loader_get_featured (GsPluginLoader *plugin_loader,
			       GsPluginRefineFlags flags,
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
	gs_plugin_loader_get_featured_async (plugin_loader,
					     flags,
					     cancellable,
					     (GAsyncReadyCallback) gs_plugin_loader_get_featured_finish_sync,
					     &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.list;
}

static void
gs_plugin_loader_get_categories_finish_sync (GsPluginLoader *plugin_loader,
					     GAsyncResult *res,
					     GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_get_categories_finish (plugin_loader,
							       res,
							       helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_get_categories:
 **/
GList *
gs_plugin_loader_get_categories (GsPluginLoader *plugin_loader,
				 GsPluginRefineFlags flags,
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
	gs_plugin_loader_get_categories_async (plugin_loader,
					       flags,
					       cancellable,
					       (GAsyncReadyCallback) gs_plugin_loader_get_categories_finish_sync,
					       &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.list;
}

static void
gs_plugin_loader_get_category_apps_finish_sync (GsPluginLoader *plugin_loader,
						GAsyncResult *res,
						GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_get_category_apps_finish (plugin_loader,
								  res,
								  helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_get_category_apps:
 **/
GList *
gs_plugin_loader_get_category_apps (GsPluginLoader *plugin_loader,
				    GsCategory *category,
				    GsPluginRefineFlags flags,
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
	gs_plugin_loader_get_category_apps_async (plugin_loader,
						  category,
						  flags,
						  cancellable,
						  (GAsyncReadyCallback) gs_plugin_loader_get_category_apps_finish_sync,
						  &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.list;
}

/**
 * gs_plugin_loader_app_refine_finish_sync:
 **/
static void
gs_plugin_loader_app_refine_finish_sync (GsPluginLoader *plugin_loader,
					 GAsyncResult *res,
					 GsPluginLoaderHelper *helper)
{
	helper->ret = gs_plugin_loader_app_refine_finish (plugin_loader,
							  res,
							  helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_app_refine:
 **/
gboolean
gs_plugin_loader_app_refine (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     GsPluginRefineFlags flags,
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
	gs_plugin_loader_app_refine_async (plugin_loader,
					   app,
					   flags,
					   cancellable,
					   (GAsyncReadyCallback) gs_plugin_loader_app_refine_finish_sync,
					   &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.ret;
}

/**
 * gs_plugin_loader_app_action_finish_sync:
 **/
static void
gs_plugin_loader_app_action_finish_sync (GsPluginLoader *plugin_loader,
					 GAsyncResult *res,
					 GsPluginLoaderHelper *helper)
{
	helper->ret = gs_plugin_loader_app_action_finish (plugin_loader,
							  res,
							  helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_app_action:
 **/
gboolean
gs_plugin_loader_app_action (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     GsPluginLoaderAction action,
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
	gs_plugin_loader_app_action_async (plugin_loader,
					   app,
					   action,
					   cancellable,
					   (GAsyncReadyCallback) gs_plugin_loader_app_action_finish_sync,
					   &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.ret;
}

/**
 * gs_plugin_loader_refresh_finish_sync:
 **/
static void
gs_plugin_loader_refresh_finish_sync (GsPluginLoader *plugin_loader,
				      GAsyncResult *res,
				      GsPluginLoaderHelper *helper)
{
	helper->ret = gs_plugin_loader_refresh_finish (plugin_loader,
						       res,
						       helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_refresh:
 **/
gboolean
gs_plugin_loader_refresh (GsPluginLoader *plugin_loader,
			  guint cache_age,
			  GsPluginRefreshFlags flags,
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
	gs_plugin_loader_refresh_async (plugin_loader,
					cache_age,
					flags,
					cancellable,
					(GAsyncReadyCallback) gs_plugin_loader_refresh_finish_sync,
					&helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.ret;
}

static void
gs_plugin_loader_filename_to_app_finish_sync (GObject *source_object,
					      GAsyncResult *res,
					      gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) user_data;
	helper->app = gs_plugin_loader_filename_to_app_finish (plugin_loader,
							       res,
							       helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * gs_plugin_loader_filename_to_app:
 **/
GsApp *
gs_plugin_loader_filename_to_app (GsPluginLoader *plugin_loader,
				  const gchar *filename,
				  GsPluginRefineFlags flags,
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
	gs_plugin_loader_filename_to_app_async (plugin_loader,
						filename,
						flags,
						cancellable,
						gs_plugin_loader_filename_to_app_finish_sync,
						&helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.app;
}

/* vim: set noexpandtab: */
