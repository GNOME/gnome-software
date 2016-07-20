/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2015 Richard Hughes <richard@hughsie.com>
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
	ret = gs_plugin_loader_app_refine (plugin_loader, app, flags,
					   cancellable, error);
	if (!ret)
		g_clear_object (&app);
	return app;
}

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
gs_plugin_loader_get_installed_finish_sync (GsPluginLoader *plugin_loader,
					    GAsyncResult *res,
					    GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_get_installed_finish (plugin_loader,
							      res,
							      helper->error);
	g_main_loop_quit (helper->loop);
}

GsAppList *
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

GsAppList *
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

GsAppList *
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
gs_plugin_loader_get_distro_upgrades_finish_sync (GsPluginLoader *plugin_loader,
					  GAsyncResult *res,
					  GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_get_distro_upgrades_finish (plugin_loader,
								    res,
								    helper->error);
	g_main_loop_quit (helper->loop);
}

GsAppList *
gs_plugin_loader_get_distro_upgrades (GsPluginLoader *plugin_loader,
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
	gs_plugin_loader_get_distro_upgrades_async (plugin_loader,
						    flags,
						    cancellable,
						    (GAsyncReadyCallback) gs_plugin_loader_get_distro_upgrades_finish_sync,
						    &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.list;
}

static void
gs_plugin_loader_get_sources_finish_sync (GsPluginLoader *plugin_loader,
					  GAsyncResult *res,
					  GsPluginLoaderHelper *helper)
{
	helper->list = gs_plugin_loader_get_sources_finish (plugin_loader,
							    res,
							    helper->error);
	g_main_loop_quit (helper->loop);
}

GsAppList *
gs_plugin_loader_get_sources (GsPluginLoader *plugin_loader,
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
	gs_plugin_loader_get_sources_async (plugin_loader,
					    flags,
					    cancellable,
					    (GAsyncReadyCallback) gs_plugin_loader_get_sources_finish_sync,
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

GsAppList *
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

GsAppList *
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
	helper->catlist = gs_plugin_loader_get_categories_finish (plugin_loader,
							       res,
							       helper->error);
	g_main_loop_quit (helper->loop);
}

GPtrArray *
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

	return helper.catlist;
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

GsAppList *
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

static void
gs_plugin_loader_review_action_finish_sync (GsPluginLoader *plugin_loader,
					    GAsyncResult *res,
					    GsPluginLoaderHelper *helper)
{
	helper->ret = gs_plugin_loader_review_action_finish (plugin_loader,
							     res,
							     helper->error);
	g_main_loop_quit (helper->loop);
}

gboolean
gs_plugin_loader_review_action (GsPluginLoader *plugin_loader,
				GsApp *app,
				AsReview *review,
				GsPluginReviewAction action,
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
	gs_plugin_loader_review_action_async (plugin_loader,
					      app,
					      review,
					      action,
					      cancellable,
					      (GAsyncReadyCallback) gs_plugin_loader_review_action_finish_sync,
					      &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.ret;
}

static void
gs_plugin_loader_auth_action_finish_sync (GsPluginLoader *plugin_loader,
					  GAsyncResult *res,
					  GsPluginLoaderHelper *helper)
{
	helper->ret = gs_plugin_loader_auth_action_finish (plugin_loader,
							   res,
							   helper->error);
	g_main_loop_quit (helper->loop);
}

gboolean
gs_plugin_loader_auth_action (GsPluginLoader *plugin_loader,
			      GsAuth *auth,
			      GsAuthAction action,
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
	gs_plugin_loader_auth_action_async (plugin_loader,
					    auth,
					    action,
					    cancellable,
					    (GAsyncReadyCallback) gs_plugin_loader_auth_action_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.ret;
}

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
gs_plugin_loader_file_to_app_finish_sync (GObject *source_object,
					  GAsyncResult *res,
					  gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) user_data;
	helper->app = gs_plugin_loader_file_to_app_finish (plugin_loader,
							   res,
							   helper->error);
	g_main_loop_quit (helper->loop);
}

GsApp *
gs_plugin_loader_file_to_app (GsPluginLoader *plugin_loader,
			      GFile *file,
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
	gs_plugin_loader_file_to_app_async (plugin_loader,
					    file,
					    flags,
					    cancellable,
					    gs_plugin_loader_file_to_app_finish_sync,
					    &helper);
	g_main_loop_run (helper.loop);

	g_main_context_pop_thread_default (helper.context);

	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return helper.app;
}

/* vim: set noexpandtab: */
