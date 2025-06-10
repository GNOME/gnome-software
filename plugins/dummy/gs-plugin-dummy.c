/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2011-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>

#include <gnome-software.h>

#include "gs-plugin-dummy.h"

/*
 * SECTION:
 * Provides some dummy data that is useful in test programs.
 *
 * This plugin runs entirely in the main thread and requires no locking.
 */

struct _GsPluginDummy {
	GsPlugin		 parent;

	guint			 quirk_id;
	guint			 allow_updates_id;
	gboolean		 allow_updates_inhibit;
	GsApp			*cached_origin;
	GHashTable		*installed_apps;	/* id:1 */
	GHashTable		*available_apps;	/* id:1 */
};

G_DEFINE_TYPE (GsPluginDummy, gs_plugin_dummy, GS_TYPE_PLUGIN)

static gboolean refine_app (GsPluginDummy               *self,
                            GsApp                       *app,
                            GsPluginRefineRequireFlags   require_flags,
                            GCancellable                *cancellable,
                            GError                     **error);

/* just flip-flop this every few seconds */
static gboolean
gs_plugin_dummy_allow_updates_cb (gpointer user_data)
{
	GsPluginDummy *self = GS_PLUGIN_DUMMY (user_data);

	gs_plugin_set_allow_updates (GS_PLUGIN (self), self->allow_updates_inhibit);
	self->allow_updates_inhibit = !self->allow_updates_inhibit;
	return G_SOURCE_CONTINUE;
}

static void
gs_plugin_dummy_init (GsPluginDummy *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	if (g_getenv ("GS_SELF_TEST_DUMMY_ENABLE") == NULL) {
		g_debug ("disabling itself as not in self test");
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	/* need help from appstream */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "os-release");
}

static void
gs_plugin_dummy_dispose (GObject *object)
{
	GsPluginDummy *self = GS_PLUGIN_DUMMY (object);

	g_clear_pointer (&self->installed_apps, g_hash_table_unref);
	g_clear_pointer (&self->available_apps, g_hash_table_unref);
	g_clear_handle_id (&self->quirk_id, g_source_remove);
	g_clear_object (&self->cached_origin);

	G_OBJECT_CLASS (gs_plugin_dummy_parent_class)->dispose (object);
}

static void
gs_plugin_dummy_setup_async (GsPlugin            *plugin,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
	GsPluginDummy *self = GS_PLUGIN_DUMMY (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_setup_async);

	/* toggle this */
	if (g_getenv ("GS_SELF_TEST_TOGGLE_ALLOW_UPDATES") != NULL) {
		self->allow_updates_id = g_timeout_add_seconds (10,
			gs_plugin_dummy_allow_updates_cb, plugin);
	}

	/* add source */
	self->cached_origin = gs_app_new (gs_plugin_get_name (plugin));
	gs_app_set_kind (self->cached_origin, AS_COMPONENT_KIND_REPOSITORY);
	gs_app_set_origin_hostname (self->cached_origin, "http://www.bbc.co.uk/");
	gs_app_set_management_plugin (self->cached_origin, plugin);

	/* add the source to the plugin cache which allows us to match the
	 * unique ID to a GsApp when creating an event */
	gs_plugin_cache_add (plugin, NULL, self->cached_origin);

	/* keep track of what apps are installed */
	self->installed_apps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->available_apps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_hash_table_insert (self->available_apps,
			     g_strdup ("chiron.desktop"),
			     GUINT_TO_POINTER (1));
	g_hash_table_insert (self->available_apps,
			     g_strdup ("zeus.desktop"),
			     GUINT_TO_POINTER (1));
	g_hash_table_insert (self->available_apps,
			     g_strdup ("zeus-spell.addon"),
			     GUINT_TO_POINTER (1));
	g_hash_table_insert (self->available_apps,
			     g_strdup ("com.hughski.ColorHug2.driver"),
			     GUINT_TO_POINTER (1));

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_dummy_setup_finish (GsPlugin      *plugin,
                              GAsyncResult  *result,
                              GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_dummy_adopt_app (GsPlugin *plugin,
			   GsApp *app)
{
	if (gs_app_get_id (app) != NULL &&
	    g_str_has_prefix (gs_app_get_id (app), "dummy:")) {
		gs_app_set_management_plugin (app, plugin);
		return;
	}
	if (g_strcmp0 (gs_app_get_id (app), "mate-spell.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "zeus.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "com.hughski.ColorHug2.driver") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "zeus-spell.addon") == 0 ||
	    g_strcmp0 (gs_app_get_default_source (app), "chiron") == 0)
		gs_app_set_management_plugin (app, plugin);
}

typedef struct {
	GsApp *app;  /* (owned) (nullable) */
	guint percent_complete;
} DelayData;

static void
delay_data_free (DelayData *data)
{
	g_clear_object (&data->app);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DelayData, delay_data_free)

static gboolean delay_timeout_cb (gpointer user_data);

/* Simulate a download on app, updating its progress one percentage point at a
 * time, with an overall interval of @timeout_ms to go from 0% to 100%. The
 * download is cancelled within @timeout_ms / 100 if @cancellable is cancelled. */
static void
gs_plugin_dummy_delay_async (GsPlugin            *plugin,
                             GsApp               *app,
                             guint                timeout_ms,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
	g_autoptr(GTask) task = NULL;
	g_autoptr(DelayData) data = NULL;
	g_autoptr(GSource) source = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_delay_async);

	data = g_new0 (DelayData, 1);
	data->app = (app != NULL) ? g_object_ref (app) : NULL;
	data->percent_complete = 0;
	g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) delay_data_free);

	source = g_timeout_source_new (timeout_ms / 100);
	g_task_attach_source (task, source, delay_timeout_cb);
}

static gboolean
delay_timeout_cb (gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	GCancellable *cancellable = g_task_get_cancellable (task);
	DelayData *data = g_task_get_task_data (task);
	g_autoptr(GError) local_error = NULL;

	/* Iterate until 100%. */
	if (data->percent_complete >= 100) {
		g_task_return_boolean (task, TRUE);
		return G_SOURCE_REMOVE;
	}

	/* Has the task been cancelled? */
	if (g_cancellable_set_error_if_cancelled (cancellable, &local_error)) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return G_SOURCE_REMOVE;
	}

	/* Update the app’s progress and continue. */
	if (data->app != NULL)
		gs_app_set_progress (data->app, data->percent_complete);

	data->percent_complete++;

	return G_SOURCE_CONTINUE;
}

static gboolean
gs_plugin_dummy_delay_finish (GsPlugin      *plugin,
                              GAsyncResult  *result,
                              GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
gs_plugin_dummy_poll_cb (gpointer user_data)
{
	g_autoptr(GsApp) app = NULL;
	GsPlugin *plugin = GS_PLUGIN (user_data);

	/* find the app in the per-plugin cache -- this assumes that we can
	 * calculate the same key as used when calling gs_plugin_cache_add() */
	app = gs_plugin_cache_lookup (plugin, "chiron");
	if (app == NULL) {
		g_warning ("app not found in cache!");
		return FALSE;
	}

	/* toggle this to animate the hide/show the 3rd party banner */
	if (!gs_app_has_quirk (app, GS_APP_QUIRK_PROVENANCE)) {
		g_debug ("about to make app distro-provided");
		gs_app_add_quirk (app, GS_APP_QUIRK_PROVENANCE);
	} else {
		g_debug ("about to make app 3rd party");
		gs_app_remove_quirk (app, GS_APP_QUIRK_PROVENANCE);
	}

	/* continue polling */
	return TRUE;
}


static void
gs_plugin_dummy_url_to_app_async (GsPlugin              *plugin,
                                  const gchar           *url,
                                  GsPluginUrlToAppFlags  flags,
                                  GsPluginEventCallback  event_callback,
                                  void                  *event_user_data,
                                  GCancellable          *cancellable,
                                  GAsyncReadyCallback    callback,
                                  gpointer               user_data)
{
	g_autoptr(GTask) task = NULL;
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GsApp) app = NULL;
	g_autofree gchar *scheme = NULL;

	task = gs_plugin_url_to_app_data_new_task (plugin, url, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_url_to_app_async);

	/* it's us */
	scheme = gs_utils_get_url_scheme (url);
	if (g_strcmp0 (scheme, "dummy") == 0) {
		g_autofree gchar *path = NULL;
		/* create app */
		path = gs_utils_get_url_path (url);
		app = gs_app_new (path);
		gs_app_set_management_plugin (app, plugin);
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_list_add (list, app);
	}

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_dummy_url_to_app_finish (GsPlugin      *plugin,
                                   GAsyncResult  *result,
                                   GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static gboolean timeout_cb (gpointer user_data);

/* Simulate a cancellable delay */
static void
gs_plugin_dummy_timeout_async (GsPluginDummy       *self,
                               guint                timeout_ms,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
	g_autoptr(GTask) task = NULL;
	g_autoptr(GSource) source = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_timeout_async);

	source = g_timeout_source_new (timeout_ms);

	if (cancellable != NULL) {
		g_autoptr(GSource) cancellable_source = NULL;

		cancellable_source = g_cancellable_source_new (cancellable);
		g_source_set_dummy_callback (cancellable_source);
		g_source_add_child_source (source, cancellable_source);
	}

	g_task_attach_source (task, source, timeout_cb);
}

static gboolean
timeout_cb (gpointer user_data)
{
	GTask *task = G_TASK (user_data);

	if (!g_task_return_error_if_cancelled (task))
		g_task_return_boolean (task, TRUE);

	return G_SOURCE_REMOVE;
}

static gboolean
gs_plugin_dummy_timeout_finish (GsPluginDummy  *self,
                                GAsyncResult   *result,
                                GError        **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

typedef struct {
	/* Input data. */
	guint n_apps;
	GsPluginProgressCallback progress_callback;
	gpointer progress_user_data;

	/* In-progress data. */
	guint n_pending_ops;
	GError *saved_error;  /* (owned) (nullable) */

	/* For progress reporting. */
	guint n_uninstalls_started;
} UninstallAppsData;

static void
uninstall_apps_data_free (UninstallAppsData *data)
{
	/* Error should have been propagated by now, and all pending ops completed. */
	g_assert (data->saved_error == NULL);
	g_assert (data->n_pending_ops == 0);

	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (UninstallAppsData, uninstall_apps_data_free)

typedef struct {
	GTask *task;  /* (owned) */
	GsApp *app;  /* (owned) */
} UninstallSingleAppData;

static void
uninstall_single_app_data_free (UninstallSingleAppData *data)
{
	g_clear_object (&data->app);
	g_clear_object (&data->task);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (UninstallSingleAppData, uninstall_single_app_data_free)

static void uninstall_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data);
static void finish_uninstall_apps_op (GTask  *task,
                                      GError *error);

static void
gs_plugin_dummy_uninstall_apps_async (GsPlugin                           *plugin,
                                      GsAppList                          *apps,
                                      GsPluginUninstallAppsFlags          flags,
                                      GsPluginProgressCallback            progress_callback,
                                      gpointer                            progress_user_data,
                                      GsPluginEventCallback               event_callback,
                                      void                               *event_user_data,
                                      GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                      gpointer                            app_needs_user_action_data,
                                      GCancellable                       *cancellable,
                                      GAsyncReadyCallback                 callback,
                                      gpointer                            user_data)
{
	GsPluginDummy *self = GS_PLUGIN_DUMMY (plugin);
	g_autoptr(GTask) task = NULL;
	UninstallAppsData *data;
	g_autoptr(UninstallAppsData) data_owned = NULL;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_uninstall_apps_async);

	data = data_owned = g_new0 (UninstallAppsData, 1);
	data->progress_callback = progress_callback;
	data->progress_user_data = progress_user_data;
	data->n_apps = gs_app_list_length (apps);
	g_task_set_task_data (task, g_steal_pointer (&data_owned), (GDestroyNotify) uninstall_apps_data_free);

	/* Start a load of operations in parallel to uninstall the apps.
	 *
	 * When all uninstalls are finished for all apps, finish_uninstall_apps_op()
	 * will return success/error for the overall #GTask. */
	data->n_pending_ops = 1;
	data->n_uninstalls_started = 0;

	for (guint i = 0; i < data->n_apps; i++) {
		GsApp *app = gs_app_list_index (apps, i);
		g_autoptr(UninstallSingleAppData) app_data = NULL;

		/* only process this app if was created by this plugin */
		if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
			continue;

		if (!g_str_equal (gs_app_get_id (app), "chiron.desktop"))
			continue;

		app_data = g_new0 (UninstallSingleAppData, 1);
		app_data->task = g_object_ref (task);
		app_data->app = g_object_ref (app);

		gs_app_set_state (app, GS_APP_STATE_REMOVING);

		data->n_pending_ops++;
		data->n_uninstalls_started++;
		gs_plugin_dummy_delay_async (GS_PLUGIN (self),
					     app,
					     500  /* ms */,
					     cancellable,
					     uninstall_cb,
					     g_steal_pointer (&app_data));
	}

	finish_uninstall_apps_op (task, NULL);
}

static void
uninstall_cb (GObject      *source_object,
              GAsyncResult *result,
              gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	g_autoptr(UninstallSingleAppData) app_data = g_steal_pointer (&user_data);
	GTask *task = app_data->task;
	GsPluginDummy *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	UninstallAppsData * data = g_task_get_task_data (task);
	g_autoptr(GError) local_error = NULL;

	if (data->progress_callback != NULL) {
		data->progress_callback (plugin,
					 (data->n_uninstalls_started - data->n_pending_ops + 1) * 100 / data->n_uninstalls_started,
					 data->progress_user_data);
	}

	if (!gs_plugin_dummy_delay_finish (plugin, result, &local_error)) {
		gs_app_set_state_recover (app_data->app);
		finish_uninstall_apps_op (task, g_steal_pointer (&local_error));
		return;
	}

	/* keep track */
	g_hash_table_remove (self->installed_apps, gs_app_get_id (app_data->app));
	g_hash_table_insert (self->available_apps,
			     g_strdup (gs_app_get_id (app_data->app)),
			     GUINT_TO_POINTER (1));

	/* Refine the app so it has the right post-uninstall state. */
	gs_app_set_state (app_data->app, GS_APP_STATE_UNKNOWN);

	if (!refine_app (self, app_data->app,
			 GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN |
			 GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION,
			 cancellable, &local_error)) {
		g_debug ("Error refining app ‘%s’ after uninstall: %s",
			 gs_app_get_id (app_data->app), local_error->message);
		g_clear_error (&local_error);
	}

	finish_uninstall_apps_op (task, NULL);
}

/* @error is (transfer full) if non-%NULL */
static void
finish_uninstall_apps_op (GTask  *task,
                          GError *error)
{
	UninstallAppsData *data = g_task_get_task_data (task);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);

	if (error_owned != NULL && data->saved_error == NULL)
		data->saved_error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while uninstalling apps: %s", error_owned->message);

	g_assert (data->n_pending_ops > 0);
	data->n_pending_ops--;

	if (data->n_pending_ops > 0)
		return;

	/* Get the results of the parallel ops. */
	if (data->saved_error != NULL)
		g_task_return_error (task, g_steal_pointer (&data->saved_error));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_dummy_uninstall_apps_finish (GsPlugin      *plugin,
                                       GAsyncResult  *result,
                                       GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

typedef struct {
	/* Input data. */
	guint n_apps;
	GsPluginProgressCallback progress_callback;
	gpointer progress_user_data;

	/* In-progress data. */
	guint n_pending_ops;
	GError *saved_error;  /* (owned) (nullable) */

	/* For progress reporting. */
	guint n_installs_started;
} InstallAppsData;

static void
install_apps_data_free (InstallAppsData *data)
{
	/* Error should have been propagated by now, and all pending ops completed. */
	g_assert (data->saved_error == NULL);
	g_assert (data->n_pending_ops == 0);

	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (InstallAppsData, install_apps_data_free)

typedef struct {
	GTask *task;  /* (owned) */
	GsApp *app;  /* (owned) */
} InstallSingleAppData;

static void
install_single_app_data_free (InstallSingleAppData *data)
{
	g_clear_object (&data->app);
	g_clear_object (&data->task);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (InstallSingleAppData, install_single_app_data_free)

static void install_cb (GObject      *source_object,
                        GAsyncResult *result,
                        gpointer      user_data);
static void finish_install_apps_op (GTask  *task,
                                    GError *error);

static void
gs_plugin_dummy_install_apps_async (GsPlugin                           *plugin,
                                    GsAppList                          *apps,
                                    GsPluginInstallAppsFlags            flags,
                                    GsPluginProgressCallback            progress_callback,
                                    gpointer                            progress_user_data,
                                    GsPluginEventCallback               event_callback,
                                    void                               *event_user_data,
                                    GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                    gpointer                            app_needs_user_action_data,
                                    GCancellable                       *cancellable,
                                    GAsyncReadyCallback                 callback,
                                    gpointer                            user_data)
{
	GsPluginDummy *self = GS_PLUGIN_DUMMY (plugin);
	g_autoptr(GTask) task = NULL;
	InstallAppsData *data;
	g_autoptr(InstallAppsData) data_owned = NULL;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_install_apps_async);

	data = data_owned = g_new0 (InstallAppsData, 1);
	data->progress_callback = progress_callback;
	data->progress_user_data = progress_user_data;
	data->n_apps = gs_app_list_length (apps);
	g_task_set_task_data (task, g_steal_pointer (&data_owned), (GDestroyNotify) install_apps_data_free);

	/* Start a load of operations in parallel to install the apps.
	 *
	 * When all installs are finished for all apps, finish_install_apps_op()
	 * will return success/error for the overall #GTask. */
	data->n_pending_ops = 1;
	data->n_installs_started = 0;

	for (guint i = 0; i < data->n_apps; i++) {
		GsApp *app = gs_app_list_index (apps, i);
		g_autoptr(InstallSingleAppData) app_data = NULL;

		/* only process this app if was created by this plugin */
		if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
			continue;

		if (!g_str_equal (gs_app_get_id (app), "chiron.desktop") &&
		    !g_str_equal (gs_app_get_id (app), "zeus.desktop"))
			continue;

		app_data = g_new0 (InstallSingleAppData, 1);
		app_data->task = g_object_ref (task);
		app_data->app = g_object_ref (app);

		gs_app_set_state (app, GS_APP_STATE_INSTALLING);

		data->n_pending_ops++;
		data->n_installs_started++;
		gs_plugin_dummy_delay_async (GS_PLUGIN (self),
					     app,
					     500  /* ms */,
					     cancellable,
					     install_cb,
					     g_steal_pointer (&app_data));
	}

	finish_install_apps_op (task, NULL);
}

static void
install_cb (GObject      *source_object,
            GAsyncResult *result,
            gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	g_autoptr(InstallSingleAppData) app_data = g_steal_pointer (&user_data);
	GTask *task = app_data->task;
	GsPluginDummy *self = g_task_get_source_object (task);
	InstallAppsData * data = g_task_get_task_data (task);
	g_autoptr(GError) local_error = NULL;

	if (data->progress_callback != NULL) {
		data->progress_callback (plugin,
					 (data->n_installs_started - data->n_pending_ops + 1) * 100 / data->n_installs_started,
					 data->progress_user_data);
	}

	if (!gs_plugin_dummy_delay_finish (plugin, result, &local_error)) {
		gs_app_set_state_recover (app_data->app);
		finish_install_apps_op (task, g_steal_pointer (&local_error));
		return;
	}

	gs_app_set_state (app_data->app, GS_APP_STATE_INSTALLED);

	/* keep track */
	g_hash_table_insert (self->installed_apps,
			     g_strdup (gs_app_get_id (app_data->app)),
			     GUINT_TO_POINTER (1));
	g_hash_table_remove (self->available_apps, gs_app_get_id (app_data->app));

	finish_install_apps_op (task, NULL);
}

/* @error is (transfer full) if non-%NULL */
static void
finish_install_apps_op (GTask  *task,
                        GError *error)
{
	InstallAppsData *data = g_task_get_task_data (task);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);

	if (error_owned != NULL && data->saved_error == NULL)
		data->saved_error = g_steal_pointer (&error_owned);
	else if (error_owned != NULL)
		g_debug ("Additional error while installing apps: %s", error_owned->message);

	g_assert (data->n_pending_ops > 0);
	data->n_pending_ops--;

	if (data->n_pending_ops > 0)
		return;

	/* Get the results of the parallel ops. */
	if (data->saved_error != NULL)
		g_task_return_error (task, g_steal_pointer (&data->saved_error));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_dummy_install_apps_finish (GsPlugin      *plugin,
                                     GAsyncResult  *result,
                                     GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
refine_app (GsPluginDummy               *self,
            GsApp                       *app,
            GsPluginRefineRequireFlags   require_flags,
            GCancellable                *cancellable,
            GError                     **error)
{
	/* make the local system EOL */
	if (gs_app_get_metadata_item (app, "GnomeSoftware::CpeName") != NULL)
		gs_app_set_state (app, GS_APP_STATE_UNAVAILABLE);

	/* state */
	if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN) {
		if (g_hash_table_lookup (self->installed_apps,
					 gs_app_get_id (app)) != NULL)
			gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		if (g_hash_table_lookup (self->available_apps,
					 gs_app_get_id (app)) != NULL)
			gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	}

	/* kind */
	if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "mate-spell.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "com.hughski.ColorHug2.driver") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "zeus.desktop") == 0) {
		if (gs_app_get_kind (app) == AS_COMPONENT_KIND_UNKNOWN)
			gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
	}

	/* license */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_LICENSE) != 0) {
		if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0 ||
		    g_strcmp0 (gs_app_get_id (app), "zeus.desktop") == 0)
			gs_app_set_license (app, GS_APP_QUALITY_HIGHEST, "GPL-2.0-or-later");
	}

	/* homepage */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_URL) != 0) {
		if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0) {
			gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
					"http://www.test.org/");
		}
	}

	/* origin */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN) != 0) {
		if (g_strcmp0 (gs_app_get_id (app), "zeus-spell.addon") == 0)
			gs_app_set_origin (app, "london-east");
	}

	/* default */
	if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0) {
		if (gs_app_get_name (app) == NULL)
			gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "tmp");
		if (gs_app_get_summary (app) == NULL)
			gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "tmp");
		if (!gs_app_has_icons (app)) {
			g_autoptr(GIcon) ic = g_themed_icon_new ("org.gnome.Software.Dummy");
			gs_app_add_icon (app, ic);
		}
	}

	/* description */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_DESCRIPTION) != 0) {
		if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0) {
			gs_app_set_description (app, GS_APP_QUALITY_NORMAL,
						"long description!");
		}
	}

	/* add fake review */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEWS) != 0) {
		g_autoptr(AsReview) review1 = NULL;
		g_autoptr(AsReview) review2 = NULL;
		g_autoptr(GDateTime) dt = NULL;

		dt = g_date_time_new_now_utc ();

		/* set first review */
		review1 = as_review_new ();
		as_review_set_rating (review1, 50);
		as_review_set_reviewer_name (review1, "Angela Avery");
		as_review_set_summary (review1, "Steep learning curve, but worth it");
		as_review_set_description (review1, "Best overall 3D application I've ever used overall 3D application I've ever used. Best overall 3D application I've ever used overall 3D application I've ever used. Best overall 3D application I've ever used overall 3D application I've ever used. Best overall 3D application I've ever used overall 3D application I've ever used.");
		as_review_set_version (review1, "3.16.4");
		as_review_set_date (review1, dt);
		gs_app_add_review (app, review1);

		/* set self review */
		review2 = as_review_new ();
		as_review_set_rating (review2, 100);
		as_review_set_reviewer_name (review2, "Just Myself");
		as_review_set_summary (review2, "I like this application");
		as_review_set_description (review2, "I'm not very wordy myself.");
		as_review_set_version (review2, "3.16.3");
		as_review_set_date (review2, dt);
		as_review_set_flags (review2, AS_REVIEW_FLAG_SELF);
		gs_app_add_review (app, review2);
	}

	/* add fake ratings */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEW_RATINGS) != 0) {
		g_autoptr(GArray) ratings = NULL;
		const gint data[] = { 0, 10, 20, 30, 15, 2 };
		ratings = g_array_sized_new (FALSE, FALSE, sizeof (gint), 6);
		g_array_append_vals (ratings, data, 6);
		gs_app_set_review_ratings (app, ratings);
	}

	/* add a rating */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_RATING) != 0) {
		gs_app_set_rating (app, 66);
	}

	return TRUE;
}

static void
gs_plugin_dummy_refine_async (GsPlugin                   *plugin,
                              GsAppList                  *list,
                              GsPluginRefineFlags         job_flags,
                              GsPluginRefineRequireFlags  require_flags,
                              GsPluginEventCallback       event_callback,
                              void                       *event_user_data,
                              GCancellable               *cancellable,
                              GAsyncReadyCallback         callback,
                              gpointer                    user_data)
{
	GsPluginDummy *self = GS_PLUGIN_DUMMY (plugin);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_refine_async);

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		if (!refine_app (self, app, require_flags, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_dummy_refine_finish (GsPlugin      *plugin,
                               GAsyncResult  *result,
                               GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void list_apps_timeout_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data);

static void
gs_plugin_dummy_list_apps_async (GsPlugin              *plugin,
                                 GsAppQuery            *query,
                                 GsPluginListAppsFlags  flags,
                                 GsPluginEventCallback  event_callback,
                                 void                  *event_user_data,
                                 GCancellable          *cancellable,
                                 GAsyncReadyCallback    callback,
                                 gpointer               user_data)
{
	GsPluginDummy *self = GS_PLUGIN_DUMMY (plugin);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GsAppList) list = gs_app_list_new ();
	GDateTime *released_since = NULL;
	GsAppQueryTristate is_curated = GS_APP_QUERY_TRISTATE_UNSET;
	guint max_results = 0;
	GsCategory *category = NULL;
	GsAppQueryTristate is_installed = GS_APP_QUERY_TRISTATE_UNSET;
	GsAppQueryTristate is_for_update = GS_APP_QUERY_TRISTATE_UNSET;
	const gchar * const *keywords = NULL;
	GsApp *alternate_of = NULL;

	task = gs_plugin_list_apps_data_new_task (plugin, query, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_list_apps_async);

	if (query != NULL) {
		released_since = gs_app_query_get_released_since (query);
		is_curated = gs_app_query_get_is_curated (query);
		max_results = gs_app_query_get_max_results (query);
		category = gs_app_query_get_category (query);
		is_installed = gs_app_query_get_is_installed (query);
		keywords = gs_app_query_get_keywords (query);
		alternate_of = gs_app_query_get_alternate_of (query);
		is_for_update = gs_app_query_get_is_for_update (query);
	}

	/* Currently only support a subset of query properties, and only one set at once.
	 * Also don’t currently support GS_APP_QUERY_TRISTATE_FALSE. */
	if ((released_since == NULL &&
	     is_curated == GS_APP_QUERY_TRISTATE_UNSET &&
	     category == NULL &&
	     is_installed == GS_APP_QUERY_TRISTATE_UNSET &&
	     keywords == NULL &&
	     alternate_of == NULL &&
	     is_for_update == GS_APP_QUERY_TRISTATE_UNSET) ||
	    is_curated == GS_APP_QUERY_TRISTATE_FALSE ||
	    is_installed == GS_APP_QUERY_TRISTATE_FALSE ||
	    is_for_update == GS_APP_QUERY_TRISTATE_FALSE ||
	    gs_app_query_get_n_properties_set (query) != 1) {
		g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					 "Unsupported query");
		return;
	}

	if (released_since != NULL) {
		g_autoptr(GIcon) icon = g_themed_icon_new ("chiron.desktop");
		g_autoptr(GsApp) app = gs_app_new ("chiron.desktop");
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Chiron");
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "View and use virtual machines");
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, "http://www.box.org");
		gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
		gs_app_add_icon (app, icon);
		gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_set_management_plugin (app, plugin);

		gs_app_list_add (list, app);
	}

	if (is_curated != GS_APP_QUERY_TRISTATE_UNSET) {
		/* Hacky way of letting callers indicate which set of results
		 * they want, for unit testing. */
		if (max_results == 6) {
			const gchar *apps[] = { "chiron.desktop", "zeus.desktop" };
			for (gsize i = 0; i < G_N_ELEMENTS (apps); i++) {
				g_autoptr(GsApp) app = gs_app_new (apps[i]);
				gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
				gs_app_list_add (list, app);
			}
		} else {
			g_autoptr(GsApp) app = NULL;
			/* add wildcard */
			app = gs_app_new ("zeus.desktop");
			gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
			gs_app_set_metadata (app, "GnomeSoftware::Creator",
					     gs_plugin_get_name (plugin));
			gs_app_list_add (list, app);
		}
	}

	if (category != NULL) {
		g_autoptr(GIcon) icon = g_themed_icon_new ("chiron.desktop");
		g_autoptr(GsApp) app = gs_app_new ("chiron.desktop");
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Chiron");
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "View and use virtual machines");
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, "http://www.box.org");
		gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
		gs_app_add_icon (app, icon);
		gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_set_management_plugin (app, plugin);
		gs_app_list_add (list, app);
	}

	if (is_installed != GS_APP_QUERY_TRISTATE_UNSET) {
		const gchar *packages[] = { "zeus", "zeus-common", NULL };
		const gchar *app_ids[] = { "Uninstall Zeus.desktop", NULL };

		/* add all packages */
		for (gsize i = 0; packages[i] != NULL; i++) {
			g_autoptr(GsApp) app = gs_app_new (NULL);
			gs_app_add_source (app, packages[i]);
			gs_app_set_state (app, GS_APP_STATE_INSTALLED);
			gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
			gs_app_set_origin (app, "london-west");
			gs_app_set_management_plugin (app, plugin);
			gs_app_list_add (list, app);
		}

		/* add all app-ids */
		for (gsize i = 0; app_ids[i] != NULL; i++) {
			g_autoptr(GsApp) app = gs_app_new (app_ids[i]);
			gs_app_set_state (app, GS_APP_STATE_INSTALLED);
			gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
			gs_app_set_management_plugin (app, plugin);
			gs_app_list_add (list, app);
		}
	}

	if (keywords != NULL) {
		if (g_strcmp0 (keywords[0], "hang") == 0) {
			/* hang the plugin for 5 seconds */
			gs_plugin_dummy_timeout_async (self, 5000, cancellable,
						       list_apps_timeout_cb, g_steal_pointer (&task));
			return;
		} else if (g_strcmp0 (keywords[0], "chiron") == 0) {
			g_autoptr(GsApp) app = NULL;

			/* does the app already exist? */
			app = gs_plugin_cache_lookup (plugin, "chiron");
			if (app != NULL) {
				g_debug ("using %s fom the cache", gs_app_get_id (app));
				gs_app_list_add (list, app);
			} else {
				g_autoptr(GIcon) icon = NULL;

				/* set up a timeout to emulate getting a GFileMonitor callback */
				self->quirk_id =
					g_timeout_add_seconds (1, gs_plugin_dummy_poll_cb, plugin);

				/* use a generic stock icon */
				icon = g_themed_icon_new ("org.gnome.Software.Dummy");

				/* add a live updatable normal application */
				app = gs_app_new ("chiron.desktop");
				gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Chiron");
				gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "A teaching application");
				gs_app_add_icon (app, icon);
				gs_app_set_size_installed (app, GS_SIZE_TYPE_VALID, 42 * 1024 * 1024);
				gs_app_set_size_download (app, GS_SIZE_TYPE_VALID, 50 * 1024 * 1024);
				gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
				gs_app_set_state (app, GS_APP_STATE_INSTALLED);
				gs_app_set_management_plugin (app, plugin);
				gs_app_set_metadata (app, "GnomeSoftware::Creator",
						     gs_plugin_get_name (plugin));
				gs_app_list_add (list, app);

				/* add to cache so it can be found by the flashing callback */
				gs_plugin_cache_add (plugin, NULL, app);
			}
		} else {
			/* Don’t do anything */
		}
	}

	if (alternate_of != NULL) {
		if (g_strcmp0 (gs_app_get_id (alternate_of), "zeus.desktop") == 0) {
			g_autoptr(GsApp) app = gs_app_new ("chiron.desktop");
			gs_app_list_add (list, app);
		}
	}

	if (is_for_update == GS_APP_QUERY_TRISTATE_TRUE) {
		/* spin */
		gs_plugin_dummy_timeout_async (self, 2000, cancellable,
					       list_apps_timeout_cb, g_steal_pointer (&task));
		return;
	}

	g_task_return_pointer (task, g_steal_pointer (&list), (GDestroyNotify) g_object_unref);
}

static GsAppList *
list_apps_finish (GsPluginDummy *self,
		  GTask *task)
{
	GsPluginListAppsData *data = g_task_get_task_data (task);
	g_autoptr(GsAppList) list = gs_app_list_new ();

	if (data->query && gs_app_query_get_is_for_update (data->query) == GS_APP_QUERY_TRISTATE_TRUE) {
		GsPlugin *plugin = GS_PLUGIN (self);
		GsApp *app;
		GsApp *proxy;
		g_autoptr(GIcon) ic = NULL;

		/* use a generic stock icon */
		ic = g_themed_icon_new ("org.gnome.Software.Dummy");

		/* add a live updatable normal application */
		app = gs_app_new ("chiron.desktop");
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Chiron");
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "A teaching application");
		gs_app_set_update_details_text (app, "Do not crash when using libvirt.");
		gs_app_set_update_urgency (app, AS_URGENCY_KIND_HIGH);
		gs_app_add_icon (app, ic);
		gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE_LIVE);
		gs_app_set_management_plugin (app, plugin);
		gs_app_list_add (list, app);
		g_object_unref (app);

		/* add a offline OS update */
		app = gs_app_new (NULL);
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "libvirt-glib-devel");
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "Development files for libvirt");
		gs_app_set_update_details_text (app, "Fix several memory leaks.");
		gs_app_set_update_urgency (app, AS_URGENCY_KIND_LOW);
		gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
		gs_app_add_source (app, "libvirt-glib-devel");
		gs_app_add_source_id (app, "libvirt-glib-devel;0.0.1;noarch;fedora");
		gs_app_set_management_plugin (app, plugin);
		gs_app_list_add (list, app);
		g_object_unref (app);

		/* add a live OS update */
		app = gs_app_new (NULL);
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "chiron-libs");
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "library for chiron");
		gs_app_set_update_details_text (app, "Do not crash when using libvirt.");
		gs_app_set_update_urgency (app, AS_URGENCY_KIND_HIGH);
		gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE_LIVE);
		gs_app_add_source (app, "chiron-libs");
		gs_app_add_source_id (app, "chiron-libs;0.0.1;i386;updates-testing");
		gs_app_set_management_plugin (app, plugin);
		gs_app_list_add (list, app);
		g_object_unref (app);

		/* add a proxy app update */
		proxy = gs_app_new ("proxy.desktop");
		gs_app_set_name (proxy, GS_APP_QUALITY_NORMAL, "Proxy");
		gs_app_set_summary (proxy, GS_APP_QUALITY_NORMAL, "A proxy app");
		gs_app_set_update_details_text (proxy, "Update all related apps.");
		gs_app_set_update_urgency (proxy, AS_URGENCY_KIND_HIGH);
		gs_app_add_icon (proxy, ic);
		gs_app_set_kind (proxy, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_add_quirk (proxy, GS_APP_QUIRK_IS_PROXY);
		gs_app_set_state (proxy, GS_APP_STATE_UPDATABLE_LIVE);
		gs_app_set_management_plugin (proxy, plugin);
		gs_app_list_add (list, proxy);
		g_object_unref (proxy);

		/* add a proxy related app */
		app = gs_app_new ("proxy-related-app.desktop");
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Related app");
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "A related app");
		gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE_LIVE);
		gs_app_set_management_plugin (app, plugin);
		gs_app_add_related (proxy, app);
		g_object_unref (app);

		/* add another proxy related app */
		app = gs_app_new ("proxy-another-related-app.desktop");
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Another Related app");
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "A related app");
		gs_app_set_kind (app, AS_COMPONENT_KIND_DESKTOP_APP);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE_LIVE);
		gs_app_set_management_plugin (app, plugin);
		gs_app_add_related (proxy, app);
		g_object_unref (app);
	}

	return g_steal_pointer (&list);
}

static void
list_apps_timeout_cb (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
	GsPluginDummy *self = GS_PLUGIN_DUMMY (object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;

	/* Return a cancelled error, or an empty app list after hanging. */
	if (gs_plugin_dummy_timeout_finish (self, result, &local_error))
		g_task_return_pointer (task, list_apps_finish (self, task), (GDestroyNotify) g_object_unref);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

static GsAppList *
gs_plugin_dummy_list_apps_finish (GsPlugin      *plugin,
                                  GAsyncResult  *result,
                                  GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void
gs_plugin_dummy_list_distro_upgrades_async (GsPlugin                        *plugin,
                                            GsPluginListDistroUpgradesFlags  flags,
                                            GCancellable                    *cancellable,
                                            GAsyncReadyCallback              callback,
                                            gpointer                         user_data)
{
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GIcon) ic = NULL;
	g_autofree gchar *background_filename = NULL;
	g_autofree gchar *css = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GsAppList) list = gs_app_list_new ();

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_list_distro_upgrades_async);

	/* use stock icon */
	ic = g_themed_icon_new ("system-component-addon");

	/* get existing item from the cache */
	app = gs_plugin_cache_lookup (plugin, "user/*/os-upgrade/org.fedoraproject.release-rawhide.upgrade/*");
	if (app != NULL) {
		gs_app_list_add (list, app);

		g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
		return;
	}

	app = gs_app_new ("org.fedoraproject.release-rawhide.upgrade");
	gs_app_set_scope (app, AS_COMPONENT_SCOPE_USER);
	gs_app_set_kind (app, AS_COMPONENT_KIND_OPERATING_SYSTEM);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, "Fedora");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
			    "A major upgrade, with new features and added polish.");
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
			"https://fedoraproject.org/wiki/Releases/24/Schedule");
	gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
	gs_app_add_quirk (app, GS_APP_QUIRK_PROVENANCE);
	gs_app_add_quirk (app, GS_APP_QUIRK_NOT_REVIEWABLE);
	gs_app_set_version (app, "34");
	gs_app_set_size_installed (app, GS_SIZE_TYPE_VALID, 256 * 1024 * 1024);
	gs_app_set_size_download (app, GS_SIZE_TYPE_VALID, 1024 * 1024 * 1024);
	gs_app_set_license (app, GS_APP_QUALITY_LOWEST, "LicenseRef-free");
	gs_app_set_management_plugin (app, plugin);

	/* Check for a background image in the standard location. */
	background_filename = gs_utils_get_upgrade_background ("34");

	if (background_filename != NULL)
		css = g_strconcat ("background: url('file://", background_filename, "');"
				   "background-size: 100% 100%;", NULL);
	gs_app_set_metadata (app, "GnomeSoftware::UpgradeBanner-css", css);

	gs_app_add_icon (app, ic);
	gs_app_list_add (list, app);

	gs_plugin_cache_add (plugin, NULL, app);

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_dummy_list_distro_upgrades_finish (GsPlugin      *plugin,
                                             GAsyncResult  *result,
                                             GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void update_apps_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data);

static void
gs_plugin_dummy_update_apps_async (GsPlugin                           *plugin,
                                   GsAppList                          *apps,
                                   GsPluginUpdateAppsFlags             flags,
                                   GsPluginProgressCallback            progress_callback,
                                   gpointer                            progress_user_data,
                                   GsPluginEventCallback               event_callback,
                                   void                               *event_user_data,
                                   GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                   gpointer                            app_needs_user_action_data,
                                   GCancellable                       *cancellable,
                                   GAsyncReadyCallback                 callback,
                                   gpointer                            user_data)
{
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_update_apps_data_new_task (plugin, apps, flags,
						    progress_callback, progress_user_data,
						    event_callback, event_user_data,
						    app_needs_user_action_callback, app_needs_user_action_data,
						    cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_update_apps_async);

	if (!(flags & GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD))
		gs_plugin_dummy_delay_async (plugin, NULL, 5100, cancellable, update_apps_cb, g_steal_pointer (&task));
	else
		update_apps_cb (G_OBJECT (plugin), NULL, g_steal_pointer (&task));
}

static void
update_apps_cb (GObject      *source_object,
                GAsyncResult *result,
                gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	GsPluginDummy *self = GS_PLUGIN_DUMMY (plugin);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginUpdateAppsData *data = g_task_get_task_data (task);
	g_autoptr(GError) local_error = NULL;

	if (result != NULL &&
	    !gs_plugin_dummy_delay_finish (plugin, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	if (!(data->flags & GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY)) {
		for (guint i = 0; i < gs_app_list_length (data->apps); i++) {
			GsApp *app = gs_app_list_index (data->apps, i);

			/* only process this app if was created by this plugin */
			if (!gs_app_has_management_plugin (app, plugin))
				continue;

			if (!g_str_has_prefix (gs_app_get_id (app), "proxy")) {
				g_autoptr(GsPluginEvent) event = NULL;

				/* always fail */
				g_set_error_literal (&local_error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
						     "no network connection is available");
				gs_utils_error_add_origin_id (&local_error, self->cached_origin);

				event = gs_plugin_event_new ("app", app,
							     "error", local_error,
							     "origin", self->cached_origin,
							     NULL);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
				if (data->flags & GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE)
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				if (data->event_callback != NULL)
					data->event_callback (plugin, event, data->event_user_data);

				g_clear_error (&local_error);
				continue;
			}

			/* simulate an update for 4 seconds */
			gs_app_set_state (app, GS_APP_STATE_INSTALLING);

			for (guint j = 1; j <= 4; ++j) {
				gs_app_set_progress (app, 25 * j);
				sleep (1); /* FIXME: make this async */
			}

			gs_app_set_state (app, GS_APP_STATE_INSTALLED);

			/* Simple progress reporting. */
			if (data->progress_callback != NULL) {
				data->progress_callback (GS_PLUGIN (self),
							 100 * ((gdouble) (i + 1) / gs_app_list_length (data->apps)),
							 data->progress_user_data);
			}
		}

		g_task_return_boolean (task, TRUE);
	} else {
		g_task_return_boolean (task, TRUE);
	}
}

static gboolean
gs_plugin_dummy_update_apps_finish (GsPlugin      *plugin,
                                    GAsyncResult  *result,
                                    GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void refresh_metadata_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);

static void
gs_plugin_dummy_refresh_metadata_async (GsPlugin                     *plugin,
                                        guint64                       cache_age_secs,
                                        GsPluginRefreshMetadataFlags  flags,
                                        GsPluginEventCallback         event_callback,
                                        void                         *event_user_data,
                                        GCancellable                 *cancellable,
                                        GAsyncReadyCallback           callback,
                                        gpointer                      user_data)
{
	g_autoptr(GTask) task = NULL;
	g_autoptr(GsApp) app = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_refresh_metadata_async);

	app = gs_app_new (NULL);
	gs_plugin_dummy_delay_async (plugin, app, 3100, cancellable, refresh_metadata_cb, g_steal_pointer (&task));
}

static void
refresh_metadata_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;

	if (!gs_plugin_dummy_delay_finish (plugin, result, &local_error))
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_dummy_refresh_metadata_finish (GsPlugin      *plugin,
                                         GAsyncResult  *result,
                                         GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
download_upgrade_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (source_object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	GsPluginDownloadUpgradeData *data = g_task_get_task_data (task);

	if (!gs_plugin_dummy_delay_finish (plugin, result, &local_error)) {
		gs_app_set_state_recover (data->app);
		g_task_return_error (task, g_steal_pointer (&local_error));
	} else {
		gs_app_set_state (data->app, GS_APP_STATE_UPDATABLE);
		g_task_return_boolean (task, TRUE);
	}
}

static void
gs_plugin_dummy_download_upgrade_async (GsPlugin                     *plugin,
                                        GsApp                        *app,
                                        GsPluginDownloadUpgradeFlags  flags,
                                        GsPluginEventCallback         event_callback,
                                        void                         *event_user_data,
                                        GCancellable                 *cancellable,
                                        GAsyncReadyCallback           callback,
                                        gpointer                      user_data)
{
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_download_upgrade_data_new_task (plugin, app, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_download_upgrade_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	g_debug ("starting download");
	gs_app_set_state (app, GS_APP_STATE_DOWNLOADING);
	gs_plugin_dummy_delay_async (plugin, app, 5000, cancellable, download_upgrade_cb, g_steal_pointer (&task));
}

static gboolean
gs_plugin_dummy_download_upgrade_finish (GsPlugin      *plugin,
					 GAsyncResult  *result,
					 GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_dummy_trigger_upgrade_async (GsPlugin                    *plugin,
                                       GsApp                       *app,
                                       GsPluginTriggerUpgradeFlags  flags,
                                       GCancellable                *cancellable,
                                       GAsyncReadyCallback          callback,
                                       gpointer                     user_data)
{
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_trigger_upgrade_data_new_task (plugin, app, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_trigger_upgrade_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* NOP */
	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_dummy_trigger_upgrade_finish (GsPlugin      *plugin,
					GAsyncResult  *result,
					GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_dummy_cancel_offline_update_async (GsPlugin                         *plugin,
                                             GsPluginCancelOfflineUpdateFlags  flags,
                                             GCancellable                     *cancellable,
                                             GAsyncReadyCallback               callback,
                                             gpointer                          user_data)
{
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_cancel_offline_update_data_new_task (plugin, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dummy_cancel_offline_update_async);

	/* success */
	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_dummy_cancel_offline_update_finish (GsPlugin      *plugin,
                                              GAsyncResult  *result,
                                              GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_dummy_class_init (GsPluginDummyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_dummy_dispose;

	plugin_class->adopt_app = gs_plugin_dummy_adopt_app;
	plugin_class->setup_async = gs_plugin_dummy_setup_async;
	plugin_class->setup_finish = gs_plugin_dummy_setup_finish;
	plugin_class->refine_async = gs_plugin_dummy_refine_async;
	plugin_class->refine_finish = gs_plugin_dummy_refine_finish;
	plugin_class->list_apps_async = gs_plugin_dummy_list_apps_async;
	plugin_class->list_apps_finish = gs_plugin_dummy_list_apps_finish;
	plugin_class->refresh_metadata_async = gs_plugin_dummy_refresh_metadata_async;
	plugin_class->refresh_metadata_finish = gs_plugin_dummy_refresh_metadata_finish;
	plugin_class->list_distro_upgrades_async = gs_plugin_dummy_list_distro_upgrades_async;
	plugin_class->list_distro_upgrades_finish = gs_plugin_dummy_list_distro_upgrades_finish;
	plugin_class->install_apps_async = gs_plugin_dummy_install_apps_async;
	plugin_class->install_apps_finish = gs_plugin_dummy_install_apps_finish;
	plugin_class->uninstall_apps_async = gs_plugin_dummy_uninstall_apps_async;
	plugin_class->uninstall_apps_finish = gs_plugin_dummy_uninstall_apps_finish;
	plugin_class->update_apps_async = gs_plugin_dummy_update_apps_async;
	plugin_class->update_apps_finish = gs_plugin_dummy_update_apps_finish;
	plugin_class->cancel_offline_update_async = gs_plugin_dummy_cancel_offline_update_async;
	plugin_class->cancel_offline_update_finish = gs_plugin_dummy_cancel_offline_update_finish;
	plugin_class->download_upgrade_async = gs_plugin_dummy_download_upgrade_async;
	plugin_class->download_upgrade_finish = gs_plugin_dummy_download_upgrade_finish;
	plugin_class->trigger_upgrade_async = gs_plugin_dummy_trigger_upgrade_async;
	plugin_class->trigger_upgrade_finish = gs_plugin_dummy_trigger_upgrade_finish;
	plugin_class->url_to_app_async = gs_plugin_dummy_url_to_app_async;
	plugin_class->url_to_app_finish = gs_plugin_dummy_url_to_app_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_DUMMY;
}
