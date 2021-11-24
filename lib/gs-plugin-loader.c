/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2007-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <locale.h>
#include <glib/gi18n.h>
#include <appstream.h>
#include <math.h>

#ifdef HAVE_SYSPROF
#include <sysprof-capture.h>
#endif

#include "gs-app-collation.h"
#include "gs-app-private.h"
#include "gs-app-list-private.h"
#include "gs-category-manager.h"
#include "gs-category-private.h"
#include "gs-external-appstream-utils.h"
#include "gs-ioprio.h"
#include "gs-os-release.h"
#include "gs-plugin-loader.h"
#include "gs-plugin.h"
#include "gs-plugin-event.h"
#include "gs-plugin-job-private.h"
#include "gs-plugin-private.h"
#include "gs-utils.h"

#define GS_PLUGIN_LOADER_UPDATES_CHANGED_DELAY	3	/* s */
#define GS_PLUGIN_LOADER_RELOAD_DELAY		5	/* s */

struct _GsPluginLoader
{
	GObject			 parent;

	GPtrArray		*plugins;
	GPtrArray		*locations;
	gchar			*language;
	gboolean		 plugin_dir_dirty;
	SoupSession		*soup_session;
	GPtrArray		*file_monitors;
	GsPluginStatus		 global_status_last;
	AsPool			*as_pool;

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
	gulong			 network_available_notify_handler;
	gulong			 network_metered_notify_handler;

	GsCategoryManager	*category_manager;
	GsOdrsProvider		*odrs_provider;  /* (owned) (nullable) */

#ifdef HAVE_SYSPROF
	SysprofCaptureWriter	*sysprof_writer;  /* (owned) (nullable) */
#endif
};

static void gs_plugin_loader_monitor_network (GsPluginLoader *plugin_loader);
static void add_app_to_install_queue (GsPluginLoader *plugin_loader, GsApp *app);
static void gs_plugin_loader_process_in_thread_pool_cb (gpointer data, gpointer user_data);

G_DEFINE_TYPE (GsPluginLoader, gs_plugin_loader, G_TYPE_OBJECT)

enum {
	SIGNAL_STATUS_CHANGED,
	SIGNAL_PENDING_APPS_CHANGED,
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_RELOAD,
	SIGNAL_BASIC_AUTH_START,
	SIGNAL_ASK_UNTRUSTED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_EVENTS,
	PROP_ALLOW_UPDATES,
	PROP_NETWORK_AVAILABLE,
	PROP_NETWORK_METERED,
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
typedef gboolean	 (*GsPluginGetLangPacksFunc)	(GsPlugin	*plugin,
							 GsAppList	*list,
							 const gchar    *locale,
							 GCancellable	*cancellable,
							 GError		**error);


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

	gs_app_set_progress (app, GS_APP_PROGRESS_UNKNOWN);

	for (guint i = 0; i < gs_app_list_length (addons); i++) {
		GsApp *app_addons = gs_app_list_index (addons, i);
		gs_app_set_progress (app_addons, GS_APP_PROGRESS_UNKNOWN);
	}
	for (guint i = 0; i < gs_app_list_length (related); i++) {
		GsApp *app_related = gs_app_list_index (related, i);
		gs_app_set_progress (app_related, GS_APP_PROGRESS_UNKNOWN);
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
	case GS_PLUGIN_ACTION_INSTALL_REPO:
	case GS_PLUGIN_ACTION_REMOVE_REPO:
	case GS_PLUGIN_ACTION_ENABLE_REPO:
	case GS_PLUGIN_ACTION_DISABLE_REPO:
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
		g_debug ("Disconnecting cancellable %p", helper->cancellable_caller);
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

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsPluginLoaderHelper, gs_plugin_loader_helper_free)

static gint
gs_plugin_loader_app_sort_name_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	return gs_utils_sort_strcmp (gs_app_get_name (app1), gs_app_get_name (app2));
}

GsPlugin *
gs_plugin_loader_find_plugin (GsPluginLoader *plugin_loader,
			      const gchar *plugin_name)
{
	for (guint i = 0; i < plugin_loader->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (plugin_loader->plugins, i);
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

void
gs_plugin_loader_add_event (GsPluginLoader *plugin_loader, GsPluginEvent *event)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin_loader->events_by_id_mutex);

	/* events should always have a unique ID, either constructed from the
	 * app they are processing or preferably from the GError message */
	if (gs_plugin_event_get_unique_id (event) == NULL) {
		g_warning ("failed to add event from action %s",
			   gs_plugin_action_to_string (gs_plugin_event_get_action (event)));
		return;
	}

	g_hash_table_insert (plugin_loader->events_by_id,
			     g_strdup (gs_plugin_event_get_unique_id (event)),
			     g_object_ref (event));
	g_idle_add (gs_plugin_loader_notify_idle_cb, plugin_loader);
}

/**
 * gs_plugin_loader_claim_error:
 * @plugin_loader: a #GsPluginLoader
 * @plugin: (nullable): a #GsPlugin to get an application from, or %NULL
 * @action: a #GsPluginAction associated with the @error
 * @app: (nullable): a #GsApp for the event, or %NULL
 * @interactive: whether to set interactive flag
 * @error: a #GError to claim
 *
 * Convert the @error into a plugin event and add it to the queue.
 *
 * The @plugin is used only if the @error contains a reference
 * to a concrete application, in which case any cached application
 * overrides the passed in @app.
 *
 * The %GS_PLUGIN_ERROR_CANCELLED and %G_IO_ERROR_CANCELLED errors
 * are automatically ignored.
 *
 * Since: 41
 **/
void
gs_plugin_loader_claim_error (GsPluginLoader *plugin_loader,
			      GsPlugin *plugin,
			      GsPluginAction action,
			      GsApp *app,
			      gboolean interactive,
			      const GError *error)
{
	g_autoptr(GError) error_copy = NULL;
	g_autofree gchar *app_id = NULL;
	g_autofree gchar *origin_id = NULL;
	g_autoptr(GsPluginEvent) event = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (error != NULL);

	if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED) ||
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		return;

	/* find and strip any unique IDs from the error message */
	error_copy = g_error_copy (error);

	for (guint i = 0; i < 2; i++) {
		if (app_id == NULL)
			app_id = gs_utils_error_strip_app_id (error_copy);
		if (origin_id == NULL)
			origin_id = gs_utils_error_strip_origin_id (error_copy);
	}

	/* invalid */
	if (error_copy->domain != GS_PLUGIN_ERROR) {
		g_warning ("not GsPlugin error %s:%i: %s",
			   g_quark_to_string (error_copy->domain),
			   error_copy->code,
			   error_copy->message);
		error_copy->domain = GS_PLUGIN_ERROR;
		error_copy->code = GS_PLUGIN_ERROR_FAILED;
	}

	/* create event which is handled by the GsShell */
	event = gs_plugin_event_new ();
	gs_plugin_event_set_error (event, error_copy);
	gs_plugin_event_set_action (event, action);
	if (app != NULL)
		gs_plugin_event_set_app (event, app);
	if (interactive)
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
	gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);

	/* set the app and origin IDs if we managed to scrape them from the error above */
	if (plugin != NULL && as_utils_data_id_valid (app_id)) {
		g_autoptr(GsApp) cached_app = gs_plugin_cache_lookup (plugin, app_id);
		if (cached_app != NULL) {
			g_debug ("found app %s in error", app_id);
			gs_plugin_event_set_app (event, cached_app);
		} else {
			g_debug ("no unique ID found for app %s", app_id);
		}
	}
	if (plugin != NULL && as_utils_data_id_valid (origin_id)) {
		g_autoptr(GsApp) origin = gs_plugin_cache_lookup (plugin, origin_id);
		if (origin != NULL) {
			g_debug ("found origin %s in error", origin_id);
			gs_plugin_event_set_origin (event, origin);
		} else {
			g_debug ("no unique ID found for origin %s", origin_id);
		}
	}

	/* add event to the queue */
	gs_plugin_loader_add_event (plugin_loader, event);
}

/**
 * gs_plugin_loader_claim_job_error:
 * @plugin_loader: a #GsPluginLoader
 * @plugin: (nullable): a #GsPlugin to get an application from, or %NULL
 * @job: a #GsPluginJob for the @error
 * @error: a #GError to claim
 *
 * The same as gs_plugin_loader_claim_error(), only reads the information
 * from the @job.
 *
 * Since: 41
 **/
void
gs_plugin_loader_claim_job_error (GsPluginLoader *plugin_loader,
				  GsPlugin *plugin,
				  GsPluginJob *job,
				  const GError *error)
{
	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_PLUGIN_JOB (job));
	g_return_if_fail (error != NULL);

	gs_plugin_loader_claim_error (plugin_loader, plugin,
		gs_plugin_job_get_action (job),
		gs_plugin_job_get_app (job),
		gs_plugin_job_get_interactive (job),
		error);
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
	return FALSE;
}

static gboolean
gs_plugin_error_handle_failure (GsPluginLoaderHelper *helper,
				GsPlugin *plugin,
				const GError *error_local,
				GError **error)
{
	g_autoptr(GError) error_local_copy = NULL;
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

	if (gs_plugin_job_get_propagate_error (helper->plugin_job)) {
		g_propagate_error (error, g_error_copy (error_local));
		return FALSE;
	}

	/* this is only ever informational */
	if (g_error_matches (error_local, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED) ||
	    g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_debug ("ignoring error cancelled: %s", error_local->message);
		return TRUE;
	}

	/* find and strip any unique IDs from the error message */
	error_local_copy = g_error_copy (error_local);

	for (guint i = 0; i < 2; i++) {
		if (app_id == NULL)
			app_id = gs_utils_error_strip_app_id (error_local_copy);
		if (origin_id == NULL)
			origin_id = gs_utils_error_strip_origin_id (error_local_copy);
	}

	/* fatal error */
	if (gs_plugin_loader_is_error_fatal (error_local_copy) ||
	    g_getenv ("GS_SELF_TEST_PLUGIN_ERROR_FAIL_HARD") != NULL) {
		if (error != NULL)
			*error = g_steal_pointer (&error_local_copy);
		return FALSE;
	}

	gs_plugin_loader_claim_job_error (helper->plugin_loader, plugin, helper->plugin_job, error_local);

	return TRUE;
}

/**
 * gs_plugin_loader_run_adopt:
 * @plugin_loader: a #GsPluginLoader
 * @list: list of apps to try and adopt
 *
 * Call the gs_plugin_adopt_app() function on each plugin on each app in @list
 * to try and find the plugin which should manage each app.
 *
 * This function is intended to be used by internal gnome-software code.
 *
 * Since: 42
 */
void
gs_plugin_loader_run_adopt (GsPluginLoader *plugin_loader, GsAppList *list)
{
	guint i;
	guint j;

	/* go through each plugin in order */
	for (i = 0; i < plugin_loader->plugins->len; i++) {
		GsPluginAdoptAppFunc adopt_app_func = NULL;
		GsPlugin *plugin = g_ptr_array_index (plugin_loader->plugins, i);
		adopt_app_func = gs_plugin_get_symbol (plugin, "gs_plugin_adopt_app");
		if (adopt_app_func == NULL)
			continue;
		for (j = 0; j < gs_app_list_length (list); j++) {
			GsApp *app = gs_app_list_index (list, j);

			if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
				continue;
			if (!gs_app_has_management_plugin (app, NULL))
				continue;

			adopt_app_func (plugin, app);

			if (!gs_app_has_management_plugin (app, NULL)) {
				g_debug ("%s adopted %s",
					 gs_plugin_get_name (plugin),
					 gs_app_get_unique_id (app));
			}
		}
	}
	for (j = 0; j < gs_app_list_length (list); j++) {
		GsApp *app = gs_app_list_index (list, j);

		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		if (!gs_app_has_management_plugin (app, NULL))
			continue;

		g_debug ("nothing adopted %s", gs_app_get_unique_id (app));
	}
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
	GsPluginLoader *plugin_loader = helper->plugin_loader;
	GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);
	gboolean ret = TRUE;
	gpointer func = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GTimer) timer = g_timer_new ();
#ifdef HAVE_SYSPROF
	gint64 begin_time_nsec = SYSPROF_CAPTURE_CURRENT_TIME;
#endif

	/* load the possible symbol */
	func = gs_plugin_get_symbol (plugin, helper->function_name);
	if (func == NULL)
		return TRUE;

	/* at least one plugin supports this vfunc */
	helper->anything_ran = TRUE;

	/* fallback if unset */
	if (app == NULL)
		app = gs_plugin_job_get_app (helper->plugin_job);
	if (list == NULL)
		list = gs_plugin_job_get_list (helper->plugin_job);
	if (refine_flags == GS_PLUGIN_REFINE_FLAGS_NONE)
		refine_flags = gs_plugin_job_get_refine_flags (helper->plugin_job);

	/* set what plugin is running on the job */
	gs_plugin_job_set_plugin (helper->plugin_job, plugin);

	/* run the correct vfunc */
	if (gs_plugin_job_get_interactive (helper->plugin_job))
		gs_plugin_interactive_inc (plugin);
	switch (action) {
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
	case GS_PLUGIN_ACTION_INSTALL_REPO:
	case GS_PLUGIN_ACTION_REMOVE_REPO:
	case GS_PLUGIN_ACTION_ENABLE_REPO:
	case GS_PLUGIN_ACTION_DISABLE_REPO:
		{
			GsPluginActionFunc plugin_func = func;
			ret = plugin_func (plugin, app, cancellable, &error_local);
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
			const gchar *search[2] = { gs_plugin_job_get_search (helper->plugin_job), NULL };
			ret = plugin_func (plugin, (gchar **) search, list,
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
	case GS_PLUGIN_ACTION_GET_LANGPACKS:
		{
			GsPluginGetLangPacksFunc plugin_func = func;
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
	if ((action == GS_PLUGIN_ACTION_INSTALL || action == GS_PLUGIN_ACTION_INSTALL_REPO) &&
	    app != NULL && gs_app_get_state (app) == GS_APP_STATE_QUEUED_FOR_INSTALL) {
	        add_app_to_install_queue (plugin_loader, app);
	}

#ifdef HAVE_SYSPROF
	if (plugin_loader->sysprof_writer != NULL) {
		g_autofree gchar *sysprof_name = NULL;
		g_autofree gchar *sysprof_message = NULL;

		sysprof_name = g_strconcat ("vfunc:", gs_plugin_action_to_string (action), NULL);
		sysprof_message = gs_plugin_job_to_string (helper->plugin_job);
		sysprof_capture_writer_add_mark (plugin_loader->sysprof_writer,
						 begin_time_nsec,
						 sched_getcpu (),
						 getpid (),
						 SYSPROF_CAPTURE_CURRENT_TIME - begin_time_nsec,
						 "gnome-software",
						 sysprof_name,
						 sysprof_message);
	}
#endif  /* HAVE_SYSPROF */

	/* check the plugin didn't take too long */
	if (g_timer_elapsed (timer, NULL) > 1.0f) {
		g_log_structured_standard (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
					   __FILE__, G_STRINGIFY (__LINE__),
					   G_STRFUNC,
					   "plugin %s took %.1f seconds to do %s",
					   gs_plugin_get_name (plugin),
					   g_timer_elapsed (timer, NULL),
					   gs_plugin_action_to_string (action));
	}

	return TRUE;
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
	sort_func = gs_plugin_job_get_sort_func (helper->plugin_job, &sort_func_data);
	if (sort_func == NULL)
		return;
	gs_app_list_sort (gs_plugin_job_get_list (helper->plugin_job), sort_func, sort_func_data);
}

static void
gs_plugin_loader_job_sorted_truncation (GsPluginLoaderHelper *helper)
{
	GsAppListSortFunc sort_func;
	gpointer sort_func_data;
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
	sort_func = gs_plugin_job_get_sort_func (helper->plugin_job, &sort_func_data);
	if (sort_func == NULL) {
		GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);
		g_debug ("no ->sort_func() set for %s, using random!",
			 gs_plugin_action_to_string (action));
		gs_app_list_randomize (list);
	} else {
		gs_app_list_sort (list, sort_func, sort_func_data);
	}
	gs_app_list_truncate (list, max_results);
}

static gboolean
gs_plugin_loader_run_results (GsPluginLoaderHelper *helper,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoader *plugin_loader = helper->plugin_loader;
	GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);
#ifdef HAVE_SYSPROF
	gint64 begin_time_nsec G_GNUC_UNUSED = SYSPROF_CAPTURE_CURRENT_TIME;
#endif

	/* Refining is done separately as it’s a special action */
	g_assert (!GS_IS_PLUGIN_JOB_REFINE (helper->plugin_job));

	/* Download updated external appstream before anything else */
#ifdef ENABLE_EXTERNAL_APPSTREAM
	if (action == GS_PLUGIN_ACTION_REFRESH) {
		/* FIXME: Using plugin_loader->plugins->pdata[0] is a hack; see
		 * comment below for details. */
		if (!gs_external_appstream_refresh (plugin_loader->plugins->pdata[0],
						    gs_plugin_job_get_age (helper->plugin_job),
						    cancellable, error))
			return FALSE;
	}
#endif

	/* run each plugin */
	for (guint i = 0; i < plugin_loader->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (plugin_loader->plugins, i);
		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}
		if (!gs_plugin_loader_call_vfunc (helper, plugin, NULL, NULL,
						  GS_PLUGIN_REFINE_FLAGS_NONE,
						  cancellable, error)) {
			return FALSE;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	if (action == GS_PLUGIN_ACTION_REFRESH &&
	    plugin_loader->odrs_provider != NULL) {
		/* FIXME: Using plugin_loader->plugins->pdata[0] is a hack; the
		 * GsOdrsProvider needs access to a GsPlugin to access global
		 * state for gs_plugin_download_file(), but it doesn’t really
		 * matter which plugin it’s accessed through. In lieu of
		 * refactoring gs_plugin_download_file(), use the first plugin
		 * in the list for now. */
		if (!gs_odrs_provider_refresh (plugin_loader->odrs_provider,
					       plugin_loader->plugins->pdata[0],
					       gs_plugin_job_get_age (helper->plugin_job),
					       cancellable, error))
			return FALSE;
	}

#ifdef HAVE_SYSPROF
	if (plugin_loader->sysprof_writer != NULL) {
		g_autofree gchar *sysprof_name = NULL;
		g_autofree gchar *sysprof_message = NULL;

		sysprof_name = g_strconcat ("run-results:",
					    gs_plugin_action_to_string (gs_plugin_job_get_action (helper->plugin_job)),
					    NULL);
		sysprof_message = gs_plugin_job_to_string (helper->plugin_job);
		sysprof_capture_writer_add_mark (plugin_loader->sysprof_writer,
						 begin_time_nsec,
						 sched_getcpu (),
						 getpid (),
						 SYSPROF_CAPTURE_CURRENT_TIME - begin_time_nsec,
						 "gnome-software",
						 sysprof_name,
						 sysprof_message);
	}
#endif  /* HAVE_SYSPROF */

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
gs_plugin_loader_app_is_valid_installed (GsApp *app, gpointer user_data)
{
	/* even without AppData, show things in progress */
	switch (gs_app_get_state (app)) {
	case GS_APP_STATE_INSTALLING:
	case GS_APP_STATE_REMOVING:
		return TRUE;
		break;
	default:
		break;
	}

	switch (gs_app_get_kind (app)) {
	case AS_COMPONENT_KIND_OPERATING_SYSTEM:
	case AS_COMPONENT_KIND_CODEC:
	case AS_COMPONENT_KIND_FONT:
		g_debug ("app invalid as %s: %s",
			 as_component_kind_to_string (gs_app_get_kind (app)),
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

gboolean
gs_plugin_loader_app_is_valid (GsApp               *app,
                               GsPluginRefineFlags  refine_flags)
{
	/* never show addons */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_ADDON) {
		g_debug ("app invalid as addon %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* never show CLI apps */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_CONSOLE_APP) {
		g_debug ("app invalid as console %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unknown state */
	if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN) {
		g_debug ("app invalid as state unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unconverted unavailables */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_UNKNOWN &&
		gs_app_get_state (app) == GS_APP_STATE_UNAVAILABLE) {
		g_debug ("app invalid as unconverted unavailable %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show blocklisted apps */
	if (gs_app_has_quirk (app, GS_APP_QUIRK_HIDE_EVERYWHERE)) {
		g_debug ("app invalid as blocklisted %s",
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

	/* don't show apps with hide-from-search quirk, unless they are already installed */
	if (!gs_app_is_installed (app) &&
	    gs_app_has_quirk (app, GS_APP_QUIRK_HIDE_FROM_SEARCH)) {
		g_debug ("app invalid as hide-from-search quirk set %s",
		         gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show sources */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_REPOSITORY) {
		g_debug ("app invalid as source %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unknown kind */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_UNKNOWN) {
		g_debug ("app invalid as kind unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unconverted packages in the application view */
	if (!(refine_flags & GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES) &&
	    gs_app_get_kind (app) == AS_COMPONENT_KIND_GENERIC &&
	    gs_app_get_special_kind (app) == GS_APP_SPECIAL_KIND_NONE) {
		g_debug ("app invalid as only a %s: %s",
			 as_component_kind_to_string (gs_app_get_kind (app)),
			 gs_plugin_loader_get_app_str (app));
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
gs_plugin_loader_app_is_valid_filter (GsApp    *app,
                                      gpointer  user_data)
{
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) user_data;

	return gs_plugin_loader_app_is_valid (app, gs_plugin_job_get_refine_flags (helper->plugin_job));
}

static gboolean
gs_plugin_loader_app_is_valid_updatable (GsApp *app, gpointer user_data)
{
	return gs_plugin_loader_app_is_valid_filter (app, user_data) &&
		(gs_app_is_updatable (app) || gs_app_get_state (app) == GS_APP_STATE_INSTALLING);
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
	const gchar *tmp;
	guint i;

	/* search for any compatible projects */
	tmp = gs_app_get_project_group (app);
	if (tmp == NULL)
		return TRUE;
	for (i = 0; plugin_loader->compatible_projects[i] != NULL; i++) {
		if (g_strcmp0 (tmp,  plugin_loader->compatible_projects[i]) == 0)
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
	if (gs_app_get_kind (app1) == AS_COMPONENT_KIND_DESKTOP_APP)
		return -1;
	if (gs_app_get_kind (app2) == AS_COMPONENT_KIND_DESKTOP_APP)
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
	return as_vercmp_simple (gs_app_get_version (app1),
				 gs_app_get_version (app2));
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
	GTask *task;
	GsAppList *list = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	task = G_TASK (res);

	/* Return cancelled if the task was cancelled and there is no other error set.
	 *
	 * This is needed because we set the task `check_cancellable` to FALSE,
	 * to be able to catch other errors such as timeout, but that means
	 * g_task_propagate_pointer() will ignore if the task was cancelled and only
	 * check if there was an error (i.e. g_task_return_*error*).
	 *
	 * We only do this if there is no error already set in the task (e.g.
	 * timeout) because in that case we want to return the existing error.
	 */
	if (!g_task_had_error (task)) {
		GCancellable *cancellable = g_task_get_cancellable (task);

		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			gs_utils_error_convert_gio (error);
			return NULL;
		}
	}
	list = g_task_propagate_pointer (task, error);
	gs_utils_error_convert_gio (error);
	return list;
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
	return gs_utils_sort_strcmp (gs_category_get_name (cata),
				     gs_category_get_name (catb));
}

static void
gs_plugin_loader_job_get_categories_thread_cb (GTask *task,
					      gpointer object,
					      gpointer task_data,
					      GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (g_task_get_source_object (task));
	GError *error = NULL;
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) task_data;
	g_autoptr(GMainContext) context = g_main_context_new ();
	g_autoptr(GMainContextPusher) pusher = g_main_context_pusher_new (context);
	GsCategory * const *categories = NULL;
	gsize n_categories;
	g_autofree gchar *job_debug = NULL;
#ifdef HAVE_SYSPROF
	gint64 begin_time_nsec G_GNUC_UNUSED = SYSPROF_CAPTURE_CURRENT_TIME;
#endif

	/* get the categories */
	categories = gs_category_manager_get_categories (plugin_loader->category_manager, &n_categories);

	for (gsize i = 0; i < n_categories; i++)
		g_ptr_array_add (helper->catlist, g_object_ref (categories[i]));

	/* run each plugin */
	if (!gs_plugin_loader_run_results (helper, cancellable, &error)) {
		g_task_return_error (task, error);
		return;
	}

	/* sort by name */
	g_ptr_array_sort (helper->catlist, gs_plugin_loader_category_sort_cb);
	for (guint i = 0; i < helper->catlist->len; i++) {
		GsCategory *cat = GS_CATEGORY (g_ptr_array_index (helper->catlist, i));
		gs_category_sort_children (cat);
	}

#ifdef HAVE_SYSPROF
	if (plugin_loader->sysprof_writer != NULL) {
		g_autofree gchar *sysprof_message = gs_plugin_job_to_string (helper->plugin_job);
		sysprof_capture_writer_add_mark (plugin_loader->sysprof_writer,
						 begin_time_nsec,
						 sched_getcpu (),
						 getpid (),
						 SYSPROF_CAPTURE_CURRENT_TIME - begin_time_nsec,
						 "gnome-software",
						 "get-categories",
						 sysprof_message);
	}
#endif  /* HAVE_SYSPROF */

	/* show elapsed time */
	job_debug = gs_plugin_job_to_string (helper->plugin_job);
	g_debug ("%s", job_debug);

	/* success */
	if (helper->catlist->len == 0)
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "no categories to show");
	else
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
	g_task_set_source_tag (task, gs_plugin_loader_job_get_categories_async);
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
	GPtrArray *array;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	array = g_task_propagate_pointer (G_TASK (res), error);
	gs_utils_error_convert_gio (error);
	return array;
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
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin_loader->pending_apps_mutex);

	g_assert (gs_app_list_length (list) > 0);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		g_ptr_array_add (plugin_loader->pending_apps, g_object_ref (app));
		/* make sure the progress is properly initialized */
		gs_app_set_progress (app, GS_APP_PROGRESS_UNKNOWN);
	}
	g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
}

static void
gs_plugin_loader_pending_apps_remove (GsPluginLoader *plugin_loader,
				      GsPluginLoaderHelper *helper)
{
	GsAppList *list = gs_plugin_job_get_list (helper->plugin_job);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin_loader->pending_apps_mutex);

	g_assert (gs_app_list_length (list) > 0);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		g_ptr_array_remove (plugin_loader->pending_apps, app);

		/* check the app is not still in an action helper */
		switch (gs_app_get_state (app)) {
		case GS_APP_STATE_INSTALLING:
		case GS_APP_STATE_REMOVING:
			g_warning ("application %s left in %s helper",
				   gs_app_get_unique_id (app),
				   gs_app_state_to_string (gs_app_get_state (app)));
			gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
			break;
		default:
			break;
		}

	}
	g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
}

static void
async_result_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	GAsyncResult **result_out = user_data;

	*result_out = g_object_ref (result);
	g_main_context_wakeup (g_main_context_get_thread_default ());
}

static gboolean
load_install_queue (GsPluginLoader *plugin_loader, GError **error)
{
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
		gs_app_set_state (app, GS_APP_STATE_QUEUED_FOR_INSTALL);
		gs_app_list_add (list, app);
	}

	/* add to pending list */
	g_mutex_lock (&plugin_loader->pending_apps_mutex);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		g_debug ("adding pending app %s", gs_app_get_unique_id (app));
		g_ptr_array_add (plugin_loader->pending_apps, g_object_ref (app));
	}
	g_mutex_unlock (&plugin_loader->pending_apps_mutex);

	/* refine */
	if (gs_app_list_length (list) > 0) {
		g_autoptr(GsPluginJob) refine_job = NULL;
		g_autoptr(GAsyncResult) refine_result = NULL;
		g_autoptr(GsAppList) new_list = NULL;

		refine_job = gs_plugin_job_refine_new (list, GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID);
		gs_plugin_loader_job_process_async (plugin_loader, refine_job,
						    NULL,
						    async_result_cb,
						    &refine_result);

		/* FIXME: Make this sync until the enclosing function is
		 * refactored to be async. */
		while (refine_result == NULL)
			g_main_context_iteration (g_main_context_get_thread_default (), TRUE);

		new_list = gs_plugin_loader_job_process_finish (plugin_loader, refine_result, error);
		if (new_list == NULL)
			return FALSE;
	}
	return TRUE;
}

static void
save_install_queue (GsPluginLoader *plugin_loader)
{
	GPtrArray *pending_apps;
	gboolean ret;
	gint i;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) s = NULL;
	g_autofree gchar *file = NULL;

	s = g_string_new ("");
	pending_apps = plugin_loader->pending_apps;
	g_mutex_lock (&plugin_loader->pending_apps_mutex);
	for (i = (gint) pending_apps->len - 1; i >= 0; i--) {
		GsApp *app;
		app = g_ptr_array_index (pending_apps, i);
		if (gs_app_get_state (app) == GS_APP_STATE_QUEUED_FOR_INSTALL) {
			g_string_append (s, gs_app_get_id (app));
			g_string_append_c (s, '\n');
		}
	}
	g_mutex_unlock (&plugin_loader->pending_apps_mutex);

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
	GsAppList *addons;
	guint i;
	guint id;

	/* queue the app itself */
	g_mutex_lock (&plugin_loader->pending_apps_mutex);
	g_ptr_array_add (plugin_loader->pending_apps, g_object_ref (app));
	g_mutex_unlock (&plugin_loader->pending_apps_mutex);

	gs_app_set_state (app, GS_APP_STATE_QUEUED_FOR_INSTALL);
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
	GsAppList *addons;
	gboolean ret;
	guint i;
	guint id;

	g_mutex_lock (&plugin_loader->pending_apps_mutex);
	ret = g_ptr_array_remove (plugin_loader->pending_apps, app);
	g_mutex_unlock (&plugin_loader->pending_apps_mutex);

	if (ret) {
		gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
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
	GHashTableIter iter;
	gpointer value;

	/* nothing */
	if (g_hash_table_size (plugin_loader->disallow_updates) == 0)
		return TRUE;

	/* list */
	g_hash_table_iter_init (&iter, plugin_loader->disallow_updates);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		const gchar *reason = value;
		g_debug ("managed updates inhibited by %s", reason);
	}
	return FALSE;
}

GsAppList *
gs_plugin_loader_get_pending (GsPluginLoader *plugin_loader)
{
	GsAppList *array;
	guint i;

	array = gs_app_list_new ();
	g_mutex_lock (&plugin_loader->pending_apps_mutex);
	for (i = 0; i < plugin_loader->pending_apps->len; i++) {
		GsApp *app = g_ptr_array_index (plugin_loader->pending_apps, i);
		gs_app_list_add (array, app);
	}
	g_mutex_unlock (&plugin_loader->pending_apps_mutex);

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
	GPtrArray *events = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin_loader->events_by_id_mutex);
	GHashTableIter iter;
	gpointer key, value;

	/* just add everything */
	g_hash_table_iter_init (&iter, plugin_loader->events_by_id);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *id = key;
		GsPluginEvent *event = value;
		if (event == NULL) {
			g_warning ("failed to get event for '%s'", id);
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
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin_loader->events_by_id_mutex);
	GHashTableIter iter;
	gpointer key, value;

	/* just add everything */
	g_hash_table_iter_init (&iter, plugin_loader->events_by_id);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		const gchar *id = key;
		GsPluginEvent *event = value;
		if (event == NULL) {
			g_warning ("failed to get event for '%s'", id);
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
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&plugin_loader->events_by_id_mutex);
	g_hash_table_remove_all (plugin_loader->events_by_id);
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
	gboolean changed = FALSE;

	/* plugin now allowing gnome-software to show updates panel */
	if (allow_updates) {
		if (g_hash_table_remove (plugin_loader->disallow_updates, plugin)) {
			g_debug ("plugin %s no longer inhibited managed updates",
				 gs_plugin_get_name (plugin));
			changed = TRUE;
		}

	/* plugin preventing the updates panel from being shown */
	} else {
		if (g_hash_table_replace (plugin_loader->disallow_updates,
					  (gpointer) plugin,
					  (gpointer) gs_plugin_get_name (plugin))) {
			g_debug ("plugin %s inhibited managed updates",
				 gs_plugin_get_name (plugin));
			changed = TRUE;
		}
	}

	/* notify display layer */
	if (changed)
		g_object_notify (G_OBJECT (plugin_loader), "allow-updates");
}

static void
gs_plugin_loader_status_changed_cb (GsPlugin *plugin,
				    GsApp *app,
				    GsPluginStatus status,
				    GsPluginLoader *plugin_loader)
{
	/* nothing specific */
	if (app == NULL || gs_app_get_id (app) == NULL) {
		if (plugin_loader->global_status_last != status) {
			g_debug ("emitting global %s",
				 gs_plugin_status_to_string (status));
			g_signal_emit (plugin_loader,
				       signals[SIGNAL_STATUS_CHANGED],
				       0, app, status);
			plugin_loader->global_status_last = status;
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

static void
gs_plugin_loader_basic_auth_start_cb (GsPlugin *plugin,
                                      const gchar *remote,
                                      const gchar *realm,
                                      GCallback callback,
                                      gpointer user_data,
                                      GsPluginLoader *plugin_loader)
{
	g_debug ("emitting basic-auth-start %s", realm);
	g_signal_emit (plugin_loader,
		       signals[SIGNAL_BASIC_AUTH_START], 0,
		       remote,
		       realm,
		       callback,
		       user_data);
}

static gboolean
gs_plugin_loader_ask_untrusted_cb (GsPlugin *plugin,
				   const gchar *title,
				   const gchar *msg,
				   const gchar *details,
				   const gchar *accept_label,
				   GsPluginLoader *plugin_loader)
{
	gboolean accepts = FALSE;
	g_debug ("emitting ask-untrusted title:'%s', msg:'%s' details:'%s' accept-label:'%s'", title, msg, details, accept_label);
	g_signal_emit (plugin_loader,
		       signals[SIGNAL_ASK_UNTRUSTED], 0,
		       title, msg, details, accept_label, &accepts);
	return accepts;
}

static gboolean
gs_plugin_loader_job_actions_changed_delay_cb (gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);

	/* notify shells */
	g_debug ("updates-changed");
	g_signal_emit (plugin_loader, signals[SIGNAL_UPDATES_CHANGED], 0);
	plugin_loader->updates_changed_id = 0;
	plugin_loader->updates_changed_cnt = 0;

	g_object_unref (plugin_loader);
	return FALSE;
}

static void
gs_plugin_loader_job_actions_changed_cb (GsPlugin *plugin, GsPluginLoader *plugin_loader)
{
	plugin_loader->updates_changed_cnt++;
}

static void
gs_plugin_loader_updates_changed (GsPluginLoader *plugin_loader)
{
	if (plugin_loader->updates_changed_id != 0)
		return;
	plugin_loader->updates_changed_id =
		g_timeout_add_seconds (GS_PLUGIN_LOADER_UPDATES_CHANGED_DELAY,
				       gs_plugin_loader_job_actions_changed_delay_cb,
				       g_object_ref (plugin_loader));
}

static gboolean
gs_plugin_loader_reload_delay_cb (gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);

	/* notify shells */
	g_debug ("emitting ::reload");
	g_signal_emit (plugin_loader, signals[SIGNAL_RELOAD], 0);
	plugin_loader->reload_id = 0;

	g_object_unref (plugin_loader);
	return FALSE;
}

static void
gs_plugin_loader_reload_cb (GsPlugin *plugin,
			    GsPluginLoader *plugin_loader)
{
	if (plugin_loader->reload_id != 0)
		return;
	plugin_loader->reload_id =
		g_timeout_add_seconds (GS_PLUGIN_LOADER_RELOAD_DELAY,
				       gs_plugin_loader_reload_delay_cb,
				       g_object_ref (plugin_loader));
}

static void
gs_plugin_loader_repository_changed_cb (GsPlugin *plugin,
					GsApp *repository,
					GsPluginLoader *plugin_loader)
{
	GApplication *application = g_application_get_default ();

	/* Can be NULL when running the self tests */
	if (application) {
		g_signal_emit_by_name (application,
			"repository-changed",
			repository);
	}
}

static void
gs_plugin_loader_open_plugin (GsPluginLoader *plugin_loader,
			      const gchar *filename)
{
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
	g_signal_connect (plugin, "basic-auth-start",
			  G_CALLBACK (gs_plugin_loader_basic_auth_start_cb),
			  plugin_loader);
	g_signal_connect (plugin, "report-event",
			  G_CALLBACK (gs_plugin_loader_report_event_cb),
			  plugin_loader);
	g_signal_connect (plugin, "allow-updates",
			  G_CALLBACK (gs_plugin_loader_allow_updates_cb),
			  plugin_loader);
	g_signal_connect (plugin, "repository-changed",
			  G_CALLBACK (gs_plugin_loader_repository_changed_cb),
			  plugin_loader);
	g_signal_connect (plugin, "ask-untrusted",
			  G_CALLBACK (gs_plugin_loader_ask_untrusted_cb),
			  plugin_loader);
	gs_plugin_set_soup_session (plugin, plugin_loader->soup_session);
	gs_plugin_set_language (plugin, plugin_loader->language);
	gs_plugin_set_scale (plugin, gs_plugin_loader_get_scale (plugin_loader));
	gs_plugin_set_network_monitor (plugin, plugin_loader->network_monitor);
	g_debug ("opened plugin %s: %s", filename, gs_plugin_get_name (plugin));

	/* add to array */
	g_ptr_array_add (plugin_loader->plugins, plugin);
}

void
gs_plugin_loader_set_scale (GsPluginLoader *plugin_loader, guint scale)
{
	/* save globally, and update each plugin */
	plugin_loader->scale = scale;
	for (guint i = 0; i < plugin_loader->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (plugin_loader->plugins, i);
		gs_plugin_set_scale (plugin, scale);
	}
}

guint
gs_plugin_loader_get_scale (GsPluginLoader *plugin_loader)
{
	return plugin_loader->scale;
}

void
gs_plugin_loader_add_location (GsPluginLoader *plugin_loader, const gchar *location)
{
	for (guint i = 0; i < plugin_loader->locations->len; i++) {
		const gchar *location_tmp = g_ptr_array_index (plugin_loader->locations, i);
		if (g_strcmp0 (location_tmp, location) == 0)
			return;
	}
	g_info ("adding plugin location %s", location);
	g_ptr_array_add (plugin_loader->locations, g_strdup (location));
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
gs_plugin_loader_software_app_created_cb (GObject *source_object,
					  GAsyncResult *result,
					  gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginEvent) event = gs_plugin_event_new ();
	g_autoptr(GError) error = NULL;

	app = gs_plugin_loader_app_create_finish (plugin_loader, result, NULL);

	/* add app */
	gs_plugin_event_set_action (event, GS_PLUGIN_ACTION_UNKNOWN);
	if (app != NULL)
		gs_plugin_event_set_app (event, app);

	/* add error */
	g_set_error_literal (&error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_RESTART_REQUIRED,
			     "A restart is required");
	gs_plugin_event_set_error (event, error);
	gs_plugin_loader_add_event (plugin_loader, event);
}

static void
gs_plugin_loader_plugin_dir_changed_cb (GFileMonitor *monitor,
					GFile *file,
					GFile *other_file,
					GFileMonitorEvent event_type,
					GsPluginLoader *plugin_loader)
{
	/* already triggered */
	if (plugin_loader->plugin_dir_dirty)
		return;

	gs_plugin_loader_app_create_async (plugin_loader, "system/*/*/org.gnome.Software.desktop/*",
		NULL, gs_plugin_loader_software_app_created_cb, NULL);

	plugin_loader->plugin_dir_dirty = TRUE;
}

void
gs_plugin_loader_clear_caches (GsPluginLoader *plugin_loader)
{
	for (guint i = 0; i < plugin_loader->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (plugin_loader->plugins, i);
		gs_plugin_cache_invalidate (plugin);
	}
}

typedef struct {
	GsPluginLoader *plugin_loader;  /* (unowned) */
	GMainContext *context;  /* (owned) */
	guint n_pending;
#ifdef HAVE_SYSPROF
	gint64 setup_begin_time_nsec;
#endif
} SetupData;

static void plugin_setup_cb (GObject      *source_object,
                             GAsyncResult *result,
                             gpointer      user_data);

static void
gs_plugin_loader_call_setup (GsPluginLoader *plugin_loader,
                             GCancellable   *cancellable)
{
	SetupData setup_data;

	setup_data.plugin_loader = plugin_loader;
	setup_data.n_pending = 1;  /* incremented until all operations have been started */
	setup_data.context = g_main_context_new ();
#ifdef HAVE_SYSPROF
	setup_data.setup_begin_time_nsec = SYSPROF_CAPTURE_CURRENT_TIME;
#endif

	g_main_context_push_thread_default (setup_data.context);

	for (guint i = 0; i < plugin_loader->plugins->len; i++) {
		GsPlugin *plugin = GS_PLUGIN (plugin_loader->plugins->pdata[i]);

		if (!gs_plugin_get_enabled (plugin))
			continue;

		if (GS_PLUGIN_GET_CLASS (plugin)->setup_async != NULL) {
			GS_PLUGIN_GET_CLASS (plugin)->setup_async (plugin, cancellable,
								   plugin_setup_cb, &setup_data);
			setup_data.n_pending++;
		}
	}

	/* Wait for setup to complete in all plugins.
	 * Nested iteration of the main context is not generally good practice,
	 * but we expect gs_plugin_loader_setup() to only ever be executed early
	 * in the process’ lifetime, so it’s probably OK. This could be
	 * refactored in future. */
	setup_data.n_pending--;

	while (setup_data.n_pending > 0)
		g_main_context_iteration (setup_data.context, TRUE);

	g_main_context_pop_thread_default (setup_data.context);
	g_clear_pointer (&setup_data.context, g_main_context_unref);
}

static void
plugin_setup_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	SetupData *data = user_data;
	g_autoptr(GError) local_error = NULL;

	g_assert (GS_PLUGIN_GET_CLASS (plugin)->setup_finish != NULL);

	if (!GS_PLUGIN_GET_CLASS (plugin)->setup_finish (plugin, result, &local_error)) {
		g_debug ("disabling %s as setup failed: %s",
			 gs_plugin_get_name (plugin),
			 local_error->message);
		gs_plugin_set_enabled (plugin, FALSE);
	}

#ifdef HAVE_SYSPROF
	if (data->plugin_loader->sysprof_writer != NULL) {
		sysprof_capture_writer_add_mark (data->plugin_loader->sysprof_writer,
						 data->setup_begin_time_nsec,
						 sched_getcpu (),
						 getpid (),
						 SYSPROF_CAPTURE_CURRENT_TIME - data->setup_begin_time_nsec,
						 "gnome-software",
						 "setup-plugin",
						 NULL);
	}
#endif  /* HAVE_SYSPROF */

	/* Indicate this plugin has finished setting up. */
	data->n_pending--;
	g_main_context_wakeup (data->context);
}

typedef struct {
	GsPluginLoader *plugin_loader;  /* (unowned) */
	GMainContext *context;  /* (owned) */
	guint n_pending;
} ShutdownData;

static void plugin_shutdown_cb (GObject      *source_object,
                                GAsyncResult *result,
                                gpointer      user_data);

static void
gs_plugin_loader_call_shutdown (GsPluginLoader *plugin_loader,
                                GCancellable   *cancellable)
{
	ShutdownData shutdown_data;

	shutdown_data.plugin_loader = plugin_loader;
	shutdown_data.n_pending = 1;  /* incremented until all operations have been started */
	shutdown_data.context = g_main_context_new ();

	g_main_context_push_thread_default (shutdown_data.context);

	for (guint i = 0; i < plugin_loader->plugins->len; i++) {
		GsPlugin *plugin = GS_PLUGIN (plugin_loader->plugins->pdata[i]);

		if (!gs_plugin_get_enabled (plugin))
			continue;

		if (GS_PLUGIN_GET_CLASS (plugin)->shutdown_async != NULL) {
			GS_PLUGIN_GET_CLASS (plugin)->shutdown_async (plugin, cancellable,
								      plugin_shutdown_cb, &shutdown_data);
			shutdown_data.n_pending++;
		}
	}

	/* Wait for setup to complete in all plugins. */
	shutdown_data.n_pending--;

	while (shutdown_data.n_pending > 0)
		g_main_context_iteration (shutdown_data.context, TRUE);

	g_main_context_pop_thread_default (shutdown_data.context);
	g_clear_pointer (&shutdown_data.context, g_main_context_unref);
}

static void
plugin_shutdown_cb (GObject      *source_object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	ShutdownData *data = user_data;
	g_autoptr(GError) local_error = NULL;

	g_assert (GS_PLUGIN_GET_CLASS (plugin)->shutdown_finish != NULL);

	if (!GS_PLUGIN_GET_CLASS (plugin)->shutdown_finish (plugin, result, &local_error)) {
		g_debug ("disabling %s as shutdown failed: %s",
			 gs_plugin_get_name (plugin),
			 local_error->message);
		gs_plugin_set_enabled (plugin, FALSE);
	}

	/* Indicate this plugin has finished shutting down. */
	data->n_pending--;
	g_main_context_wakeup (data->context);
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
#ifdef HAVE_SYSPROF
	gint64 begin_time_nsec G_GNUC_UNUSED = SYSPROF_CAPTURE_CURRENT_TIME;
#endif

	/* Shut down */
	gs_plugin_loader_call_shutdown (plugin_loader, NULL);

	/* clear global cache */
	gs_plugin_loader_clear_caches (plugin_loader);

	/* remove any events */
	gs_plugin_loader_remove_events (plugin_loader);

	/* Start all the plugins setting up again in parallel. */
	gs_plugin_loader_call_setup (plugin_loader, NULL);

#ifdef HAVE_SYSPROF
	if (plugin_loader->sysprof_writer != NULL) {
		sysprof_capture_writer_add_mark (plugin_loader->sysprof_writer,
						 begin_time_nsec,
						 sched_getcpu (),
						 getpid (),
						 SYSPROF_CAPTURE_CURRENT_TIME - begin_time_nsec,
						 "gnome-software",
						 "setup-again",
						 NULL);
	}
#endif  /* HAVE_SYSPROF */
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
 * @allowlist: list of plugin names, or %NULL
 * @blocklist: list of plugin names, or %NULL
 * @cancellable: A #GCancellable, or %NULL
 * @error: A #GError, or %NULL
 *
 * Sets up the plugin loader ready for use.
 *
 * Returns: %TRUE for success
 */
gboolean
gs_plugin_loader_setup (GsPluginLoader *plugin_loader,
			gchar **allowlist,
			gchar **blocklist,
			GCancellable *cancellable,
			GError **error)
{
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
#ifdef HAVE_SYSPROF
	gint64 begin_time_nsec G_GNUC_UNUSED = SYSPROF_CAPTURE_CURRENT_TIME;
#endif

	/* use the default, but this requires a 'make install' */
	if (plugin_loader->locations->len == 0) {
		g_autofree gchar *filename = NULL;
		filename = g_strdup_printf ("plugins-%s", GS_PLUGIN_API_VERSION);
		g_ptr_array_add (plugin_loader->locations, g_build_filename (LIBDIR, "gnome-software", filename, NULL));
	}

	for (i = 0; i < plugin_loader->locations->len; i++) {
		GFileMonitor *monitor;
		const gchar *location = g_ptr_array_index (plugin_loader->locations, i);
		g_autoptr(GFile) plugin_dir = g_file_new_for_path (location);
		g_debug ("monitoring plugin location %s", location);
		monitor = g_file_monitor_directory (plugin_dir,
						    G_FILE_MONITOR_NONE,
						    cancellable,
						    error);
		if (monitor == NULL)
			return FALSE;
		g_signal_connect (monitor, "changed",
				  G_CALLBACK (gs_plugin_loader_plugin_dir_changed_cb), plugin_loader);
		g_ptr_array_add (plugin_loader->file_monitors, monitor);
	}

	/* search for plugins */
	for (i = 0; i < plugin_loader->locations->len; i++) {
		const gchar *location = g_ptr_array_index (plugin_loader->locations, i);
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

	/* optional allowlist */
	if (allowlist != NULL) {
		for (i = 0; i < plugin_loader->plugins->len; i++) {
			gboolean ret;
			plugin = g_ptr_array_index (plugin_loader->plugins, i);
			if (!gs_plugin_get_enabled (plugin))
				continue;
			ret = g_strv_contains ((const gchar * const *) allowlist,
					       gs_plugin_get_name (plugin));
			if (!ret) {
				g_debug ("%s not in allowlist, disabling",
					 gs_plugin_get_name (plugin));
			}
			gs_plugin_set_enabled (plugin, ret);
		}
	}

	/* optional blocklist */
	if (blocklist != NULL) {
		for (i = 0; i < plugin_loader->plugins->len; i++) {
			gboolean ret;
			plugin = g_ptr_array_index (plugin_loader->plugins, i);
			if (!gs_plugin_get_enabled (plugin))
				continue;
			ret = g_strv_contains ((const gchar * const *) blocklist,
					       gs_plugin_get_name (plugin));
			if (ret)
				gs_plugin_set_enabled (plugin, FALSE);
		}
	}

	/* order by deps */
	do {
		changes = FALSE;
		for (i = 0; i < plugin_loader->plugins->len; i++) {
			plugin = g_ptr_array_index (plugin_loader->plugins, i);
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
		for (i = 0; i < plugin_loader->plugins->len; i++) {
			plugin = g_ptr_array_index (plugin_loader->plugins, i);
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
	for (i = 0; i < plugin_loader->plugins->len; i++) {
		plugin = g_ptr_array_index (plugin_loader->plugins, i);
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
	g_ptr_array_sort (plugin_loader->plugins,
			  gs_plugin_loader_plugin_sort_fn);

	/* assign priority values */
	do {
		changes = FALSE;
		for (i = 0; i < plugin_loader->plugins->len; i++) {
			plugin = g_ptr_array_index (plugin_loader->plugins, i);
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
	gs_plugin_loader_call_setup (plugin_loader, cancellable);

	/* now we can load the install-queue */
	if (!load_install_queue (plugin_loader, error))
		return FALSE;

#ifdef HAVE_SYSPROF
	if (plugin_loader->sysprof_writer != NULL) {
		sysprof_capture_writer_add_mark (plugin_loader->sysprof_writer,
						 begin_time_nsec,
						 sched_getcpu (),
						 getpid (),
						 SYSPROF_CAPTURE_CURRENT_TIME - begin_time_nsec,
						 "gnome-software",
						 "setup",
						 NULL);
	}
#endif  /* HAVE_SYSPROF */

	return TRUE;
}

void
gs_plugin_loader_dump_state (GsPluginLoader *plugin_loader)
{
	g_autoptr(GString) str_enabled = g_string_new (NULL);
	g_autoptr(GString) str_disabled = g_string_new (NULL);

	/* print what the priorities are if verbose */
	for (guint i = 0; i < plugin_loader->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (plugin_loader->plugins, i);
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

	switch (prop_id) {
	case PROP_EVENTS:
		g_value_set_pointer (value, plugin_loader->events_by_id);
		break;
	case PROP_ALLOW_UPDATES:
		g_value_set_boolean (value, gs_plugin_loader_get_allow_updates (plugin_loader));
		break;
	case PROP_NETWORK_AVAILABLE:
		g_value_set_boolean (value, gs_plugin_loader_get_network_available (plugin_loader));
		break;
	case PROP_NETWORK_METERED:
		g_value_set_boolean (value, gs_plugin_loader_get_network_metered (plugin_loader));
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

	if (plugin_loader->plugins != NULL) {
		/* Shut down all the plugins first. */
		gs_plugin_loader_call_shutdown (plugin_loader, NULL);

		g_clear_pointer (&plugin_loader->plugins, g_ptr_array_unref);
	}
	if (plugin_loader->updates_changed_id != 0) {
		g_source_remove (plugin_loader->updates_changed_id);
		plugin_loader->updates_changed_id = 0;
	}
	if (plugin_loader->network_changed_handler != 0) {
		g_signal_handler_disconnect (plugin_loader->network_monitor,
					     plugin_loader->network_changed_handler);
		plugin_loader->network_changed_handler = 0;
	}
	if (plugin_loader->network_available_notify_handler != 0) {
		g_signal_handler_disconnect (plugin_loader->network_monitor,
					     plugin_loader->network_available_notify_handler);
		plugin_loader->network_available_notify_handler = 0;
	}
	if (plugin_loader->network_metered_notify_handler != 0) {
		g_signal_handler_disconnect (plugin_loader->network_monitor,
					     plugin_loader->network_metered_notify_handler);
		plugin_loader->network_metered_notify_handler = 0;
	}
	if (plugin_loader->queued_ops_pool != NULL) {
		/* stop accepting more requests and wait until any currently
		 * running ones are finished */
		g_thread_pool_free (plugin_loader->queued_ops_pool, TRUE, TRUE);
		plugin_loader->queued_ops_pool = NULL;
	}
	g_clear_object (&plugin_loader->network_monitor);
	g_clear_object (&plugin_loader->soup_session);
	g_clear_object (&plugin_loader->settings);
	g_clear_pointer (&plugin_loader->pending_apps, g_ptr_array_unref);
	g_clear_object (&plugin_loader->category_manager);
	g_clear_object (&plugin_loader->odrs_provider);

#ifdef HAVE_SYSPROF
	g_clear_pointer (&plugin_loader->sysprof_writer, sysprof_capture_writer_unref);
#endif

	G_OBJECT_CLASS (gs_plugin_loader_parent_class)->dispose (object);
}

static void
gs_plugin_loader_finalize (GObject *object)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);

	g_strfreev (plugin_loader->compatible_projects);
	g_ptr_array_unref (plugin_loader->locations);
	g_free (plugin_loader->language);
	g_ptr_array_unref (plugin_loader->file_monitors);
	g_hash_table_unref (plugin_loader->events_by_id);
	g_hash_table_unref (plugin_loader->disallow_updates);
	g_clear_object (&plugin_loader->as_pool);

	g_mutex_clear (&plugin_loader->pending_apps_mutex);
	g_mutex_clear (&plugin_loader->events_by_id_mutex);

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

	pspec = g_param_spec_boolean ("network-metered", NULL, NULL,
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_NETWORK_METERED, pspec);

	signals [SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
	signals [SIGNAL_PENDING_APPS_CHANGED] =
		g_signal_new ("pending-apps-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [SIGNAL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [SIGNAL_RELOAD] =
		g_signal_new ("reload",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [SIGNAL_BASIC_AUTH_START] =
		g_signal_new ("basic-auth-start",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_POINTER);
	signals [SIGNAL_ASK_UNTRUSTED] =
		g_signal_new ("ask-untrusted",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_BOOLEAN, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
}

static void
gs_plugin_loader_allow_updates_recheck (GsPluginLoader *plugin_loader)
{
	if (g_settings_get_boolean (plugin_loader->settings, "allow-updates")) {
		g_hash_table_remove (plugin_loader->disallow_updates, plugin_loader);
	} else {
		g_hash_table_insert (plugin_loader->disallow_updates,
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
	guint mem_total = gs_utils_get_memory_total ();
	if (mem_total == 0)
		return 8;
	/* allow 1 op per GB of memory */
	return (gint) MAX (round((gdouble) mem_total / 1024), 1.0);
}

static void
gs_plugin_loader_init (GsPluginLoader *plugin_loader)
{
	const gchar *tmp;
	gchar *match;
	gchar **projects;
	guint i;
	g_autofree gchar *review_server = NULL;
	g_autofree gchar *user_hash = NULL;
	g_autoptr(GError) local_error = NULL;
	const guint64 odrs_review_max_cache_age_secs = 237000;  /* 1 week */
	const guint odrs_review_n_results_max = 20;
	const gchar *locale;

#ifdef HAVE_SYSPROF
	plugin_loader->sysprof_writer = sysprof_capture_writer_new_from_env (0);
#endif  /* HAVE_SYSPROF */

	plugin_loader->scale = 1;
	plugin_loader->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	plugin_loader->pending_apps = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	plugin_loader->queued_ops_pool = g_thread_pool_new (gs_plugin_loader_process_in_thread_pool_cb,
						   NULL,
						   get_max_parallel_ops (),
						   FALSE,
						   NULL);
	plugin_loader->file_monitors = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	plugin_loader->locations = g_ptr_array_new_with_free_func (g_free);
	plugin_loader->settings = g_settings_new ("org.gnome.software");
	g_signal_connect (plugin_loader->settings, "changed",
			  G_CALLBACK (gs_plugin_loader_settings_changed_cb), plugin_loader);
	plugin_loader->events_by_id = g_hash_table_new_full ((GHashFunc) as_utils_data_id_hash,
							     (GEqualFunc) as_utils_data_id_equal,
							     g_free,
							     (GDestroyNotify) g_object_unref);

	/* share a soup session (also disable the double-compression) */
	plugin_loader->soup_session = soup_session_new_with_options ("user-agent", gs_user_agent (),
								     "timeout", 10,
								     NULL);

	/* get the category manager */
	plugin_loader->category_manager = gs_category_manager_new ();

	/* set up the ODRS provider */

	/* get the machine+user ID hash value */
	user_hash = gs_utils_get_user_hash (&local_error);
	if (user_hash == NULL) {
		g_warning ("Failed to get machine+user hash: %s", local_error->message);
		plugin_loader->odrs_provider = NULL;
	} else {
		review_server = g_settings_get_string (plugin_loader->settings, "review-server");

		if (review_server != NULL && *review_server != '\0') {
			const gchar *distro = NULL;
			g_autoptr(GsOsRelease) os_release = NULL;

			/* get the distro name (e.g. 'Fedora') but allow a fallback */
			os_release = gs_os_release_new (&local_error);
			if (os_release != NULL) {
				distro = gs_os_release_get_name (os_release);
				if (distro == NULL)
					g_warning ("no distro name specified");
			} else {
				g_warning ("failed to get distro name: %s", local_error->message);
			}

			/* Fallback */
			if (distro == NULL)
				distro = C_("Distribution name", "Unknown");

			plugin_loader->odrs_provider = gs_odrs_provider_new (review_server,
									     user_hash,
									     distro,
									     odrs_review_max_cache_age_secs,
									     odrs_review_n_results_max,
									     gs_plugin_loader_get_soup_session (plugin_loader));
		}
	}

	/* the settings key sets the initial override */
	plugin_loader->disallow_updates = g_hash_table_new (g_direct_hash, g_direct_equal);
	gs_plugin_loader_allow_updates_recheck (plugin_loader);

	/* get the language from the locale (i.e. strip the territory, codeset
	 * and modifier) */
	locale = setlocale (LC_MESSAGES, NULL);
	plugin_loader->language = g_strdup (locale);
	match = strpbrk (plugin_loader->language, "._@");
	if (match != NULL)
		*match = '\0';

	g_debug ("Using locale = %s, language = %s", locale, plugin_loader->language);

	g_mutex_init (&plugin_loader->pending_apps_mutex);
	g_mutex_init (&plugin_loader->events_by_id_mutex);

	/* monitor the network as the many UI operations need the network */
	gs_plugin_loader_monitor_network (plugin_loader);

	/* by default we only show project-less apps or compatible projects */
	tmp = g_getenv ("GNOME_SOFTWARE_COMPATIBLE_PROJECTS");
	if (tmp == NULL) {
		projects = g_settings_get_strv (plugin_loader->settings,
						"compatible-projects");
	} else {
		projects = g_strsplit (tmp, ",", -1);
	}
	for (i = 0; projects[i] != NULL; i++)
		g_debug ("compatible-project: %s", projects[i]);
	plugin_loader->compatible_projects = projects;
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
	if (plugin_loader->network_monitor == NULL) {
		g_debug ("no network monitor, so returning network-available=TRUE");
		return TRUE;
	}
	return g_network_monitor_get_network_available (plugin_loader->network_monitor);
}

gboolean
gs_plugin_loader_get_network_metered (GsPluginLoader *plugin_loader)
{
	if (plugin_loader->network_monitor == NULL) {
		g_debug ("no network monitor, so returning network-metered=FALSE");
		return FALSE;
	}
	return g_network_monitor_get_network_metered (plugin_loader->network_monitor);
}

static void
gs_plugin_loader_network_changed_cb (GNetworkMonitor *monitor,
				     gboolean available,
				     GsPluginLoader *plugin_loader)
{
	gboolean metered = g_network_monitor_get_network_metered (plugin_loader->network_monitor);

	g_debug ("network status change: %s [%s]",
		 available ? "online" : "offline",
		 metered ? "metered" : "unmetered");

	g_object_notify (G_OBJECT (plugin_loader), "network-available");
	g_object_notify (G_OBJECT (plugin_loader), "network-metered");

	if (available && !metered) {
		g_autoptr(GsAppList) queue = NULL;
		g_mutex_lock (&plugin_loader->pending_apps_mutex);
		queue = gs_app_list_new ();
		for (guint i = 0; i < plugin_loader->pending_apps->len; i++) {
			GsApp *app = g_ptr_array_index (plugin_loader->pending_apps, i);
			if (gs_app_get_state (app) == GS_APP_STATE_QUEUED_FOR_INSTALL) {
				gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
				gs_app_list_add (queue, app);
			}
		}
		g_mutex_unlock (&plugin_loader->pending_apps_mutex);
		for (guint i = 0; i < gs_app_list_length (queue); i++) {
			GsApp *app = gs_app_list_index (queue, i);
			GsPluginAction action = gs_app_get_kind (app) == AS_COMPONENT_KIND_REPOSITORY ? GS_PLUGIN_ACTION_INSTALL_REPO : GS_PLUGIN_ACTION_INSTALL;
			g_autoptr(GsPluginJob) plugin_job = NULL;
			plugin_job = gs_plugin_job_newv (action,
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
gs_plugin_loader_network_available_notify_cb (GObject    *obj,
					      GParamSpec *pspec,
					      gpointer    user_data)
{
	GNetworkMonitor *monitor = G_NETWORK_MONITOR (obj);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);

	gs_plugin_loader_network_changed_cb (monitor, g_network_monitor_get_network_available (monitor), plugin_loader);
}

static void
gs_plugin_loader_network_metered_notify_cb (GObject    *obj,
					    GParamSpec *pspec,
					    gpointer    user_data)
{
	GNetworkMonitor *monitor = G_NETWORK_MONITOR (obj);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);

	gs_plugin_loader_network_changed_cb (monitor, g_network_monitor_get_network_available (monitor), plugin_loader);
}

static void
gs_plugin_loader_monitor_network (GsPluginLoader *plugin_loader)
{
	GNetworkMonitor *network_monitor;

	network_monitor = g_network_monitor_get_default ();
	if (network_monitor == NULL || plugin_loader->network_changed_handler != 0)
		return;
	plugin_loader->network_monitor = g_object_ref (network_monitor);

	plugin_loader->network_changed_handler =
		g_signal_connect (plugin_loader->network_monitor, "network-changed",
				  G_CALLBACK (gs_plugin_loader_network_changed_cb), plugin_loader);
	plugin_loader->network_available_notify_handler =
		g_signal_connect (plugin_loader->network_monitor, "notify::network-available",
				  G_CALLBACK (gs_plugin_loader_network_available_notify_cb), plugin_loader);
	plugin_loader->network_metered_notify_handler =
		g_signal_connect (plugin_loader->network_monitor, "notify::network-metered",
				  G_CALLBACK (gs_plugin_loader_network_metered_notify_cb), plugin_loader);

	gs_plugin_loader_network_changed_cb (plugin_loader->network_monitor,
			    g_network_monitor_get_network_available (plugin_loader->network_monitor),
			    plugin_loader);
}

/******************************************************************************/

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
	guint cancel_handler_id = 0;
	GsAppList *list;

	/* run each plugin, per-app version */
	list = gs_plugin_job_get_list (helper->plugin_job);
	for (guint i = 0; i < plugin_loader->plugins->len; i++) {
		GsPluginActionFunc plugin_app_func = NULL;
		GsPlugin *plugin = g_ptr_array_index (plugin_loader->plugins, i);
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
			if (gs_app_get_state (app) == GS_APP_STATE_INSTALLED)
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

	if (gs_plugin_job_get_action (helper->plugin_job) == GS_PLUGIN_ACTION_UPDATE)
		gs_utils_set_online_updates_timestamp (plugin_loader->settings);

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
	g_autoptr(GsAppList) list = g_object_ref (gs_plugin_job_get_list (helper->plugin_job));
	GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginRefineFlags filter_flags;
	gboolean add_to_pending_array = FALSE;
	guint max_results;
	GsAppListSortFunc sort_func;
	g_autoptr(GMainContext) context = g_main_context_new ();
	g_autoptr(GMainContextPusher) pusher = g_main_context_pusher_new (context);
	g_autofree gchar *job_debug = NULL;
#ifdef HAVE_SYSPROF
	gint64 begin_time_nsec G_GNUC_UNUSED = SYSPROF_CAPTURE_CURRENT_TIME;
#endif

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
	if (!GS_IS_PLUGIN_JOB_REFINE (helper->plugin_job)) {
		if (!gs_plugin_loader_run_results (helper, cancellable, &error)) {
			if (add_to_pending_array) {
				gs_app_set_state_recover (gs_plugin_job_get_app (helper->plugin_job));
				gs_plugin_loader_pending_apps_remove (plugin_loader, helper);
			}
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, error);
			return;
		}

		if (action == GS_PLUGIN_ACTION_URL_TO_APP) {
			const gchar *search = gs_plugin_job_get_search (helper->plugin_job);
			if (search && g_ascii_strncasecmp (search, "file://", 7) == 0 && (
			    gs_plugin_job_get_list (helper->plugin_job) == NULL ||
			    gs_app_list_length (gs_plugin_job_get_list (helper->plugin_job)) == 0)) {
				g_autoptr(GError) local_error = NULL;
				g_autoptr(GFile) file = NULL;
				file = g_file_new_for_uri (search);
				gs_plugin_job_set_action (helper->plugin_job, GS_PLUGIN_ACTION_FILE_TO_APP);
				gs_plugin_job_set_file (helper->plugin_job, file);
				helper->function_name = gs_plugin_action_to_function_name (GS_PLUGIN_ACTION_FILE_TO_APP);
				if (gs_plugin_loader_run_results (helper, cancellable, &local_error)) {
					for (guint j = 0; j < gs_app_list_length (list); j++) {
						GsApp *app = gs_app_list_index (list, j);
						if (gs_app_get_local_file (app) == NULL)
							gs_app_set_local_file (app, gs_plugin_job_get_file (helper->plugin_job));
					}
				} else {
					g_debug ("Failed to convert file:// URI to app using file-to-app action: %s", local_error->message);
				}
				gs_plugin_job_set_action (helper->plugin_job, GS_PLUGIN_ACTION_URL_TO_APP);
				gs_plugin_job_set_file (helper->plugin_job, NULL);
			}
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

	if (action == GS_PLUGIN_ACTION_UPGRADE_TRIGGER)
		gs_utils_set_online_updates_timestamp (plugin_loader->settings);

	/* remove from pending list */
	if (add_to_pending_array)
		gs_plugin_loader_pending_apps_remove (plugin_loader, helper);

	/* some functions are really required for proper operation */
	switch (action) {
	case GS_PLUGIN_ACTION_GET_INSTALLED:
	case GS_PLUGIN_ACTION_GET_UPDATES:
	case GS_PLUGIN_ACTION_INSTALL:
	case GS_PLUGIN_ACTION_DOWNLOAD:
	case GS_PLUGIN_ACTION_LAUNCH:
	case GS_PLUGIN_ACTION_REFRESH:
	case GS_PLUGIN_ACTION_REMOVE:
	case GS_PLUGIN_ACTION_SEARCH:
	case GS_PLUGIN_ACTION_UPDATE:
	case GS_PLUGIN_ACTION_INSTALL_REPO:
	case GS_PLUGIN_ACTION_REMOVE_REPO:
	case GS_PLUGIN_ACTION_ENABLE_REPO:
	case GS_PLUGIN_ACTION_DISABLE_REPO:
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
	default:
		if (!helper->anything_ran && !GS_IS_PLUGIN_JOB_REFINE (helper->plugin_job)) {
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

	/* refine with enough data so that the sort_func in
	 * gs_plugin_loader_job_sorted_truncation() can do what it needs */
	filter_flags = gs_plugin_job_get_filter_flags (helper->plugin_job);
	max_results = gs_plugin_job_get_max_results (helper->plugin_job);
	sort_func = gs_plugin_job_get_sort_func (helper->plugin_job, NULL);
	if (filter_flags > 0 && max_results > 0 && sort_func != NULL) {
		g_autoptr(GsPluginJob) refine_job = NULL;
		g_autoptr(GAsyncResult) refine_result = NULL;
		g_autoptr(GsAppList) new_list = NULL;

		g_debug ("running filter flags with early refine");

		refine_job = gs_plugin_job_refine_new (list, filter_flags);
		gs_plugin_loader_job_process_async (plugin_loader, refine_job,
						    cancellable,
						    async_result_cb,
						    &refine_result);

		/* FIXME: Make this sync until the enclosing function is
		 * refactored to be async. */
		while (refine_result == NULL)
			g_main_context_iteration (g_main_context_get_thread_default (), TRUE);

		new_list = gs_plugin_loader_job_process_finish (plugin_loader, refine_result, &error);
		if (new_list == NULL) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, g_steal_pointer (&error));
			return;
		}

		/* Update the app list in case the refine resolved any wildcards. */
		g_set_object (&list, new_list);
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
	case GS_PLUGIN_ACTION_INSTALL_REPO:
	case GS_PLUGIN_ACTION_REMOVE_REPO:
		gs_plugin_job_add_refine_flags (helper->plugin_job,
		                                GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN |
		                                GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION);
		break;
	default:
		break;
	}

	/* run refine() on each one if required */
	if (gs_plugin_job_get_refine_flags (helper->plugin_job) != 0 &&
	    list != NULL &&
	    gs_app_list_length (list) > 0) {
		g_autoptr(GsPluginJob) refine_job = NULL;
		g_autoptr(GAsyncResult) refine_result = NULL;
		g_autoptr(GsAppList) new_list = NULL;

		refine_job = gs_plugin_job_refine_new (list, gs_plugin_job_get_refine_flags (helper->plugin_job));
		gs_plugin_loader_job_process_async (plugin_loader, refine_job,
						    cancellable,
						    async_result_cb,
						    &refine_result);

		/* FIXME: Make this sync until the enclosing function is
		 * refactored to be async. */
		while (refine_result == NULL)
			g_main_context_iteration (g_main_context_get_thread_default (), TRUE);

		new_list = gs_plugin_loader_job_process_finish (plugin_loader, refine_result, &error);
		if (new_list == NULL) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, g_steal_pointer (&error));
			return;
		}

		/* Update the app list in case the refine resolved any wildcards. */
		g_set_object (&list, new_list);
	} else {
		g_debug ("no refine flags set for transaction");
	}

	/* check the local files have an icon set */
	switch (action) {
	case GS_PLUGIN_ACTION_URL_TO_APP:
	case GS_PLUGIN_ACTION_FILE_TO_APP: {
		g_autoptr(GsPluginJob) refine_job = NULL;
		g_autoptr(GAsyncResult) refine_result = NULL;
		g_autoptr(GsAppList) new_list = NULL;

		for (guint j = 0; j < gs_app_list_length (list); j++) {
			GsApp *app = gs_app_list_index (list, j);
			if (gs_app_get_icons (app) == NULL) {
				g_autoptr(GIcon) ic = NULL;
				const gchar *icon_name;
				if (gs_app_has_quirk (app, GS_APP_QUIRK_HAS_SOURCE))
					icon_name = "x-package-repository";
				else
					icon_name = "system-component-application";
				ic = g_themed_icon_new (icon_name);
				gs_app_add_icon (app, ic);
			}
		}

		refine_job = gs_plugin_job_refine_new (list, GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON);
		gs_plugin_loader_job_process_async (plugin_loader, refine_job,
						    cancellable,
						    async_result_cb,
						    &refine_result);

		/* FIXME: Make this sync until the enclosing function is
		 * refactored to be async. */
		while (refine_result == NULL)
			g_main_context_iteration (g_main_context_get_thread_default (), TRUE);

		new_list = gs_plugin_loader_job_process_finish (plugin_loader, refine_result, &error);
		if (new_list == NULL) {
			gs_utils_error_convert_gio (&error);
			g_task_return_error (task, g_steal_pointer (&error));
			return;
		}

		/* Update the app list in case the refine resolved any wildcards. */
		g_set_object (&list, new_list);

		break;
	}
	default:
		break;
	}

	/* filter package list */
	switch (action) {
	case GS_PLUGIN_ACTION_URL_TO_APP:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_filter, helper);
		break;
	case GS_PLUGIN_ACTION_SEARCH:
	case GS_PLUGIN_ACTION_SEARCH_FILES:
	case GS_PLUGIN_ACTION_SEARCH_PROVIDES:
	case GS_PLUGIN_ACTION_GET_ALTERNATES:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_filter, helper);
		gs_app_list_filter (list, gs_plugin_loader_filter_qt_for_gtk, NULL);
		gs_app_list_filter (list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
		break;
	case GS_PLUGIN_ACTION_GET_CATEGORY_APPS:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_filter, helper);
		gs_app_list_filter (list, gs_plugin_loader_filter_qt_for_gtk, NULL);
		gs_app_list_filter (list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
		break;
	case GS_PLUGIN_ACTION_GET_INSTALLED:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_filter, helper);
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_installed, helper);
		break;
	case GS_PLUGIN_ACTION_GET_FEATURED:
		if (g_getenv ("GNOME_SOFTWARE_FEATURED") != NULL) {
			gs_app_list_filter (list, gs_plugin_loader_featured_debug, NULL);
		} else {
			gs_app_list_filter (list, gs_plugin_loader_app_is_valid_filter, helper);
			gs_app_list_filter (list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
		}
		break;
	case GS_PLUGIN_ACTION_GET_UPDATES:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_updatable, helper);
		break;
	case GS_PLUGIN_ACTION_GET_RECENT:
		gs_app_list_filter (list, gs_plugin_loader_app_is_non_compulsory, NULL);
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_filter, helper);
		gs_app_list_filter (list, gs_plugin_loader_filter_qt_for_gtk, NULL);
		gs_app_list_filter (list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
		break;
	case GS_PLUGIN_ACTION_GET_POPULAR:
		gs_app_list_filter (list, gs_plugin_loader_app_is_valid_filter, helper);
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
			g_set_error (&error_local,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no application was created for %s", str);
			if (!gs_plugin_job_get_propagate_error (helper->plugin_job))
				gs_plugin_loader_claim_job_error (plugin_loader, NULL, helper->plugin_job, error_local);
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
	dedupe_flags = gs_plugin_job_get_dedupe_flags (helper->plugin_job);
	if (dedupe_flags != GS_APP_LIST_FILTER_FLAG_NONE)
		gs_app_list_filter_duplicates (list, dedupe_flags);

	/* sort these again as the refine may have added useful metadata */
	gs_plugin_loader_job_sorted_truncation_again (helper);

	/* if the plugin used updates-changed actually schedule it now */
	if (plugin_loader->updates_changed_cnt > 0)
		gs_plugin_loader_updates_changed (plugin_loader);

#ifdef HAVE_SYSPROF
	if (plugin_loader->sysprof_writer != NULL) {
		g_autofree gchar *sysprof_name = g_strconcat ("process-thread:", gs_plugin_action_to_string (action), NULL);
		g_autofree gchar *sysprof_message = gs_plugin_job_to_string (helper->plugin_job);
		sysprof_capture_writer_add_mark (plugin_loader->sysprof_writer,
						 begin_time_nsec,
						 sched_getcpu (),
						 getpid (),
						 SYSPROF_CAPTURE_CURRENT_TIME - begin_time_nsec,
						 "gnome-software",
						 sysprof_name,
						 sysprof_message);
	}
#endif  /* HAVE_SYSPROF */

	/* show elapsed time */
	job_debug = gs_plugin_job_to_string (helper->plugin_job);
	g_debug ("%s", job_debug);

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
	GsPluginLoaderHelper *helper = g_task_get_task_data (task);
	GsApp *app = gs_plugin_job_get_app (helper->plugin_job);
	GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);

	gs_ioprio_init ();

	gs_plugin_loader_process_thread_cb (task, source_object, task_data, cancellable);

	/* Clear any pending action set in gs_plugin_loader_schedule_task() */
	if (app != NULL && gs_app_get_pending_action (app) == action)
		gs_app_set_pending_action (app, GS_PLUGIN_ACTION_UNKNOWN);

	g_object_unref (task);
}

/* This needs to err on the side of being fast, rather than perfectly accurate.
 * False positives (saying the program is running under gdb when it isn’t) are
 * not OK. False negatives (saying the program is not running under gdb when it
 * is) are OK. */
static gboolean
is_running_under_gdb (void)
{
	g_autofree gchar *status = NULL;
	gsize status_len = 0;
	const gchar *tracer_pid, *end;

	/* Look for a line of the form:
	 * ```
	 * TracerPid:	748899
	 * ```
	 * or
	 * ```
	 * TracerPid:	0
	 * ```
	 * in `/proc/self/status`. If it’s 0, the process is not being traced. */
	if (!g_file_get_contents ("/proc/self/status", &status, &status_len, NULL))
		return FALSE;

	tracer_pid = g_strstr_len (status, status_len, "TracerPid:");
	if (tracer_pid == NULL)
		return FALSE;

	end = status + status_len;

	/* Find the number. */
	for (tracer_pid += strlen ("TracerPid:");
	     tracer_pid < end &&
	     g_ascii_isspace (*tracer_pid);
	     tracer_pid++);

	if (tracer_pid >= end)
		return FALSE;

	return (*tracer_pid != '0');
}

static gboolean
gs_plugin_loader_job_timeout_cb (gpointer user_data)
{
	GsPluginLoaderHelper *helper = (GsPluginLoaderHelper *) user_data;

	/* Don’t impose timeouts if running under gdb. */
	if (is_running_under_gdb ()) {
		g_debug ("Not cancelling job %s even though it took longer "
			 "than %u seconds, as running under gdb",
			 helper->function_name,
			 gs_plugin_job_get_timeout (helper->plugin_job));
		return G_SOURCE_REMOVE;
	}

	/* call the cancellable */
	g_debug ("cancelling job %s as it took longer than %u seconds",
		 helper->function_name,
		 gs_plugin_job_get_timeout (helper->plugin_job));
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
	g_debug ("Cancelling job with cancellable %p", helper->cancellable);
	g_cancellable_cancel (helper->cancellable);
}

static void
gs_plugin_loader_schedule_task (GsPluginLoader *plugin_loader,
				GTask *task)
{
	GsPluginLoaderHelper *helper = g_task_get_task_data (task);
	GsApp *app = gs_plugin_job_get_app (helper->plugin_job);

	if (app != NULL) {
		/* set the pending-action to the app */
		GsPluginAction action = gs_plugin_job_get_action (helper->plugin_job);
		gs_app_set_pending_action (app, action);
	}
	g_thread_pool_push (plugin_loader->queued_ops_pool, g_object_ref (task), NULL);
}

static void
run_job_cb (GObject      *source_object,
            GAsyncResult *result,
            gpointer      user_data)
{
	GsPluginJob *plugin_job = GS_PLUGIN_JOB (source_object);
	GsPluginJobClass *job_class;
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginLoader *plugin_loader = g_task_get_source_object (task);
#ifdef HAVE_SYSPROF
	gint64 begin_time_nsec = GPOINTER_TO_SIZE (g_task_get_task_data (task));
#endif  /* HAVE_SYSPROF */
	g_autoptr(GError) local_error = NULL;

#ifdef HAVE_SYSPROF
	if (plugin_loader->sysprof_writer != NULL) {
		g_autofree gchar *sysprof_name = g_strconcat ("process-thread:", G_OBJECT_TYPE_NAME (plugin_job), NULL);
		g_autofree gchar *sysprof_message = gs_plugin_job_to_string (plugin_job);
		sysprof_capture_writer_add_mark (plugin_loader->sysprof_writer,
						 begin_time_nsec,
						 sched_getcpu (),
						 getpid (),
						 SYSPROF_CAPTURE_CURRENT_TIME - begin_time_nsec,
						 "gnome-software",
						 sysprof_name,
						 sysprof_message);
	}
#endif  /* HAVE_SYSPROF */

	/* if the plugin used updates-changed actually schedule it now */
	if (plugin_loader->updates_changed_cnt > 0)
		gs_plugin_loader_updates_changed (plugin_loader);

	/* FIXME: This will eventually go away when
	 * gs_plugin_loader_job_process_finish() is removed. */
	job_class = GS_PLUGIN_JOB_GET_CLASS (plugin_job);

	g_assert (job_class->run_finish != NULL);

	if (!job_class->run_finish (plugin_job, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	if (GS_IS_PLUGIN_JOB_REFINE (plugin_job)) {
		GsAppList *list = gs_plugin_job_refine_get_result_list (GS_PLUGIN_JOB_REFINE (plugin_job));
		g_task_return_pointer (task, g_object_ref (list), (GDestroyNotify) g_object_unref);
		return;
	}

	g_assert_not_reached ();
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
	GsPluginJobClass *job_class;
	GsPluginAction action;
	GsPluginLoaderHelper *helper;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GCancellable) cancellable_job = g_cancellable_new ();
	g_autofree gchar *task_name = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_PLUGIN_JOB (plugin_job));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* If the job provides a more specific async run function, use that.
	 *
	 * FIXME: This will eventually go away when
	 * gs_plugin_loader_job_process_async() is removed. */
	job_class = GS_PLUGIN_JOB_GET_CLASS (plugin_job);

	if (job_class->run_async != NULL) {
#ifdef HAVE_SYSPROF
		gint64 begin_time_nsec G_GNUC_UNUSED = SYSPROF_CAPTURE_CURRENT_TIME;
#endif

		task = g_task_new (plugin_loader, cancellable, callback, user_data);
		task_name = g_strdup_printf ("%s %s", G_STRFUNC, G_OBJECT_TYPE_NAME (plugin_job));
		g_task_set_name (task, task_name);
#ifdef HAVE_SYSPROF
		g_task_set_task_data (task, GSIZE_TO_POINTER (begin_time_nsec), NULL);
#endif

		job_class->run_async (plugin_job, plugin_loader, cancellable,
				      run_job_cb, g_steal_pointer (&task));
		return;
	}

	action = gs_plugin_job_get_action (plugin_job);
	task_name = g_strdup_printf ("%s %s", G_STRFUNC, gs_plugin_action_to_string (action));

	/* check job has valid action */
	if (action == GS_PLUGIN_ACTION_UNKNOWN) {
		g_autofree gchar *job_str = gs_plugin_job_to_string (plugin_job);
		task = g_task_new (plugin_loader, cancellable_job, callback, user_data);
		g_task_set_name (task, task_name);
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "job has no valid action: %s", job_str);
		return;
	}

	/* deal with the install queue */
	if (action == GS_PLUGIN_ACTION_REMOVE || action == GS_PLUGIN_ACTION_REMOVE_REPO) {
		if (remove_app_from_install_queue (plugin_loader, gs_plugin_job_get_app (plugin_job))) {
			GsAppList *list = gs_plugin_job_get_list (plugin_job);
			task = g_task_new (plugin_loader, cancellable, callback, user_data);
			g_task_set_name (task, task_name);
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
			apps = g_settings_get_strv (plugin_loader->settings, "popular-overrides");
		}
		if (apps != NULL && g_strv_length (apps) > 0) {
			GsAppList *list = gs_plugin_job_get_list (plugin_job);
			g_autoptr(GsPluginJob) refine_job = NULL;

			for (guint i = 0; apps[i] != NULL; i++) {
				g_autoptr(GsApp) app = gs_app_new (apps[i]);
				gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
				gs_app_list_add (list, app);
			}

			/* Refine the list of wildcard popular apps and return
			 * to the caller. */
			refine_job = gs_plugin_job_refine_new (list, GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID);
			gs_plugin_loader_job_process_async (plugin_loader, refine_job,
							    cancellable,
							    callback, user_data);
			return;
		}
	}

	/* FIXME: the plugins should specify this, rather than hardcoding */
	if (gs_plugin_job_has_refine_flags (plugin_job,
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_UI)) {
		gs_plugin_job_add_refine_flags (plugin_job,
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN);
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
	g_task_set_name (task, task_name);

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
	default:
		break;
	}

	/* sorting fallbacks */
	switch (action) {
	case GS_PLUGIN_ACTION_SEARCH:
		if (gs_plugin_job_get_sort_func (plugin_job, NULL) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_match_value_cb, NULL);
		}
		break;
	case GS_PLUGIN_ACTION_GET_RECENT:
		if (gs_plugin_job_get_sort_func (plugin_job, NULL) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_kind_cb, NULL);
		}
		break;
	case GS_PLUGIN_ACTION_GET_CATEGORY_APPS:
		if (gs_plugin_job_get_sort_func (plugin_job, NULL) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_name_cb, NULL);
		}
		break;
	case GS_PLUGIN_ACTION_GET_ALTERNATES:
		if (gs_plugin_job_get_sort_func (plugin_job, NULL) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_prio_cb, NULL);
		}
		break;
	case GS_PLUGIN_ACTION_GET_DISTRO_UPDATES:
		if (gs_plugin_job_get_sort_func (plugin_job, NULL) == NULL) {
			gs_plugin_job_set_sort_func (plugin_job,
						     gs_plugin_loader_app_sort_version_cb, NULL);
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

	/* AppStream metadata pool, we only need it to create good search tokens */
	if (plugin_loader->as_pool == NULL)
		plugin_loader->as_pool = as_pool_new ();

	/* pre-tokenize search */
	if (action == GS_PLUGIN_ACTION_SEARCH) {
		const gchar *search = gs_plugin_job_get_search (plugin_job);
		helper->tokens = as_pool_build_search_tokens (plugin_loader->as_pool, search);
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
	g_debug ("Chaining cancellation from %p to %p", cancellable, cancellable_job);
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
		if (gs_plugin_job_get_timeout (plugin_job) > 0) {
			helper->timeout_id =
				g_timeout_add_seconds (gs_plugin_job_get_timeout (plugin_job),
						       gs_plugin_loader_job_timeout_cb,
						       helper);
		}
		break;
	default:
		break;
	}

	switch (action) {
	case GS_PLUGIN_ACTION_INSTALL:
	case GS_PLUGIN_ACTION_INSTALL_REPO:
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
	for (guint i = 0; i < plugin_loader->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (plugin_loader->plugins, i);
		if (gs_plugin_get_symbol (plugin, function_name) != NULL)
			return TRUE;
	}
	return FALSE;
}

/**
 * gs_plugin_loader_get_plugins:
 * @plugin_loader: a #GsPluginLoader
 *
 * Get the set of currently loaded plugins.
 *
 * This includes disabled plugins, which should be checked for using
 * gs_plugin_get_enabled().
 *
 * This is intended to be used by internal gnome-software code. Plugin and UI
 * code should typically use #GsPluginJob to run operations.
 *
 * Returns: (transfer none) (element-type GsPlugin): list of #GsPlugins
 * Since: 42
 */
GPtrArray *
gs_plugin_loader_get_plugins (GsPluginLoader *plugin_loader)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);

	return plugin_loader->plugins;
}

static void app_create_cb (GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data);

/**
 * gs_plugin_loader_app_create_async:
 * @plugin_loader: a #GsPluginLoader
 * @unique_id: a unique_id
 * @cancellable: a #GCancellable, or %NULL
 * @callback: function to call when complete
 * @user_data: user data to pass to @callback
 *
 * Create a #GsApp identified by @unique_id asynchronously.
 * Finish the call with gs_plugin_loader_app_create_finish().
 *
 * Since: 41
 **/
void
gs_plugin_loader_app_create_async (GsPluginLoader *plugin_loader,
				   const gchar *unique_id,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer user_data)
{
	g_autoptr(GTask) task = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GsPluginJob) refine_job = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (unique_id != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_loader_app_create_async);
	g_task_set_task_data (task, g_strdup (unique_id), g_free);

	/* use the plugin loader to convert a wildcard app */
	app = gs_app_new (NULL);
	gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
	gs_app_set_from_unique_id (app, unique_id, AS_COMPONENT_KIND_UNKNOWN);
	gs_app_list_add (list, app);

	/* Refine the wildcard app. */
	refine_job = gs_plugin_job_refine_new (list, GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID);
	gs_plugin_loader_job_process_async (plugin_loader, refine_job,
					    cancellable,
					    app_create_cb,
					    g_steal_pointer (&task));
}

static void
app_create_cb (GObject      *source_object,
               GAsyncResult *result,
               gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (g_task_get_source_object (task));
	const gchar *unique_id = g_task_get_task_data (task);
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) local_error = NULL;

	list = gs_plugin_loader_job_process_finish (plugin_loader, result, &local_error);
	if (list == NULL) {
		g_prefix_error (&local_error, "Failed to refine '%s': ", unique_id);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* return the matching GsApp */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app_tmp = gs_app_list_index (list, i);
		if (g_strcmp0 (unique_id, gs_app_get_unique_id (app_tmp)) == 0) {
			g_task_return_pointer (task, g_object_ref (app_tmp), g_object_unref);
			return;
		}
	}

	/* return the first returned app that's not a wildcard */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app_tmp = gs_app_list_index (list, i);
		if (!gs_app_has_quirk (app_tmp, GS_APP_QUIRK_IS_WILDCARD)) {
			g_debug ("returning imperfect match: %s != %s",
				 unique_id, gs_app_get_unique_id (app_tmp));
			g_task_return_pointer (task, g_object_ref (app_tmp), g_object_unref);
			return;
		}
	}

	/* does not exist */
	g_task_return_new_error (task,
				 GS_PLUGIN_ERROR,
				 GS_PLUGIN_ERROR_FAILED,
				 "Failed to create an app for '%s'", unique_id);
}

/**
 * gs_plugin_loader_app_create_finish:
 * @plugin_loader: a #GsPluginLoader
 * @res: a #GAsyncResult
 * @error: A #GError, or %NULL
 *
 * Finishes call to gs_plugin_loader_app_create_async().
 *
 * Returns: (transfer full): a #GsApp, or %NULL on error.
 *
 * Since: 41
 **/
GsApp *
gs_plugin_loader_app_create_finish (GsPluginLoader *plugin_loader,
				    GAsyncResult *res,
				    GError **error)
{
	GsApp *app;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	app = g_task_propagate_pointer (G_TASK (res), error);
	gs_utils_error_convert_gio (error);
	return app;
}

/**
 * gs_plugin_loader_get_system_app_async:
 * @plugin_loader: a #GsPluginLoader
 * @cancellable: a #GCancellable, or %NULL
 * @callback: function to call when complete
 * @user_data: user data to pass to @callback
 *
 * Get the application that represents the currently installed OS
 * asynchronously. Finish the call with gs_plugin_loader_get_system_app_finish().
 *
 * Since: 41
 **/
void
gs_plugin_loader_get_system_app_async (GsPluginLoader *plugin_loader,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer user_data)
{
	gs_plugin_loader_app_create_async (plugin_loader, "*/*/*/system/*", cancellable, callback, user_data);
}

/**
 * gs_plugin_loader_get_system_app_finish:
 * @plugin_loader: a #GsPluginLoader
 * @res: a #GAsyncResult
 * @error: A #GError, or %NULL
 *
 * Finishes call to gs_plugin_loader_get_system_app_async().
 *
 * Returns: (transfer full): a #GsApp, which represents
 *    the currently installed OS, or %NULL on error.
 *
 * Since: 41
 **/
GsApp *
gs_plugin_loader_get_system_app_finish (GsPluginLoader *plugin_loader,
					GAsyncResult *res,
					GError **error)
{
	return gs_plugin_loader_app_create_finish (plugin_loader, res, error);
}

/**
 * gs_plugin_loader_get_soup_session:
 * @plugin_loader: a #GsPluginLoader
 *
 * Get the internal #SoupSession which is used to download things.
 *
 * Returns: (transfer none) (not nullable): a #SoupSession
 * Since: 41
 */
SoupSession *
gs_plugin_loader_get_soup_session (GsPluginLoader *plugin_loader)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);

	return plugin_loader->soup_session;
}

/**
 * gs_plugin_loader_get_odrs_provider:
 * @plugin_loader: a #GsPluginLoader
 *
 * Get the singleton #GsOdrsProvider which provides access to ratings and
 * reviews data from ODRS.
 *
 * Returns: (transfer none) (nullable): a #GsOdrsProvider, or %NULL if disabled
 * Since: 41
 */
GsOdrsProvider *
gs_plugin_loader_get_odrs_provider (GsPluginLoader *plugin_loader)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);

	return plugin_loader->odrs_provider;
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
	if (max_ops == 0)
		max_ops = get_max_parallel_ops ();
	if (!g_thread_pool_set_max_threads (plugin_loader->queued_ops_pool, max_ops, &error))
		g_warning ("Failed to set the maximum number of ops in parallel: %s",
			   error->message);
}

/**
 * gs_plugin_loader_get_category_manager:
 * @plugin_loader: a #GsPluginLoader
 *
 * Get the category manager singleton.
 *
 * Returns: (transfer none): a category manager
 * Since: 40
 */
GsCategoryManager *
gs_plugin_loader_get_category_manager (GsPluginLoader *plugin_loader)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);

	return plugin_loader->category_manager;
}
