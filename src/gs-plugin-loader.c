/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2016 Richard Hughes <richard@hughsie.com>
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

#include <locale.h>
#include <glib/gi18n.h>
#include <appstream-glib.h>

#include "gs-app-private.h"
#include "gs-app-list-private.h"
#include "gs-category-private.h"
#include "gs-plugin-loader.h"
#include "gs-plugin.h"
#include "gs-plugin-event.h"
#include "gs-plugin-private.h"
#include "gs-common.h"
#include "gs-utils.h"

#define GS_PLUGIN_LOADER_UPDATES_CHANGED_DELAY	3	/* s */
#define GS_PLUGIN_LOADER_RELOAD_DELAY		5	/* s */

typedef struct
{
	GPtrArray		*plugins;
	gchar			*location;
	gchar			*locale;
	gchar			*language;
	GsPluginStatus		 status_last;
	GsAppList		*global_cache;
	AsProfile		*profile;
	SoupSession		*soup_session;
	GPtrArray		*auth_array;

	GMutex			 pending_apps_mutex;
	GPtrArray		*pending_apps;

	GSettings		*settings;

	GMutex			 events_by_id_mutex;
	GHashTable		*events_by_id;		/* unique-id : GsPluginEvent */

	gchar			**compatible_projects;
	guint			 scale;

	guint			 updates_changed_id;
	guint			 reload_id;
	gboolean		 online; 
} GsPluginLoaderPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsPluginLoader, gs_plugin_loader, G_TYPE_OBJECT)

enum {
	SIGNAL_STATUS_CHANGED,
	SIGNAL_PENDING_APPS_CHANGED,
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_RELOAD,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_EVENTS,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

typedef void		 (*GsPluginFunc)		(GsPlugin	*plugin);
typedef gboolean	 (*GsPluginSetupFunc)		(GsPlugin	*plugin,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginSearchFunc)		(GsPlugin	*plugin,
							 gchar		**value,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginCategoryFunc)	(GsPlugin	*plugin,
							 GsCategory	*category,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginResultsFunc)		(GsPlugin	*plugin,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginCategoriesFunc)	(GsPlugin	*plugin,
							 GPtrArray	*list,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginActionFunc)		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginReviewFunc)		(GsPlugin	*plugin,
							 GsApp		*app,
							 AsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginAuthFunc)		(GsPlugin	*plugin,
							 GsAuth		*auth,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginRefineFunc)		(GsPlugin	*plugin,
							 GsAppList	*list,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginRefineAppFunc)	(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginRefineWildcardFunc)	(GsPlugin	*plugin,
							 GsApp		*app,
							 GsAppList	*list,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginRefreshFunc	)	(GsPlugin	*plugin,
							 guint		 cache_age,
							 GsPluginRefreshFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginFileToAppFunc)	(GsPlugin	*plugin,
							 GsAppList	*list,
							 GFile		*file,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginUpdateFunc)		(GsPlugin	*plugin,
							 GsAppList	*apps,
							 GCancellable	*cancellable,
							 GError		**error);
typedef void		 (*GsPluginAdoptAppFunc)	(GsPlugin	*plugin,
							 GsApp		*app);

/* async state */
typedef struct {
	const gchar			*function_name;
	GsAppList			*list;
	GPtrArray			*catlist;
	GsPluginRefineFlags		 flags;
	gchar				*value;
	GFile				*file;
	guint				 cache_age;
	GsCategory			*category;
	GsApp				*app;
	AsReview			*review;
	GsAuth				*auth;
	GsPluginAction			 action;
} GsPluginLoaderAsyncState;

static void
gs_plugin_loader_free_async_state (GsPluginLoaderAsyncState *state)
{
	if (state->category != NULL)
		g_object_unref (state->category);
	if (state->app != NULL)
		g_object_unref (state->app);
	if (state->auth != NULL)
		g_object_unref (state->auth);
	if (state->review != NULL)
		g_object_unref (state->review);
	if (state->file != NULL)
		g_object_unref (state->file);
	if (state->list != NULL)
		g_object_unref (state->list);
	if (state->catlist != NULL)
		g_ptr_array_unref (state->catlist);

	g_free (state->value);
	g_slice_free (GsPluginLoaderAsyncState, state);
}

static gint
gs_plugin_loader_app_sort_name_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	return g_strcmp0 (gs_app_get_name (app1),
			  gs_app_get_name (app2));
}

static gint
gs_plugin_loader_app_sort_id_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	return g_strcmp0 (gs_app_get_id (app1), gs_app_get_id (app2));
}

static GsPlugin *
gs_plugin_loader_find_plugin (GsPluginLoader *plugin_loader,
			      const gchar *plugin_name)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	guint i;

	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		if (g_strcmp0 (gs_plugin_get_name (plugin), plugin_name) == 0)
			return plugin;
	}
	return NULL;
}

static void
gs_plugin_loader_action_start (GsPluginLoader *plugin_loader,
			       GsPlugin *plugin,
			       gboolean exclusive)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	guint i;

	/* set plugin as SELF and all plugins as OTHER */
	gs_plugin_action_start (plugin, exclusive);
	for (i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin_tmp;
		plugin_tmp = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin_tmp))
			continue;
		gs_plugin_set_running_other (plugin_tmp, TRUE);
	}
}

static void
gs_plugin_loader_action_stop (GsPluginLoader *plugin_loader, GsPlugin *plugin)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	guint i;

	/* clear plugin as SELF and all plugins as OTHER */
	gs_plugin_action_stop (plugin);
	for (i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin_tmp;
		plugin_tmp = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin_tmp))
			continue;
		gs_plugin_set_running_other (plugin_tmp, FALSE);
	}
}

static gboolean
gs_plugin_loader_notify_idle_cb (gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	g_object_notify (G_OBJECT (plugin_loader), "events");
	return FALSE;
}

static void
gs_plugin_loader_add_event (GsPluginLoader *plugin_loader, GsPluginEvent *event)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->events_by_id_mutex);
	g_hash_table_insert (priv->events_by_id,
			     g_strdup (gs_plugin_event_get_unique_id (event)),
			     g_object_ref (event));
	g_idle_add (gs_plugin_loader_notify_idle_cb, plugin_loader);
}

/* if the error is worthy of notifying then create a plugin event */
static void
gs_plugin_loader_create_event_from_error (GsPluginLoader *plugin_loader,
					  GsPluginAction action,
					  GsPlugin *plugin,
					  GsApp *app,
					  const GError *error)
{
	guint i;
	g_autoptr(GsApp) origin = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GsPluginEvent) event = NULL;

	/* invalid */
	if (error == NULL)
		return;
	if (error->domain != GS_PLUGIN_ERROR) {
		g_critical ("not GsPlugin error %s:%i",
			    g_quark_to_string (error->domain),
			    error->code);
		return;
	}

	/* create plugin event */
	event = gs_plugin_event_new ();
	if (app != NULL)
		gs_plugin_event_set_app (event, app);
	gs_plugin_event_set_error (event, error);
	gs_plugin_event_set_action (event, action);
	gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);

	/* can we find a unique ID */
	split = g_strsplit_set (error->message, "[]: ", -1);
	for (i = 0; split[i] != NULL; i++) {
		if (as_utils_unique_id_valid (split[i])) {
			origin = gs_plugin_cache_lookup (plugin, split[i]);
			if (origin != NULL) {
				g_debug ("found origin %s in error",
					 gs_app_get_unique_id (origin));
				gs_plugin_event_set_origin (event, origin);
				break;
			} else {
				g_debug ("no unique ID found for %s", split[i]);
			}
		}
	}

	/* add event to queue */
	gs_plugin_loader_add_event (plugin_loader, event);
}

static void
gs_plugin_loader_run_adopt (GsPluginLoader *plugin_loader, GsAppList *list)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	guint i;
	guint j;

	/* go through each plugin in order */
	for (i = 0; i < priv->plugins->len; i++) {
		GsPluginAdoptAppFunc adopt_app_func = NULL;
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		g_module_symbol (gs_plugin_get_module (plugin),
				 "gs_plugin_adopt_app",
				 (gpointer *) &adopt_app_func);
		if (adopt_app_func == NULL)
			continue;
		for (j = 0; j < gs_app_list_length (list); j++) {
			GsApp *app = gs_app_list_index (list, j);
			if (gs_app_get_management_plugin (app) != NULL)
				continue;
			if (gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX))
				continue;
			gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
			adopt_app_func (plugin, app);
			gs_plugin_loader_action_stop (plugin_loader, plugin);
			if (gs_app_get_management_plugin (app) != NULL) {
				g_debug ("%s adopted %s",
					 gs_plugin_get_name (plugin),
					 gs_app_get_unique_id (app));
			}
		}
	}
	for (j = 0; j < gs_app_list_length (list); j++) {
		GsApp *app = gs_app_list_index (list, j);
		if (gs_app_get_management_plugin (app) != NULL)
			continue;
		if (gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX))
			continue;
		g_debug ("nothing adopted %s", gs_app_get_unique_id (app));
	}
}

static gint
gs_plugin_loader_review_score_sort_cb (gconstpointer a, gconstpointer b)
{
	AsReview *ra = *((AsReview **) a);
	AsReview *rb = *((AsReview **) b);
	if (as_review_get_priority (ra) < as_review_get_priority (rb))
		return 1;
	if (as_review_get_priority (ra) > as_review_get_priority (rb))
		return -1;
	return 0;
}

static void
gs_plugin_loader_run_refine_wildcard (GsPluginLoader *plugin_loader,
				      GsPlugin *plugin,
				      GsApp *app,
				      GsAppList *list,
				      GsPluginRefineFlags flags,
				      GCancellable *cancellable)
{
	GsPluginRefineWildcardFunc plugin_func = NULL;
	const gchar *function_name = "gs_plugin_refine_wildcard";
	gboolean ret;
	g_autoptr(GError) error_local = NULL;

	/* load the possible symbols */
	g_module_symbol (gs_plugin_get_module (plugin),
			 function_name,
			 (gpointer *) &plugin_func);
	if (plugin_func == NULL)
		return;

	gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
	ret = plugin_func (plugin, app, list, flags,
			   cancellable, &error_local);
	gs_plugin_loader_action_stop (plugin_loader, plugin);
	if (!ret) {
		/* badly behaved plugin */
		if (error_local == NULL) {
			g_critical ("%s did not set error for %s",
				    gs_plugin_get_name (plugin),
				    function_name);
			return;
		}
		g_warning ("failed to call %s on %s: %s",
			   function_name,
			   gs_plugin_get_name (plugin),
			   error_local->message);
		gs_plugin_loader_create_event_from_error (plugin_loader,
							  GS_PLUGIN_ACTION_REFINE,
							  plugin,
							  NULL, /* app */
							  error_local);
	}
}

static void
gs_plugin_loader_run_refine_app (GsPluginLoader *plugin_loader,
				GsPlugin *plugin,
				GsApp *app,
				GsAppList *list,
				GsPluginRefineFlags flags,
				GCancellable *cancellable)
{
	GsPluginRefineAppFunc plugin_func = NULL;
	const gchar *function_name = "gs_plugin_refine_app";
	gboolean ret;
	g_autoptr(GError) error_local = NULL;

	g_module_symbol (gs_plugin_get_module (plugin),
			 function_name,
			 (gpointer *) &plugin_func);
	if (plugin_func == NULL)
		return;

	gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
	ret = plugin_func (plugin, app, flags,
				     cancellable, &error_local);
	gs_plugin_loader_action_stop (plugin_loader, plugin);
	if (!ret) {
		/* badly behaved plugin */
		if (error_local == NULL) {
			g_critical ("%s did not set error for %s",
				    gs_plugin_get_name (plugin),
				    function_name);
			return;
		}
		g_warning ("failed to call %s on %s: %s",
			   function_name,
			   gs_plugin_get_name (plugin),
			   error_local->message);
		gs_plugin_loader_create_event_from_error (plugin_loader,
							  GS_PLUGIN_ACTION_REFINE,
							  plugin,
							  app,
							  error_local);
	}

}

static gboolean
gs_plugin_loader_run_refine_internal (GsPluginLoader *plugin_loader,
				      const gchar *function_name_parent,
				      GsAppList *list,
				      GsPluginRefineFlags flags,
				      GCancellable *cancellable,
				      GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	guint i;
	guint j;
	GPtrArray *addons;
	GPtrArray *related;
	GsApp *app;
	GsPlugin *plugin;
	const gchar *function_name = "gs_plugin_refine";
	gboolean ret = TRUE;

	/* this implies the other */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_UI)
		flags |= GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN;
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME)
		flags |= GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN;

	/* try to adopt each application with a plugin */
	gs_plugin_loader_run_adopt (plugin_loader, list);

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		GsPluginRefineFunc plugin_func = NULL;
		g_autoptr(AsProfileTask) ptask = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;

		/* load the possible symbols */
		g_module_symbol (gs_plugin_get_module (plugin),
				 function_name,
				 (gpointer *) &plugin_func);

		/* profile the plugin runtime */
		if (function_name_parent == NULL) {
			ptask = as_profile_start (priv->profile,
						  "GsPlugin::%s(%s)",
						  gs_plugin_get_name (plugin),
						  function_name);
		} else {
			ptask = as_profile_start (priv->profile,
						  "GsPlugin::%s(%s;%s)",
						  gs_plugin_get_name (plugin),
						  function_name_parent,
						  function_name);
		}
		g_assert (ptask != NULL);

		/* run the batched plugin symbol then the per-app plugin */
		if (plugin_func != NULL) {
			g_autoptr(GError) error_local = NULL;
			gboolean ret_local;

			gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
			ret_local = plugin_func (plugin, list, flags,
			                         cancellable, &error_local);
			gs_plugin_loader_action_stop (plugin_loader, plugin);
			if (!ret_local) {
				/* badly behaved plugin */
				if (error_local == NULL) {
					g_critical ("%s did not set error for %s",
						    gs_plugin_get_name (plugin),
						    function_name);
					continue;
				}
				g_warning ("failed to call %s on %s: %s",
					   function_name,
					   gs_plugin_get_name (plugin),
					   error_local->message);
				gs_plugin_loader_create_event_from_error (plugin_loader,
									  GS_PLUGIN_ACTION_REFINE,
									  plugin,
									  NULL, /* app */
									  error_local);
				continue;
			}
		}
		for (j = 0; j < gs_app_list_length (list); j++) {
			app = gs_app_list_index (list, j);
			if (!gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX)) {
				gs_plugin_loader_run_refine_app (plugin_loader,
								 plugin,
								 app,
								 list,
								 flags,
								 cancellable);
				continue;
			}
			gs_plugin_loader_run_refine_wildcard (plugin_loader,
							      plugin,
							      app,
							      list,
							      flags,
							      cancellable);
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* ensure these are sorted by score */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS) > 0) {
		GPtrArray *reviews;
		for (i = 0; i < gs_app_list_length (list); i++) {
			app = gs_app_list_index (list, i);
			reviews = gs_app_get_reviews (app);
			g_ptr_array_sort (reviews,
					  gs_plugin_loader_review_score_sort_cb);
		}
	}

	/* refine addons one layer deep */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS) > 0) {
		g_autoptr(GsAppList) addons_list = NULL;

		flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS;
		flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS;
		flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS;
		addons_list = gs_app_list_new ();
		for (i = 0; i < gs_app_list_length (list); i++) {
			app = gs_app_list_index (list, i);
			addons = gs_app_get_addons (app);
			for (j = 0; j < addons->len; j++) {
				GsApp *addon = g_ptr_array_index (addons, j);
				g_debug ("refining app %s addon %s",
					 gs_app_get_id (app),
					 gs_app_get_id (addon));
				gs_app_list_add (addons_list, addon);
			}
		}
		if (gs_app_list_length (addons_list) > 0) {
			ret = gs_plugin_loader_run_refine_internal (plugin_loader,
								    function_name_parent,
								    addons_list,
								    flags,
								    cancellable,
								    error);
			if (!ret)
				return FALSE;
		}
	}

	/* also do runtime */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED) > 0) {
		g_autoptr(GsAppList) list2 = gs_app_list_new ();
		for (i = 0; i < gs_app_list_length (list); i++) {
			GsApp *runtime;
			app = gs_app_list_index (list, i);
			runtime = gs_app_get_runtime (app);
			if (runtime != NULL)
				gs_app_list_add (list2, runtime);
		}
		if (gs_app_list_length (list2) > 0) {
			ret = gs_plugin_loader_run_refine_internal (plugin_loader,
								    function_name_parent,
								    list2,
								    flags,
								    cancellable,
								    error);
			if (!ret)
				return FALSE;
		}
	}

	/* also do related packages one layer deep */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED) > 0) {
		g_autoptr(GsAppList) related_list = NULL;

		flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED;
		related_list = gs_app_list_new ();
		for (i = 0; i < gs_app_list_length (list); i++) {
			app = gs_app_list_index (list, i);
			related = gs_app_get_related (app);
			for (j = 0; j < related->len; j++) {
				app = g_ptr_array_index (related, j);
				g_debug ("refining related: %s[%s]",
					 gs_app_get_id (app),
					 gs_app_get_source_default (app));
				gs_app_list_add (related_list, app);
			}
		}
		if (gs_app_list_length (related_list) > 0) {
			ret = gs_plugin_loader_run_refine_internal (plugin_loader,
								    function_name_parent,
								    related_list,
								    flags,
								    cancellable,
								    error);
			if (!ret)
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
gs_plugin_loader_run_refine (GsPluginLoader *plugin_loader,
			     const gchar *function_name_parent,
			     GsAppList *list,
			     GsPluginRefineFlags flags,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean has_match_any_prefix = FALSE;
	gboolean ret;
	guint i;
	g_autoptr(GsAppList) freeze_list = NULL;

	/* nothing to do */
	if (gs_app_list_length (list) == 0)
		return TRUE;

	/* freeze all apps */
	freeze_list = gs_app_list_copy (list);
	for (i = 0; i < gs_app_list_length (freeze_list); i++) {
		GsApp *app = gs_app_list_index (freeze_list, i);
		g_object_freeze_notify (G_OBJECT (app));
	}

	/* first pass */
	ret = gs_plugin_loader_run_refine_internal (plugin_loader,
						    function_name_parent,
						    list,
						    flags,
						    cancellable,
						    error);
	if (!ret)
		goto out;

	/* second pass for any unadopted apps */
	for (i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX)) {
			has_match_any_prefix = TRUE;
			break;
		}
	}
	if (has_match_any_prefix) {
		g_debug ("2nd resolve pass for unadopted wildcards");
		ret = gs_plugin_loader_run_refine_internal (plugin_loader,
							    function_name_parent,
							    list,
							    flags,
							    cancellable,
							    error);
		if (!ret)
			goto out;
	}

out:
	/* now emit all the changed signals */
	for (i = 0; i < gs_app_list_length (freeze_list); i++) {
		GsApp *app = gs_app_list_index (freeze_list, i);
		g_object_thaw_notify (G_OBJECT (app));
	}
	return ret;
}

static void gs_plugin_loader_add_os_update_item (GsAppList *list);

static GsAppList *
gs_plugin_loader_run_results (GsPluginLoader *plugin_loader,
			      GsPluginAction action,
			      const gchar *function_name,
			      GsPluginRefineFlags flags,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GsAppList) list = NULL;
	GsPluginResultsFunc plugin_func = NULL;
	GsPlugin *plugin;
	gboolean exists;
	gboolean ret = TRUE;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (function_name != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);

	/* profile */
	ptask = as_profile_start (priv->profile, "GsPlugin::*(%s)", function_name);
	g_assert (ptask != NULL);

	/* run each plugin */
	list = gs_app_list_new ();
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(AsProfileTask) ptask2 = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			gs_utils_error_convert_gio (error);
			return NULL;
		}

		/* get symbol */
		exists = g_module_symbol (gs_plugin_get_module (plugin),
					  function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;

		/* run function */
		ptask2 = as_profile_start (priv->profile,
					   "GsPlugin::%s(%s)",
					   gs_plugin_get_name (plugin),
					   function_name);
		g_assert (ptask2 != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, list, cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}
			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			gs_plugin_loader_create_event_from_error (plugin_loader,
								  action,
								  plugin,
								  NULL, /* app */
								  error_local);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   list,
					   flags,
					   cancellable,
					   error);
	if (!ret)
		return NULL;

	/* coalesce all packages down into one os-update */
	if (g_strcmp0 (function_name, "gs_plugin_add_updates") == 0) {
		gs_plugin_loader_add_os_update_item (list);
		ret = gs_plugin_loader_run_refine (plugin_loader,
						   function_name,
						   list,
						   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						   cancellable,
						   error);
		if (!ret)
			return NULL;
	}

	return g_steal_pointer (&list);
}

static const gchar *
gs_plugin_loader_get_app_str (GsApp *app)
{
	const gchar *id;

	/* first try the actual id */
	id = gs_app_get_unique_id (app);
	if (id != NULL)
		return id;

	/* then try the source */
	id = gs_app_get_source_default (app);
	if (id != NULL)
		return id;

	/* lastly try the source id */
	id = gs_app_get_source_id_default (app);
	if (id != NULL)
		return id;

	/* urmmm */
	return "<invalid>";
}

static gboolean
gs_plugin_loader_app_set_prio (GsApp *app, gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	GsPlugin *plugin;
	const gchar *tmp;

	/* if set, copy the priority */
	tmp = gs_app_get_management_plugin (app);
	if (tmp == NULL)
		return TRUE;
	plugin = gs_plugin_loader_find_plugin (plugin_loader, tmp);
	if (plugin == NULL)
		return TRUE;
	gs_app_set_priority (app, gs_plugin_get_priority (plugin));
	return TRUE;
}

static gboolean
gs_plugin_loader_app_is_valid_installed (GsApp *app, gpointer user_data)
{
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_OS_UPGRADE:
	case AS_APP_KIND_CODEC:
	case AS_APP_KIND_FONT:
		g_debug ("app invalid as %s: %s",
			 as_app_kind_to_string (gs_app_get_kind (app)),
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
		break;
	default:
		break;
	}

	/* ignore this crazy application */
	if (g_strcmp0 (gs_app_get_id (app), "gnome-system-monitor-kde.desktop") == 0) {
		g_debug ("Ignoring KDE version of %s", gs_app_get_id (app));
		return FALSE;
	}

	/* sanity check */
	if (!gs_app_is_installed (app)) {
		g_autofree gchar *tmp = gs_app_to_string (app);
		g_warning ("ignoring non-installed app %s", tmp);
		return FALSE;
	}

	return TRUE;
}

static gboolean
gs_plugin_loader_app_is_valid (GsApp *app, gpointer user_data)
{
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) user_data;

	/* never show addons */
	if (gs_app_get_kind (app) == AS_APP_KIND_ADDON) {
		g_debug ("app invalid as addon %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unknown state */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN) {
		g_debug ("app invalid as state unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unconverted unavailables */
	if (gs_app_get_kind (app) == AS_APP_KIND_UNKNOWN &&
		gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		g_debug ("app invalid as unconverted unavailable %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show blacklisted apps */
	if (gs_app_has_category (app, "Blacklisted")) {
		g_debug ("app invalid as blacklisted %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show sources */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE) {
		g_debug ("app invalid as source %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unknown kind */
	if (gs_app_get_kind (app) == AS_APP_KIND_UNKNOWN) {
		g_debug ("app invalid as kind unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unconverted packages in the application view */
	if (((state->flags & GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES) == 0) &&
	    (gs_app_get_kind (app) == AS_APP_KIND_GENERIC)) {
//		g_debug ("app invalid as only a %s: %s",
//			 as_app_kind_to_string (gs_app_get_kind (app)),
//			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show apps that do not have the required details */
	if (gs_app_get_name (app) == NULL) {
		g_debug ("app invalid as no name %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	if (gs_app_get_summary (app) == NULL) {
		g_debug ("app invalid as no summary %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	if (gs_app_get_kind (app) == AS_APP_KIND_DESKTOP &&
	    gs_app_get_pixbuf (app) == NULL) {
		g_debug ("app invalid as no pixbuf %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_loader_filter_qt_for_gtk (GsApp *app, gpointer user_data)
{
	/* hide the QT versions in preference to the GTK ones */
	if (g_strcmp0 (gs_app_get_id (app), "transmission-qt.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "nntpgrab_qt.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "gimagereader-qt4.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "gimagereader-qt5.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "nntpgrab_server_qt.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "hotot-qt.desktop") == 0) {
		g_debug ("removing QT version of %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* hide the KDE version in preference to the GTK one */
	if (g_strcmp0 (gs_app_get_id (app), "qalculate_kde.desktop") == 0) {
		g_debug ("removing KDE version of %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* hide the KDE version in preference to the Qt one */
	if (g_strcmp0 (gs_app_get_id (app), "kid3.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "kchmviewer.desktop") == 0) {
		g_debug ("removing KDE version of %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_loader_app_is_non_compulsory (GsApp *app, gpointer user_data)
{
	return !gs_app_has_quirk (app, AS_APP_QUIRK_COMPULSORY);
}

static gboolean
gs_plugin_loader_app_is_non_installed (GsApp *app, gpointer user_data)
{
	if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
		return FALSE;
	if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE)
		return FALSE;
	if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE_LIVE)
		return FALSE;
	return TRUE;
}

static gboolean
gs_plugin_loader_get_app_is_compatible (GsApp *app, gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *tmp;
	guint i;

	/* search for any compatible projects */
	tmp = gs_app_get_project_group (app);
	if (tmp == NULL)
		return TRUE;
	for (i = 0; priv->compatible_projects[i] != NULL; i++) {
		if (g_strcmp0 (tmp,  priv->compatible_projects[i]) == 0)
			return TRUE;
	}
	g_debug ("removing incompatible %s from project group %s",
		 gs_app_get_id (app), gs_app_get_project_group (app));
	return FALSE;
}

/**
 * gs_plugin_loader_get_event_by_id:
 * @list: A #GsAppList
 * @unique_id: A unique_id
 *
 * Finds the first matching event in the list using the usual wildcard
 * rules allowed in unique_ids.
 *
 * Returns: (transfer none): a #GsPluginEvent, or %NULL if not found
 **/
GsPluginEvent *
gs_plugin_loader_get_event_by_id (GsPluginLoader *plugin_loader, const gchar *unique_id)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->events_by_id_mutex);
	return g_hash_table_lookup (priv->events_by_id, unique_id);
}

static gboolean
gs_plugin_loader_is_auth_error (GError *err)
{
	if (g_error_matches (err, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_AUTH_REQUIRED))
		return TRUE;
	if (g_error_matches (err, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_AUTH_INVALID))
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_loader_run_action (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     GsPluginAction action,
			     const gchar *function_name,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginActionFunc plugin_func = NULL;
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	gboolean anything_ran = FALSE;
	gboolean exists;
	gboolean ret;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}
		exists = g_module_symbol (gs_plugin_get_module (plugin),
					  function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, app, cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}

			/* abort early to allow main thread to process */
			if (gs_plugin_loader_is_auth_error (error_local)) {
				g_propagate_error (error, error_local);
				error_local = NULL;
				return FALSE;
			}

			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			gs_plugin_loader_create_event_from_error (plugin_loader,
								  action,
								  plugin,
								  app,
								  error_local);
			continue;
		}
		anything_ran = TRUE;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* nothing ran */
	if (!anything_ran) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no plugin could handle %s",
			     function_name);
		return FALSE;
	}
	return TRUE;
}

/******************************************************************************/

static gboolean
gs_plugin_loader_merge_into_os_update (GsApp *app)
{
	if (gs_app_get_kind (app) == AS_APP_KIND_GENERIC)
		return TRUE;
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
		return TRUE;
	return FALSE;
}

static void
gs_plugin_loader_add_os_update_item (GsAppList *list)
{
	gboolean has_os_update = FALSE;
	guint i;
	GsApp *app_tmp;
	g_autoptr(GError) error = NULL;
	g_autoptr(GdkPixbuf) pixbuf = NULL;
	g_autoptr(GsApp) app_os = NULL;
	g_autoptr(AsIcon) ic = NULL;

	/* do we have any packages left that are not apps? */
	for (i = 0; i < gs_app_list_length (list); i++) {
		app_tmp = gs_app_list_index (list, i);
		if (gs_plugin_loader_merge_into_os_update (app_tmp)) {
			has_os_update = TRUE;
			break;
		}
	}
	if (!has_os_update)
		return;

	/* create new meta object */
	app_os = gs_app_new ("os-update.virtual");
	gs_app_set_management_plugin (app_os, "");
	gs_app_set_kind (app_os, AS_APP_KIND_OS_UPDATE);
	gs_app_set_state (app_os, AS_APP_STATE_UPDATABLE_LIVE);
	gs_app_set_name (app_os,
			 GS_APP_QUALITY_NORMAL,
			 /* TRANSLATORS: this is a group of updates that are not
			  * packages and are not shown in the main list */
			 _("OS Updates"));
	gs_app_set_summary (app_os,
			    GS_APP_QUALITY_NORMAL,
			    /* TRANSLATORS: this is a longer description of the
			     * "OS Updates" string */
			    _("Includes performance, stability and security improvements."));
	gs_app_set_description (app_os,
				GS_APP_QUALITY_NORMAL,
				gs_app_get_summary (app_os));
	for (i = 0; i < gs_app_list_length (list); i++) {
		app_tmp = gs_app_list_index (list, i);
		if (!gs_plugin_loader_merge_into_os_update (app_tmp))
			continue;
		gs_app_add_related (app_os, app_tmp);
	}
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "software-update-available-symbolic");
	gs_app_add_icon (app_os, ic);
	gs_app_list_add (list, app_os);
}

static void
gs_plugin_loader_get_updates_thread_cb (GTask *task,
					gpointer object,
					gpointer task_data,
					GCancellable *cancellable)
{
	const gchar *method_name = "gs_plugin_add_updates";
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GError *error = NULL;

	/* do things that would block */
	if ((state->flags & GS_PLUGIN_REFINE_FLAGS_USE_HISTORY) > 0)
		method_name = "gs_plugin_add_updates_historical";

	state->list = gs_plugin_loader_run_results (plugin_loader,
						    state->action,
						    method_name,
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* remove any packages that are not proper applications or
	 * OS updates */
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_valid, state);

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_NONE);

	/* predictable return order */
	gs_app_list_sort (state->list, gs_plugin_loader_app_sort_id_cb, NULL);

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_get_updates_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_updates()
 * function. The plugins can either return #GsApp objects of kind
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC that remain after refining are
 * added to a new virtual #GsApp of kind %AS_APP_KIND_OS_UPDATE and all the
 * %AS_APP_KIND_GENERIC objects are moved to related packages of this object.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP or %AS_APP_KIND_OS_UPDATE.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_get_updates_async (GsPluginLoader *plugin_loader,
				    GsPluginRefineFlags flags,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->action = GS_PLUGIN_ACTION_GET_UPDATES;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_get_updates_thread_cb);
}

/**
 * gs_plugin_loader_get_updates_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_get_updates_finish (GsPluginLoader *plugin_loader,
				       GAsyncResult *res,
				       GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_get_distro_upgrades_thread_cb (GTask *task,
						gpointer object,
						gpointer task_data,
						GCancellable *cancellable)
{
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GError *error = NULL;

	state->list = gs_plugin_loader_run_results (plugin_loader,
						    state->action,
						    "gs_plugin_add_distro_upgrades",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_NONE);

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_get_distro_upgrades_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_distro_upgrades()
 * function. The plugins can returns #GsApp objects of kind %AS_APP_KIND_OS_UPGRADE.
 **/
void
gs_plugin_loader_get_distro_upgrades_async (GsPluginLoader *plugin_loader,
					    GsPluginRefineFlags flags,
					    GCancellable *cancellable,
					    GAsyncReadyCallback callback,
					    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->action = GS_PLUGIN_ACTION_GET_DISTRO_UPDATES;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_get_distro_upgrades_thread_cb);
}

/**
 * gs_plugin_loader_get_distro_upgrades_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_get_distro_upgrades_finish (GsPluginLoader *plugin_loader,
					     GAsyncResult *res,
					     GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_get_unvoted_reviews_thread_cb (GTask *task,
						gpointer object,
						gpointer task_data,
						GCancellable *cancellable)
{
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GError *error = NULL;

	state->list = gs_plugin_loader_run_results (plugin_loader,
						    state->action,
						    "gs_plugin_add_unvoted_reviews",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_NONE);

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_get_unvoted_reviews_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_unvoted_reviews()
 * function.
 **/
void
gs_plugin_loader_get_unvoted_reviews_async (GsPluginLoader *plugin_loader,
					    GsPluginRefineFlags flags,
					    GCancellable *cancellable,
					    GAsyncReadyCallback callback,
					    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->action = GS_PLUGIN_ACTION_GET_UNVOTED_REVIEWS;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_get_unvoted_reviews_thread_cb);
}

/**
 * gs_plugin_loader_get_unvoted_reviews_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_get_unvoted_reviews_finish (GsPluginLoader *plugin_loader,
					     GAsyncResult *res,
					     GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_get_sources_thread_cb (GTask *task,
					gpointer object,
					gpointer task_data,
					GCancellable *cancellable)
{
	GsPluginLoaderAsyncState *state = task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GError *error = NULL;

	state->list = gs_plugin_loader_run_results (plugin_loader,
						    state->action,
						    "gs_plugin_add_sources",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_NONE);

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_get_sources_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_sources()
 * function. The plugins return #GsApp objects of kind %AS_APP_KIND_SOURCE..
 *
 * The *applications* installed from each source can be obtained using
 * gs_app_get_related() if this information is available.
 **/
void
gs_plugin_loader_get_sources_async (GsPluginLoader *plugin_loader,
				    GsPluginRefineFlags flags,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->action = GS_PLUGIN_ACTION_GET_SOURCES;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_get_sources_thread_cb);
}

/**
 * gs_plugin_loader_get_sources_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_get_sources_finish (GsPluginLoader *plugin_loader,
				       GAsyncResult *res,
				       GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_get_installed_thread_cb (GTask *task,
					  gpointer object,
					  gpointer task_data,
					  GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GError *error = NULL;

	/* do things that would block */
	state->list = gs_plugin_loader_run_results (plugin_loader,
						    state->action,
						    "gs_plugin_add_installed",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_valid, state);
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_valid_installed, state);
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_get_installed_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_installed()
 * function. The plugins can either return #GsApp objects of kind
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
 *
 * The #GsApps will all initially be in state %AS_APP_STATE_INSTALLED.
 **/
void
gs_plugin_loader_get_installed_async (GsPluginLoader *plugin_loader,
				      GsPluginRefineFlags flags,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->action = GS_PLUGIN_ACTION_GET_INSTALLED;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_get_installed_thread_cb);
}

/**
 * gs_plugin_loader_get_installed_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_get_installed_finish (GsPluginLoader *plugin_loader,
				       GAsyncResult *res,
				       GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_get_popular_thread_cb (GTask *task,
					gpointer object,
					gpointer task_data,
					GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GError *error = NULL;
	g_auto(GStrv) apps = NULL;

	/* debugging only */
	if (g_getenv ("GNOME_SOFTWARE_POPULAR"))
		apps = g_strsplit (g_getenv ("GNOME_SOFTWARE_POPULAR"), ",", 0);

	/* are we using a corporate build */
	if (apps == NULL)
		apps = g_settings_get_strv (priv->settings, "popular-overrides");
	if (apps != NULL && g_strv_length (apps) > 0) {
		guint i;
		state->list = gs_app_list_new ();
		for (i = 0; apps[i] != NULL; i++) {
			g_autoptr(GsApp) app = gs_app_new (apps[i]);
			gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
			gs_app_list_add (state->list, app);
		}
	} else {
		/* do things that would block */
		state->list = gs_plugin_loader_run_results (plugin_loader,
							    state->action,
							    "gs_plugin_add_popular",
							    state->flags,
							    cancellable,
							    &error);
		if (error != NULL) {
			g_task_return_error (task, error);
			return;
		}
	}

	/* filter package list */
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_valid, state);
	gs_app_list_filter (state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_app_list_filter (state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_PRIORITY);

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

void
gs_plugin_loader_get_popular_async (GsPluginLoader *plugin_loader,
				    GsPluginRefineFlags flags,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->action = GS_PLUGIN_ACTION_GET_POPULAR;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_get_popular_thread_cb);
}

/**
 * gs_plugin_loader_get_popular_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_get_popular_finish (GsPluginLoader *plugin_loader,
				     GAsyncResult *res,
				     GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static gboolean
gs_plugin_loader_featured_debug (GsApp *app, gpointer user_data)
{
	if (g_strcmp0 (gs_app_get_id (app),
	    g_getenv ("GNOME_SOFTWARE_FEATURED")) == 0)
		return TRUE;
	return FALSE;
}

static void
gs_plugin_loader_get_featured_thread_cb (GTask *task,
					 gpointer object,
					 gpointer task_data,
					 GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GError *error = NULL;

	/* do things that would block */
	state->list = gs_plugin_loader_run_results (plugin_loader,
						    state->action,
						    "gs_plugin_add_featured",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	if (g_getenv ("GNOME_SOFTWARE_FEATURED") != NULL) {
		gs_app_list_filter (state->list, gs_plugin_loader_featured_debug, NULL);
	} else {
		gs_app_list_filter (state->list, gs_plugin_loader_app_is_valid, state);
		gs_app_list_filter (state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
	}

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_PRIORITY);

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_get_featured_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_featured()
 * function. The plugins can either return #GsApp objects of kind
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC that remain after refining are
 * automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_get_featured_async (GsPluginLoader *plugin_loader,
				     GsPluginRefineFlags flags,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->action = GS_PLUGIN_ACTION_GET_FEATURED;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_get_featured_thread_cb);
}

/**
 * gs_plugin_loader_get_featured_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_get_featured_finish (GsPluginLoader *plugin_loader,
				      GAsyncResult *res,
				      GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static gboolean
gs_plugin_loader_convert_unavailable_app (GsApp *app, const gchar *search)
{
	GPtrArray *keywords;
	const gchar *keyword;
	guint i;
	g_autoptr(GString) tmp = NULL;

	/* is the search string one of the codec keywords */
	keywords = gs_app_get_keywords (app);
	for (i = 0; i < keywords->len; i++) {
		keyword = g_ptr_array_index (keywords, i);
		if (g_ascii_strcasecmp (search, keyword) == 0) {
			search = keyword;
			break;
		}
	}

	tmp = g_string_new ("");
	/* TRANSLATORS: this is when we know about an application or
	 * addon, but it can't be listed for some reason */
	g_string_append_printf (tmp, _("No addon codecs are available "
				"for the %s format."), search);
	g_string_append (tmp, "\n");
	g_string_append_printf (tmp, _("Information about %s, as well as options "
				"for how to get a codec that can play this format "
				"can be found on the website."), search);
	gs_app_set_summary_missing (app, tmp->str);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);
	gs_app_set_size_installed (app, GS_APP_SIZE_UNKNOWABLE);
	gs_app_set_size_download (app, GS_APP_SIZE_UNKNOWABLE);
	return TRUE;
}

static void
gs_plugin_loader_convert_unavailable (GsAppList *list, const gchar *search)
{
	guint i;
	GsApp *app;

	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		if (gs_app_get_kind (app) != AS_APP_KIND_GENERIC)
			continue;
		if (gs_app_get_state (app) != AS_APP_STATE_UNAVAILABLE)
			continue;
		if (gs_app_get_kind (app) != AS_APP_KIND_CODEC)
			continue;
		if (gs_app_get_url (app, AS_URL_KIND_MISSING) == NULL)
			continue;

		/* only convert the first unavailable codec */
		if (gs_plugin_loader_convert_unavailable_app (app, search))
			break;
	}
}

static void
gs_plugin_loader_search_thread_cb (GTask *task,
				   gpointer object,
				   gpointer task_data,
				   GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_search";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginSearchFunc plugin_func = NULL;
	guint i;
	g_auto(GStrv) values = NULL;

	/* run each plugin */
	values = as_utils_search_tokenize (state->value);
	if (values == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "no valid search terms");
		return;
	}
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, values, state->list,
				   cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}
			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* convert any unavailables */
	gs_plugin_loader_convert_unavailable (state->list, state->value);

	/* filter package list */
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_valid, state);
	gs_app_list_filter (state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_app_list_filter (state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_NONE);

	/* too many */
	if (gs_app_list_length (state->list) > 500) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "Too many search results returned");
		return;
	}

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_search_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_search()
 * function. The plugins can either return #GsApp objects of kind
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_search_async (GsPluginLoader *plugin_loader,
			       const gchar *value,
			       GsPluginRefineFlags flags,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->list = gs_app_list_new ();
	state->value = g_strdup (value);
	state->action = GS_PLUGIN_ACTION_SEARCH;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_search_thread_cb);
}

/**
 * gs_plugin_loader_search_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_search_finish (GsPluginLoader *plugin_loader,
				GAsyncResult *res,
				GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_search_files_thread_cb (GTask *task,
                                         gpointer object,
                                         gpointer task_data,
                                         GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_search_files";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginSearchFunc plugin_func = NULL;
	guint i;
	g_auto(GStrv) values = NULL;

	values = g_new0 (gchar *, 2);
	values[0] = g_strdup (state->value);

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, values, state->list,
				   cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}
			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* convert any unavailables */
	gs_plugin_loader_convert_unavailable (state->list, state->value);

	/* filter package list */
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_valid, state);
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_non_installed, NULL);
	gs_app_list_filter (state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_app_list_filter (state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_NONE);

	/* too many */
	if (gs_app_list_length (state->list) > 500) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "Too many search results returned");
		return;
	}

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_search_files_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_search_files()
 * function. The plugins can either return #GsApp objects of kind
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_search_files_async (GsPluginLoader *plugin_loader,
                                     const gchar *value,
                                     GsPluginRefineFlags flags,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->list = gs_app_list_new ();
	state->value = g_strdup (value);
	state->action = GS_PLUGIN_ACTION_SEARCH_FILES;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_search_files_thread_cb);
}

/**
 * gs_plugin_loader_search_files_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_search_files_finish (GsPluginLoader *plugin_loader,
                                      GAsyncResult *res,
                                      GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_search_what_provides_thread_cb (GTask *task,
                                                 gpointer object,
                                                 gpointer task_data,
                                                 GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_search_what_provides";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginSearchFunc plugin_func = NULL;
	guint i;
	g_auto(GStrv) values = NULL;

	values = g_new0 (gchar *, 2);
	values[0] = g_strdup (state->value);

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, values, state->list,
				   cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}
			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* convert any unavailables */
	gs_plugin_loader_convert_unavailable (state->list, state->value);

	/* filter package list */
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_valid, state);
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_non_installed, NULL);
	gs_app_list_filter (state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_app_list_filter (state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_NONE);

	/* too many */
	if (gs_app_list_length (state->list) > 500) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "Too many search results returned");
		return;
	}

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_search_what_provides_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_search_what_provides()
 * function. The plugins can either return #GsApp objects of kind
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_search_what_provides_async (GsPluginLoader *plugin_loader,
                                             const gchar *value,
                                             GsPluginRefineFlags flags,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->list = gs_app_list_new ();
	state->value = g_strdup (value);
	state->action = GS_PLUGIN_ACTION_SEARCH_PROVIDES;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_search_what_provides_thread_cb);
}

/**
 * gs_plugin_loader_search_what_provides_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_search_what_provides_finish (GsPluginLoader *plugin_loader,
                                              GAsyncResult *res,
                                              GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static gint
gs_plugin_loader_category_sort_cb (gconstpointer a, gconstpointer b)
{
	GsCategory *cata = GS_CATEGORY (*(GsCategory **) a);
	GsCategory *catb = GS_CATEGORY (*(GsCategory **) b);
	if (gs_category_get_score (cata) < gs_category_get_score (catb))
		return 1;
	if (gs_category_get_score (cata) > gs_category_get_score (catb))
		return -1;
	return g_strcmp0 (gs_category_get_name (cata),
			  gs_category_get_name (catb));
}

static void
gs_plugin_loader_fix_category_all (GsCategory *category)
{
	GPtrArray *children;
	GsCategory *cat_all;
	guint i, j;

	/* set correct size */
	cat_all = gs_category_find_child (category, "all");
	if (cat_all == NULL)
		return;
	gs_category_set_size (cat_all, gs_category_get_size (category));

	/* add the desktop groups from all children */
	children = gs_category_get_children (category);
	for (i = 0; i < children->len; i++) {
		GPtrArray *desktop_groups;
		GsCategory *child;

		/* ignore the all category */
		child = g_ptr_array_index (children, i);
		if (g_strcmp0 (gs_category_get_id (child), "all") == 0)
			continue;

		/* add all desktop groups */
		desktop_groups = gs_category_get_desktop_groups (child);
		for (j = 0; j < desktop_groups->len; j++) {
			const gchar *tmp = g_ptr_array_index (desktop_groups, j);
			gs_category_add_desktop_group (cat_all, tmp);
		}
	}
}

static void
gs_plugin_loader_get_categories_thread_cb (GTask *task,
					   gpointer object,
					   gpointer task_data,
					   GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_categories";
	gboolean ret = TRUE;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginCategoriesFunc plugin_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, state->catlist,
				   cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}
			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			gs_plugin_loader_create_event_from_error (plugin_loader,
								  state->action,
								  plugin,
								  NULL, /* app */
								  error_local);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* make sure 'All' has the right categories */
	for (i = 0; i < state->catlist->len; i++) {
		GsCategory *cat = g_ptr_array_index (state->catlist, i);
		gs_plugin_loader_fix_category_all (cat);
	}

	/* sort by name */
	g_ptr_array_sort (state->catlist, gs_plugin_loader_category_sort_cb);
	for (i = 0; i < state->catlist->len; i++) {
		GsCategory *cat = GS_CATEGORY (g_ptr_array_index (state->catlist, i));
		gs_category_sort_children (cat);
	}

	/* success */
	if (state->catlist->len == 0) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "no categories to show");
		return;
	}

	/* success */
	g_task_return_pointer (task, g_ptr_array_ref (state->catlist), (GDestroyNotify) g_ptr_array_unref);
}

/**
 * gs_plugin_loader_get_categories_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_categories()
 * function. The plugins return #GsCategory objects.
 **/
void
gs_plugin_loader_get_categories_async (GsPluginLoader *plugin_loader,
				       GsPluginRefineFlags flags,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->catlist = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	state->action = GS_PLUGIN_ACTION_GET_CATEGORIES;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_get_categories_thread_cb);
}

/**
 * gs_plugin_loader_get_categories_finish:
 *
 * Return value: (element-type GsCategory) (transfer full): A list of applications
 **/
GPtrArray *
gs_plugin_loader_get_categories_finish (GsPluginLoader *plugin_loader,
					GAsyncResult *res,
					GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_get_category_apps_thread_cb (GTask *task,
					      gpointer object,
					      gpointer task_data,
					      GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_category_apps";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginCategoryFunc plugin_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, state->category, state->list,
				   cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}
			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			gs_plugin_loader_create_event_from_error (plugin_loader,
								  state->action,
								  plugin,
								  NULL, /* app */
								  error_local);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_non_compulsory, NULL);
	gs_app_list_filter (state->list, gs_plugin_loader_app_is_valid, state);
	gs_app_list_filter (state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_app_list_filter (state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);

	/* filter duplicates with priority */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_PRIORITY);

	/* sort, just in case the UI doesn't do this */
	gs_app_list_sort (state->list, gs_plugin_loader_app_sort_name_cb, NULL);

	/* success */
	g_task_return_pointer (task, g_object_ref (state->list), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_get_category_apps_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_category_apps()
 * function. The plugins can either return #GsApp objects of kind
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_get_category_apps_async (GsPluginLoader *plugin_loader,
					  GsCategory *category,
					  GsPluginRefineFlags flags,
					  GCancellable *cancellable,
					  GAsyncReadyCallback callback,
					  gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->list = gs_app_list_new ();
	state->category = g_object_ref (category);
	state->action = GS_PLUGIN_ACTION_GET_CATEGORY_APPS;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_get_category_apps_thread_cb);
}

/**
 * gs_plugin_loader_get_category_apps_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_get_category_apps_finish (GsPluginLoader *plugin_loader,
					   GAsyncResult *res,
					   GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_app_refine_thread_cb (GTask *task,
				       gpointer object,
				       gpointer task_data,
				       GCancellable *cancellable)
{
	GError *error = NULL;
	GsAppList *list;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	gboolean ret;

	list = gs_app_list_new ();
	gs_app_list_add (list, state->app);
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   NULL,
					   list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		goto out;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
out:
	g_object_unref (list);
}

/**
 * gs_plugin_loader_app_refine_async:
 *
 * This method calls all plugins that implement the gs_plugin_refine()
 * function.
 **/
void
gs_plugin_loader_app_refine_async (GsPluginLoader *plugin_loader,
				   GsApp *app,
				   GsPluginRefineFlags flags,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->app = g_object_ref (app);
	state->flags = flags;
	state->action = GS_PLUGIN_ACTION_REFINE;

	/* enforce this */
	if (state->flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KEY_COLORS)
		state->flags |= GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_app_refine_thread_cb);
}

/**
 * gs_plugin_loader_app_refine_finish:
 *
 * Return value: success
 **/
gboolean
gs_plugin_loader_app_refine_finish (GsPluginLoader *plugin_loader,
				    GAsyncResult *res,
				    GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/******************************************************************************/

static gboolean
emit_pending_apps_idle (gpointer loader)
{
	g_signal_emit (loader, signals[SIGNAL_PENDING_APPS_CHANGED], 0);
	g_object_unref (loader);

	return G_SOURCE_REMOVE;
}

static void
gs_plugin_loader_app_action_thread_cb (GTask *task,
				       gpointer object,
				       gpointer task_data,
				       GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GPtrArray *addons;
	gboolean ret;
	guint i;
	g_autoptr(GsAppList) list = NULL;

	/* add to list */
	g_mutex_lock (&priv->pending_apps_mutex);
	g_ptr_array_add (priv->pending_apps, g_object_ref (state->app));
	g_mutex_unlock (&priv->pending_apps_mutex);
	g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));

	/* perform action */
	ret = gs_plugin_loader_run_action (plugin_loader,
					   state->app,
					   state->action,
					   state->function_name,
					   cancellable,
					   &error);
	if (ret) {
		/* unstage addons */
		addons = gs_app_get_addons (state->app);
		for (i = 0; i < addons->len; i++) {
			GsApp *addon = g_ptr_array_index (addons, i);
			if (gs_app_get_to_be_installed (addon))
				gs_app_set_to_be_installed (addon, FALSE);
		}

		/* refine again to make sure we pick up new source id */
		list = gs_app_list_new ();
		gs_app_list_add (list, state->app);
		ret = gs_plugin_loader_run_refine (plugin_loader,
						   state->function_name,
						   list,
						   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN,
						   cancellable,
						   &error);
		if (ret) {
			g_task_return_boolean (task, TRUE);
		} else {
			g_task_return_error (task, error);
		}
	} else {
		gs_app_set_state_recover (state->app);
		g_task_return_error (task, error);
	}

	/* check the app is not still in an action state */
	switch (gs_app_get_state (state->app)) {
	case AS_APP_STATE_INSTALLING:
	case AS_APP_STATE_REMOVING:
		g_warning ("application %s left in %s state",
			   gs_app_get_unique_id (state->app),
			   as_app_state_to_string (gs_app_get_state (state->app)));
		gs_app_set_state (state->app, AS_APP_STATE_UNKNOWN);
		break;
	default:
		break;
	}

	/* remove from list */
	g_mutex_lock (&priv->pending_apps_mutex);
	g_ptr_array_remove (priv->pending_apps, state->app);
	g_mutex_unlock (&priv->pending_apps_mutex);
	g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
}

static void
gs_plugin_loader_review_action_thread_cb (GTask *task,
					  gpointer object,
					  gpointer task_data,
					  GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	GsPluginReviewFunc plugin_func = NULL;
	gboolean anything_ran = FALSE;
	gboolean exists;
	gboolean ret;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, error);
		}

		exists = g_module_symbol (gs_plugin_get_module (plugin),
					  state->function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  state->function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, state->app, state->review,
				   cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    state->function_name);
				continue;
			}

			/* abort early to allow main thread to process */
			if (gs_plugin_loader_is_auth_error (error_local)) {
				g_task_return_error (task, error_local);
				error_local = NULL;
				return;
			}
			g_warning ("failed to call %s on %s: %s",
				   state->function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			gs_plugin_loader_create_event_from_error (plugin_loader,
								  state->action,
								  plugin,
								  state->app,
								  error_local);
			continue;
		}
		anything_ran = TRUE;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* nothing ran */
	if (!anything_ran) {
		g_set_error (&error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no plugin could handle %s",
			     state->function_name);
		g_task_return_error (task, error);
	}

	/* add this to the app */
	if (g_strcmp0 (state->function_name, "gs_plugin_review_submit") == 0)
		gs_app_add_review (state->app, state->review);

	/* remove this from the app */
	if (g_strcmp0 (state->function_name, "gs_plugin_review_remove") == 0)
		gs_app_remove_review (state->app, state->review);

	g_task_return_boolean (task, TRUE);
}

static gboolean
load_install_queue (GsPluginLoader *plugin_loader, GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean ret = TRUE;
	guint i;
	g_autofree gchar *contents = NULL;
	g_autofree gchar *file = NULL;
	g_auto(GStrv) names = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* load from file */
	file = g_build_filename (g_get_user_data_dir (),
				 "gnome-software",
				 "install-queue",
				 NULL);
	if (!g_file_test (file, G_FILE_TEST_EXISTS))
		return TRUE;
	g_debug ("loading install queue from %s", file);
	if (!g_file_get_contents (file, &contents, NULL, error))
		return FALSE;

	/* add each app-id */
	list = gs_app_list_new ();
	names = g_strsplit (contents, "\n", 0);
	for (i = 0; names[i]; i++) {
		g_autoptr(GsApp) app = NULL;
		if (strlen (names[i]) == 0)
			continue;
		app = gs_app_new (names[i]);
		gs_app_set_state (app, AS_APP_STATE_QUEUED_FOR_INSTALL);

		g_mutex_lock (&priv->pending_apps_mutex);
		g_ptr_array_add (priv->pending_apps,
				 g_object_ref (app));
		g_mutex_unlock (&priv->pending_apps_mutex);

		g_debug ("adding pending app %s", gs_app_get_unique_id (app));
		gs_app_list_add (list, app);
	}

	/* refine */
	if (gs_app_list_length (list) > 0) {
		ret = gs_plugin_loader_run_refine (plugin_loader,
						   NULL,
						   list,
						   GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						   NULL, //FIXME?
						   error);
		if (!ret)
			return FALSE;
	}
	return TRUE;
}

static void
save_install_queue (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GPtrArray *pending_apps;
	gboolean ret;
	gint i;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) s = NULL;
	g_autofree gchar *file = NULL;

	s = g_string_new ("");
	pending_apps = priv->pending_apps;
	g_mutex_lock (&priv->pending_apps_mutex);
	for (i = (gint) pending_apps->len - 1; i >= 0; i--) {
		GsApp *app;
		app = g_ptr_array_index (pending_apps, i);
		if (gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL) {
			g_string_append (s, gs_app_get_id (app));
			g_string_append_c (s, '\n');
		}
	}
	g_mutex_unlock (&priv->pending_apps_mutex);

	/* save file */
	file = g_build_filename (g_get_user_data_dir (),
				 "gnome-software",
				 "install-queue",
				 NULL);
	g_debug ("saving install queue to %s", file);
	ret = g_file_set_contents (file, s->str, (gssize) s->len, &error);
	if (!ret)
		g_warning ("failed to save install queue: %s", error->message);
}

static void
add_app_to_install_queue (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GPtrArray *addons;
	guint i;
	guint id;

	/* queue the app itself */
	g_mutex_lock (&priv->pending_apps_mutex);
	g_ptr_array_add (priv->pending_apps, g_object_ref (app));
	g_mutex_unlock (&priv->pending_apps_mutex);

	gs_app_set_state (app, AS_APP_STATE_QUEUED_FOR_INSTALL);
	id = g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
	g_source_set_name_by_id (id, "[gnome-software] emit_pending_apps_idle");
	save_install_queue (plugin_loader);

	/* recursively queue any addons */
	addons = gs_app_get_addons (app);
	for (i = 0; i < addons->len; i++) {
		GsApp *addon = g_ptr_array_index (addons, i);
		if (gs_app_get_to_be_installed (addon))
			add_app_to_install_queue (plugin_loader, addon);
	}
}

static gboolean
remove_app_from_install_queue (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GPtrArray *addons;
	gboolean ret;
	guint i;
	guint id;

	g_mutex_lock (&priv->pending_apps_mutex);
	ret = g_ptr_array_remove (priv->pending_apps, app);
	g_mutex_unlock (&priv->pending_apps_mutex);

	if (ret) {
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		id = g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
		g_source_set_name_by_id (id, "[gnome-software] emit_pending_apps_idle");
		save_install_queue (plugin_loader);

		/* recursively remove any queued addons */
		addons = gs_app_get_addons (app);
		for (i = 0; i < addons->len; i++) {
			GsApp *addon = g_ptr_array_index (addons, i);
			remove_app_from_install_queue (plugin_loader, addon);
		}
	}

	return ret;
}

/**
 * gs_plugin_loader_app_action_async:
 *
 * This method calls all plugins that implement the gs_plugin_action()
 * function.
 **/
void
gs_plugin_loader_app_action_async (GsPluginLoader *plugin_loader,
				   GsApp *app,
				   GsPluginAction action,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer user_data)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* handle with a fake list */
	if (action == GS_PLUGIN_ACTION_UPDATE) {
		g_autoptr(GsAppList) list = gs_app_list_new ();
		gs_app_list_add (list, app);
		gs_plugin_loader_update_async (plugin_loader, list,
					       cancellable, callback,
					       user_data);
		return;
	}

	if (action == GS_PLUGIN_ACTION_REMOVE) {
		if (remove_app_from_install_queue (plugin_loader, app)) {
			task = g_task_new (plugin_loader, cancellable, callback, user_data);
			g_task_return_boolean (task, TRUE);
			return;
		}
	}

	if (action == GS_PLUGIN_ACTION_INSTALL &&
	    !priv->online) {
		add_app_to_install_queue (plugin_loader, app);
		task = g_task_new (plugin_loader, cancellable, callback, user_data);
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->app = g_object_ref (app);
	state->action = action;

	switch (action) {
	case GS_PLUGIN_ACTION_INSTALL:
		state->function_name = "gs_plugin_app_install";
		break;
	case GS_PLUGIN_ACTION_REMOVE:
		state->function_name = "gs_plugin_app_remove";
		break;
	case GS_PLUGIN_ACTION_SET_RATING:
		state->function_name = "gs_plugin_app_set_rating";
		break;
	case GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD:
		state->function_name = "gs_plugin_app_upgrade_download";
		break;
	case GS_PLUGIN_ACTION_UPGRADE_TRIGGER:
		state->function_name = "gs_plugin_app_upgrade_trigger";
		break;
	case GS_PLUGIN_ACTION_LAUNCH:
		state->function_name = "gs_plugin_launch";
		break;
	case GS_PLUGIN_ACTION_UPDATE_CANCEL:
		state->function_name = "gs_plugin_update_cancel";
		break;
	case GS_PLUGIN_ACTION_ADD_SHORTCUT:
		state->function_name = "gs_plugin_add_shortcut";
		break;
	case GS_PLUGIN_ACTION_REMOVE_SHORTCUT:
		state->function_name = "gs_plugin_remove_shortcut";
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_app_action_thread_cb);
}

void
gs_plugin_loader_review_action_async (GsPluginLoader *plugin_loader,
				      GsApp *app,
				      AsReview *review,
				      GsPluginAction action,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->app = g_object_ref (app);
	state->review = g_object_ref (review);
	state->action = action;

	switch (action) {
	case GS_PLUGIN_ACTION_REVIEW_SUBMIT:
		state->function_name = "gs_plugin_review_submit";
		break;
	case GS_PLUGIN_ACTION_REVIEW_UPVOTE:
		state->function_name = "gs_plugin_review_upvote";
		break;
	case GS_PLUGIN_ACTION_REVIEW_DOWNVOTE:
		state->function_name = "gs_plugin_review_downvote";
		break;
	case GS_PLUGIN_ACTION_REVIEW_REPORT:
		state->function_name = "gs_plugin_review_report";
		break;
	case GS_PLUGIN_ACTION_REVIEW_REMOVE:
		state->function_name = "gs_plugin_review_remove";
		break;
	case GS_PLUGIN_ACTION_REVIEW_DISMISS:
		state->function_name = "gs_plugin_review_dismiss";
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_review_action_thread_cb);
}

gboolean
gs_plugin_loader_review_action_finish (GsPluginLoader *plugin_loader,
				       GAsyncResult *res,
				       GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_auth_action_thread_cb (GTask *task,
					  gpointer object,
					  gpointer task_data,
					  GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	GsPluginAuthFunc plugin_func = NULL;
	gboolean exists;
	gboolean ret;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, error);
		}

		exists = g_module_symbol (gs_plugin_get_module (plugin),
					  state->function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  state->function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, state->auth, cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    state->function_name);
				continue;
			}

			/* stop running other plugins on failure */
			g_task_return_error (task, error_local);
			error_local = NULL;
			return;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	g_task_return_boolean (task, TRUE);
}

void
gs_plugin_loader_auth_action_async (GsPluginLoader *plugin_loader,
				    GsAuth *auth,
				    GsPluginAction action,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_AUTH (auth));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->auth = g_object_ref (auth);
	state->action = action;

	switch (action) {
	case GS_PLUGIN_ACTION_AUTH_LOGIN:
		state->function_name = "gs_plugin_auth_login";
		break;
	case GS_PLUGIN_ACTION_AUTH_LOGOUT:
		state->function_name = "gs_plugin_auth_logout";
		break;
	case GS_PLUGIN_ACTION_AUTH_REGISTER:
		state->function_name = "gs_plugin_auth_register";
		break;
	case GS_PLUGIN_ACTION_AUTH_LOST_PASSWORD:
		state->function_name = "gs_plugin_auth_lost_password";
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_auth_action_thread_cb);
}

gboolean
gs_plugin_loader_auth_action_finish (GsPluginLoader *plugin_loader,
				     GAsyncResult *res,
				     GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_app_action_finish:
 *
 * Return value: success
 **/
gboolean
gs_plugin_loader_app_action_finish (GsPluginLoader *plugin_loader,
				    GAsyncResult *res,
				    GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/******************************************************************************/

GsAppList *
gs_plugin_loader_get_pending (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsAppList *array;
	guint i;

	array = gs_app_list_new ();
	g_mutex_lock (&priv->pending_apps_mutex);
	for (i = 0; i < priv->pending_apps->len; i++) {
		GsApp *app = g_ptr_array_index (priv->pending_apps, i);
		gs_app_list_add (array, app);
	}
	g_mutex_unlock (&priv->pending_apps_mutex);

	return array;
}

static void
gs_plugin_loader_run (GsPluginLoader *plugin_loader, const gchar *function_name)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean ret;
	GsPluginFunc plugin_func = NULL;
	GsPlugin *plugin;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		ret = g_module_symbol (gs_plugin_get_module (plugin),
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		plugin_func (plugin);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}
}

gboolean
gs_plugin_loader_get_enabled (GsPluginLoader *plugin_loader,
			      const gchar *plugin_name)
{
	GsPlugin *plugin;
	plugin = gs_plugin_loader_find_plugin (plugin_loader, plugin_name);
	if (plugin == NULL)
		return FALSE;
	return gs_plugin_get_enabled (plugin);
}

/**
 * gs_plugin_loader_get_events:
 * @plugin_loader: A #GsPluginLoader
 *
 * Gets all plugin events, even ones that are not active or visible anymore.
 *
 * Returns: (transfer container) (element-type GsPluginEvent): events
 **/
GPtrArray *
gs_plugin_loader_get_events (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GPtrArray *events = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	GList *l;
	g_autoptr(GList) keys = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->events_by_id_mutex);

	/* just add everything */
	keys = g_hash_table_get_keys (priv->events_by_id);
	for (l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		GsPluginEvent *event = g_hash_table_lookup (priv->events_by_id, key);
		if (event == NULL) {
			g_warning ("failed to get event for '%s'", key);
			continue;
		}
		g_ptr_array_add (events, g_object_ref (event));
	}
	return events;
}

/**
 * gs_plugin_loader_get_event_default:
 * @plugin_loader: A #GsPluginLoader
 *
 * Gets an active plugin event where active means that it was not been
 * already dismissed by the user.
 *
 * Returns: a #GsPluginEvent, or %NULL for none
 **/
GsPluginEvent *
gs_plugin_loader_get_event_default (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GList *l;
	g_autoptr(GList) keys = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->events_by_id_mutex);

	/* just add everything */
	keys = g_hash_table_get_keys (priv->events_by_id);
	for (l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		GsPluginEvent *event = g_hash_table_lookup (priv->events_by_id, key);
		if (event == NULL) {
			g_warning ("failed to get event for '%s'", key);
			continue;
		}
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INVALID))
			return event;
	}
	return NULL;
}

/**
 * gs_plugin_loader_remove_events:
 * @plugin_loader: A #GsPluginLoader
 *
 * Removes all plugin events from the loader. This function should only be
 * called from the self tests.
 **/
void
gs_plugin_loader_remove_events (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->events_by_id_mutex);
	g_hash_table_remove_all (priv->events_by_id);
}

static void
gs_plugin_loader_status_changed_cb (GsPlugin *plugin,
				    GsApp *app,
				    GsPluginStatus status,
				    GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	/* nothing specific */
	if (gs_app_get_id (app) == NULL) {
		if (status == priv->status_last)
			return;
		g_debug ("emitting global %s",
			 gs_plugin_status_to_string (status));
		priv->status_last = status;
		g_signal_emit (plugin_loader,
			       signals[SIGNAL_STATUS_CHANGED],
			       0, app, status);
		return;
	}

	/* a specific app */
	g_debug ("emitting %s(%s)",
		 gs_plugin_status_to_string (status),
		 gs_app_get_id (app));
	g_signal_emit (plugin_loader,
		       signals[SIGNAL_STATUS_CHANGED],
		       0, app, status);
}

static gboolean
gs_plugin_loader_updates_changed_delay_cb (gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	/* notify shells */
	g_debug ("updates-changed");
	g_signal_emit (plugin_loader, signals[SIGNAL_UPDATES_CHANGED], 0);
	priv->updates_changed_id = 0;

	g_object_unref (plugin_loader);
	return FALSE;
}

static void
gs_plugin_loader_updates_changed_cb (GsPlugin *plugin,
				     GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	if (priv->updates_changed_id != 0)
		return;
	priv->updates_changed_id =
		g_timeout_add_seconds (GS_PLUGIN_LOADER_UPDATES_CHANGED_DELAY,
				       gs_plugin_loader_updates_changed_delay_cb,
				       g_object_ref (plugin_loader));
}

static gboolean
gs_plugin_loader_reload_delay_cb (gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	/* notify shells */
	g_debug ("emitting ::reload");
	g_signal_emit (plugin_loader, signals[SIGNAL_RELOAD], 0);
	priv->reload_id = 0;

	g_object_unref (plugin_loader);
	return FALSE;
}

static void
gs_plugin_loader_reload_cb (GsPlugin *plugin,
			    GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	if (priv->reload_id != 0)
		return;
	priv->reload_id =
		g_timeout_add_seconds (GS_PLUGIN_LOADER_RELOAD_DELAY,
				       gs_plugin_loader_reload_delay_cb,
				       g_object_ref (plugin_loader));
}

static void
gs_plugin_loader_open_plugin (GsPluginLoader *plugin_loader,
			      const gchar *filename)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	g_autoptr(GError) error = NULL;

	/* create plugin from file */
	plugin = gs_plugin_create (filename, &error);
	if (plugin == NULL) {
		g_warning ("Failed to load %s: %s", filename, error->message);
		return;
	}
	g_signal_connect (plugin, "updates-changed",
			  G_CALLBACK (gs_plugin_loader_updates_changed_cb),
			  plugin_loader);
	g_signal_connect (plugin, "reload",
			  G_CALLBACK (gs_plugin_loader_reload_cb),
			  plugin_loader);
	g_signal_connect (plugin, "status-changed",
			  G_CALLBACK (gs_plugin_loader_status_changed_cb),
			  plugin_loader);
	gs_plugin_set_soup_session (plugin, priv->soup_session);
	gs_plugin_set_auth_array (plugin, priv->auth_array);
	gs_plugin_set_profile (plugin, priv->profile);
	gs_plugin_set_locale (plugin, priv->locale);
	gs_plugin_set_language (plugin, priv->language);
	gs_plugin_set_scale (plugin, gs_plugin_loader_get_scale (plugin_loader));
	gs_plugin_set_global_cache (plugin, priv->global_cache);
	g_debug ("opened plugin %s: %s", filename, gs_plugin_get_name (plugin));

	/* add to array */
	g_ptr_array_add (priv->plugins, plugin);
}

void
gs_plugin_loader_set_scale (GsPluginLoader *plugin_loader, guint scale)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	guint i;

	/* save globally, and update each plugin */
	priv->scale = scale;
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		gs_plugin_set_scale (plugin, scale);
	}
}

guint
gs_plugin_loader_get_scale (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	return priv->scale;
}

GsAuth *
gs_plugin_loader_get_auth_by_id (GsPluginLoader *plugin_loader,
				 const gchar *provider_id)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	guint i;

	/* match on ID */
	for (i = 0; i < priv->auth_array->len; i++) {
		GsAuth *auth = g_ptr_array_index (priv->auth_array, i);
		if (g_strcmp0 (gs_auth_get_provider_id (auth), provider_id) == 0)
			return auth;
	}
	return NULL;
}

void
gs_plugin_loader_set_location (GsPluginLoader *plugin_loader, const gchar *location)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autofree gchar *filename = NULL;

	g_free (priv->location);

	/* something non-default specified */
	if (location != NULL) {
		priv->location = g_strdup (location);
		return;
	}

	/* use the default, but this requires a 'make install' */
	filename = g_strdup_printf ("gs-plugins-%s", GS_PLUGIN_API_VERSION);
	priv->location = g_build_filename (LIBDIR, filename, NULL);
}

static gint
gs_plugin_loader_plugin_sort_fn (gconstpointer a, gconstpointer b)
{
	GsPlugin **pa = (GsPlugin **) a;
	GsPlugin **pb = (GsPlugin **) b;
	if (gs_plugin_get_order (*pa) < gs_plugin_get_order (*pb))
		return -1;
	if (gs_plugin_get_order (*pa) > gs_plugin_get_order (*pb))
		return 1;
	return 0;
}

/**
 * gs_plugin_loader_setup:
 * @plugin_loader: a #GsPluginLoader
 * @whitelist: list of plugin names, or %NULL
 * @blacklist: list of plugin names, or %NULL
 * @error: A #GError, or %NULL
 *
 * Sets up the plugin loader ready for use.
 *
 * Returns: %TRUE for success
 */
gboolean
gs_plugin_loader_setup (GsPluginLoader *plugin_loader,
			gchar **whitelist,
			gchar **blacklist,
			GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *filename_tmp;
	const gchar *plugin_name;
	gboolean changes;
	GPtrArray *deps;
	GsPlugin *dep;
	GsPlugin *plugin;
	guint dep_loop_check = 0;
	guint i;
	guint j;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	g_return_val_if_fail (priv->location != NULL, FALSE);

	/* search in the plugin directory for plugins */
	ptask = as_profile_start_literal (priv->profile, "GsPlugin::setup");
	g_assert (ptask != NULL);
	dir = g_dir_open (priv->location, 0, error);
	if (dir == NULL)
		return FALSE;

	/* try to open each plugin */
	g_debug ("searching for plugins in %s", priv->location);
	do {
		g_autofree gchar *filename_plugin = NULL;
		filename_tmp = g_dir_read_name (dir);
		if (filename_tmp == NULL)
			break;
		if (!g_str_has_suffix (filename_tmp, ".so"))
			continue;
		filename_plugin = g_build_filename (priv->location,
						    filename_tmp,
						    NULL);
		gs_plugin_loader_open_plugin (plugin_loader, filename_plugin);
	} while (TRUE);

	/* optional whitelist */
	if (whitelist != NULL) {
		for (i = 0; i < priv->plugins->len; i++) {
			gboolean ret;
			plugin = g_ptr_array_index (priv->plugins, i);
			if (!gs_plugin_get_enabled (plugin))
				continue;
			ret = g_strv_contains ((const gchar * const *) whitelist,
					       gs_plugin_get_name (plugin));
			gs_plugin_set_enabled (plugin, ret);
		}
	}

	/* optional blacklist */
	if (blacklist != NULL) {
		for (i = 0; i < priv->plugins->len; i++) {
			gboolean ret;
			plugin = g_ptr_array_index (priv->plugins, i);
			if (!gs_plugin_get_enabled (plugin))
				continue;
			ret = g_strv_contains ((const gchar * const *) blacklist,
					       gs_plugin_get_name (plugin));
			if (ret)
				gs_plugin_set_enabled (plugin, FALSE);
		}
	}

	/* run the plugins */
	gs_plugin_loader_run (plugin_loader, "gs_plugin_initialize");

	/* order by deps */
	do {
		changes = FALSE;
		for (i = 0; i < priv->plugins->len; i++) {
			plugin = g_ptr_array_index (priv->plugins, i);
			deps = gs_plugin_get_rules (plugin, GS_PLUGIN_RULE_RUN_AFTER);
			for (j = 0; j < deps->len && !changes; j++) {
				plugin_name = g_ptr_array_index (deps, j);
				dep = gs_plugin_loader_find_plugin (plugin_loader,
								    plugin_name);
				if (dep == NULL) {
					g_debug ("cannot find plugin '%s' "
						 "requested by '%s'",
						 plugin_name,
						 gs_plugin_get_name (plugin));
					continue;
				}
				if (!gs_plugin_get_enabled (dep))
					continue;
				if (gs_plugin_get_order (plugin) <= gs_plugin_get_order (dep)) {
					g_debug ("%s [%u] to be ordered after %s [%u] "
						 "so promoting to [%u]",
						 gs_plugin_get_name (plugin),
						 gs_plugin_get_order (plugin),
						 gs_plugin_get_name (dep),
						 gs_plugin_get_order (dep),
						 gs_plugin_get_order (dep) + 1);
					gs_plugin_set_order (plugin, gs_plugin_get_order (dep) + 1);
					changes = TRUE;
				}
			}
		}
		for (i = 0; i < priv->plugins->len; i++) {
			plugin = g_ptr_array_index (priv->plugins, i);
			deps = gs_plugin_get_rules (plugin, GS_PLUGIN_RULE_RUN_BEFORE);
			for (j = 0; j < deps->len && !changes; j++) {
				plugin_name = g_ptr_array_index (deps, j);
				dep = gs_plugin_loader_find_plugin (plugin_loader,
								    plugin_name);
				if (dep == NULL) {
					g_debug ("cannot find plugin '%s' "
						 "requested by '%s'",
						 plugin_name,
						 gs_plugin_get_name (plugin));
					continue;
				}
				if (!gs_plugin_get_enabled (dep))
					continue;
				if (gs_plugin_get_order (plugin) >= gs_plugin_get_order (dep)) {
					g_debug ("%s [%u] to be ordered before %s [%u] "
						 "so promoting to [%u]",
						 gs_plugin_get_name (plugin),
						 gs_plugin_get_order (plugin),
						 gs_plugin_get_name (dep),
						 gs_plugin_get_order (dep),
						 gs_plugin_get_order (dep) + 1);
					gs_plugin_set_order (dep, gs_plugin_get_order (plugin) + 1);
					changes = TRUE;
				}
			}
		}

		/* check we're not stuck */
		if (dep_loop_check++ > 100) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_PLUGIN_DEPSOLVE_FAILED,
				     "got stuck in dep loop");
			return FALSE;
		}
	} while (changes);

	/* check for conflicts */
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		deps = gs_plugin_get_rules (plugin, GS_PLUGIN_RULE_CONFLICTS);
		for (j = 0; j < deps->len && !changes; j++) {
			plugin_name = g_ptr_array_index (deps, j);
			dep = gs_plugin_loader_find_plugin (plugin_loader,
							    plugin_name);
			if (dep == NULL)
				continue;
			if (!gs_plugin_get_enabled (dep))
				continue;
			g_debug ("disabling %s as conflicts with %s",
				 gs_plugin_get_name (dep),
				 gs_plugin_get_name (plugin));
			gs_plugin_set_enabled (dep, FALSE);
		}
	}

	/* sort by order */
	g_ptr_array_sort (priv->plugins,
			  gs_plugin_loader_plugin_sort_fn);

	/* assign priority values */
	do {
		changes = FALSE;
		for (i = 0; i < priv->plugins->len; i++) {
			plugin = g_ptr_array_index (priv->plugins, i);
			deps = gs_plugin_get_rules (plugin, GS_PLUGIN_RULE_BETTER_THAN);
			for (j = 0; j < deps->len && !changes; j++) {
				plugin_name = g_ptr_array_index (deps, j);
				dep = gs_plugin_loader_find_plugin (plugin_loader,
								    plugin_name);
				if (dep == NULL) {
					g_debug ("cannot find plugin '%s' "
						 "requested by '%s'",
						 plugin_name,
						 gs_plugin_get_name (plugin));
					continue;
				}
				if (!gs_plugin_get_enabled (dep))
					continue;
				if (gs_plugin_get_priority (plugin) <= gs_plugin_get_priority (dep)) {
					g_debug ("%s [%u] is better than %s [%u] "
						 "so promoting to [%u]",
						 gs_plugin_get_name (plugin),
						 gs_plugin_get_priority (plugin),
						 gs_plugin_get_name (dep),
						 gs_plugin_get_priority (dep),
						 gs_plugin_get_priority (dep) + 1);
					gs_plugin_set_priority (plugin, gs_plugin_get_priority (dep) + 1);
					changes = TRUE;
				}
			}
		}

		/* check we're not stuck */
		if (dep_loop_check++ > 100) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_PLUGIN_DEPSOLVE_FAILED,
				     "got stuck in priority loop");
			return FALSE;
		}
	} while (changes);

	/* run setup */
	for (i = 0; i < priv->plugins->len; i++) {
		GsPluginSetupFunc plugin_func = NULL;
		const gchar *function_name = "gs_plugin_setup";
		gboolean ret;
		g_autoptr(AsProfileTask) ptask2 = NULL;
		g_autoptr(GError) error_local = NULL;

		/* run setup() if it exists */
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask2 = as_profile_start (priv->profile,
					   "GsPlugin::%s(%s)",
					   gs_plugin_get_name (plugin),
					   function_name);
		g_assert (ptask2 != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, TRUE);
		ret = plugin_func (plugin, NULL, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
			} else {
				g_debug ("disabling %s as setup failed: %s",
					 gs_plugin_get_name (plugin),
					 error_local->message);
				gs_plugin_loader_create_event_from_error (plugin_loader,
									  GS_PLUGIN_ACTION_SETUP,
									  plugin,
									  NULL, /* app */
									  error_local);
			}
			gs_plugin_set_enabled (plugin, FALSE);
		}
	}

	/* now we can load the install-queue */
	if (!load_install_queue (plugin_loader, error))
		return FALSE;
	return TRUE;
}

void
gs_plugin_loader_dump_state (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	guint i;

	/* print what the priorities are */
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		g_debug ("[%s]\t%u\t->\t%s",
			 gs_plugin_get_enabled (plugin) ? "enabled" : "disabld",
			 gs_plugin_get_order (plugin),
			 gs_plugin_get_name (plugin));
	}
}

static void
gs_plugin_loader_get_property (GObject *object, guint prop_id,
			       GValue *value, GParamSpec *pspec)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	switch (prop_id) {
	case PROP_EVENTS:
		g_value_set_pointer (value, priv->events_by_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_loader_set_property (GObject *object, guint prop_id,
			       const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_loader_dispose (GObject *object)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	if (priv->plugins != NULL) {
		gs_plugin_loader_run (plugin_loader, "gs_plugin_destroy");
		g_clear_pointer (&priv->plugins, g_ptr_array_unref);
	}
	if (priv->updates_changed_id != 0) {
		g_source_remove (priv->updates_changed_id);
		priv->updates_changed_id = 0;
	}
	g_clear_object (&priv->soup_session);
	g_clear_object (&priv->profile);
	g_clear_object (&priv->settings);
	g_clear_pointer (&priv->auth_array, g_ptr_array_unref);
	g_clear_pointer (&priv->pending_apps, g_ptr_array_unref);

	G_OBJECT_CLASS (gs_plugin_loader_parent_class)->dispose (object);
}

static void
gs_plugin_loader_finalize (GObject *object)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	g_strfreev (priv->compatible_projects);
	g_free (priv->location);
	g_free (priv->locale);
	g_free (priv->language);
	g_object_unref (priv->global_cache);
	g_hash_table_unref (priv->events_by_id);

	g_mutex_clear (&priv->pending_apps_mutex);
	g_mutex_clear (&priv->events_by_id_mutex);

	G_OBJECT_CLASS (gs_plugin_loader_parent_class)->finalize (object);
}

static void
gs_plugin_loader_class_init (GsPluginLoaderClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gs_plugin_loader_get_property;
	object_class->set_property = gs_plugin_loader_set_property;
	object_class->dispose = gs_plugin_loader_dispose;
	object_class->finalize = gs_plugin_loader_finalize;

	pspec = g_param_spec_string ("events", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_EVENTS, pspec);

	signals [SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginLoaderClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
	signals [SIGNAL_PENDING_APPS_CHANGED] =
		g_signal_new ("pending-apps-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginLoaderClass, pending_apps_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [SIGNAL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginLoaderClass, updates_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [SIGNAL_RELOAD] =
		g_signal_new ("reload",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginLoaderClass, reload),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
gs_plugin_loader_init (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *tmp;
	gchar *match;
	gchar **projects;
	guint i;

	priv->scale = 1;
	priv->global_cache = gs_app_list_new ();
	priv->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->status_last = GS_PLUGIN_STATUS_LAST;
	priv->pending_apps = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	priv->auth_array = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	priv->profile = as_profile_new ();
	priv->settings = g_settings_new ("org.gnome.software");
	priv->events_by_id = g_hash_table_new_full ((GHashFunc) as_utils_unique_id_hash,
					            (GEqualFunc) as_utils_unique_id_equal,
						    g_free,
						    (GDestroyNotify) g_object_unref);

	/* share a soup session (also disable the double-compression) */
	priv->soup_session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, gs_user_agent (),
							    SOUP_SESSION_TIMEOUT, 10,
							    NULL);
	soup_session_remove_feature_by_type (priv->soup_session,
					     SOUP_TYPE_CONTENT_DECODER);

	/* get the locale without the various UTF-8 suffixes */
	tmp = g_getenv ("GS_SELF_TEST_LOCALE");
	if (tmp != NULL) {
		g_debug ("using self test locale of %s", tmp);
		priv->locale = g_strdup (tmp);
	} else {
		priv->locale = g_strdup (setlocale (LC_MESSAGES, NULL));
		match = g_strstr_len (priv->locale, -1, ".UTF-8");
		if (match != NULL)
			*match = '\0';
		match = g_strstr_len (priv->locale, -1, ".utf8");
		if (match != NULL)
			*match = '\0';
	}

	/* get the language from the locale */
	priv->language = g_strdup (priv->locale);
	match = g_strrstr (priv->language, "_");
	if (match != NULL)
		*match = '\0';

	g_mutex_init (&priv->pending_apps_mutex);
	g_mutex_init (&priv->events_by_id_mutex);

	/* by default we only show project-less apps or compatible projects */
	tmp = g_getenv ("GNOME_SOFTWARE_COMPATIBLE_PROJECTS");
	if (tmp == NULL) {
		projects = g_settings_get_strv (priv->settings,
						"compatible-projects");
	} else {
		projects = g_strsplit (tmp, ",", -1);
	}
	for (i = 0; projects[i] != NULL; i++)
		g_debug ("compatible-project: %s", projects[i]);
	priv->compatible_projects = projects;
}

/**
 * gs_plugin_loader_new:
 *
 * Return value: a new GsPluginLoader object.
 **/
GsPluginLoader *
gs_plugin_loader_new (void)
{
	GsPluginLoader *plugin_loader;
	plugin_loader = g_object_new (GS_TYPE_PLUGIN_LOADER, NULL);
	return GS_PLUGIN_LOADER (plugin_loader);
}

static void
gs_plugin_loader_app_installed_cb (GObject *source,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = GS_APP (user_data);

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		remove_app_from_install_queue (plugin_loader, app);
		g_warning ("failed to install %s: %s",
			   gs_app_get_unique_id (app), error->message);
	}
}

void
gs_plugin_loader_set_network_status (GsPluginLoader *plugin_loader,
				     gboolean online)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsApp *app;
	guint i;
	g_autoptr(GsAppList) queue = NULL;

	if (priv->online == online)
		return;

	g_debug ("network status change: %s", online ? "online" : "offline");

	priv->online = online;

	if (!online)
		return;

	g_mutex_lock (&priv->pending_apps_mutex);
	queue = gs_app_list_new ();
	for (i = 0; i < priv->pending_apps->len; i++) {
		app = g_ptr_array_index (priv->pending_apps, i);
		if (gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL)
			gs_app_list_add (queue, app);
	}
	g_mutex_unlock (&priv->pending_apps_mutex);
	for (i = 0; i < gs_app_list_length (queue); i++) {
		app = gs_app_list_index (queue, i);
		gs_plugin_loader_app_action_async (plugin_loader,
						   app,
						   GS_PLUGIN_ACTION_INSTALL,
						   NULL,
						   gs_plugin_loader_app_installed_cb,
						   g_object_ref (app));
	}
}

/******************************************************************************/

static gboolean
gs_plugin_loader_run_refresh (GsPluginLoader *plugin_loader,
			      guint cache_age,
			      GsPluginRefreshFlags flags,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	GsPluginRefreshFunc plugin_func = NULL;
	const gchar *function_name = "gs_plugin_refresh";
	gboolean anything_ran = FALSE;
	gboolean ret;
	guint i;
	gboolean exists;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(AsProfileTask) ptask = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}

		exists = g_module_symbol (gs_plugin_get_module (plugin),
					  function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, TRUE);
		ret = plugin_func (plugin, cache_age, flags, cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}
			if (flags & GS_PLUGIN_REFRESH_FLAGS_INTERACTIVE) {
				g_propagate_error (error, error_local);
				error_local = NULL;
				return FALSE;
			} else {
				gs_plugin_loader_create_event_from_error (plugin_loader,
									  GS_PLUGIN_ACTION_REFRESH,
									  plugin,
									  NULL, /* app */
									  error_local);
			}
			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			continue;
		}
		anything_ran = TRUE;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* nothing ran */
	if (!anything_ran) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no plugin could handle refresh");
		return FALSE;
	}
	return TRUE;
}

static void
gs_plugin_loader_refresh_thread_cb (GTask *task,
				    gpointer object,
				    gpointer task_data,
				    GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	gboolean ret;

	ret = gs_plugin_loader_run_refresh (plugin_loader,
					    state->cache_age,
					    state->flags,
					    cancellable,
					    &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * gs_plugin_loader_refresh_async:
 * @cache_age, the age in seconds, or %G_MAXUINT for "any"
 *
 * This method calls all plugins that implement the gs_plugin_refresh()
 * function.
 **/
void
gs_plugin_loader_refresh_async (GsPluginLoader *plugin_loader,
				guint cache_age,
				GsPluginRefreshFlags flags,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->cache_age = cache_age;
	state->action = GS_PLUGIN_ACTION_REFRESH;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_refresh_thread_cb);
}

/**
 * gs_plugin_loader_refresh_finish:
 *
 * Return value: success
 **/
gboolean
gs_plugin_loader_refresh_finish (GsPluginLoader *plugin_loader,
				 GAsyncResult *res,
				 GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/******************************************************************************/

static AsIcon *
_gs_app_get_icon_by_kind (GsApp *app, AsIconKind kind)
{
	GPtrArray *icons = gs_app_get_icons (app);
	guint i;
	for (i = 0; i < icons->len; i++) {
		AsIcon *ic = g_ptr_array_index (icons, i);
		if (as_icon_get_kind (ic) == kind)
			return ic;
	}
	return NULL;
}

static void
gs_plugin_loader_file_to_app_thread_cb (GTask *task,
					gpointer object,
					gpointer task_data,
					GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_file_to_app";
	gboolean ret = TRUE;
	GError *error = NULL;
	guint i;
	guint j;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginFileToAppFunc plugin_func = NULL;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, state->list, state->file,
				   cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}
			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			gs_plugin_loader_create_event_from_error (plugin_loader,
								  state->action,
								  plugin,
								  NULL, /* app */
								  error_local);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* set the local file on any of the returned results */
	for (j = 0; j < gs_app_list_length (state->list); j++) {
		GsApp *app = gs_app_list_index (state->list, j);
		if (gs_app_get_local_file (app) == NULL)
			gs_app_set_local_file (app, state->file);
	}

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_app_list_filter (state->list, gs_plugin_loader_app_set_prio, plugin_loader);
	gs_app_list_filter_duplicates (state->list, GS_APP_LIST_FILTER_FLAG_PRIORITY);

	/* check the apps have an icon set */
	for (j = 0; j < gs_app_list_length (state->list); j++) {
		GsApp *app = gs_app_list_index (state->list, j);
		if (_gs_app_get_icon_by_kind (app, AS_ICON_KIND_STOCK) == NULL &&
		    _gs_app_get_icon_by_kind (app, AS_ICON_KIND_LOCAL) == NULL &&
		    _gs_app_get_icon_by_kind (app, AS_ICON_KIND_CACHED) == NULL) {
			g_autoptr(AsIcon) ic = as_icon_new ();
			as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
			if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
				as_icon_set_name (ic, "x-package-repository");
			else
				as_icon_set_name (ic, "application-x-executable");
			gs_app_add_icon (app, ic);
		}
	}

	/* run refine() on each one again to pick up any icons */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   state->list,
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* success */
	if (gs_app_list_length (state->list) != 1) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "no application was created for %s",
					 g_file_get_path (state->file));
		return;
	}
	g_task_return_pointer (task, g_object_ref (gs_app_list_index (state->list, 0)), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_file_to_app_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_file_to_app()
 * function. The plugins can either return #GsApp objects of kind
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications.
 *
 * Files that are supported will have the GFile used to create them available
 * from the gs_app_get_local_file() method.
 **/
void
gs_plugin_loader_file_to_app_async (GsPluginLoader *plugin_loader,
				    GFile *file,
				    GsPluginRefineFlags flags,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->list = gs_app_list_new ();
	state->file = g_object_ref (file);
	state->action = GS_PLUGIN_ACTION_FILE_TO_APP;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_file_to_app_thread_cb);
}

/**
 * gs_plugin_loader_file_to_app_finish:
 *
 * Return value: (element-type GsApp) (transfer full): An application, or %NULL
 **/
GsApp *
gs_plugin_loader_file_to_app_finish (GsPluginLoader *plugin_loader,
				     GAsyncResult *res,
				     GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	gs_utils_error_convert_gio (error);
	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

static void
gs_plugin_loader_update_thread_cb (GTask *task,
				   gpointer object,
				   gpointer task_data,
				   GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_update";
	gboolean ret = TRUE;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginUpdateFunc plugin_func = NULL;
	GsPluginActionFunc plugin_app_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
		                       function_name,
		                       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  gs_plugin_get_name (plugin),
					  function_name);
		g_assert (ptask != NULL);
		gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
		ret = plugin_func (plugin, state->list, cancellable, &error_local);
		gs_plugin_loader_action_stop (plugin_loader, plugin);
		if (!ret) {
			/* badly behaved plugin */
			if (error_local == NULL) {
				g_critical ("%s did not set error for %s",
					    gs_plugin_get_name (plugin),
					    function_name);
				continue;
			}
			g_warning ("failed to call %s on %s: %s",
				   function_name,
				   gs_plugin_get_name (plugin),
				   error_local->message);
			gs_plugin_loader_create_event_from_error (plugin_loader,
								  state->action,
								  plugin,
								  NULL, /* app */
								  error_local);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* run each plugin, per-app version */
	function_name = "gs_plugin_update_app";
	for (i = 0; i < priv->plugins->len; i++) {
		guint j;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
		                       function_name,
		                       (gpointer *) &plugin_app_func);
		if (!ret)
			continue;

		/* for each app */
		for (j = 0; j < gs_app_list_length (state->list); j++) {
			GsApp *app = gs_app_list_index (state->list, j);
			g_autoptr(AsProfileTask) ptask = NULL;
			g_autoptr(GError) error_local = NULL;

			ptask = as_profile_start (priv->profile,
						  "GsPlugin::%s(%s){%s}",
						  gs_plugin_get_name (plugin),
						  function_name,
						  gs_app_get_id (app));
			g_assert (ptask != NULL);
			gs_plugin_loader_action_start (plugin_loader, plugin, FALSE);
			ret = plugin_app_func (plugin, app,
					       cancellable,
					       &error_local);
			gs_plugin_loader_action_stop (plugin_loader, plugin);
			if (!ret) {
				/* badly behaved plugin */
				if (error_local == NULL) {
					g_critical ("%s did not set error for %s",
						    gs_plugin_get_name (plugin),
						    function_name);
					continue;
				}
				g_warning ("failed to call %s on %s: %s",
					   function_name,
					   gs_plugin_get_name (plugin),
					   error_local->message);
				gs_plugin_loader_create_event_from_error (plugin_loader,
									  state->action,
									  plugin,
									  app,
									  error_local);
				continue;
			}
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	g_task_return_boolean (task, TRUE);
}

/**
 * gs_plugin_loader_update_async:
 *
 * This method calls all plugins that implement the gs_plugin_update()
 * or gs_plugin_update_app() functions.
 **/
void
gs_plugin_loader_update_async (GsPluginLoader *plugin_loader,
			       GsAppList *apps,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->list = gs_app_list_copy (apps);
	state->action = GS_PLUGIN_ACTION_UPDATE;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_run_in_thread (task, gs_plugin_loader_update_thread_cb);
}

gboolean
gs_plugin_loader_update_finish (GsPluginLoader *plugin_loader,
				GAsyncResult *res,
				GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/**
 * gs_plugin_loader_get_plugin_supported:
 *
 * This function returns TRUE if the symbol is found in any enabled plugin.
 */
gboolean
gs_plugin_loader_get_plugin_supported (GsPluginLoader *plugin_loader,
				       const gchar *function_name)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean ret;
	gpointer dummy;
	guint i;

	for (i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_get_enabled (plugin))
			continue;
		ret = g_module_symbol (gs_plugin_get_module (plugin),
				       function_name,
				       (gpointer *) &dummy);
		if (ret)
			return TRUE;
	}
	return FALSE;
}

/******************************************************************************/

AsProfile *
gs_plugin_loader_get_profile (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	return priv->profile;
}

/* vim: set noexpandtab: */
