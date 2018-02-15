/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <locale.h>
#include <glib/gi18n.h>
#include <appstream-glib.h>
#include <math.h>

#include "gs-app-collation.h"
#include "gs-app-private.h"
#include "gs-app-list-private.h"
#include "gs-category-private.h"
#include "gs-ioprio.h"
#include "gs-plugin-loader.h"
#include "gs-plugin.h"
#include "gs-plugin-event.h"
#include "gs-plugin-job-private.h"
#include "gs-plugin-private.h"
#include "gs-utils.h"

#define GS_PLUGIN_LOADER_UPDATES_CHANGED_DELAY	3	/* s */
#define GS_PLUGIN_LOADER_RELOAD_DELAY		5	/* s */

typedef struct
{
	GPtrArray		*plugins;
	GPtrArray		*locations;
	gchar			*locale;
	gchar			*language;
	gboolean		 plugin_dir_dirty;
	SoupSession		*soup_session;
	GPtrArray		*auth_array;
	GPtrArray		*file_monitors;
	GsPluginStatus		 global_status_last;

	GMutex			 pending_apps_mutex;
	GPtrArray		*pending_apps;

	GThreadPool		*queued_ops_pool;

	GSettings		*settings;

	GMutex			 events_by_id_mutex;
	GHashTable		*events_by_id;		/* unique-id : GsPluginEvent */

	gchar			**compatible_projects;
	guint			 scale;

	guint			 updates_changed_id;
	guint			 updates_changed_cnt;
	guint			 reload_id;
	GHashTable		*disallow_updates;	/* GsPlugin : const char *name */

	GNetworkMonitor		*network_monitor;
	gulong			 network_changed_handler;

	MwscScheduler		*download_scheduler;
	gulong			 download_scheduler_invalidated_id;
} GsPluginLoaderPrivate;

static void gs_plugin_loader_monitor_network (GsPluginLoader *plugin_loader);
static void add_app_to_install_queue (GsPluginLoader *plugin_loader, GsApp *app);
static void gs_plugin_loader_process_in_thread_pool_cb (gpointer data, gpointer user_data);

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
	PROP_ALLOW_UPDATES,
	PROP_NETWORK_AVAILABLE,
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
typedef gboolean	 (*GsPluginAlternatesFunc)	(GsPlugin	*plugin,
							 GsApp		*app,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginCategoryFunc)	(GsPlugin	*plugin,
							 GsCategory	*category,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginGetRecentFunc)	(GsPlugin	*plugin,
							 GsAppList	*list,
							 guint64	 age,
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
typedef gboolean	 (*GsPluginPurchaseFunc)	(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPrice	*price,
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
							 GsPluginRefineFlags refine_flags,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginRefineAppFunc)	(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginRefineFlags refine_flags,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginRefineWildcardFunc)	(GsPlugin	*plugin,
							 GsApp		*app,
							 GsAppList	*list,
							 GsPluginRefineFlags refine_flags,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginRefreshFunc)		(GsPlugin	*plugin,
							 guint		 cache_age,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginFileToAppFunc)	(GsPlugin	*plugin,
							 GsAppList	*list,
							 GFile		*file,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginUrlToAppFunc)	(GsPlugin	*plugin,
							 GsAppList	*list,
							 const gchar	*url,
							 GCancellable	*cancellable,
							 GError		**error);
typedef gboolean	 (*GsPluginUpdateFunc)		(GsPlugin	*plugin,
							 GsAppList	*apps,
							 GCancellable	*cancellable,
							 GError		**error);
typedef void		 (*GsPluginAdoptAppFunc)	(GsPlugin	*plugin,
							 GsApp		*app);

/* async helper */
typedef struct {
	GsPluginLoader			*plugin_loader;
	GCancellable			*cancellable;
	GCancellable			*cancellable_caller;
	gulong				 cancellable_id;
	const gchar			*function_name;
	const gchar			*function_name_parent;
	GPtrArray			*catlist;
	GsPluginJob			*plugin_job;
	gboolean			 anything_ran;
	guint				 timeout_id;
	gboolean			 timeout_triggered;
	gchar				**tokens;
} GsPluginLoaderHelper;

static GsPluginLoaderHelper *
gs_plugin_loader_helper_new (GsPluginLoader *plugin_loader, GsPluginJob *plugin_job)
{
	GsPluginLoaderHelper *helper = g_slice_new0 (GsPluginLoaderHelper);
	GsPluginAction action = gs_plugin_job_get_action (plugin_job);
	helper->plugin_loader = g_object_ref (plugin_loader);
	helper->plugin_job = g_object_ref (plugin_job);
	helper->function_name = gs_plugin_action_to_function_name (action);
	return helper;
}

static void
reset_app_progress (GsApp *app)
{
	GsAppList *addons = gs_app_get_addons (app);
	GsAppList *related = gs_app_get_related (app);

	gs_app_set_progress (app, 0);

	for (guint i = 0; i < gs_app_list_length (addons); i++) {
		GsApp *app_addons = gs_app_list_index (addons, i);
		gs_app_set_progress (app_addons, 0);
	}
	for (guint i = 0; i < gs_app_list_length (related); i++) {
		GsApp *app_related = gs_app_list_index (related, i);
		gs_app_set_progress (app_related, 0);
	}
}

static void
gs_plugin_loader_helper_free (GsPluginLoaderHelper *helper)
{
	/* reset progress */
	switch (gs_plugin_job_get_action (helper->plugin_job)) {
	case GS_PLUGIN_ACTION_INSTALL:
	case GS_PLUGIN_ACTION_REMOVE:
	case GS_PLUGIN_ACTION_UPDATE:
	case GS_PLUGIN_ACTION_DOWNLOAD:
		{
			GsApp *app;
			GsAppList *list;

			app = gs_plugin_job_get_app (helper->plugin_job);
			if (app != NULL)
				reset_app_progress (app);

			list = gs_plugin_job_get_list (helper->plugin_job);
			for (guint i = 0; i < gs_app_list_length (list); i++) {
				GsApp *app_tmp = gs_app_list_index (list, i);
				reset_app_progress (app_tmp);
			}
		}
		break;
	default:
		break;
	}

	if (helper->cancellable_id > 0) {
		g_cancellable_disconnect (helper->cancellable_caller,
					  helper->cancellable_id);
	}
	g_object_unref (helper->plugin_loader);
	if (helper->timeout_id != 0)
		g_source_remove (helper->timeout_id);
	if (helper->plugin_job != NULL)
		g_object_unref (helper->plugin_job);
	if (helper->cancellable != NULL)
		g_object_unref (helper->cancellable);
	if (helper->cancellable_caller != NULL)
		g_object_unref (helper->cancellable_caller);
	if (helper->catlist != NULL)
		g_ptr_array_unref (helper->catlist);
	g_strfreev (helper->tokens);
	g_slice_free (GsPluginLoaderHelper, helper);
}

static void
gs_plugin_loader_job_debug (GsPluginLoaderHelper *helper)
{
	g_autofree gchar *str = gs_plugin_job_to_string (helper->plugin_job);
	g_debug ("%s", str);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsPluginLoaderHelper, gs_plugin_loader_helper_free)

static gint
gs_plugin_loader_app_sort_name_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	g_autofree gchar *casefolded_name1 = NULL;
	g_autofree gchar *casefolded_name2 = NULL;

	if (gs_app_get_name (app1) != NULL)
		casefolded_name1 = g_utf8_casefold (gs_app_get_name (app1), -1);
	if (gs_app_get_name (app2) != NULL)
		casefolded_name2 = g_utf8_casefold (gs_app_get_name (app2), -1);
	return g_strcmp0 (casefolded_name1, casefolded_name2);
}

GsPlugin *
gs_plugin_loader_find_plugin (GsPluginLoader *plugin_loader,
			      const gchar *plugin_name)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	for (guint i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (g_strcmp0 (gs_plugin_get_name (plugin), plugin_name) == 0)
			return plugin;
	}
	return NULL;
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

	/* events should always have a unique ID, either constructed from the
	 * app they are processing or preferably from the GError message */
	if (gs_plugin_event_get_unique_id (event) == NULL) {
		g_warning ("failed to add event from action %s",
			   gs_plugin_action_to_string (gs_plugin_event_get_action (event)));
		return;
	}

	g_hash_table_insert (priv->events_by_id,
			     g_strdup (gs_plugin_event_get_unique_id (event)),
			     g_object_ref (event));
	g_idle_add (gs_plugin_loader_notify_idle_cb, plugin_loader);
}

static GsPluginEvent *
gs_plugin_job_to_failed_event (GsPluginJob *plugin_job, const GError *error)
{
	GsPluginEvent *event;
	g_autoptr(GError) error_copy = NULL;

	g_return_val_if_fail (error != NULL, NULL);

	/* invalid */
	if (error->domain != GS_PLUGIN_ERROR) {
		g_warning ("not GsPlugin error %s:%i: %s",
			   g_quark_to_string (error->domain),
			   error->code,
			   error->message);
		g_set_error_literal (&error_copy,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     error->message);
	} else {
		error_copy = g_error_copy (error);
	}

	/* create plugin event */
	event = gs_plugin_event_new ();
	gs_plugin_event_set_error (event, error_copy);
	gs_plugin_event_set_action (event, gs_plugin_job_get_action (plugin_job));
	if (gs_plugin_job_get_app (plugin_job) != NULL)
		gs_plugin_event_set_app (event, gs_plugin_job_get_app (plugin_job));
	if (gs_plugin_job_get_interactive (plugin_job))
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
	gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
	return event;
}

static gboolean
gs_plugin_loader_is_error_fatal (const GError *err)
{
	if (g_error_matches (err, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_TIMED_OUT))
		return TRUE;
	if (g_error_matches (err, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_AUTH_REQUIRED))
		return TRUE;
	if (g_error_matches (err, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_AUTH_INVALID))
		return TRUE;
	if (g_error_matches (err, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_PURCHASE_NOT_SETUP))
		return TRUE;
	if (g_error_matches (err, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_PURCHASE_DECLINED))
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_error_handle_failure (GsPluginLoaderHelper *helper,
				GsPlugin *plugin,
				const GError *error_local,
				GError **error)
{
	g_autofree gchar *app_id = NULL;
	g_autofree gchar *origin_id = NULL;
	g_autoptr(GsPluginEvent) event = NULL;

	/* badly behaved plugin */
	if (error_local == NULL) {
		g_critical ("%s did not set error for %s",
			    gs_plugin_get_name (plugin),
			    helper->function_name);
		return TRUE;
	}

	/* this is only ever informational */
	if (g_error_matches (error_local, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED)) {
		g_debug ("ignoring error cancelled: %s", error_local->message);
		return TRUE;
	}

	/* find and strip any unique IDs from the error message */
	for (guint i = 0; i < 2; i++) {
		if (app_id == NULL)
			app_id = gs_utils_error_strip_app_id (error_local);
		if (origin_id == NULL)
			origin_id = gs_utils_error_strip_origin_id (error_local);
	}

	/* fatal error */
	if (gs_plugin_job_get_action (helper->plugin_job) == GS_PLUGIN_ACTION_SETUP ||
	    gs_plugin_loader_is_error_fatal (error_local) ||
	    g_getenv ("GS_SELF_TEST_PLUGIN_ERROR_FAIL_HARD") != NULL) {
		if (error != NULL)
			*error = g_error_copy (error_local);
		return FALSE;
	}

	/* create event which is handled by the GsShell */
	event = gs_plugin_job_to_failed_event (helper->plugin_job, error_local);

	/* set the app and origin IDs if we managed to scrape them from the error above */
	if (as_utils_unique_id_valid (app_id)) {
		g_autoptr(GsApp) app = gs_plugin_cache_lookup (plugin, app_id);
		if (app != NULL) {
			g_debug ("found app %s in error", origin_id);
			gs_plugin_event_set_app (event, app);
		} else {
			g_debug ("no unique ID found for app %s", app_id);
		}
	}
	if (as_utils_unique_id_valid (origin_id)) {
		g_autoptr(GsApp) origin = gs_plugin_cache_lookup (plugin, origin_id);
		if (origin != NULL) {
			g_debug ("found origin %s in error", origin_id);
			gs_plugin_event_set_origin (event, origin);
		} else {
			g_debug ("no unique ID found for origin %s", origin_id);
		}
	}

	/* add event to queue */
	gs_plugin_loader_add_event (helper->plugin_loader, event);
	return TRUE;
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
		adopt_app_func = gs_plugin_get_symbol (plugin, "gs_plugin_adopt_app");
		if (adopt_app_func == NULL)
			continue;
		for (j = 0; j < gs_app_list_length (list); j++) {
			GsApp *app = gs_app_list_index (list, j);
			if (gs_app_get_management_plugin (app) != NULL)
				continue;
			if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
				continue;
			adopt_app_func (plugin, app);
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
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
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

static gboolean
gs_plugin_loader_call_vfunc (GsPluginLoaderHelper *helper,
			     GsPlugin *plugin,
			     GsApp *app,
			     GsAppList *list,
			     GsPluginRefineFlags refine_flags,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);
	gboolean ret = TRUE;
	gpointer func = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GTimer) timer = g_timer_new ();

	/* load the possible symbol */
	func = gs_plugin_get_symbol (plugin, helper->function_name);
	if (func == NULL)
		return TRUE;

	/* fallback if unset */
	if (app == NULL)
		app = gs_plugin_job_get_app (helper->plugin_job);
	if (list == NULL)
		list = gs_plugin_job_get_list (helper->plugin_job);
	if (refine_flags == GS_PLUGIN_REFINE_FLAGS_DEFAULT)
		refine_flags = gs_plugin_job_get_refine_flags (helper->plugin_job);

	/* set what plugin is running on the job */
	gs_plugin_job_set_plugin (helper->plugin_job, plugin);

	/* run the correct vfunc */
	if (gs_plugin_job_get_interactive (helper->plugin_job))
		gs_plugin_interactive_inc (plugin);
	switch (action) {
	case GS_PLUGIN_ACTION_INITIALIZE:
	case GS_PLUGIN_ACTION_DESTROY:
		{
			GsPluginFunc plugin_func = func;
			plugin_func (plugin);
		}
		break;
	case GS_PLUGIN_ACTION_SETUP:
		{
			GsPluginSetupFunc plugin_func = func;
			ret = plugin_func (plugin, cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_REFINE:
		if (g_strcmp0 (helper->function_name, "gs_plugin_refine_wildcard") == 0) {
			GsPluginRefineWildcardFunc plugin_func = func;
			ret = plugin_func (plugin, app, list, refine_flags, cancellable, &error_local);
		} else if (g_strcmp0 (helper->function_name, "gs_plugin_refine_app") == 0) {
			GsPluginRefineAppFunc plugin_func = func;
			ret = plugin_func (plugin, app, refine_flags, cancellable, &error_local);
		} else if (g_strcmp0 (helper->function_name, "gs_plugin_refine") == 0) {
			GsPluginRefineFunc plugin_func = func;
			ret = plugin_func (plugin, list, refine_flags, cancellable, &error_local);
		} else {
			g_critical ("function_name %s invalid for %s",
				    helper->function_name,
				    gs_plugin_action_to_string (action));
		}
		break;
	case GS_PLUGIN_ACTION_UPDATE:
		if (g_strcmp0 (helper->function_name, "gs_plugin_update_app") == 0) {
			GsPluginActionFunc plugin_func = func;
			ret = plugin_func (plugin, app, cancellable, &error_local);
		} else if (g_strcmp0 (helper->function_name, "gs_plugin_update") == 0) {
			GsPluginUpdateFunc plugin_func = func;
			ret = plugin_func (plugin, list, cancellable, &error_local);
		} else {
			g_critical ("function_name %s invalid for %s",
				    helper->function_name,
				    gs_plugin_action_to_string (action));
		}
		break;
	case GS_PLUGIN_ACTION_DOWNLOAD:
		if (g_strcmp0 (helper->function_name, "gs_plugin_download_app") == 0) {
			GsPluginActionFunc plugin_func = func;
			ret = plugin_func (plugin, app, cancellable, &error_local);
		} else if (g_strcmp0 (helper->function_name, "gs_plugin_download") == 0) {
			GsPluginUpdateFunc plugin_func = func;
			ret = plugin_func (plugin, list, cancellable, &error_local);
		} else {
			g_critical ("function_name %s invalid for %s",
				    helper->function_name,
				    gs_plugin_action_to_string (action));
		}
		break;
	case GS_PLUGIN_ACTION_INSTALL:
	case GS_PLUGIN_ACTION_REMOVE:
	case GS_PLUGIN_ACTION_SET_RATING:
	case GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD:
	case GS_PLUGIN_ACTION_UPGRADE_TRIGGER:
	case GS_PLUGIN_ACTION_LAUNCH:
	case GS_PLUGIN_ACTION_UPDATE_CANCEL:
	case GS_PLUGIN_ACTION_ADD_SHORTCUT:
	case GS_PLUGIN_ACTION_REMOVE_SHORTCUT:
		{
			GsPluginActionFunc plugin_func = func;
			ret = plugin_func (plugin, app, cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_PURCHASE:
		{
			GsPluginPurchaseFunc plugin_func = func;
			ret = plugin_func (plugin, app,
					   gs_plugin_job_get_price (helper->plugin_job),
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_REVIEW_SUBMIT:
	case GS_PLUGIN_ACTION_REVIEW_UPVOTE:
	case GS_PLUGIN_ACTION_REVIEW_DOWNVOTE:
	case GS_PLUGIN_ACTION_REVIEW_REPORT:
	case GS_PLUGIN_ACTION_REVIEW_REMOVE:
	case GS_PLUGIN_ACTION_REVIEW_DISMISS:
		{
			GsPluginReviewFunc plugin_func = func;
			ret = plugin_func (plugin, app,
					   gs_plugin_job_get_review (helper->plugin_job),
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_GET_RECENT:
		{
			GsPluginGetRecentFunc plugin_func = func;
			ret = plugin_func (plugin, list,
					   gs_plugin_job_get_age (helper->plugin_job),
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_GET_UPDATES:
	case GS_PLUGIN_ACTION_GET_UPDATES_HISTORICAL:
	case GS_PLUGIN_ACTION_GET_DISTRO_UPDATES:
	case GS_PLUGIN_ACTION_GET_UNVOTED_REVIEWS:
	case GS_PLUGIN_ACTION_GET_SOURCES:
	case GS_PLUGIN_ACTION_GET_INSTALLED:
	case GS_PLUGIN_ACTION_GET_POPULAR:
	case GS_PLUGIN_ACTION_GET_FEATURED:
		{
			GsPluginResultsFunc plugin_func = func;
			ret = plugin_func (plugin, list, cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_SEARCH:
		{
			GsPluginSearchFunc plugin_func = func;
			ret = plugin_func (plugin, helper->tokens, list,
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_SEARCH_FILES:
	case GS_PLUGIN_ACTION_SEARCH_PROVIDES:
		{
			GsPluginSearchFunc plugin_func = func;
			gchar *search[2] = { gs_plugin_job_get_search (helper->plugin_job), NULL };
			ret = plugin_func (plugin, search, list,
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_GET_ALTERNATES:
		{
			GsPluginAlternatesFunc plugin_func = func;
			ret = plugin_func (plugin, app, list,
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_GET_CATEGORIES:
		{
			GsPluginCategoriesFunc plugin_func = func;
			ret = plugin_func (plugin, helper->catlist,
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_GET_CATEGORY_APPS:
		{
			GsPluginCategoryFunc plugin_func = func;
			ret = plugin_func (plugin,
					   gs_plugin_job_get_category (helper->plugin_job),
					   list,
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_REFRESH:
		{
			GsPluginRefreshFunc plugin_func = func;
			ret = plugin_func (plugin,
					   gs_plugin_job_get_age (helper->plugin_job),
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_FILE_TO_APP:
		{
			GsPluginFileToAppFunc plugin_func = func;
			ret = plugin_func (plugin, list,
					   gs_plugin_job_get_file (helper->plugin_job),
					   cancellable, &error_local);
		}
		break;
	case GS_PLUGIN_ACTION_URL_TO_APP:
		{
			GsPluginUrlToAppFunc plugin_func = func;
			ret = plugin_func (plugin, list,
					   gs_plugin_job_get_search (helper->plugin_job),
					   cancellable, &error_local);
		}
		break;
	default:
		g_critical ("no handler for %s", helper->function_name);
		break;
	}
	if (gs_plugin_job_get_interactive (helper->plugin_job))
		gs_plugin_interactive_dec (plugin);

	/* plugin did not return error on cancellable abort */
	if (ret && g_cancellable_set_error_if_cancelled (cancellable, &error_local)) {
		g_debug ("plugin %s did not return error with cancellable set",
			 gs_plugin_get_name (plugin));
		gs_utils_error_convert_gio (&error_local);
		ret = FALSE;
	}

	/* failed */
	if (!ret) {
		/* we returned cancelled, but this was because of a timeout,
		 * so re-create error, throwing the plugin under the bus */
		if (helper->timeout_triggered &&
		    g_error_matches (error_local, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED)) {
			g_debug ("converting cancelled to timeout");
			g_clear_error (&error_local);
			g_set_error (&error_local,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_TIMED_OUT,
				     "Timeout was reached as %s took "
				     "too long to return results",
				     gs_plugin_get_name (plugin));
		}
		return gs_plugin_error_handle_failure (helper,
							plugin,
							error_local,
							error);
	}

	/* add app to the pending installation queue if necessary */
	if (action == GS_PLUGIN_ACTION_INSTALL &&
	    app != NULL && gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL) {
	        add_app_to_install_queue (helper->plugin_loader, app);
	}

	/* check the plugin didn't take too long */
	switch (action) {
	case GS_PLUGIN_ACTION_INITIALIZE:
	case GS_PLUGIN_ACTION_DESTROY:
	case GS_PLUGIN_ACTION_SETUP:
		if (g_timer_elapsed (timer, NULL) > 1.0f) {
			g_warning ("plugin %s took %.1f seconds to do %s",
				   gs_plugin_get_name (plugin),
				   g_timer_elapsed (timer, NULL),
				   gs_plugin_action_to_string (action));
		}
		break;
	default:
		if (g_timer_elapsed (timer, NULL) > 1.0f) {
			g_debug ("plugin %s took %.1f seconds to do %s",
				 gs_plugin_get_name (plugin),
				 g_timer_elapsed (timer, NULL),
				 gs_plugin_action_to_string (action));
			}
		break;
	}

	/* success */
	helper->anything_ran = TRUE;
	return TRUE;
}

static gboolean
gs_plugin_loader_run_refine_filter (GsPluginLoaderHelper *helper,
				    GsAppList *list,
				    GsPluginRefineFlags refine_flags,
				    GCancellable *cancellable,
				    GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (helper->plugin_loader);

	/* run each plugin */
	for (guint i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		g_autoptr(GsAppList) app_list = NULL;

		/* run the batched plugin symbol then the per-app plugin */
		helper->function_name = "gs_plugin_refine";
		if (!gs_plugin_loader_call_vfunc (helper, plugin, NULL, list,
						  refine_flags, cancellable, error)) {
			return FALSE;
		}

		/* use a copy of the list for the loop because a function called
		 * on the plugin may affect the list which can lead to problems
		 * (e.g. inserting an app in the list on every call results in
		 * an infinite loop) */
		app_list = gs_app_list_copy (list);
		for (guint j = 0; j < gs_app_list_length (app_list); j++) {
			GsApp *app = gs_app_list_index (app_list, j);
			if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD)) {
				helper->function_name = "gs_plugin_refine_app";
			} else {
				helper->function_name = "gs_plugin_refine_wildcard";
			}
			if (!gs_plugin_loader_call_vfunc (helper, plugin, app, NULL,
							  refine_flags, cancellable, error)) {
				return FALSE;
			}
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}
	return TRUE;
}

static gboolean
gs_plugin_loader_app_is_non_wildcard (GsApp *app, gpointer user_data)
{
	return !gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
}

static gboolean
gs_plugin_loader_run_refine_internal (GsPluginLoaderHelper *helper,
				      GsAppList *list,
				      GCancellable *cancellable,
				      GError **error)
{
	/* try to adopt each application with a plugin */
	gs_plugin_loader_run_adopt (helper->plugin_loader, list);

	/* run each plugin */
	if (!gs_plugin_loader_run_refine_filter (helper, list,
						 GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						 cancellable, error))
		return FALSE;

	/* ensure these are sorted by score */
	if (gs_plugin_job_has_refine_flags (helper->plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS)) {
		GPtrArray *reviews;
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			reviews = gs_app_get_reviews (app);
			g_ptr_array_sort (reviews,
					  gs_plugin_loader_review_score_sort_cb);
		}
	}

	/* refine addons one layer deep */
	if (gs_plugin_job_has_refine_flags (helper->plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS)) {
		g_autoptr(GsAppList) addons_list = NULL;
		gs_plugin_job_remove_refine_flags (helper->plugin_job,
						   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS |
						   GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS |
						   GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS);
		addons_list = gs_app_list_new ();
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
		if (gs_app_list_length (addons_list) > 0) {
			if (!gs_plugin_loader_run_refine_internal (helper,
								   addons_list,
								   cancellable,
								   error)) {
				return FALSE;
			}
		}
	}

	/* also do runtime */
	if (gs_plugin_job_has_refine_flags (helper->plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME)) {
		g_autoptr(GsAppList) list2 = gs_app_list_new ();
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *runtime;
			GsApp *app = gs_app_list_index (list, i);
			runtime = gs_app_get_runtime (app);
			if (runtime != NULL)
				gs_app_list_add (list2, runtime);
		}
		if (gs_app_list_length (list2) > 0) {
			if (!gs_plugin_loader_run_refine_internal (helper,
								   list2,
								   cancellable,
								   error)) {
				return FALSE;
			}
		}
	}

	/* also do related packages one layer deep */
	if (gs_plugin_job_has_refine_flags (helper->plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED)) {
		g_autoptr(GsAppList) related_list = NULL;
		gs_plugin_job_remove_refine_flags (helper->plugin_job,
						   GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED);
		related_list = gs_app_list_new ();
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
		if (gs_app_list_length (related_list) > 0) {
			if (!gs_plugin_loader_run_refine_internal (helper,
								   related_list,
								   cancellable,
								   error)) {
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

static gboolean
gs_plugin_loader_run_refine (GsPluginLoaderHelper *helper,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean ret;
	g_autoptr(GsAppList) freeze_list = NULL;
	g_autoptr(GsPluginLoaderHelper) helper2 = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* nothing to do */
	if (gs_app_list_length (list) == 0)
		return TRUE;

	/* freeze all apps */
	freeze_list = gs_app_list_copy (list);
	for (guint i = 0; i < gs_app_list_length (freeze_list); i++) {
		GsApp *app = gs_app_list_index (freeze_list, i);
		g_object_freeze_notify (G_OBJECT (app));
	}

	/* first pass */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "list", list,
					 "refine-flags", gs_plugin_job_get_refine_flags (helper->plugin_job),
					 NULL);
	helper2 = gs_plugin_loader_helper_new (helper->plugin_loader, plugin_job);
	helper2->function_name_parent = helper->function_name;
	ret = gs_plugin_loader_run_refine_internal (helper2, list, cancellable, error);
	if (!ret)
		goto out;

	/* filter any MATCH_ANY_PREFIX apps left in the list */
	gs_app_list_filter (list, gs_plugin_loader_app_is_non_wildcard, NULL);

	/* remove any addons that have the same source as the parent app */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		g_autoptr(GPtrArray) to_remove = g_ptr_array_new ();
		GsApp *app = gs_app_list_index (list, i);
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

out:
	/* now emit all the changed signals */
	for (guint i = 0; i < gs_app_list_length (freeze_list); i++) {
		GsApp *app = gs_app_list_index (freeze_list, i);
		g_idle_add (app_thaw_notify_idle, g_object_ref (app));
	}
	return ret;
}

static void
gs_plugin_loader_job_sorted_truncation_again (GsPluginLoaderHelper *helper)
{
	GsAppListSortFunc sort_func;
	gpointer sort_func_data;

	/* not valid */
	if (gs_plugin_job_get_list (helper->plugin_job) == NULL)
		return;

	/* unset */
	sort_func = gs_plugin_job_get_sort_func (helper->plugin_job);
	if (sort_func == NULL)
		return;
	sort_func_data = gs_plugin_job_get_sort_func_data (helper->plugin_job);
	gs_app_list_sort (gs_plugin_job_get_list (helper->plugin_job), sort_func, sort_func_data);
}

static void
gs_plugin_loader_job_sorted_truncation (GsPluginLoaderHelper *helper)
{
	GsAppListSortFunc sort_func;
	guint max_results;
	GsAppList *list = gs_plugin_job_get_list (helper->plugin_job);

	/* not valid */
	if (list == NULL)
		return;

	/* unset */
	max_results = gs_plugin_job_get_max_results (helper->plugin_job);
	if (max_results == 0)
		return;

	/* already small enough */
	if (gs_app_list_length (list) <= max_results)
		return;

	/* nothing set */
	g_debug ("truncating results to %u from %u",
		 max_results, gs_app_list_length (list));
	sort_func = gs_plugin_job_get_sort_func (helper->plugin_job);
	if (sort_func == NULL) {
		GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);
		g_debug ("no ->sort_func() set for %s, using random!",
			 gs_plugin_action_to_string (action));
		gs_app_list_randomize (list);
	} else {
		gpointer sort_func_data;
		sort_func_data = gs_plugin_job_get_sort_func_data (helper->plugin_job);
		gs_app_list_sort (list, sort_func, sort_func_data);
	}
	gs_app_list_truncate (list, max_results);
}

static gboolean
gs_plugin_loader_run_results (GsPluginLoaderHelper *helper,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (helper->plugin_loader);

	/* run each plugin */
	for (guint i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}
		if (!gs_plugin_loader_call_vfunc (helper, plugin, NULL, NULL,
						  GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						  cancellable, error)) {
			return FALSE;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}
	return TRUE;
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
	/* even without AppData, show things in progress */
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_INSTALLING:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_PURCHASING:
		return TRUE;
		break;
	default:
		break;
	}

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
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) user_data;

	/* never show addons */
	if (gs_app_get_kind (app) == AS_APP_KIND_ADDON) {
		g_debug ("app invalid as addon %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* never show CLI apps */
	if (gs_app_get_kind (app) == AS_APP_KIND_CONSOLE) {
		g_debug ("app invalid as console %s",
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

	/* Don’t show parentally filtered apps unless they’re already
	 * installed. See the comments in gs-details-page.c for details. */
	if (!gs_app_is_installed (app) &&
	    gs_app_has_quirk (app, GS_APP_QUIRK_PARENTAL_FILTER)) {
		g_debug ("app invalid as parentally filtered %s",
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
	if (!gs_plugin_job_has_refine_flags (helper->plugin_job,
						 GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES) &&
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

	/* ignore this crazy application */
	if (g_strcmp0 (gs_app_get_id (app), "gnome-system-monitor-kde.desktop") == 0) {
		g_debug ("Ignoring KDE version of %s", gs_app_get_id (app));
		return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_loader_app_is_valid_updatable (GsApp *app, gpointer user_data)
{
	return gs_plugin_loader_app_is_valid (app, user_data) &&
		gs_app_is_updatable (app);
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
	return !gs_app_has_quirk (app, GS_APP_QUIRK_COMPULSORY);
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

/******************************************************************************/

static gboolean
gs_plugin_loader_featured_debug (GsApp *app, gpointer user_data)
{
	if (g_strcmp0 (gs_app_get_id (app),
	    g_getenv ("GNOME_SOFTWARE_FEATURED")) == 0)
		return TRUE;
	return FALSE;
}

static gint
gs_plugin_loader_app_sort_kind_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	if (gs_app_get_kind (app1) == AS_APP_KIND_DESKTOP)
		return -1;
	if (gs_app_get_kind (app2) == AS_APP_KIND_DESKTOP)
		return 1;
	return 0;
}

static gint
gs_plugin_loader_app_sort_match_value_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	if (gs_app_get_match_value (app1) > gs_app_get_match_value (app2))
		return -1;
	if (gs_app_get_match_value (app1) < gs_app_get_match_value (app2))
		return 1;
	return 0;
}

static gint
gs_plugin_loader_app_sort_prio_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	return gs_app_compare_priority (app1, app2);
}

static gint
gs_plugin_loader_app_sort_version_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
#if AS_CHECK_VERSION(0,7,15)
	return as_utils_vercmp_full (gs_app_get_version (app1),
	                             gs_app_get_version (app2),
	                             AS_VERSION_COMPARE_FLAG_NONE);
#else
	return as_utils_vercmp (gs_app_get_version (app1),
	                        gs_app_get_version (app2));
#endif
}

/******************************************************************************/

/**
 * gs_plugin_loader_job_process_finish:
 * @plugin_loader: A #GsPluginLoader
 * @res: a #GAsyncResult
 * @error: A #GError, or %NULL
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GsAppList *
gs_plugin_loader_job_process_finish (GsPluginLoader *plugin_loader,
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

/**
 * gs_plugin_loader_job_action_finish:
 * @plugin_loader: A #GsPluginLoader
 * @res: a #GAsyncResult
 * @error: A #GError, or %NULL
 *
 * Return value: success
 **/
gboolean
gs_plugin_loader_job_action_finish (GsPluginLoader *plugin_loader,
				     GAsyncResult *res,
				     GError **error)
{
	g_autoptr(GsAppList) list = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	list = g_task_propagate_pointer (G_TASK (res), error);
	return list != NULL;
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
gs_plugin_loader_job_get_categories_thread_cb (GTask *task,
					      gpointer object,
					      gpointer task_data,
					      GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) task_data;

	/* run each plugin */
	if (!gs_plugin_loader_run_results (helper, cancellable, &error)) {
		g_task_return_error (task, error);
		return;
	}

	/* make sure 'All' has the right categories */
	for (guint i = 0; i < helper->catlist->len; i++) {
		GsCategory *cat = g_ptr_array_index (helper->catlist, i);
		gs_plugin_loader_fix_category_all (cat);
	}

	/* sort by name */
	g_ptr_array_sort (helper->catlist, gs_plugin_loader_category_sort_cb);
	for (guint i = 0; i < helper->catlist->len; i++) {
		GsCategory *cat = GS_CATEGORY (g_ptr_array_index (helper->catlist, i));
		gs_category_sort_children (cat);
	}

	/* success */
	if (helper->catlist->len == 0) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "no categories to show");
		return;
	}

	/* show elapsed time */
	gs_plugin_loader_job_debug (helper);

	/* success */
	g_task_return_pointer (task, g_ptr_array_ref (helper->catlist), (GDestroyNotify) g_ptr_array_unref);
}

/**
 * gs_plugin_loader_job_get_categories_async:
 * @plugin_loader: A #GsPluginLoader
 * @plugin_job: job to process
 * @cancellable: a #GCancellable, or %NULL
 * @callback: function to call when complete
 * @user_data: user data to pass to @callback
 *
 * This method calls all plugins that implement the gs_plugin_add_categories()
 * function. The plugins return #GsCategory objects.
 **/
void
gs_plugin_loader_job_get_categories_async (GsPluginLoader *plugin_loader,
				       GsPluginJob *plugin_job,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer user_data)
{
	GsPluginLoaderHelper *helper;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_PLUGIN_JOB (plugin_job));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save helper */
	helper = gs_plugin_loader_helper_new (plugin_loader, plugin_job);
	helper->catlist = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, helper, (GDestroyNotify) gs_plugin_loader_helper_free);
	g_task_run_in_thread (task, gs_plugin_loader_job_get_categories_thread_cb);
}

/**
 * gs_plugin_loader_job_get_categories_finish:
 * @plugin_loader: A #GsPluginLoader
 * @res: a #GAsyncResult
 * @error: A #GError, or %NULL
 *
 * Return value: (element-type GsCategory) (transfer full): A list of applications
 **/
GPtrArray *
gs_plugin_loader_job_get_categories_finish (GsPluginLoader *plugin_loader,
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
emit_pending_apps_idle (gpointer loader)
{
	g_signal_emit (loader, signals[SIGNAL_PENDING_APPS_CHANGED], 0);
	g_object_unref (loader);

	return G_SOURCE_REMOVE;
}

static void
gs_plugin_loader_pending_apps_add (GsPluginLoader *plugin_loader,
				   GsPluginLoaderHelper *helper)
{
	GsAppList *list = gs_plugin_job_get_list (helper->plugin_job);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->pending_apps_mutex);

	g_assert (gs_app_list_length (list) > 0);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		g_ptr_array_add (priv->pending_apps, g_object_ref (app));
		/* make sure the progress is properly initialized */
		gs_app_set_progress (app, 0);
	}
	g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
}

static void
gs_plugin_loader_pending_apps_remove (GsPluginLoader *plugin_loader,
				      GsPluginLoaderHelper *helper)
{
	GsAppList *list = gs_plugin_job_get_list (helper->plugin_job);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->pending_apps_mutex);

	g_assert (gs_app_list_length (list) > 0);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		g_ptr_array_remove (priv->pending_apps, app);

		/* check the app is not still in an action helper */
		switch (gs_app_get_state (app)) {
		case AS_APP_STATE_INSTALLING:
		case AS_APP_STATE_REMOVING:
			g_warning ("application %s left in %s helper",
				   gs_app_get_unique_id (app),
				   as_app_state_to_string (gs_app_get_state (app)));
			gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
			break;
		default:
			break;
		}

	}
	g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
}

static gboolean
load_install_queue (GsPluginLoader *plugin_loader, GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
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

	/* add to GsAppList, deduplicating if required */
	list = gs_app_list_new ();
	names = g_strsplit (contents, "\n", 0);
	for (guint i = 0; names[i] != NULL; i++) {
		g_autoptr(GsApp) app = NULL;
		if (strlen (names[i]) == 0)
			continue;
		app = gs_app_new (names[i]);
		gs_app_set_state (app, AS_APP_STATE_QUEUED_FOR_INSTALL);
		gs_app_list_add (list, app);
	}

	/* add to pending list */
	g_mutex_lock (&priv->pending_apps_mutex);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		g_debug ("adding pending app %s", gs_app_get_unique_id (app));
		g_ptr_array_add (priv->pending_apps, g_object_ref (app));
	}
	g_mutex_unlock (&priv->pending_apps_mutex);

	/* refine */
	if (gs_app_list_length (list) > 0) {
		g_autoptr(GsPluginLoaderHelper) helper = NULL;
		g_autoptr(GsPluginJob) plugin_job = NULL;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE, NULL);
		helper = gs_plugin_loader_helper_new (plugin_loader, plugin_job);
		if (!gs_plugin_loader_run_refine (helper, list, NULL, error))
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
	if (!gs_mkdir_parent (file, &error)) {
		g_warning ("failed to create dir for %s: %s",
			   file, error->message);
		return;
	}
	g_debug ("saving install queue to %s", file);
	ret = g_file_set_contents (file, s->str, (gssize) s->len, &error);
	if (!ret)
		g_warning ("failed to save install queue: %s", error->message);
}

static void
add_app_to_install_queue (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsAppList *addons;
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
	for (i = 0; i < gs_app_list_length (addons); i++) {
		GsApp *addon = gs_app_list_index (addons, i);
		if (gs_app_get_to_be_installed (addon))
			add_app_to_install_queue (plugin_loader, addon);
	}
}

static gboolean
remove_app_from_install_queue (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsAppList *addons;
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
		for (i = 0; i < gs_app_list_length (addons); i++) {
			GsApp *addon = gs_app_list_index (addons, i);
			remove_app_from_install_queue (plugin_loader, addon);
		}
	}

	return ret;
}

/******************************************************************************/

gboolean
gs_plugin_loader_get_allow_updates (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GList) list = NULL;

	/* nothing */
	if (g_hash_table_size (priv->disallow_updates) == 0)
		return TRUE;

	/* list */
	list = g_hash_table_get_values (priv->disallow_updates);
	for (GList *l = list; l != NULL; l = l->next) {
		const gchar *reason = l->data;
		g_debug ("managed updates inhibited by %s", reason);
	}
	return FALSE;
}

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
	g_autoptr(GList) keys = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->events_by_id_mutex);

	/* just add everything */
	keys = g_hash_table_get_keys (priv->events_by_id);
	for (GList *l = keys; l != NULL; l = l->next) {
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
 * Returns: (transfer full): a #GsPluginEvent, or %NULL for none
 **/
GsPluginEvent *
gs_plugin_loader_get_event_default (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GList) keys = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->events_by_id_mutex);

	/* just add everything */
	keys = g_hash_table_get_keys (priv->events_by_id);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		GsPluginEvent *event = g_hash_table_lookup (priv->events_by_id, key);
		if (event == NULL) {
			g_warning ("failed to get event for '%s'", key);
			continue;
		}
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INVALID))
			return g_object_ref (event);
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
gs_plugin_loader_report_event_cb (GsPlugin *plugin,
				  GsPluginEvent *event,
				  GsPluginLoader *plugin_loader)
{
	if (gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE))
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
	gs_plugin_loader_add_event (plugin_loader, event);
}

static void
gs_plugin_loader_allow_updates_cb (GsPlugin *plugin,
				   gboolean allow_updates,
				   GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gpointer exists;

	/* plugin now allowing gnome-software to show updates panel */
	exists = g_hash_table_lookup (priv->disallow_updates, plugin);
	if (allow_updates) {
		if (exists == NULL)
			return;
		g_debug ("plugin %s no longer inhibited managed updates",
			 gs_plugin_get_name (plugin));
		g_hash_table_remove (priv->disallow_updates, plugin);

	/* plugin preventing the updates panel from being shown */
	} else {
		if (exists != NULL)
			return;
		g_debug ("plugin %s inhibited managed updates",
			 gs_plugin_get_name (plugin));
		g_hash_table_insert (priv->disallow_updates,
				     (gpointer) plugin,
				     (gpointer) gs_plugin_get_name (plugin));
	}

	/* something possibly changed, so notify display layer */
	g_object_notify (G_OBJECT (plugin_loader), "allow-updates");
}

static void
gs_plugin_loader_status_changed_cb (GsPlugin *plugin,
				    GsApp *app,
				    GsPluginStatus status,
				    GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	/* nothing specific */
	if (app == NULL || gs_app_get_id (app) == NULL) {
		if (priv->global_status_last != status) {
			g_debug ("emitting global %s",
				 gs_plugin_status_to_string (status));
			g_signal_emit (plugin_loader,
				       signals[SIGNAL_STATUS_CHANGED],
				       0, app, status);
			priv->global_status_last = status;
		}
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
gs_plugin_loader_job_actions_changed_delay_cb (gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	/* notify shells */
	g_debug ("updates-changed");
	g_signal_emit (plugin_loader, signals[SIGNAL_UPDATES_CHANGED], 0);
	priv->updates_changed_id = 0;
	priv->updates_changed_cnt = 0;

	g_object_unref (plugin_loader);
	return FALSE;
}

static void
gs_plugin_loader_job_actions_changed_cb (GsPlugin *plugin, GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	priv->updates_changed_cnt++;
}

static void
gs_plugin_loader_updates_changed (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	if (priv->updates_changed_id != 0)
		return;
	priv->updates_changed_id =
		g_timeout_add_seconds (GS_PLUGIN_LOADER_UPDATES_CHANGED_DELAY,
				       gs_plugin_loader_job_actions_changed_delay_cb,
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
			  G_CALLBACK (gs_plugin_loader_job_actions_changed_cb),
			  plugin_loader);
	g_signal_connect (plugin, "reload",
			  G_CALLBACK (gs_plugin_loader_reload_cb),
			  plugin_loader);
	g_signal_connect (plugin, "status-changed",
			  G_CALLBACK (gs_plugin_loader_status_changed_cb),
			  plugin_loader);
	g_signal_connect (plugin, "report-event",
			  G_CALLBACK (gs_plugin_loader_report_event_cb),
			  plugin_loader);
	g_signal_connect (plugin, "allow-updates",
			  G_CALLBACK (gs_plugin_loader_allow_updates_cb),
			  plugin_loader);
	gs_plugin_set_soup_session (plugin, priv->soup_session);
	gs_plugin_set_download_scheduler (plugin, priv->download_scheduler);
	gs_plugin_set_auth_array (plugin, priv->auth_array);
	gs_plugin_set_locale (plugin, priv->locale);
	gs_plugin_set_language (plugin, priv->language);
	gs_plugin_set_scale (plugin, gs_plugin_loader_get_scale (plugin_loader));
	gs_plugin_set_network_monitor (plugin, priv->network_monitor);
	g_debug ("opened plugin %s: %s", filename, gs_plugin_get_name (plugin));

	/* add to array */
	g_ptr_array_add (priv->plugins, plugin);
}

void
gs_plugin_loader_set_scale (GsPluginLoader *plugin_loader, guint scale)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	/* save globally, and update each plugin */
	priv->scale = scale;
	for (guint i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
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
				 const gchar *auth_id)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	guint i;

	/* match on ID */
	for (i = 0; i < priv->auth_array->len; i++) {
		GsAuth *auth = g_ptr_array_index (priv->auth_array, i);
		if (g_strcmp0 (gs_auth_get_auth_id (auth), auth_id) == 0)
			return auth;
	}
	return NULL;
}

GPtrArray *
gs_plugin_loader_get_auths (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	return priv->auth_array;
}

void
gs_plugin_loader_add_location (GsPluginLoader *plugin_loader, const gchar *location)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	for (guint i = 0; i < priv->locations->len; i++) {
		const gchar *location_tmp = g_ptr_array_index (priv->locations, i);
		if (g_strcmp0 (location_tmp, location) == 0)
			return;
	}
	g_info ("adding plugin location %s", location);
	g_ptr_array_add (priv->locations, g_strdup (location));
}

static gint
gs_plugin_loader_plugin_sort_fn (gconstpointer a, gconstpointer b)
{
	GsPlugin *pa = *((GsPlugin **) a);
	GsPlugin *pb = *((GsPlugin **) b);
	if (gs_plugin_get_order (pa) < gs_plugin_get_order (pb))
		return -1;
	if (gs_plugin_get_order (pa) > gs_plugin_get_order (pb))
		return 1;
	return g_strcmp0 (gs_plugin_get_name (pa), gs_plugin_get_name (pb));
}

static void
gs_plugin_loader_plugin_dir_changed_cb (GFileMonitor *monitor,
					GFile *file,
					GFile *other_file,
					GFileMonitorEvent event_type,
					GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginEvent) event = gs_plugin_event_new ();
	g_autoptr(GError) error = NULL;

	/* already triggered */
	if (priv->plugin_dir_dirty)
		return;

	/* add app */
	gs_plugin_event_set_action (event, GS_PLUGIN_ACTION_SETUP);
	app = gs_plugin_loader_app_create (plugin_loader,
		"system/*/*/*/org.gnome.Software.desktop/*");
	if (app != NULL)
		gs_plugin_event_set_app (event, app);

	/* add error */
	g_set_error_literal (&error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_RESTART_REQUIRED,
			     "A restart is required");
	gs_plugin_event_set_error (event, error);
	gs_plugin_loader_add_event (plugin_loader, event);
	priv->plugin_dir_dirty = TRUE;
}

void
gs_plugin_loader_clear_caches (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	for (guint i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		gs_plugin_cache_invalidate (plugin);
	}
}

/**
 * gs_plugin_loader_setup_again:
 * @plugin_loader: a #GsPluginLoader
 *
 * Calls setup on each plugin. This should only be used from the self tests
 * and in a controlled way.
 */
void
gs_plugin_loader_setup_again (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPluginAction actions[] = {
		GS_PLUGIN_ACTION_DESTROY,
		GS_PLUGIN_ACTION_INITIALIZE,
		GS_PLUGIN_ACTION_SETUP,
		GS_PLUGIN_ACTION_UNKNOWN };

	/* clear global cache */
	gs_plugin_loader_clear_caches (plugin_loader);

	/* remove any events */
	gs_plugin_loader_remove_events (plugin_loader);

	/* call in order */
	for (guint j = 0; actions[j] != GS_PLUGIN_ACTION_UNKNOWN; j++) {
		for (guint i = 0; i < priv->plugins->len; i++) {
			g_autoptr(GError) error_local = NULL;
			g_autoptr(GsPluginLoaderHelper) helper = NULL;
			g_autoptr(GsPluginJob) plugin_job = NULL;
			GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
			if (!gs_plugin_get_enabled (plugin))
				continue;

			plugin_job = gs_plugin_job_newv (actions[j], NULL);
			helper = gs_plugin_loader_helper_new (plugin_loader, plugin_job);
			if (!gs_plugin_loader_call_vfunc (helper, plugin, NULL, NULL,
							  GS_PLUGIN_REFINE_FLAGS_DEFAULT,
							  NULL, &error_local)) {
				g_warning ("resetup of %s failed: %s",
					   gs_plugin_get_name (plugin),
					   error_local->message);
				break;
			}
			if (actions[j] == GS_PLUGIN_ACTION_DESTROY)
				gs_plugin_clear_data (plugin);
		}
	}
}

static gint
gs_plugin_loader_path_sort_fn (gconstpointer a, gconstpointer b)
{
	const gchar *sa = *((const gchar **) a);
	const gchar *sb = *((const gchar **) b);
	return g_strcmp0 (sa, sb);
}

static GPtrArray *
gs_plugin_loader_find_plugins (const gchar *path, GError **error)
{
	const gchar *fn_tmp;
	g_autoptr(GPtrArray) fns = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GDir) dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return NULL;
	while ((fn_tmp = g_dir_read_name (dir)) != NULL) {
		if (!g_str_has_suffix (fn_tmp, ".so"))
			continue;
		g_ptr_array_add (fns, g_build_filename (path, fn_tmp, NULL));
	}
	g_ptr_array_sort (fns, gs_plugin_loader_path_sort_fn);
	return g_steal_pointer (&fns);
}

/**
 * gs_plugin_loader_setup:
 * @plugin_loader: a #GsPluginLoader
 * @whitelist: list of plugin names, or %NULL
 * @blacklist: list of plugin names, or %NULL
 * @cancellable: A #GCancellable, or %NULL
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
			GCancellable *cancellable,
			GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *plugin_name;
	gboolean changes;
	GPtrArray *deps;
	GsPlugin *dep;
	GsPlugin *plugin;
	guint dep_loop_check = 0;
	guint i;
	guint j;
	g_autoptr(GsPluginLoaderHelper) helper = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* use the default, but this requires a 'make install' */
	if (priv->locations->len == 0) {
		g_autofree gchar *filename = NULL;
		filename = g_strdup_printf ("gs-plugins-%s", GS_PLUGIN_API_VERSION);
		g_ptr_array_add (priv->locations, g_build_filename (LIBDIR, filename, NULL));
	}

	for (i = 0; i < priv->locations->len; i++) {
		GFileMonitor *monitor;
		const gchar *location = g_ptr_array_index (priv->locations, i);
		g_autoptr(GFile) plugin_dir = g_file_new_for_path (location);
		monitor = g_file_monitor_directory (plugin_dir,
						    G_FILE_MONITOR_NONE,
						    cancellable,
						    error);
		if (monitor == NULL)
			return FALSE;
		g_signal_connect (monitor, "changed",
				  G_CALLBACK (gs_plugin_loader_plugin_dir_changed_cb), plugin_loader);
		g_ptr_array_add (priv->file_monitors, monitor);
	}

	/* search for plugins */
	for (i = 0; i < priv->locations->len; i++) {
		const gchar *location = g_ptr_array_index (priv->locations, i);
		g_autoptr(GPtrArray) fns = NULL;

		/* search in the plugin directory for plugins */
		g_debug ("searching for plugins in %s", location);
		fns = gs_plugin_loader_find_plugins (location, error);
		if (fns == NULL)
			return FALSE;
		for (j = 0; j < fns->len; j++) {
			const gchar *fn = g_ptr_array_index (fns, j);
			gs_plugin_loader_open_plugin (plugin_loader, fn);
		}
	}

	/* optional whitelist */
	if (whitelist != NULL) {
		for (i = 0; i < priv->plugins->len; i++) {
			gboolean ret;
			plugin = g_ptr_array_index (priv->plugins, i);
			if (!gs_plugin_get_enabled (plugin))
				continue;
			ret = g_strv_contains ((const gchar * const *) whitelist,
					       gs_plugin_get_name (plugin));
			if (!ret) {
				g_debug ("%s not in whitelist, disabling",
					 gs_plugin_get_name (plugin));
			}
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
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INITIALIZE, NULL);
	helper = gs_plugin_loader_helper_new (plugin_loader, plugin_job);
	if (!gs_plugin_loader_run_results (helper, cancellable, error))
		return FALSE;

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
	gs_plugin_job_set_action (helper->plugin_job, GS_PLUGIN_ACTION_SETUP);
	helper->function_name = "gs_plugin_setup";
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(GError) error_local = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!gs_plugin_loader_call_vfunc (helper, plugin, NULL, NULL,
						  GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						  cancellable, &error_local)) {
			g_debug ("disabling %s as setup failed: %s",
				 gs_plugin_get_name (plugin),
				 error_local->message);
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
	g_autoptr(GString) str_enabled = g_string_new (NULL);
	g_autoptr(GString) str_disabled = g_string_new (NULL);

	/* print what the priorities are if verbose */
	for (guint i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		GString *str = gs_plugin_get_enabled (plugin) ? str_enabled : str_disabled;
		g_string_append_printf (str, "%s, ", gs_plugin_get_name (plugin));
		g_debug ("[%s]\t%u\t->\t%s",
			 gs_plugin_get_enabled (plugin) ? "enabled" : "disabld",
			 gs_plugin_get_order (plugin),
			 gs_plugin_get_name (plugin));
	}
	if (str_enabled->len > 2)
		g_string_truncate (str_enabled, str_enabled->len - 2);
	if (str_disabled->len > 2)
		g_string_truncate (str_disabled, str_disabled->len - 2);
	g_info ("enabled plugins: %s", str_enabled->str);
	g_info ("disabled plugins: %s", str_disabled->str);
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
	case PROP_ALLOW_UPDATES:
		g_value_set_boolean (value, gs_plugin_loader_get_allow_updates (plugin_loader));
		break;
	case PROP_NETWORK_AVAILABLE:
		g_value_set_boolean (value, gs_plugin_loader_get_network_available (plugin_loader));
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
		g_autoptr(GsPluginLoaderHelper) helper = NULL;
		g_autoptr(GsPluginJob) plugin_job = NULL;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_DESTROY, NULL);
		helper = gs_plugin_loader_helper_new (plugin_loader, plugin_job);
		gs_plugin_loader_run_results (helper, NULL, NULL);
		g_clear_pointer (&priv->plugins, g_ptr_array_unref);
	}
	if (priv->updates_changed_id != 0) {
		g_source_remove (priv->updates_changed_id);
		priv->updates_changed_id = 0;
	}
	if (priv->network_changed_handler != 0) {
		g_signal_handler_disconnect (priv->network_monitor,
					     priv->network_changed_handler);
		priv->network_changed_handler = 0;
	}
	if (priv->queued_ops_pool != NULL) {
		/* stop accepting more requests and wait until any currently
		 * running ones are finished */
		g_thread_pool_free (priv->queued_ops_pool, TRUE, TRUE);
		priv->queued_ops_pool = NULL;
	}
	g_clear_object (&priv->network_monitor);
	if (priv->download_scheduler != NULL && priv->download_scheduler_invalidated_id != 0) {
		g_signal_handler_disconnect (priv->download_scheduler, priv->download_scheduler_invalidated_id);
		priv->download_scheduler_invalidated_id = 0;
	}
	g_clear_object (&priv->download_scheduler);
	g_clear_object (&priv->soup_session);
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
	g_ptr_array_unref (priv->locations);
	g_free (priv->locale);
	g_free (priv->language);
	g_ptr_array_unref (priv->file_monitors);
	g_hash_table_unref (priv->events_by_id);
	g_hash_table_unref (priv->disallow_updates);

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

	pspec = g_param_spec_boolean ("allow-updates", NULL, NULL,
				      TRUE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ALLOW_UPDATES, pspec);

	pspec = g_param_spec_boolean ("network-available", NULL, NULL,
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_NETWORK_AVAILABLE, pspec);

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
gs_plugin_loader_allow_updates_recheck (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	if (g_settings_get_boolean (priv->settings, "allow-updates")) {
		g_hash_table_remove (priv->disallow_updates, plugin_loader);
	} else {
		g_hash_table_insert (priv->disallow_updates,
				     (gpointer) plugin_loader,
				     (gpointer) "GSettings");
	}
}

static void
gs_plugin_loader_settings_changed_cb (GSettings *settings,
				      const gchar *key,
				      GsPluginLoader *plugin_loader)
{
	if (g_strcmp0 (key, "allow-updates") == 0)
		gs_plugin_loader_allow_updates_recheck (plugin_loader);
}

static gint
get_max_parallel_ops (void)
{
	/* We're allowing 1 op per GB of memory */
	return (gint) MAX (round((gdouble) gs_utils_get_memory_total () / 1024), 1.0);
}

static void
async_result_cb (GObject      *object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	GAsyncResult **result_out = user_data;
	*result_out = g_object_ref (result);
}

static void
download_scheduler_invalidated_cb (MwscScheduler *scheduler,
                                   const GError  *error,
                                   gpointer       user_data)
{
	GsPluginLoader *self = GS_PLUGIN_LOADER (user_data);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (self);

	g_assert (priv->download_scheduler != NULL);
	g_assert (priv->download_scheduler_invalidated_id != 0);

	g_warning ("Download scheduler invalidated; no longer scheduling downloads: %s",
		   error->message);

	/* Unset the scheduler on all plugins.
	 * FIXME: Do we want to poll to create a new scheduler? */
	for (guint i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		gs_plugin_set_download_scheduler (plugin, NULL);
	}

	g_signal_handler_disconnect (priv->download_scheduler, priv->download_scheduler_invalidated_id);
	priv->download_scheduler_invalidated_id = 0;
	g_clear_object (&priv->download_scheduler);
}

static void
gs_plugin_loader_init (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *tmp;
	gchar *match;
	gchar **projects;
	guint i;
	g_autoptr(GMainContext) context = NULL;
	g_autoptr(GAsyncResult) construct_result = NULL;
	g_autoptr(GError) local_error = NULL;

	priv->scale = 1;
	priv->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->pending_apps = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	priv->queued_ops_pool = g_thread_pool_new (gs_plugin_loader_process_in_thread_pool_cb,
						   NULL,
						   get_max_parallel_ops (),
						   FALSE,
						   NULL);
	priv->auth_array = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	priv->file_monitors = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	priv->locations = g_ptr_array_new_with_free_func (g_free);
	priv->settings = g_settings_new ("org.gnome.software");
	g_signal_connect (priv->settings, "changed",
			  G_CALLBACK (gs_plugin_loader_settings_changed_cb), plugin_loader);
	priv->events_by_id = g_hash_table_new_full ((GHashFunc) as_utils_unique_id_hash,
					            (GEqualFunc) as_utils_unique_id_equal,
						    g_free,
						    (GDestroyNotify) g_object_unref);

	/* share a soup session (also disable the double-compression) */
	priv->soup_session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, gs_user_agent (),
							    SOUP_SESSION_TIMEOUT, 10,
							    NULL);

	/* Share a download scheduler.
	 * FIXME: This does some D-Bus calls to set up the scheduler, so should
	 * really be constructed asynchronously. We assume for the moment that
	 * plugin loading always happens before the UI is created, so it won’t
	 * block the UI. */
	context = g_main_context_new ();
	g_main_context_push_thread_default (context);

	/* Get a scheduler object. */
	mwsc_scheduler_new_async (NULL, async_result_cb, &construct_result);
	while (construct_result == NULL)
		g_main_context_iteration (context, TRUE);
	priv->download_scheduler = mwsc_scheduler_new_finish (construct_result, &local_error);
	if (priv->download_scheduler == NULL) {
		g_warning ("Could not create download scheduler; not scheduling downloads: %s",
			   local_error->message);
		g_clear_error (&local_error);
	} else {
		priv->download_scheduler_invalidated_id =
			g_signal_connect (priv->download_scheduler, "invalidated",
					  (GCallback) download_scheduler_invalidated_cb, plugin_loader);
	}

	g_main_context_pop_thread_default (context);

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

	/* the settings key sets the initial override */
	priv->disallow_updates = g_hash_table_new (g_direct_hash, g_direct_equal);
	gs_plugin_loader_allow_updates_recheck (plugin_loader);

	/* get the language from the locale */
	priv->language = g_strdup (priv->locale);
	match = g_strrstr (priv->language, "_");
	if (match != NULL)
		*match = '\0';

	g_mutex_init (&priv->pending_apps_mutex);
	g_mutex_init (&priv->events_by_id_mutex);

	/* monitor the network as the many UI operations need the network */
	gs_plugin_loader_monitor_network (plugin_loader);

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

	ret = gs_plugin_loader_job_action_finish (plugin_loader,
						   res,
						   &error);
	if (!ret) {
		remove_app_from_install_queue (plugin_loader, app);
		g_warning ("failed to install %s: %s",
			   gs_app_get_unique_id (app), error->message);
	}
}

gboolean
gs_plugin_loader_get_network_available (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	if (priv->network_monitor == NULL) {
		g_debug ("no network monitor, so returning network-available=TRUE");
		return TRUE;
	}
	return g_network_monitor_get_network_available (priv->network_monitor);
}

gboolean
gs_plugin_loader_get_network_metered (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	if (priv->network_monitor == NULL) {
		g_debug ("no network monitor, so returning network-metered=FALSE");
		return FALSE;
	}
	return g_network_monitor_get_network_metered (priv->network_monitor);
}

static void
gs_plugin_loader_network_changed_cb (GNetworkMonitor *monitor,
				     gboolean available,
				     GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean metered = g_network_monitor_get_network_metered (priv->network_monitor);

	g_debug ("network status change: %s [%s]",
		 available ? "online" : "offline",
		 metered ? "metered" : "unmetered");

	g_object_notify (G_OBJECT (plugin_loader), "network-available");

	if (available && !metered) {
		g_autoptr(GsAppList) queue = NULL;
		g_mutex_lock (&priv->pending_apps_mutex);
		queue = gs_app_list_new ();
		for (guint i = 0; i < priv->pending_apps->len; i++) {
			GsApp *app = g_ptr_array_index (priv->pending_apps, i);
			if (gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL)
				gs_app_list_add (queue, app);
		}
		g_mutex_unlock (&priv->pending_apps_mutex);
		for (guint i = 0; i < gs_app_list_length (queue); i++) {
			GsApp *app = gs_app_list_index (queue, i);
			g_autoptr(GsPluginJob) plugin_job = NULL;
			plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
							 "app", app,
							 NULL);
			gs_plugin_loader_job_process_async (plugin_loader, plugin_job,
							    NULL,
							    gs_plugin_loader_app_installed_cb,
							    g_object_ref (app));
		}
	}
}

static void
gs_plugin_loader_monitor_network (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GNetworkMonitor *network_monitor;

	network_monitor = g_network_monitor_get_default ();
	if (network_monitor == NULL || priv->network_changed_handler != 0)
		return;
	priv->network_monitor = g_object_ref (network_monitor);

	priv->network_changed_handler =
		g_signal_connect (priv->network_monitor, "network-changed",
				  G_CALLBACK (gs_plugin_loader_network_changed_cb), plugin_loader);

	gs_plugin_loader_network_changed_cb (priv->network_monitor,
			    g_network_monitor_get_network_available (priv->network_monitor),
			    plugin_loader);
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
generic_update_cancelled_cb (GCancellable *cancellable, gpointer data)
{
	GCancellable *app_cancellable = G_CANCELLABLE (data);
	g_cancellable_cancel (app_cancellable);
}

static gboolean
gs_plugin_loader_generic_update (GsPluginLoader *plugin_loader,
				 GsPluginLoaderHelper *helper,
				 GCancellable *cancellable,
				 GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	guint cancel_handler_id = 0;
	GsAppList *list;

	/* run each plugin, per-app version */
	list = gs_plugin_job_get_list (helper->plugin_job);
	for (guint i = 0; i < priv->plugins->len; i++) {
		GsPluginActionFunc plugin_app_func = NULL;
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}
		plugin_app_func = gs_plugin_get_symbol (plugin, helper->function_name);
		if (plugin_app_func == NULL)
			continue;

		/* for each app */
		for (guint j = 0; j < gs_app_list_length (list); j++) {
			GCancellable *app_cancellable;
			GsApp *app = gs_app_list_index (list, j);
			gboolean ret;
			g_autoptr(GError) error_local = NULL;

			/* if the whole operation should be cancelled */
			if (g_cancellable_set_error_if_cancelled (cancellable, error))
				return FALSE;

			/* already installed? */
			if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
				continue;

			/* make sure that the app update is cancelled when the whole op is cancelled */
			app_cancellable = gs_app_get_cancellable (app);
			cancel_handler_id = g_cancellable_connect (cancellable,
								   G_CALLBACK (generic_update_cancelled_cb),
								   g_object_ref (app_cancellable),
								   g_object_unref);

			gs_plugin_job_set_app (helper->plugin_job, app);
			ret = plugin_app_func (plugin, app, app_cancellable, &error_local);
			g_cancellable_disconnect (cancellable, cancel_handler_id);

			if (!ret) {
				if (!gs_plugin_error_handle_failure (helper,
								     plugin,
								     error_local,
								     error)) {
					return FALSE;
				}
			}
		}
		helper->anything_ran = TRUE;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}
	return TRUE;
}

static void
gs_plugin_loader_process_thread_cb (GTask *task,
				    gpointer object,
				    gpointer task_data,
				    GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) task_data;
	GsAppListFilterFlags dedupe_flags;
	GsAppList *list = gs_plugin_job_get_list (helper->plugin_job);
	GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPluginRefineFlags filter_flags;
	GsPluginRefineFlags refine_flags;
	gboolean add_to_pending_array = FALSE;

	/* these change the pending count on the installed panel */
	switch (action) {
	case GS_PLUGIN_ACTION_INSTALL:
	case GS_PLUGIN_ACTION_REMOVE:
		add_to_pending_array = TRUE;
		break;
	default:
		break;
	}

	/* add to pending list */
	if (add_to_pending_array)
		gs_plugin_loader_pending_apps_add (plugin_loader, helper);

	/* run each plugin */
	if (action != GS_PLUGIN_ACTION_REFINE) {
		if (!gs_plugin_loader_run_results (helper, cancellable, &error)) {
			if (add_to_pending_array) {
				gs_app_set_state_recover (gs_plugin_job_get_app (helper->plugin_job));
				gs_plugin_loader_pending_apps_remove (plugin_loader, helper);
			}
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, error);
			return;
		}
	}

	/* run per-app version */
	if (action == GS_PLUGIN_ACTION_UPDATE) {
		helper->function_name = "gs_plugin_update_app";
		if (!gs_plugin_loader_generic_update (plugin_loader, helper,
						      cancellable, &error)) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, error);
			return;
		}
	} else if (action == GS_PLUGIN_ACTION_DOWNLOAD) {
		helper->function_name = "gs_plugin_download_app";
		if (!gs_plugin_loader_generic_update (plugin_loader, helper,
						      cancellable, &error)) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, error);
			return;
		}
	}

	/* remove from pending list */
	if (add_to_pending_array)
		gs_plugin_loader_pending_apps_remove (plugin_loader, helper);

	/* some functions are really required for proper operation */
	switch (action) {
	case GS_PLUGIN_ACTION_DESTROY:
	case GS_PLUGIN_ACTION_GET_INSTALLED:
	case GS_PLUGIN_ACTION_GET_UPDATES:
	case GS_PLUGIN_ACTION_INITIALIZE:
	case GS_PLUGIN_ACTION_INSTALL:
	case GS_PLUGIN_ACTION_DOWNLOAD:
	case GS_PLUGIN_ACTION_LAUNCH:
	case GS_PLUGIN_ACTION_REFRESH:
	case GS_PLUGIN_ACTION_REMOVE:
	case GS_PLUGIN_ACTION_SEARCH:
	case GS_PLUGIN_ACTION_SETUP:
	case GS_PLUGIN_ACTION_UPDATE:
		if (!helper->anything_ran) {
			g_set_error (&error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no plugin could handle %s",
				     gs_plugin_action_to_string (action));
			g_task_return_error (task, error);
			return;
		}
		break;
	case GS_PLUGIN_ACTION_REFINE:
		break;
	default:
		if (!helper->anything_ran) {
			g_debug ("no plugin could handle %s",
				 gs_plugin_action_to_string (action));
		}
		break;
	}

	/* unstage addons */
	if (add_to_pending_array) {
		GsAppList *addons;
		addons = gs_app_get_addons (gs_plugin_job_get_app (helper->plugin_job));
		for (guint i = 0; i < gs_app_list_length (addons); i++) {
			GsApp *addon = gs_app_list_index (addons, i);
			if (gs_app_get_to_be_installed (addon))
				gs_app_set_to_be_installed (addon, FALSE);
		}
	}

	/* modify the local app */
	switch (action) {
	case GS_PLUGIN_ACTION_REVIEW_SUBMIT:
		gs_app_add_review (gs_plugin_job_get_app (helper->plugin_job), gs_plugin_job_get_review (helper->plugin_job));
		break;
	case GS_PLUGIN_ACTION_REVIEW_REMOVE:
		gs_app_remove_review (gs_plugin_job_get_app (helper->plugin_job), gs_plugin_job_get_review (helper->plugin_job));
		break;
	default:
		break;
	}

	/* refine with enough data so that the sort_func can do what it needs */
	filter_flags = gs_plugin_job_get_filter_flags (helper->plugin_job);
	if (filter_flags > 0) {
		g_autoptr(GsPluginLoaderHelper) helper2 = NULL;
		g_autoptr(GsPluginJob) plugin_job = NULL;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
						 "list", list,
						 "refine-flags", filter_flags,
						 NULL);
		helper2 = gs_plugin_loader_helper_new (helper->plugin_loader, plugin_job);
		helper2->function_name_parent = helper->function_name;
		g_debug ("running filter flags with early refine");
		if (!gs_plugin_loader_run_refine_filter (helper2, list,
							 filter_flags,
							 cancellable, &error)) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, error);
			return;
		}
	}

	/* filter to reduce to a sane set */
	gs_plugin_loader_job_sorted_truncation (helper);

	/* set the local file on any of the returned results */
	switch (action) {
	case GS_PLUGIN_ACTION_FILE_TO_APP:
		for (guint j = 0; j < gs_app_list_length (list); j++) {
			GsApp *app = gs_app_list_index (list, j);
			if (gs_app_get_local_file (app) == NULL)
				gs_app_set_local_file (app, gs_plugin_job_get_file (helper->plugin_job));
		}
	default:
		break;
	}

	/* pick up new source id */
	switch (action) {
	case GS_PLUGIN_ACTION_INSTALL:
	case GS_PLUGIN_ACTION_REMOVE:
		gs_plugin_job_add_refine_flags (helper->plugin_job,
		                                GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN |
		                                GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION);
		break;
	default:
		break;
	}

	/* run refine() on each one if required */
	if (gs_plugin_job_get_refine_flags (helper->plugin_job) != 0) {
		if (!gs_plugin_loader_run_refine (helper, list, cancellable, &error)) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, error);
			return;
		}
	} else {
		g_debug ("no refine flags set for transaction");
	}

	/* check the local files have an icon set */
	switch (action) {
	case GS_PLUGIN_ACTION_URL_TO_APP:
	case GS_PLUGIN_ACTION_FILE_TO_APP:
		for (guint j = 0; j < gs_app_list_length (list); j++) {
			GsApp *app = gs_app_list_index (list, j);
			if (_gs_app_get_icon_by_kind (app, AS_ICON_KIND_STOCK) == NULL &&
			    _gs_app_get_icon_by_kind (app, AS_ICON_KIND_LOCAL) == NULL &&
			    _gs_app_get_icon_by_kind (app, AS_ICON_KIND_CACHED) == NULL) {
				g_autoptr(AsIcon) ic = as_icon_new ();
				as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
				if (gs_app_has_quirk (app, GS_APP_QUIRK_HAS_SOURCE))
					as_icon_set_name (ic, "x-package-repository");
				else
					as_icon_set_name (ic, "application-x-executable");
				gs_app_add_icon (app, ic);
			}
		}

		/* run refine() on each one again to pick up any icons */
		refine_flags = gs_plugin_job_get_refine_flags (helper->plugin_job);
		gs_plugin_job_set_refine_flags (helper->plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON);
		if (!gs_plugin_loader_run_refine (helper, list, cancellable, &error)) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, error);
			return;
		}
		/* restore the refine flags so that gs_app_list_filter sees the right thing */
		gs_plugin_job_set_refine_flags (helper->plugin_job, refine_flags);
		break;
	default:
		break;
	}

	/* filter package list */
	switch (action) {
	case GS_PLUGIN_ACTION_URL_TO_APP:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid, helper);
		break;
	case GS_PLUGIN_ACTION_SEARCH:
	case GS_PLUGIN_ACTION_SEARCH_FILES:
	case GS_PLUGIN_ACTION_SEARCH_PROVIDES:
	case GS_PLUGIN_ACTION_GET_ALTERNATES:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid, helper);
		gs_app_list_filter (list, gs_plugin_loader_filter_qt_for_gtk, NULL);
		gs_app_list_filter (list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
		break;
	case GS_PLUGIN_ACTION_GET_CATEGORY_APPS:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid, helper);
		gs_app_list_filter (list, gs_plugin_loader_filter_qt_for_gtk, NULL);
		gs_app_list_filter (list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
		break;
	case GS_PLUGIN_ACTION_GET_INSTALLED:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid, helper);
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_installed, helper);
		break;
	case GS_PLUGIN_ACTION_GET_FEATURED:
		if (g_getenv ("GNOME_SOFTWARE_FEATURED") != NULL) {
			gs_app_list_filter (list, gs_plugin_loader_featured_debug, NULL);
		} else {
			gs_app_list_filter (list, gs_plugin_loader_app_is_valid, helper);
			gs_app_list_filter (list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
		}
		break;
	case GS_PLUGIN_ACTION_GET_UPDATES:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_updatable, helper);
		break;
	case GS_PLUGIN_ACTION_GET_RECENT:
		gs_app_list_filter (list, gs_plugin_loader_app_is_non_compulsory, NULL);
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid, helper);
		gs_app_list_filter (list, gs_plugin_loader_filter_qt_for_gtk, NULL);
		gs_app_list_filter (list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
		break;
	case GS_PLUGIN_ACTION_REFINE:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid, helper);
		break;
	case GS_PLUGIN_ACTION_GET_POPULAR:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid, helper);
		gs_app_list_filter (list, gs_plugin_loader_filter_qt_for_gtk, NULL);
		gs_app_list_filter (list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
		break;
	default:
		break;
	}

	/* only allow one result */
	if (action == GS_PLUGIN_ACTION_URL_TO_APP ||
	    action == GS_PLUGIN_ACTION_FILE_TO_APP) {
		if (gs_app_list_length (list) == 0) {
			g_autofree gchar *str = gs_plugin_job_to_string (helper->plugin_job);
			g_autoptr(GError) error_local = NULL;
			g_autoptr(GsPluginEvent) event = NULL;
			g_set_error (&error_local,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no application was created for %s", str);
			event = gs_plugin_job_to_failed_event (helper->plugin_job, error_local);
			gs_plugin_loader_add_event (plugin_loader, event);
			g_task_return_error (task, g_steal_pointer (&error_local));
			return;
		}
		if (gs_app_list_length (list) > 1) {
			g_autofree gchar *str = gs_plugin_job_to_string (helper->plugin_job);
			g_debug ("more than one application was created for %s", str);
		}
	}

	/* filter duplicates with priority, taking into account the source name
	 * & version, so we combine available updates with the installed app */
	gs_app_list_filter (list, gs_plugin_loader_app_set_prio, plugin_loader);
	dedupe_flags = gs_plugin_job_get_dedupe_flags (helper->plugin_job);
	if (dedupe_flags != GS_APP_LIST_FILTER_FLAG_NONE)
		gs_app_list_filter_duplicates (list, dedupe_flags);

	/* sort these again as the refine may have added useful metadata */
	gs_plugin_loader_job_sorted_truncation_again (helper);

	/* if the plugin used updates-changed actually schedule it now */
	if (priv->updates_changed_cnt > 0)
		gs_plugin_loader_updates_changed (plugin_loader);

	/* show elapsed time */
	gs_plugin_loader_job_debug (helper);

	/* success */
	g_task_return_pointer (task, g_object_ref (list), (GDestroyNotify) g_object_unref);
}

static void
gs_plugin_loader_process_in_thread_pool_cb (gpointer data,
					    gpointer user_data)
{
	GTask *task = data;
	gpointer source_object = g_task_get_source_object (task);
	gpointer task_data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);

	gs_ioprio_init ();

	gs_plugin_loader_process_thread_cb (task, source_object, task_data, cancellable);
	g_object_unref (task);
}

static gboolean
gs_plugin_loader_job_timeout_cb (gpointer user_data)
{
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) user_data;

	/* call the cancellable */
	g_debug ("cancelling job as it took too long");
	g_cancellable_cancel (helper->cancellable);

	/* failed */
	helper->timeout_triggered = TRUE;
	helper->timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static void
gs_plugin_loader_cancelled_cb (GCancellable *cancellable, GsPluginLoaderHelper *helper)
{
	/* just proxy this forward */
	g_cancellable_cancel (helper->cancellable);
}

static void
gs_plugin_loader_schedule_task (GsPluginLoader *plugin_loader,
				GTask *task)
{
	GsPluginLoaderHelper *helper = g_task_get_task_data (task);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsApp *app = gs_plugin_job_get_app (helper->plugin_job);

	if (app != NULL) {
		/* set the pending-action to the app */
		GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);
		gs_app_set_pending_action (app, action);
	}
	g_thread_pool_push (priv->queued_ops_pool, g_object_ref (task), NULL);
}

/**
 * gs_plugin_loader_job_process_async:
 * @plugin_loader: A #GsPluginLoader
 * @plugin_job: job to process
 * @cancellable: a #GCancellable, or %NULL
 * @callback: function to call when complete
 * @user_data: user data to pass to @callback
 *
 * This method calls all plugins.
 **/
void
gs_plugin_loader_job_process_async (GsPluginLoader *plugin_loader,
				    GsPluginJob *plugin_job,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	GsPluginAction action;
	GsPluginLoaderHelper *helper;
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GCancellable) cancellable_job = g_cancellable_new ();

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_PLUGIN_JOB (plugin_job));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* check job has valid action */
	if (gs_plugin_job_get_action (plugin_job) == GS_PLUGIN_ACTION_UNKNOWN) {
		g_autofree gchar *job_str = gs_plugin_job_to_string (plugin_job);
		task = g_task_new (plugin_loader, cancellable_job, callback, user_data);
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "job has no valid action: %s", job_str);
		return;
	}

	/* deal with the install queue */
	action = gs_plugin_job_get_action (plugin_job);
	if (action == GS_PLUGIN_ACTION_REMOVE) {
		if (remove_app_from_install_queue (plugin_loader, gs_plugin_job_get_app (plugin_job))) {
			GsAppList *list = gs_plugin_job_get_list (plugin_job);
			task = g_task_new (plugin_loader, cancellable, callback, user_data);
			g_task_return_pointer (task, g_object_ref (list), (GDestroyNotify) g_object_unref);
			return;
		}
	}

	/* hardcoded, so resolve a set list */
	if (action == GS_PLUGIN_ACTION_GET_POPULAR) {
		g_auto(GStrv) apps = NULL;
		if (g_getenv ("GNOME_SOFTWARE_POPULAR") != NULL) {
			apps = g_strsplit (g_getenv ("GNOME_SOFTWARE_POPULAR"), ",", 0);
		} else {
			apps = g_settings_get_strv (priv->settings, "popular-overrides");
		}
		if (apps != NULL && g_strv_length (apps) > 0) {
			GsAppList *list = gs_plugin_job_get_list (plugin_job);
			for (guint i = 0; apps[i] != NULL; i++) {
				g_autoptr(GsApp) app = gs_app_new (apps[i]);
				gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
				gs_app_list_add (list, app);
			}
			gs_plugin_job_set_action (plugin_job, GS_PLUGIN_ACTION_REFINE);
		}
	}

	/* FIXME: the plugins should specify this, rather than hardcoding */
	if (gs_plugin_job_has_refine_flags (plugin_job,
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_KEY_COLORS)) {
		gs_plugin_job_add_refine_flags (plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON);
	}
	if (gs_plugin_job_has_refine_flags (plugin_job,
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_UI)) {
		gs_plugin_job_add_refine_flags (plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN);
	}
	if (gs_plugin_job_has_refine_flags (plugin_job,
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH)) {
		gs_plugin_job_add_refine_flags (plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_CATEGORIES);
	}
	if (gs_plugin_job_has_refine_flags (plugin_job,
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME)) {
		gs_plugin_job_add_refine_flags (plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN);
	}
	if (gs_plugin_job_has_refine_flags (plugin_job,
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE)) {
		gs_plugin_job_add_refine_flags (plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME);
	}

	/* FIXME: this is probably a bug */
	if (action == GS_PLUGIN_ACTION_GET_DISTRO_UPDATES ||
	    action == GS_PLUGIN_ACTION_GET_SOURCES) {
		gs_plugin_job_add_refine_flags (plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION);
	}

	/* get alternates is unusual in that it needs an app input and a list
	 * output -- so undo the helpful app add in gs_plugin_job_set_app() */
	if (action == GS_PLUGIN_ACTION_GET_ALTERNATES) {
		GsAppList *list = gs_plugin_job_get_list (plugin_job);
		gs_app_list_remove_all (list);
	}

	/* check required args */
	task = g_task_new (plugin_loader, cancellable_job, callback, user_data);
	switch (action) {
	case GS_PLUGIN_ACTION_SEARCH:
	case GS_PLUGIN_ACTION_SEARCH_FILES:
	case GS_PLUGIN_ACTION_SEARCH_PROVIDES:
	case GS_PLUGIN_ACTION_URL_TO_APP:
		if (gs_plugin_job_get_search (plugin_job) == NULL) {
			g_task_return_new_error (task,
						 GS_PLUGIN_ERROR,
						 GS_PLUGIN_ERROR_NOT_SUPPORTED,
						 "no valid search terms");
			return;
		}
		break;
	case GS_PLUGIN_ACTION_REVIEW_SUBMIT:
	case GS_PLUGIN_ACTION_REVIEW_UPVOTE:
	case GS_PLUGIN_ACTION_REVIEW_DOWNVOTE:
	case GS_PLUGIN_ACTION_REVIEW_REPORT:
	case GS_PLUGIN_ACTION_REVIEW_REMOVE:
	case GS_PLUGIN_ACTION_REVIEW_DISMISS:
		if (gs_plugin_job_get_review (plugin_job) == NULL) {
			g_task_return_new_error (task,
						 GS_PLUGIN_ERROR,
						 GS_PLUGIN_ERROR_NOT_SUPPORTED,
						 "no valid review object");
			return;
		}
		break;
	default:
		break;
	}

	/* sorting fallbacks */
	switch (action) {
	case GS_PLUGIN_ACTION_SEARCH:
		if (gs_plugin_job_get_sort_func (plugin_job) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_match_value_cb);
		}
		break;
	case GS_PLUGIN_ACTION_GET_RECENT:
		if (gs_plugin_job_get_sort_func (plugin_job) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_kind_cb);
		}
		break;
	case GS_PLUGIN_ACTION_GET_CATEGORY_APPS:
		if (gs_plugin_job_get_sort_func (plugin_job) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_name_cb);
		}
		break;
	case GS_PLUGIN_ACTION_GET_ALTERNATES:
		if (gs_plugin_job_get_sort_func (plugin_job) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_prio_cb);
		}
		break;
	case GS_PLUGIN_ACTION_GET_DISTRO_UPDATES:
		if (gs_plugin_job_get_sort_func (plugin_job) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_version_cb);
		}
		break;
	default:
		break;
	}

	/* save helper */
	helper = gs_plugin_loader_helper_new (plugin_loader, plugin_job);
	g_task_set_task_data (task, helper, (GDestroyNotify) gs_plugin_loader_helper_free);

	/* let the task cancel itself */
	g_task_set_check_cancellable (task, FALSE);
	g_task_set_return_on_cancel (task, FALSE);

	/* pre-tokenize search */
	if (action == GS_PLUGIN_ACTION_SEARCH) {
		const gchar *search = gs_plugin_job_get_search (plugin_job);
		helper->tokens = as_utils_search_tokenize (search);
		if (helper->tokens == NULL) {
			g_task_return_new_error (task,
						 GS_PLUGIN_ERROR,
						 GS_PLUGIN_ERROR_NOT_SUPPORTED,
						 "failed to tokenize %s", search);
			return;
		}
	}

	/* jobs always have a valid cancellable, so proxy the caller */
	helper->cancellable = g_object_ref (cancellable_job);
	if (cancellable != NULL) {
		helper->cancellable_caller = g_object_ref (cancellable);
		helper->cancellable_id =
			g_cancellable_connect (helper->cancellable_caller,
					       G_CALLBACK (gs_plugin_loader_cancelled_cb),
					       helper, NULL);
	}

	/* set up a hang handler */
	switch (action) {
	case GS_PLUGIN_ACTION_GET_ALTERNATES:
	case GS_PLUGIN_ACTION_GET_CATEGORY_APPS:
	case GS_PLUGIN_ACTION_GET_FEATURED:
	case GS_PLUGIN_ACTION_GET_INSTALLED:
	case GS_PLUGIN_ACTION_GET_POPULAR:
	case GS_PLUGIN_ACTION_GET_RECENT:
	case GS_PLUGIN_ACTION_SEARCH:
	case GS_PLUGIN_ACTION_SEARCH_FILES:
	case GS_PLUGIN_ACTION_SEARCH_PROVIDES:
		helper->timeout_id =
			g_timeout_add_seconds (gs_plugin_job_get_timeout (plugin_job),
					       gs_plugin_loader_job_timeout_cb,
					       helper);
		break;
	default:
		break;
	}

	switch (action) {
	case GS_PLUGIN_ACTION_INSTALL:
	case GS_PLUGIN_ACTION_UPDATE:
	case GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD:
		/* these actions must be performed by the thread pool because we
		 * want to limit the number of them running in parallel */
		gs_plugin_loader_schedule_task (plugin_loader, task);
		return;
	default:
		break;
	}

	/* run in a thread */
	g_task_run_in_thread (task, gs_plugin_loader_process_thread_cb);
}

/******************************************************************************/

/**
 * gs_plugin_loader_get_plugin_supported:
 * @plugin_loader: A #GsPluginLoader
 * @function_name: a function name
 *
 * This function returns TRUE if the symbol is found in any enabled plugin.
 */
gboolean
gs_plugin_loader_get_plugin_supported (GsPluginLoader *plugin_loader,
				       const gchar *function_name)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	for (guint i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (gs_plugin_get_symbol (plugin, function_name) != NULL)
			return TRUE;
	}
	return FALSE;
}

/**
 * gs_plugin_loader_app_create:
 * @plugin_loader: a #GsPluginLoader
 * @unique_id: a unique_id
 *
 * Returns an application from the global cache, creating if required.
 *
 * Returns: (transfer full): a #GsApp
 **/
GsApp *
gs_plugin_loader_app_create (GsPluginLoader *plugin_loader, const gchar *unique_id)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GsPluginLoaderHelper) helper = NULL;

	/* use the plugin loader to convert a wildcard app*/
	app = gs_app_new (NULL);
	gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
	gs_app_set_from_unique_id (app, unique_id);
	gs_app_list_add (list, app);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE, NULL);
	helper = gs_plugin_loader_helper_new (plugin_loader, plugin_job);
	if (!gs_plugin_loader_run_refine (helper, list, NULL, &error)) {
		g_warning ("%s", error->message);
		return NULL;
	}

	/* return the first returned app that's not a wildcard */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app_tmp = gs_app_list_index (list, i);
		if (!gs_app_has_quirk (app_tmp, GS_APP_QUIRK_IS_WILDCARD))
			return g_object_ref (app_tmp);
	}

	/* does not exist */
	g_warning ("failed to create an app for %s", unique_id);
	return NULL;
}

/**
 * gs_plugin_loader_get_system_app:
 * @plugin_loader: a #GsPluginLoader
 *
 * Returns the application that represents the currently installed OS.
 *
 * Returns: (transfer full): a #GsApp
 **/
GsApp *
gs_plugin_loader_get_system_app (GsPluginLoader *plugin_loader)
{
	return gs_plugin_loader_app_create (plugin_loader, "*/*/*/*/system/*");
}

/**
 * gs_plugin_loader_set_max_parallel_ops:
 * @plugin_loader: a #GsPluginLoader
 * @max_ops: the maximum number of parallel operations
 *
 * Sets the number of maximum number of queued operations (install/update/upgrade-download)
 * to be processed at a time. If @max_ops is 0, then it will set the default maximum number.
 */
void
gs_plugin_loader_set_max_parallel_ops (GsPluginLoader *plugin_loader,
				       guint max_ops)
{
	g_autoptr(GError) error = NULL;
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	if (max_ops == 0)
		max_ops = get_max_parallel_ops ();
	if (!g_thread_pool_set_max_threads (priv->queued_ops_pool, max_ops, &error))
		g_warning ("Failed to set the maximum number of ops in parallel: %s",
			   error->message);
}
