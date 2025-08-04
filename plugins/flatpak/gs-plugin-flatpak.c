/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Joaquim Rocha <jrocha@endlessm.com>
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017-2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * SECTION:
 * Exposes flatpaks from the user and system repositories.
 *
 * All GsApp's created have management-plugin set to flatpak
 * Some GsApp's created have have flatpak::kind of app or runtime
 * The GsApp:origin is the remote name, e.g. test-repo
 *
 * The plugin has two worker threads which all operations are delegated to, as
 * the libflatpak API is entirely synchronous (and thread-safe). Message passing
 * to the worker threads is by gs_worker_thread_queue(). One worker thread is
 * for ‘long-running’ operations, such as updating, installing and removing
 * apps. These are operations which might take a long time to complete, and we
 * don’t want them blocking other flatpak plugin operations in the meantime,
 * such as listing apps. The other worker thread is for all other operations.
 */

#include <config.h>

#include <flatpak.h>
#include <glib/gi18n.h>
#include <gnome-software.h>

#include "gs-appstream.h"
#include "gs-flatpak-app.h"
#include "gs-flatpak.h"
#include "gs-flatpak-transaction.h"
#include "gs-flatpak-utils.h"
#include "gs-metered.h"
#include "gs-profiler.h"
#include "gs-worker-thread.h"

#include "gs-plugin-flatpak.h"

/* Timeout for pure of unused refs:
 * - A timer checks every 2h
 * - If the plugin is enabled, and unused refs have not yet been
 *   removed (successfully or not) in the last 24h, then `flatpak-purge-timestamp`
 *   is updated and a purge operation is started
 * - Timeout callbacks are ignored until another 24h has passed
 */
#define PURGE_TIMEOUT_SECONDS (60 * 60 * 2)

struct _GsPluginFlatpak
{
	GsPlugin		 parent;

	GsWorkerThread		*worker;  /* (owned) */
	GsWorkerThread		*long_running_worker;  /* (owned) */

	GPtrArray		*installations;  /* (element-type GsFlatpak) (owned); may be NULL before setup or after shutdown */
	gboolean		 has_system_helper;
	const gchar		*destdir_for_tests;

	GCancellable		*purge_cancellable;
	guint			 purge_timeout_id;

	GPtrArray		*cache_files_to_delete;  /* (element-type GFile) (nullable) */
};

G_DEFINE_TYPE (GsPluginFlatpak, gs_plugin_flatpak, GS_TYPE_PLUGIN)

#define assert_in_worker(self) \
	g_assert (gs_worker_thread_is_in_worker_context (self->worker) || \
		  gs_worker_thread_is_in_worker_context (self->long_running_worker))

/* Work around flatpak_transaction_get_no_interaction() not existing before
 * flatpak 1.13.0. */
#if !FLATPAK_CHECK_VERSION(1,13,0)
#define flatpak_transaction_get_no_interaction(transaction) \
	GPOINTER_TO_INT (g_object_get_data (G_OBJECT (transaction), "flatpak-no-interaction"))
#define flatpak_transaction_set_no_interaction(transaction, no_interaction) \
	G_STMT_START { \
		FlatpakTransaction *ftsni_transaction = (transaction); \
		gboolean ftsni_no_interaction = (no_interaction); \
		(flatpak_transaction_set_no_interaction) (ftsni_transaction, ftsni_no_interaction); \
		g_object_set_data (G_OBJECT (ftsni_transaction), "flatpak-no-interaction", GINT_TO_POINTER (ftsni_no_interaction)); \
	} G_STMT_END
#endif  /* flatpak < 1.13.0 */

static void
gs_plugin_flatpak_dispose (GObject *object)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (object);

	g_assert (self->cache_files_to_delete == NULL || self->cache_files_to_delete->len == 0);
	g_clear_pointer (&self->cache_files_to_delete, g_ptr_array_unref);

	g_cancellable_cancel (self->purge_cancellable);
	g_assert (self->purge_timeout_id == 0);

	g_clear_pointer (&self->installations, g_ptr_array_unref);
	g_clear_object (&self->purge_cancellable);
	g_clear_object (&self->worker);
	g_clear_object (&self->long_running_worker);

	G_OBJECT_CLASS (gs_plugin_flatpak_parent_class)->dispose (object);
}

static void
gs_plugin_flatpak_init (GsPluginFlatpak *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	self->installations = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* getting app properties from appstream is quicker */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");

	/* like appstream, we need the icon plugin to load cached icons into pixbufs */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "icons");

	/* prioritize over packages */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_BETTER_THAN, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_BETTER_THAN, "rpm-ostree");

	/* used for self tests */
	self->destdir_for_tests = g_getenv ("GS_SELF_TEST_FLATPAK_DATADIR");
}

/* Run in @worker. */
static void
gs_plugin_flatpak_purge_thread_cb (GTask        *task,
				   gpointer      source_object,
				   gpointer      task_data,
				   GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GPtrArray *flatpaks = task_data;

	assert_in_worker (self);

	for (guint i = 0; i < flatpaks->len; i++) {
		g_autoptr(GError) local_error = NULL;
		GsFlatpak *flatpak = g_ptr_array_index (flatpaks, i);

		if (!gs_flatpak_purge_sync (flatpak, cancellable, &local_error)) {
			gs_flatpak_error_convert (&local_error);
			g_debug ("Failed to purge unused refs at '%s': %s",
				 gs_flatpak_get_id (flatpak), local_error->message);
		}
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_flatpak_purge_timeout_cb (gpointer user_data)
{
	GsPluginFlatpak *self = user_data;
	if (gs_plugin_get_enabled (GS_PLUGIN (self))) {
		g_autoptr(GSettings) settings = g_settings_new ("org.gnome.software");
		gint64 current_time = g_get_real_time () / G_USEC_PER_SEC;
		if ((current_time / (60 * 60 * 24)) != (g_settings_get_int64 (settings, "flatpak-purge-timestamp") / (60 * 60 * 24))) {
			g_autoptr(GPtrArray) flatpaks = g_ptr_array_new_with_free_func (g_object_unref);
			g_settings_set_int64 (settings, "flatpak-purge-timestamp", current_time);
			g_cancellable_cancel (self->purge_cancellable);
			g_clear_object (&self->purge_cancellable);
			self->purge_cancellable = g_cancellable_new ();
			for (guint i = 0; i < self->installations->len; i++) {
				GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
				if (gs_flatpak_get_busy (flatpak)) {
					g_debug ("Skipping '%s' in this round, it's busy right now", gs_flatpak_get_id (flatpak));
					continue;
				}
				g_ptr_array_add (flatpaks, g_object_ref (flatpak));
			}
			if (flatpaks->len > 0) {
				g_autoptr(GTask) task = NULL;

				task = g_task_new (self, self->purge_cancellable, NULL, NULL);
				g_task_set_source_tag (task, gs_plugin_flatpak_purge_timeout_cb);
				g_task_set_task_data (task, g_steal_pointer (&flatpaks), (GDestroyNotify) g_ptr_array_unref);

				gs_worker_thread_queue (self->long_running_worker, G_PRIORITY_LOW,
						        gs_plugin_flatpak_purge_thread_cb, g_steal_pointer (&task));
			}
		}
	} else {
		self->purge_timeout_id = 0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
_as_component_scope_is_compatible (AsComponentScope scope1, AsComponentScope scope2)
{
	if (scope1 == AS_COMPONENT_SCOPE_UNKNOWN)
		return TRUE;
	if (scope2 == AS_COMPONENT_SCOPE_UNKNOWN)
		return TRUE;
	return scope1 == scope2;
}

static void
gs_plugin_flatpak_adopt_app (GsPlugin *plugin,
			     GsApp *app)
{
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_FLATPAK)
		gs_app_set_management_plugin (app, plugin);
}

static gboolean
gs_plugin_flatpak_add_installation (GsPluginFlatpak      *self,
                                    FlatpakInstallation  *installation,
                                    GCancellable         *cancellable,
                                    GError              **error)
{
	g_autoptr(GsFlatpak) flatpak = NULL;
	GsFlatpakFlags flags = GS_FLATPAK_FLAG_NONE;

	if (!flatpak_installation_get_is_user (installation) && !self->has_system_helper)
		flags |= GS_FLATPAK_FLAG_DISABLE_UPDATE;

	/* create and set up */
	flatpak = gs_flatpak_new (GS_PLUGIN (self), installation, flags);
	if (!gs_flatpak_setup (flatpak, cancellable, error))
		return FALSE;
	g_debug ("successfully set up %s", gs_flatpak_get_id (flatpak));

	/* add objects that set up correctly */
	g_ptr_array_add (self->installations, g_steal_pointer (&flatpak));
	return TRUE;
}

/* Reports a warning/error at the plugin level, rather than at the job level.
 * This should only be used for warnings/errors which aren’t associated with a
 * particular `GsPluginJob`; otherwise, use the `event_callback()` from the job
 * as that exposes more information to the shell. */
static void
gs_plugin_flatpak_report_warning (GsPlugin *plugin,
				  GError **error)
{
	g_autoptr(GsPluginEvent) event = NULL;
	g_assert (error != NULL);
	if (*error != NULL && (*error)->domain != GS_PLUGIN_ERROR)
		gs_flatpak_error_convert (error);

	event = gs_plugin_event_new ("error", *error,
				     NULL);
	gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING | GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
	gs_plugin_report_event (plugin, event);
}

static gint
get_priority_for_interactivity (gboolean interactive)
{
	return interactive ? G_PRIORITY_DEFAULT : G_PRIORITY_LOW;
}

static void setup_thread_cb (GTask        *task,
                             gpointer      source_object,
                             gpointer      task_data,
                             GCancellable *cancellable);

static void
gs_plugin_flatpak_setup_async (GsPlugin            *plugin,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;

	g_debug ("Flatpak version: %d.%d.%d",
		FLATPAK_MAJOR_VERSION,
		FLATPAK_MINOR_VERSION,
		FLATPAK_MICRO_VERSION);

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_setup_async);

	/* Shouldn’t end up setting up twice */
	g_assert (self->installations == NULL || self->installations->len == 0);

	/* Start up two worker threads to process all the plugin’s function calls. */
	self->worker = gs_worker_thread_new ("gs-plugin-flatpak");
	self->long_running_worker = gs_worker_thread_new ("gs-plugin-flatpak-long");

	/* Queue a job to find and set up the installations. */
	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				setup_thread_cb, g_steal_pointer (&task));

	if (!self->purge_timeout_id)
		self->purge_timeout_id = g_timeout_add_seconds (PURGE_TIMEOUT_SECONDS,
								gs_plugin_flatpak_purge_timeout_cb,
								self);
}

/* Run in @worker. */
static void
setup_thread_cb (GTask        *task,
                 gpointer      source_object,
                 gpointer      task_data,
                 GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPlugin *plugin = GS_PLUGIN (self);
	g_autoptr(GPtrArray) installations = NULL;
	const gchar *action_id = "org.freedesktop.Flatpak.appstream-update";
	g_autoptr(GError) permission_error = NULL;
	g_autoptr(GPermission) permission = NULL;

	assert_in_worker (self);

	/* if we can't update the AppStream database system-wide don't even
	 * pull the data as we can't do anything with it */
	permission = gs_utils_get_permission (action_id, NULL, &permission_error);
	if (permission == NULL) {
		g_debug ("no permission for %s: %s", action_id, permission_error->message);
		g_clear_error (&permission_error);
	} else {
		self->has_system_helper = g_permission_get_allowed (permission) ||
					  g_permission_get_can_acquire (permission);
	}

	/* if we're not just running the tests */
	if (self->destdir_for_tests == NULL) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FlatpakInstallation) installation = NULL;

		/* include the system installations */
		installations = flatpak_get_system_installations (cancellable,
								  &error_local);

		if (installations == NULL) {
			gs_plugin_flatpak_report_warning (plugin, &error_local);
			g_clear_error (&error_local);
		}

		/* include the user installation */
		installation = flatpak_installation_new_user (cancellable,
							      &error_local);
		if (installation == NULL) {
			/* if some error happened, report it as an event, but
			 * do not return it, otherwise it will disable the whole
			 * plugin (meaning that support for Flatpak will not be
			 * possible even if a system installation is working) */
			gs_plugin_flatpak_report_warning (plugin, &error_local);
		} else {
			if (installations == NULL)
				installations = g_ptr_array_new_with_free_func (g_object_unref);

			g_ptr_array_add (installations, g_steal_pointer (&installation));
		}
	} else {
		g_autoptr(GError) error_local = NULL;

		/* use the test installation */
		g_autofree gchar *full_path = g_build_filename (self->destdir_for_tests,
								"flatpak",
								NULL);
		g_autoptr(GFile) file = g_file_new_for_path (full_path);
		g_autoptr(FlatpakInstallation) installation = NULL;
		g_debug ("using custom flatpak path %s", full_path);
		installation = flatpak_installation_new_for_path (file, TRUE,
								  cancellable,
								  &error_local);
		if (installation == NULL) {
			gs_flatpak_error_convert (&error_local);
			g_task_return_error (task, g_steal_pointer (&error_local));
			return;
		}

		installations = g_ptr_array_new_with_free_func (g_object_unref);
		g_ptr_array_add (installations, g_steal_pointer (&installation));
	}

	/* add the installations */
	for (guint i = 0; installations != NULL && i < installations->len; i++) {
		g_autoptr(GError) error_local = NULL;

		FlatpakInstallation *installation = g_ptr_array_index (installations, i);
		if (!gs_plugin_flatpak_add_installation (self,
							 installation,
							 cancellable,
							 &error_local)) {
			gs_plugin_flatpak_report_warning (plugin,
							  &error_local);
			continue;
		}
	}

	/* when no installation has been loaded, return the error so the
	 * plugin gets disabled */
	if (self->installations->len == 0) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
					 "Failed to load any Flatpak installations");
		return;
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_flatpak_setup_finish (GsPlugin      *plugin,
                                GAsyncResult  *result,
                                GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void shutdown_cb (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data);

static void
gs_plugin_flatpak_shutdown_async (GsPlugin            *plugin,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;

	g_clear_handle_id (&self->purge_timeout_id, g_source_remove);
	g_cancellable_cancel (self->purge_cancellable);

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_shutdown_async);

	/* Delete any cache files from this session. This should not really
	 * block, as they’re all local. */
	for (guint i = 0; self->cache_files_to_delete != NULL && i < self->cache_files_to_delete->len; i++) {
		GFile *cache_file = g_ptr_array_index (self->cache_files_to_delete, i);

		g_file_delete (cache_file, NULL, NULL);
	}

	g_clear_pointer (&self->cache_files_to_delete, g_ptr_array_unref);

	/* Stop the worker threads. */
	gs_worker_thread_shutdown_async (self->worker, cancellable, shutdown_cb, g_steal_pointer (&task));
}

static void
shutdown_cb (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	GsPluginFlatpak *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GsWorkerThread) worker = NULL;
	g_autoptr(GError) local_error = NULL;

	if (self->worker != NULL)
		worker = g_steal_pointer (&self->worker);
	else
		worker = g_steal_pointer (&self->long_running_worker);

	if (!gs_worker_thread_shutdown_finish (worker, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* Clear the other worker. */
	if (self->long_running_worker != NULL) {
		gs_worker_thread_shutdown_async (self->long_running_worker, cancellable, shutdown_cb, g_steal_pointer (&task));
		return;
	}

	/* Clear the flatpak installations */
	g_ptr_array_set_size (self->installations, 0);

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_flatpak_shutdown_finish (GsPlugin      *plugin,
                                   GAsyncResult  *result,
                                   GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

typedef struct {
	GsPlugin *plugin;  /* (owned) (not nullable) */
	GsPluginEvent *event;  /* (owned) (not nullable) */
	GsPluginEventCallback event_callback;
	void *event_user_data;
} EventCallbackData;

static void
event_callback_data_free (EventCallbackData *data)
{
	g_clear_object (&data->plugin);
	g_clear_object (&data->event);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EventCallbackData, event_callback_data_free)

static gboolean
event_callback_idle_cb (void *user_data)
{
	EventCallbackData *data = user_data;

	data->event_callback (data->plugin, data->event, data->event_user_data);
	return G_SOURCE_REMOVE;
}

static void
event_callback_invoke_take (GsPluginFlatpak       *plugin,
                            GsPluginEvent         *event,  /* (transfer full) */
                            GMainContext          *context,
                            GsPluginEventCallback  event_callback,
                            void                  *event_user_data)
{
	g_autoptr(EventCallbackData) event_data = NULL;
	g_autoptr(GsPluginEvent) event_owned = g_steal_pointer (&event);

	g_return_if_fail (event_callback != NULL);

	event_data = g_new0 (EventCallbackData, 1);
	event_data->plugin = GS_PLUGIN (g_object_ref (plugin));
	event_data->event = g_steal_pointer (&event_owned);
	event_data->event_callback = event_callback;
	event_data->event_user_data = event_user_data;

	g_main_context_invoke_full (context, G_PRIORITY_DEFAULT,
				    event_callback_idle_cb, g_steal_pointer (&event_data), (GDestroyNotify) event_callback_data_free);
}

static void refresh_metadata_thread_cb (GTask        *task,
                                        gpointer      source_object,
                                        gpointer      task_data,
                                        GCancellable *cancellable);

static void
gs_plugin_flatpak_refresh_metadata_async (GsPlugin                     *plugin,
                                          guint64                       cache_age_secs,
                                          GsPluginRefreshMetadataFlags  flags,
                                          GsPluginEventCallback         event_callback,
                                          void                         *event_user_data,
                                          GCancellable                 *cancellable,
                                          GAsyncReadyCallback           callback,
                                          gpointer                      user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE);

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_refresh_metadata_async);
	g_task_set_task_data (task, gs_plugin_refresh_metadata_data_new (cache_age_secs, flags, event_callback, event_user_data), (GDestroyNotify) gs_plugin_refresh_metadata_data_free);

	/* Queue a job to get the installed apps. */
	gs_worker_thread_queue (self->worker, get_priority_for_interactivity (interactive),
				refresh_metadata_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
refresh_metadata_thread_cb (GTask        *task,
                            gpointer      source_object,
                            gpointer      task_data,
                            GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPluginRefreshMetadataData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE);

	assert_in_worker (self);

	for (guint i = 0; i < self->installations->len; i++) {
		g_autoptr(GError) local_error = NULL;
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);

		if (!self->has_system_helper && gs_flatpak_get_scope (flatpak) == AS_COMPONENT_SCOPE_SYSTEM)
			continue;

		if (!gs_flatpak_refresh (flatpak, data->cache_age_secs, interactive, data->event_callback, data->event_user_data, cancellable, &local_error))
			g_debug ("Failed to refresh metadata for '%s': %s", gs_flatpak_get_id (flatpak), local_error->message);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_flatpak_refresh_metadata_finish (GsPlugin      *plugin,
                                           GAsyncResult  *result,
                                           GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static GsFlatpak *
gs_plugin_flatpak_get_handler (GsPluginFlatpak *self,
                               GsApp           *app)
{
	const gchar *object_id;

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
		return NULL;

	/* specified an explicit name */
	object_id = gs_flatpak_app_get_object_id (app);
	if (object_id != NULL) {
		for (guint i = 0; i < self->installations->len; i++) {
			GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
			if (g_strcmp0 (gs_flatpak_get_id (flatpak), object_id) == 0)
				return flatpak;
		}
	}

	/* find a scope that matches */
	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		if (_as_component_scope_is_compatible (gs_flatpak_get_scope (flatpak),
						 gs_app_get_scope (app)))
			return flatpak;
	}
	return NULL;
}

static gboolean
gs_plugin_flatpak_refine_app (GsPluginFlatpak             *self,
                              GsApp                       *app,
                              GsPluginRefineRequireFlags   require_flags,
                              gboolean                     interactive,
                              GsPluginEventCallback        event_callback,
                              void                        *event_user_data,
                              GCancellable                *cancellable,
                              GError                     **error)
{
	GsFlatpak *flatpak = NULL;

	/* not us */
	if (gs_app_get_bundle_kind (app) != AS_BUNDLE_KIND_FLATPAK) {
		g_debug ("%s not a package, ignoring", gs_app_get_unique_id (app));
		return TRUE;
	}

	/* we have to look for the app in all GsFlatpak stores */
	if (gs_app_get_scope (app) == AS_COMPONENT_SCOPE_UNKNOWN) {
		for (guint i = 0; i < self->installations->len; i++) {
			GsFlatpak *flatpak_tmp = g_ptr_array_index (self->installations, i);
			g_autoptr(GError) error_local = NULL;
			if (gs_flatpak_refine_app_state (flatpak_tmp, app, interactive, FALSE,
							 event_callback, event_user_data,
							 cancellable, &error_local)) {
				flatpak = flatpak_tmp;
				break;
			} else {
				g_debug ("%s", error_local->message);
			}
		}
	} else {
		flatpak = gs_plugin_flatpak_get_handler (self, app);
	}
	if (flatpak == NULL)
		return TRUE;
	return gs_flatpak_refine_app (flatpak, app, require_flags, interactive, FALSE, event_callback, event_user_data, cancellable, error);
}

static void
unref_nonnull_hash_table (gpointer ptr)
{
	GHashTable *hash_table = ptr;
	if (hash_table != NULL)
		g_hash_table_unref (hash_table);
}

static gboolean
refine_app (GsPluginFlatpak             *self,
            GsApp                       *app,
            GsPluginRefineRequireFlags   require_flags,
            gboolean                     interactive,
            GsPluginEventCallback        event_callback,
            void                        *event_user_data,
            GCancellable                *cancellable,
            GError                     **error)
{
	GS_PROFILER_BEGIN_SCOPED (FlatpakRefineApp, "Flatpak (refine app)", NULL);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
		return TRUE;

	/* get the runtime first */
	if (!gs_plugin_flatpak_refine_app (self, app, require_flags, interactive, event_callback, event_user_data, cancellable, error))
		return FALSE;

	GS_PROFILER_END_SCOPED (FlatpakRefineApp);

	/* the runtime might be installed in a different scope */
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_RUNTIME) {
		GsApp *runtime = gs_app_get_runtime (app);
		if (runtime != NULL) {
			GS_PROFILER_BEGIN_SCOPED (FlatpakRefineAppRuntime, "Flatpak (refine runtime)", NULL);

			if (!gs_plugin_flatpak_refine_app (self, runtime,
							   require_flags,
							   interactive,
							   event_callback,
							   event_user_data,
							   cancellable,
							   error)) {
				return FALSE;
			}

			GS_PROFILER_END_SCOPED (FlatpakRefineAppRuntime);
		}
	}
	return TRUE;
}

static void refine_thread_cb (GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable);

static void
gs_plugin_flatpak_refine_async (GsPlugin                   *plugin,
                                GsAppList                  *list,
                                GsPluginRefineFlags         job_flags,
                                GsPluginRefineRequireFlags  require_flags,
                                GsPluginEventCallback       event_callback,
                                void                       *event_user_data,
                                GCancellable               *cancellable,
                                GAsyncReadyCallback         callback,
                                gpointer                    user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (job_flags & GS_PLUGIN_REFINE_FLAGS_INTERACTIVE) != 0;

	task = gs_plugin_refine_data_new_task (plugin, list, job_flags, require_flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_refine_async);

	/* Queue a job to refine the apps. */
	gs_worker_thread_queue (self->worker, get_priority_for_interactivity (interactive),
				refine_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
refine_thread_cb (GTask        *task,
                  gpointer      source_object,
                  gpointer      task_data,
                  GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPluginRefineData *data = task_data;
	GsAppList *list = data->list;
	GsPluginRefineRequireFlags require_flags = data->require_flags;
	gboolean interactive = (data->job_flags & GS_PLUGIN_REFINE_FLAGS_INTERACTIVE) != 0;
	GsPluginEventCallback event_callback = data->event_callback;
	void *event_user_data = data->event_user_data;
	g_autoptr(GPtrArray) array_components_by_id = NULL; /* (element-type GHashTable) */
	g_autoptr(GPtrArray) array_components_by_bundle = NULL; /* (element-type GHashTable) */
	g_autoptr(GsAppList) app_list = NULL;
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (self, app, require_flags, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

	/* Refine wildcards.
	 *
	 * Use a copy of the list for the loop because a function called
	 * on the plugin may affect the list which can lead to problems
	 * (e.g. inserting an app in the list on every call results in
	 * an infinite loop) */
	app_list = gs_app_list_copy (list);
	array_components_by_id = g_ptr_array_new_full (self->installations->len, unref_nonnull_hash_table);
	g_ptr_array_set_size (array_components_by_id, self->installations->len);
	array_components_by_bundle = g_ptr_array_new_full (self->installations->len, unref_nonnull_hash_table);
	g_ptr_array_set_size (array_components_by_bundle, self->installations->len);

	for (guint j = 0; j < gs_app_list_length (app_list); j++) {
		GsApp *app = gs_app_list_index (app_list, j);

		if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;

		for (guint i = 0; i < self->installations->len; i++) {
			GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
			GHashTable *components_by_id = array_components_by_id->pdata[i];
			GHashTable *components_by_bundle = array_components_by_bundle->pdata[i];

			if (!gs_flatpak_refine_wildcard (flatpak, app, list, require_flags, interactive, &components_by_id, &components_by_bundle,
							 cancellable, &local_error)) {
				g_task_return_error (task, g_steal_pointer (&local_error));
				return;
			}
			array_components_by_id->pdata[i] = components_by_id;
			array_components_by_bundle->pdata[i] = components_by_bundle;
		}
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_flatpak_refine_finish (GsPlugin      *plugin,
                                 GAsyncResult  *result,
                                 GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

/* Run in @worker. */
static void
launch_thread_cb (GTask        *task,
                  gpointer      source_object,
                  gpointer      task_data,
                  GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPluginLaunchData *data = task_data;
	GsFlatpak *flatpak;
	g_autoptr(GError) local_error = NULL;
	gboolean interactive = (data->flags & GS_PLUGIN_LAUNCH_FLAGS_INTERACTIVE) != 0;

	assert_in_worker (self);

	flatpak = gs_plugin_flatpak_get_handler (self, data->app);
	if (flatpak == NULL) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	if (gs_flatpak_launch (flatpak, data->app, interactive, cancellable, &local_error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

static void
gs_plugin_flatpak_launch_async (GsPlugin            *plugin,
                                GsApp               *app,
                                GsPluginLaunchFlags  flags,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_LAUNCH_FLAGS_INTERACTIVE) != 0;

	task = gs_plugin_launch_data_new_task (plugin, app, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_launch_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* Queue a job to launch the app. */
	gs_worker_thread_queue (self->worker, get_priority_for_interactivity (interactive),
				launch_thread_cb, g_steal_pointer (&task));
}

static gboolean
gs_plugin_flatpak_launch_finish (GsPlugin      *plugin,
                                 GAsyncResult  *result,
                                 GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

/* ref full */
static GsApp *
gs_plugin_flatpak_find_app_by_ref (GsPluginFlatpak  *self,
                                   const gchar      *ref,
                                   gboolean          interactive,
                                   GsApp            *alternate_of,
                                   GCancellable     *cancellable,
                                   GError          **error)
{
	g_debug ("finding ref %s", ref);
	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak_tmp = g_ptr_array_index (self->installations, i);
		g_autoptr(GsApp) app = NULL;
		g_autoptr(GError) error_local = NULL;

		app = gs_flatpak_ref_to_app (flatpak_tmp, ref, interactive, cancellable, &error_local);
		if (app == NULL) {
			g_debug ("%s", error_local->message);
			continue;
		}
		if (alternate_of != NULL && alternate_of == app) {
			g_debug ("skipping ref=%s->%s, due to being alternate_of", ref, gs_app_get_unique_id (app));
			continue;
		}
		g_debug ("found ref=%s->%s", ref, gs_app_get_unique_id (app));
		return g_steal_pointer (&app);
	}
	return NULL;
}

/* ref full */
static GsApp *
_ref_to_app (FlatpakTransaction *transaction,
             const gchar        *ref,
             GsPluginFlatpak    *self)
{
	g_return_val_if_fail (GS_IS_FLATPAK_TRANSACTION (transaction), NULL);
	g_return_val_if_fail (ref != NULL, NULL);
	g_return_val_if_fail (GS_IS_PLUGIN_FLATPAK (self), NULL);

	/* search through each GsFlatpak */
	return gs_plugin_flatpak_find_app_by_ref (self, ref,
						  !flatpak_transaction_get_no_interaction (transaction),
						  NULL, NULL, NULL);
}

static void
_group_apps_by_installation_recurse (GsPluginFlatpak *self,
                                     GsAppList       *list,
                                     GHashTable      *applist_by_flatpaks)
{
	if (!list)
		return;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsFlatpak *flatpak = gs_plugin_flatpak_get_handler (self, app);
		if (flatpak != NULL) {
			GsAppList *list_tmp = g_hash_table_lookup (applist_by_flatpaks, flatpak);
			GsAppList *related_list;
			if (list_tmp == NULL) {
				list_tmp = gs_app_list_new ();
				g_hash_table_insert (applist_by_flatpaks,
						     g_object_ref (flatpak),
						     list_tmp);
			}
			gs_app_list_add (list_tmp, app);

			/* Add also related apps, which can be those recognized for update,
			   while the 'app' is already up to date. */
			related_list = gs_app_get_related (app);
			_group_apps_by_installation_recurse (self, related_list, applist_by_flatpaks);
		}
	}
}

/*
 * Returns: (transfer full) (element-type GsFlatpak GsAppList):
 *  a map from GsFlatpak to non-empty lists of apps from @list associated
 *  with that installation.
 */
static GHashTable *
_group_apps_by_installation (GsPluginFlatpak *self,
                             GsAppList       *list)
{
	g_autoptr(GHashTable) applist_by_flatpaks = NULL;

	/* list of apps to be handled by each flatpak installation */
	applist_by_flatpaks = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						     (GDestroyNotify) g_object_unref,
						     (GDestroyNotify) g_object_unref);

	/* put each app into the correct per-GsFlatpak list */
	_group_apps_by_installation_recurse (self, list, applist_by_flatpaks);

	return g_steal_pointer (&applist_by_flatpaks);
}

typedef struct {
	FlatpakTransaction *transaction;
	guint id;
} BasicAuthData;

static void
basic_auth_data_free (BasicAuthData *data)
{
	g_object_unref (data->transaction);
	g_slice_free (BasicAuthData, data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(BasicAuthData, basic_auth_data_free)

static void
_basic_auth_cb (const gchar *user, const gchar *password, gpointer user_data)
{
	g_autoptr(BasicAuthData) data = user_data;

	g_debug ("Submitting basic auth data");

	/* NULL user aborts the basic auth request */
	flatpak_transaction_complete_basic_auth (data->transaction, data->id, user, password, NULL /* options */);
}

static gboolean
_basic_auth_start (FlatpakTransaction *transaction,
                   const char *remote,
                   const char *realm,
                   GVariant *options,
                   guint id,
                   GsPlugin *plugin)
{
	BasicAuthData *data;

	if (flatpak_transaction_get_no_interaction (transaction))
		return FALSE;

	data = g_slice_new0 (BasicAuthData);
	data->transaction = g_object_ref (transaction);
	data->id = id;

	g_debug ("Login required remote %s (realm %s)\n", remote, realm);
	gs_plugin_basic_auth_start (plugin, remote, realm, G_CALLBACK (_basic_auth_cb), data);
	return TRUE;
}

typedef struct {
	GsPluginFlatpak *plugin;  /* (not owned) (not nullable) */
	GMainContext *callback_context;  /* (owned) (not nullable) */
	GsPluginEventCallback event_callback;  /* (nullable) */
	void *event_user_data;
} WebflowStartData;

static void
webflow_start_data_free (WebflowStartData *data)
{
	g_clear_pointer (&data->callback_context, g_main_context_unref);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WebflowStartData, webflow_start_data_free)

static void
webflow_start_data_free_closure (WebflowStartData *data,
                                 GClosure         *closure)
{
	webflow_start_data_free (data);
}

static gboolean
_webflow_start (FlatpakTransaction *transaction,
                const char *remote,
                const char *url,
                GVariant *options,
                guint id,
                void *user_data)
{
	WebflowStartData *data = user_data;
	const char *browser;
	g_autoptr(GError) error_local = NULL;

	if (flatpak_transaction_get_no_interaction (transaction))
		return FALSE;

	g_debug ("Authentication required for remote '%s'", remote);

	/* Allow hard overrides with $BROWSER */
	browser = g_getenv ("BROWSER");
	if (browser != NULL) {
		const char *args[3] = { NULL, url, NULL };
		args[0] = browser;
		if (!g_spawn_async (NULL, (char **)args, NULL, G_SPAWN_SEARCH_PATH,
		                    NULL, NULL, NULL, &error_local)) {
			g_warning ("Failed to start browser %s: %s", browser, error_local->message);

			gs_flatpak_error_convert (&error_local);

			if (data->event_callback != NULL) {
				g_autoptr(GsPluginEvent) event = NULL;

				event = gs_plugin_event_new ("error", error_local,
							     NULL);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING | GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				event_callback_invoke_take (data->plugin, g_steal_pointer (&event), data->callback_context,
							    data->event_callback, data->event_user_data);
			}

			return FALSE;
		}
	} else {
		if (!g_app_info_launch_default_for_uri (url, NULL, &error_local)) {
			g_warning ("Failed to show url: %s", error_local->message);

			gs_flatpak_error_convert (&error_local);

			if (data->event_callback != NULL) {
				g_autoptr(GsPluginEvent) event = NULL;

				event = gs_plugin_event_new ("error", error_local,
							     NULL);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING | GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				event_callback_invoke_take (data->plugin, g_steal_pointer (&event), data->callback_context,
							    data->event_callback, data->event_user_data);
			}

			return FALSE;
		}
	}

	g_debug ("Waiting for browser...");

	return TRUE;
}

static void
_webflow_done (FlatpakTransaction *transaction,
               GVariant *options,
               guint id,
               GsPlugin *plugin)
{
	g_debug ("Browser done");
}

/* This can only fail if flatpak_dir_ensure_repo() fails, for example if the
 * repo is configured but doesn’t exist and can’t be created on disk. */
static FlatpakTransaction *
_build_transaction (GsPluginFlatpak        *plugin,
                    GsFlatpak              *flatpak,
                    gboolean                stop_on_first_error,
                    gboolean                interactive,
                    GMainContext           *callback_context,
                    GsPluginEventCallback   event_callback,
                    void                   *event_user_data,
                    GCancellable           *cancellable,
                    GError                **error)
{
	FlatpakInstallation *installation;
	g_autoptr(FlatpakInstallation) installation_clone = NULL;
	g_autoptr(FlatpakTransaction) transaction = NULL;
	g_autoptr(WebflowStartData) webflow_start_data = NULL;

	installation = gs_flatpak_get_installation (flatpak, interactive);

	installation_clone = g_object_ref (installation);

	/* create transaction */
	transaction = gs_flatpak_transaction_new (installation_clone, stop_on_first_error, cancellable, error);
	if (transaction == NULL) {
		g_prefix_error (error, "failed to build transaction: ");
		gs_flatpak_error_convert (error);
		return NULL;
	}

	/* Let flatpak know if it is a background operation */
	flatpak_transaction_set_no_interaction (transaction, !interactive);

	/* connect up signals */
	webflow_start_data = g_new0 (WebflowStartData, 1);
	webflow_start_data->plugin = plugin;
	webflow_start_data->callback_context = g_main_context_ref (callback_context);
	webflow_start_data->event_callback = event_callback;
	webflow_start_data->event_user_data = event_user_data;

	g_signal_connect (transaction, "ref-to-app",
			  G_CALLBACK (_ref_to_app), plugin);
	g_signal_connect (transaction, "basic-auth-start",
			  G_CALLBACK (_basic_auth_start), plugin);
	g_signal_connect_data (transaction, "webflow-start",
			       G_CALLBACK (_webflow_start),
			       g_steal_pointer (&webflow_start_data),
			       (GClosureNotify) webflow_start_data_free_closure,
			       G_CONNECT_DEFAULT);
	g_signal_connect (transaction, "webflow-done",
			  G_CALLBACK (_webflow_done), plugin);

	/* use system installations as dependency sources for user installations */
	flatpak_transaction_add_default_dependency_sources (transaction);

	return g_steal_pointer (&transaction);
}

static void
remove_schedule_entry (gpointer schedule_entry_handle)
{
	g_autoptr(GError) error_local = NULL;

	if (!gs_metered_remove_from_download_scheduler (schedule_entry_handle, NULL, &error_local))
		g_warning ("Failed to remove schedule entry: %s", error_local->message);
}

static void update_apps_thread_cb (GTask        *task,
                                   gpointer      source_object,
                                   gpointer      task_data,
                                   GCancellable *cancellable);

static void
gs_plugin_flatpak_update_apps_async (GsPlugin                           *plugin,
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
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE);

	task = gs_plugin_update_apps_data_new_task (plugin, apps, flags,
						    progress_callback, progress_user_data,
						    event_callback, event_user_data,
						    app_needs_user_action_callback, app_needs_user_action_data,
						    cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_update_apps_async);

	/* Queue a job to get the apps. */
	gs_worker_thread_queue (self->long_running_worker, get_priority_for_interactivity (interactive),
				update_apps_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
update_apps_thread_cb (GTask        *task,
                       gpointer      source_object,
                       gpointer      task_data,
                       GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPluginUpdateAppsData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE);
	g_autoptr(GHashTable) applist_by_flatpaks = NULL;
	GHashTableIter iter;
	gpointer key, value;
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	/* Mark all the apps as pending installation. While the op/progress
	 * handling code in #GsFlatpakTransaction does this more accurately and
	 * in more detail, we need to pre-emptively do it here, since multiple
	 * transactions are run sequentially below. That means that all the apps
	 * from the 2nd, 3rd, etc. transactions will not have their state
	 * updated until that transaction is prepared. That’s a long time for
	 * the apps to look like they’ve been left out of the update in the UI. */
	applist_by_flatpaks = _group_apps_by_installation (self, data->apps);

	g_hash_table_iter_init (&iter, applist_by_flatpaks);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GsAppList *apps_for_installation = GS_APP_LIST (value);

		for (guint i = 0; i < gs_app_list_length (apps_for_installation); i++) {
			GsApp *app = gs_app_list_index (apps_for_installation, i);

			gs_app_set_state (app, GS_APP_STATE_INSTALLING);
		}
	}

	/* build and run transaction for each flatpak installation */
	g_hash_table_iter_init (&iter, applist_by_flatpaks);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GsFlatpak *flatpak = GS_FLATPAK (key);
		GsAppList *list_tmp = GS_APP_LIST (value);
		g_autoptr(FlatpakTransaction) transaction = NULL;
		gpointer schedule_entry_handle = NULL;

		g_assert (GS_IS_FLATPAK (flatpak));
		g_assert (list_tmp != NULL);
		g_assert (gs_app_list_length (list_tmp) > 0);

		if (!interactive) {
			if (!gs_metered_block_app_list_on_download_scheduler (list_tmp, &schedule_entry_handle, cancellable, &local_error)) {
				g_warning ("Failed to block on download scheduler: %s",
					   local_error->message);
				g_clear_error (&local_error);
			}
		}

		/* Now apply the updates. */
		gs_flatpak_set_busy (flatpak, TRUE);

		/* Build and run transaction. Pass %FALSE to stop_on_first_error
		 * so that the transaction continues past the first fatal error
		 * in an attempt to try and update as many apps as possible.
		 *
		 * Internally, `FlatpakTransaction` uses `op->fail_if_op_fails`
		 * and `op->non_fatal` to track the relationships between ops
		 * (such as updating an app and its runtime, or add-ons and
		 * their app). If, for example, updating a runtime fails, the
		 * ops to update apps which use that runtime will automatically
		 * be skipped and will fail with `FLATPAK_ERROR_SKIPPED`.
		 *
		 * %GS_FLATPAK_ERROR_MODE_IGNORE_ERRORS does not ignore
		 * `FLATPAK_ERROR_SKIPPED` errors, so this will not cause
		 * corruption of the transaction.
		 *
		 * This approach is the same as what the `flatpak` CLI uses in
		 * `flatpak-builtins-update.c` in flatpak.
		 */
		transaction = _build_transaction (self, flatpak, GS_FLATPAK_ERROR_MODE_IGNORE_ERRORS, interactive, g_task_get_context (task), data->event_callback, data->event_user_data, cancellable, &local_error);
		if (transaction == NULL) {
			g_autoptr(GsPluginEvent) event = NULL;

			/* Reset the state of all the apps in this transaction. */
			for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
				GsApp *app = gs_app_list_index (list_tmp, i);
				gs_app_set_state_recover (app);
			}

			/* This can only fail if the repo doesn’t exist and can’t
			 * be created, which is unlikely. */
			gs_flatpak_error_convert (&local_error);

			if (data->event_callback != NULL) {
				event = gs_plugin_event_new ("error", local_error,
							     NULL);
				if (interactive)
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
				event_callback_invoke_take (self, g_steal_pointer (&event), g_task_get_context (task),
							    data->event_callback, data->event_user_data);
			}

			g_clear_error (&local_error);

			remove_schedule_entry (schedule_entry_handle);
			gs_flatpak_set_busy (flatpak, FALSE);

			continue;
		}

		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);
			g_autofree gchar *ref = NULL;

			ref = gs_flatpak_app_get_ref_display (app);
			if (flatpak_transaction_add_update (transaction, ref, NULL, NULL, &local_error)) {
				/* add to the transaction cache for quick look up -- other unrelated
				 * refs will be matched using gs_plugin_flatpak_find_app_by_ref() */
				gs_flatpak_transaction_add_app (transaction, app);

				continue;
			}

			/* Errors are not fatal, as otherwise a single app
			 * failure will take down the whole update, blocking
			 * updates for all other apps.
			 *
			 * The common two errors to see here are
			 *  - FLATPAK_ERROR_REMOTE_NOT_FOUND
			 *  - FLATPAK_ERROR_NOT_INSTALLED
			 */
			{
				g_autoptr(GsPluginEvent) event = NULL;

				g_warning ("Skipping update for ‘%s’: %s", ref, local_error->message);

				/* Reset the state of the app. */
				gs_app_set_state_recover (app);

				gs_flatpak_error_convert (&local_error);

				if (data->event_callback != NULL) {
					event = gs_plugin_event_new ("error", local_error,
								     "app", app,
								     NULL);
					if (interactive)
						gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
					event_callback_invoke_take (self, g_steal_pointer (&event), g_task_get_context (task),
								    data->event_callback, data->event_user_data);
				}

				g_clear_error (&local_error);
				continue;
			}
		}

		/* automatically clean up unused EOL runtimes when updating */
		flatpak_transaction_set_include_unused_uninstall_ops (transaction, TRUE);

		/* FIXME: Link progress reporting from #FlatpakTransaction
		 * up to `data->progress_callback`. */
		if (!gs_flatpak_transaction_run (transaction, cancellable, &local_error)) {
			g_autoptr(GError) prune_error = NULL;

			/* Reset the state of all the apps in this transaction. */
			for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
				GsApp *app = gs_app_list_index (list_tmp, i);
				gs_app_set_state_recover (app);
			}

			/* Try pruning the repo, just in case this is a failure
			 * caused by running out of disk space. The transaction
			 * typically won’t try this itself, and will only prune
			 * on success (if it knows an update has potentially
			 * left dangling objects). */
			if (!flatpak_installation_prune_local_repo (gs_flatpak_get_installation (flatpak, interactive),
								    NULL, &prune_error)) {
				gs_flatpak_error_convert (&prune_error);
				g_warning ("Error pruning flatpak repo for %s after failed update: %s",
					   gs_flatpak_get_id (flatpak), prune_error->message);
			}

			gs_flatpak_error_convert (&local_error);

			if (data->event_callback != NULL) {
				g_autoptr(GsPluginEvent) event = NULL;

				event = gs_plugin_event_new ("error", local_error,
							     NULL);
				if (interactive)
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
				event_callback_invoke_take (self, g_steal_pointer (&event), g_task_get_context (task),
							    data->event_callback, data->event_user_data);
			}

			g_clear_error (&local_error);

			remove_schedule_entry (schedule_entry_handle);
			gs_flatpak_set_busy (flatpak, FALSE);

			continue;
		}

		remove_schedule_entry (schedule_entry_handle);
		gs_plugin_updates_changed (GS_PLUGIN (self));

		/* Get any new state. Ignore failure and fall through to
		 * refining the apps, since refreshing is not an entirely
		 * necessary part of the update operation. */
		if (!gs_flatpak_refresh (flatpak, G_MAXUINT, interactive, data->event_callback, data->event_user_data, cancellable, &local_error)) {
			gs_flatpak_error_convert (&local_error);
			g_warning ("Error refreshing flatpak data for ‘%s’ after update: %s",
				   gs_flatpak_get_id (flatpak), local_error->message);
			g_clear_error (&local_error);
		}

		/* Refine all the updated apps to make sure they’re up to date
		 * in the UI. Ignore failure since it’s not an entirely
		 * necessary part of the update operation. */
		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);
			g_autofree gchar *ref = NULL;

			ref = gs_flatpak_app_get_ref_display (app);
			if (!gs_flatpak_refine_app (flatpak, app,
						    GS_PLUGIN_REFINE_REQUIRE_FLAGS_RUNTIME,
						    interactive, TRUE,
						    data->event_callback, data->event_user_data,
						    cancellable, &local_error)) {
				gs_flatpak_error_convert (&local_error);
				g_warning ("Error refining app ‘%s’ after update: %s", ref, local_error->message);
				g_clear_error (&local_error);
				continue;
			}
		}

		gs_flatpak_set_busy (flatpak, FALSE);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_flatpak_update_apps_finish (GsPlugin      *plugin,
                                      GAsyncResult  *result,
                                      GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_flatpak_cover_addons_in_transaction (GsPluginFlatpak       *plugin,
                                        FlatpakTransaction    *transaction,
                                        GsApp                 *parent_app,
                                        GsAppState             state,
                                        gboolean               interactive,
                                        GMainContext          *callback_context,
                                        GsPluginEventCallback  event_callback,
                                        void                  *event_user_data)
{
	g_autoptr(GsAppList) addons = NULL;
	g_autoptr(GString) errors = NULL;
	guint ii, sz;

	g_return_if_fail (transaction != NULL);
	g_return_if_fail (GS_IS_APP (parent_app));

	addons = gs_app_dup_addons (parent_app);
	sz = addons ? gs_app_list_length (addons) : 0;

	for (ii = 0; ii < sz; ii++) {
		GsApp *addon = gs_app_list_index (addons, ii);
		g_autoptr(GError) local_error = NULL;

		if (state == GS_APP_STATE_INSTALLING && gs_app_get_to_be_installed (addon)) {
			g_autofree gchar *ref = NULL;

			ref = gs_flatpak_app_get_ref_display (addon);
			if (flatpak_transaction_add_install (transaction, gs_app_get_origin (addon), ref, NULL, &local_error)) {
				gs_app_set_state (addon, state);
			} else {
				if (errors)
					g_string_append_c (errors, '\n');
				else
					errors = g_string_new (NULL);
				g_string_append_printf (errors, _("Failed to add to install for addon ‘%s’: %s"),
					gs_app_get_name (addon), local_error->message);
			}
		} else if (state == GS_APP_STATE_REMOVING && gs_app_get_state (addon) == GS_APP_STATE_INSTALLED) {
			g_autofree gchar *ref = NULL;

			ref = gs_flatpak_app_get_ref_display (addon);
			if (flatpak_transaction_add_uninstall (transaction, ref, &local_error)) {
				gs_app_set_state (addon, state);
			} else {
				if (errors)
					g_string_append_c (errors, '\n');
				else
					errors = g_string_new (NULL);
				g_string_append_printf (errors, _("Failed to add to uninstall for addon ‘%s’: %s"),
					gs_app_get_name (addon), local_error->message);
			}
		}
	}

	if (errors && event_callback != NULL) {
		g_autoptr(GsPluginEvent) event = NULL;
		g_autoptr(GError) error_local = g_error_new_literal (GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			errors->str);

		event = gs_plugin_event_new ("error", error_local,
					     NULL);
		if (interactive)
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);

		event_callback_invoke_take (plugin, g_steal_pointer (&event), callback_context,
					    event_callback, event_user_data);
	}
}

static void uninstall_apps_thread_cb (GTask        *task,
                                      gpointer      source_object,
                                      gpointer      task_data,
                                      GCancellable *cancellable);

static void
gs_plugin_flatpak_uninstall_apps_async (GsPlugin                           *plugin,
                                        GsAppList                          *apps,
                                        GsPluginUninstallAppsFlags          flags,
                                        GsPluginProgressCallback            progress_callback,
                                        gpointer                            progress_user_data,
                                        GsPluginEventCallback               event_callback,
                                        gpointer                            event_user_data,
                                        GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                        gpointer                            app_needs_user_action_data,
                                        GCancellable                       *cancellable,
                                        GAsyncReadyCallback                 callback,
                                        gpointer                            user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_UNINSTALL_APPS_FLAGS_INTERACTIVE);

	task = gs_plugin_uninstall_apps_data_new_task (plugin, apps, flags,
						       progress_callback, progress_user_data,
						       event_callback, event_user_data,
						       app_needs_user_action_callback, app_needs_user_action_data,
						       cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_uninstall_apps_async);

	/* Queue a job to uninstall the apps. */
	gs_worker_thread_queue (self->long_running_worker, get_priority_for_interactivity (interactive),
				uninstall_apps_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
uninstall_apps_thread_cb (GTask        *task,
                          gpointer      source_object,
                          gpointer      task_data,
                          GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPluginUninstallAppsData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_UNINSTALL_APPS_FLAGS_INTERACTIVE);
	g_autoptr(GHashTable) applist_by_flatpaks = NULL;
	GHashTableIter iter;
	gpointer key, value;
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	/* Mark all the apps as pending uninstallation. While the op/progress
	 * handling code in #GsFlatpakTransaction does this more accurately and
	 * in more detail, we need to pre-emptively do it here, since multiple
	 * transactions are run sequentially below. That means that all the apps
	 * from the 2nd, 3rd, etc. transactions will not have their state
	 * updated until that transaction is prepared. That’s a long time for
	 * the apps to look like they’ve been left out of the uninstall in the UI. */
	applist_by_flatpaks = _group_apps_by_installation (self, data->apps);

	g_hash_table_iter_init (&iter, applist_by_flatpaks);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GsAppList *apps_for_installation = GS_APP_LIST (value);

		for (guint i = 0; i < gs_app_list_length (apps_for_installation); i++) {
			GsApp *app = gs_app_list_index (apps_for_installation, i);

			gs_app_set_state (app, GS_APP_STATE_REMOVING);
		}
	}

	/* build and run transaction for each flatpak installation */
	g_hash_table_iter_init (&iter, applist_by_flatpaks);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GsFlatpak *flatpak = GS_FLATPAK (key);
		GsAppList *list_tmp = GS_APP_LIST (value);
		g_autoptr(FlatpakTransaction) transaction = NULL;

		g_assert (GS_IS_FLATPAK (flatpak));
		g_assert (list_tmp != NULL);
		g_assert (gs_app_list_length (list_tmp) > 0);

		gs_flatpak_set_busy (flatpak, TRUE);

		/* build */
		transaction = _build_transaction (self, flatpak, GS_FLATPAK_ERROR_MODE_STOP_ON_FIRST_ERROR, interactive, g_task_get_context (task), data->event_callback, data->event_user_data, cancellable, &local_error);
		if (transaction == NULL) {
			/* Reset the state of all the apps in this transaction. */
			for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
				GsApp *app = gs_app_list_index (list_tmp, i);
				gs_app_set_state_recover (app);
			}

			/* This can only fail if the repo doesn’t exist and can’t
			 * be created, which is unlikely. */
			gs_flatpak_error_convert (&local_error);

			if (data->event_callback != NULL) {
				g_autoptr(GsPluginEvent) event = NULL;

				event = gs_plugin_event_new ("error", local_error,
							     NULL);
				if (interactive)
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);

				event_callback_invoke_take (self, g_steal_pointer (&event), g_task_get_context (task),
							    data->event_callback, data->event_user_data);
			}

			g_clear_error (&local_error);

			gs_flatpak_set_busy (flatpak, FALSE);

			continue;
		}

		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);
			g_autofree char *ref = NULL;

			/* not supported */
			if (gs_plugin_flatpak_get_handler (self, app) == NULL)
				continue;

			/* is a source, handled by dedicated function */
			g_assert (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY);

			/* add to the transaction cache for quick look up -- other unrelated
			 * refs will be matched using gs_plugin_flatpak_find_app_by_ref() */
			gs_flatpak_transaction_add_app (transaction, app);

			/* add to the transaction */
			ref = gs_flatpak_app_get_ref_display (app);

			if (!flatpak_transaction_add_uninstall (transaction, ref, &local_error)) {
				/* Somehow, the app might already be uninstalled. */
				if (g_error_matches (local_error, FLATPAK_ERROR,
						     FLATPAK_ERROR_NOT_INSTALLED)) {
					g_clear_error (&local_error);
				}
			}

			/* Reset state if adding the app to the transaction failed. */
			if (local_error != NULL) {
				/* Reset the state of all the apps in this transaction. */
				for (guint j = 0; j < gs_app_list_length (list_tmp); j++) {
					GsApp *recover_app = gs_app_list_index (list_tmp, j);
					gs_app_set_state_recover (recover_app);
				}

				gs_flatpak_error_convert (&local_error);

				if (data->event_callback != NULL) {
					g_autoptr(GsPluginEvent) event = NULL;

					event = gs_plugin_event_new ("error", local_error,
								     NULL);
					if (interactive)
						gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);

					event_callback_invoke_take (self, g_steal_pointer (&event), g_task_get_context (task),
								    data->event_callback, data->event_user_data);
				}

				g_clear_error (&local_error);

				gs_flatpak_set_busy (flatpak, FALSE);

				continue;
			}

			gs_flatpak_cover_addons_in_transaction (self, transaction, app, GS_APP_STATE_REMOVING, interactive,
								g_task_get_context (task),
								data->event_callback,
								data->event_user_data);
		}

		/* run transaction */
		/* FIXME: Link progress reporting from #FlatpakTransaction
		 * up to `data->progress_callback`. */
		if (!gs_flatpak_transaction_run (transaction, cancellable, &local_error)) {
			GsApp *error_app = NULL;

			gs_flatpak_transaction_get_error_operation (GS_FLATPAK_TRANSACTION (transaction), &error_app);

			/* Reset the state of all the apps in this transaction. */
			for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
				GsApp *app = gs_app_list_index (list_tmp, i);
				gs_app_set_state_recover (app);
			}

			/* Somehow, the app might already be uninstalled. */
			if (g_error_matches (local_error, FLATPAK_ERROR,
					     FLATPAK_ERROR_NOT_INSTALLED)) {
				g_clear_error (&local_error);

				/* Set the app back to UNKNOWN so that refining it gets all the right details. */
				if (error_app != NULL) {
					g_debug ("App %s is already uninstalled", gs_app_get_unique_id (error_app));
					gs_app_set_state (error_app, GS_APP_STATE_UNKNOWN);
				}
			} else {
				gs_flatpak_error_convert (&local_error);

				if (data->event_callback != NULL) {
					g_autoptr(GsPluginEvent) event = NULL;

					event = gs_plugin_event_new ("error", local_error,
								     NULL);
					if (interactive)
						gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);

					event_callback_invoke_take (self, g_steal_pointer (&event), g_task_get_context (task),
								    data->event_callback, data->event_user_data);
				}

				g_clear_error (&local_error);

				gs_flatpak_set_busy (flatpak, FALSE);

				continue;
			}
		}

		/* Get any new state. Ignore failure and fall through to
		 * refining the apps, since refreshing is not an entirely
		 * necessary part of the uninstall operation. */
		if (!gs_flatpak_refresh (flatpak, G_MAXUINT, interactive, data->event_callback, data->event_user_data, cancellable, &local_error)) {
			gs_flatpak_error_convert (&local_error);
			g_debug ("Error refreshing flatpak data for ‘%s’ after uninstall: %s",
				 gs_flatpak_get_id (flatpak), local_error->message);
			g_clear_error (&local_error);
		}

		/* Refine all the uninstalled apps to make sure they’re up to date
		 * in the UI. Ignore failure since it’s not an entirely
		 * necessary part of the uninstall operation. */
		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);
			g_autofree gchar *ref = NULL;

			gs_app_set_size_download (app, GS_SIZE_TYPE_UNKNOWN, 0);
			gs_app_set_size_installed (app, GS_SIZE_TYPE_UNKNOWN, 0);

			ref = gs_flatpak_app_get_ref_display (app);
			if (!gs_flatpak_refine_app (flatpak, app,
						    GS_PLUGIN_REFINE_REQUIRE_FLAGS_ID |
						    GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN |
						    GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION,
						    interactive, FALSE,
						    data->event_callback, data->event_user_data,
						    cancellable, &local_error)) {
				gs_flatpak_error_convert (&local_error);
				g_debug ("Error refining app ‘%s’ after uninstall: %s", ref, local_error->message);
				g_clear_error (&local_error);
				continue;
			}

			gs_flatpak_refine_addons (flatpak,
						  app,
						  GS_PLUGIN_REFINE_REQUIRE_FLAGS_ID,
						  GS_APP_STATE_REMOVING,
						  interactive,
						  data->event_callback,
						  data->event_user_data,
						  cancellable);
		}

		gs_flatpak_set_busy (flatpak, FALSE);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_flatpak_uninstall_apps_finish (GsPlugin      *plugin,
                                         GAsyncResult  *result,
                                         GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
app_has_local_source (GsApp *app)
{
	const gchar *url = gs_app_get_origin_hostname (app);

	if (gs_flatpak_app_get_file_kind (app) == GS_FLATPAK_APP_FILE_KIND_BUNDLE)
		return TRUE;

	if (gs_flatpak_app_get_file_kind (app) == GS_FLATPAK_APP_FILE_KIND_REF &&
	    g_strcmp0 (url, "localhost") == 0)
		return TRUE;

	return FALSE;
}

static void
gs_plugin_flatpak_ensure_scope (GsPlugin *plugin,
				GsApp *app)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);

	if (gs_app_get_scope (app) == AS_COMPONENT_SCOPE_UNKNOWN) {
		g_autoptr(GSettings) settings = g_settings_new ("org.gnome.software");

		/* get the new GsFlatpak for handling of local files */
		gs_app_set_scope (app, g_settings_get_boolean (settings, "install-bundles-system-wide") ?
					AS_COMPONENT_SCOPE_SYSTEM : AS_COMPONENT_SCOPE_USER);
		if (!self->has_system_helper) {
			g_info ("no flatpak system helper is available, using user");
			gs_app_set_scope (app, AS_COMPONENT_SCOPE_USER);
		}
		if (self->destdir_for_tests != NULL) {
			g_debug ("in self tests, using user");
			gs_app_set_scope (app, AS_COMPONENT_SCOPE_USER);
		}
	}
}

static void install_apps_thread_cb (GTask        *task,
                                    gpointer      source_object,
                                    gpointer      task_data,
                                    GCancellable *cancellable);

static void
gs_plugin_flatpak_install_apps_async (GsPlugin                           *plugin,
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
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE);

	task = gs_plugin_install_apps_data_new_task (plugin, apps, flags,
						     progress_callback, progress_user_data,
						     event_callback, event_user_data,
						     app_needs_user_action_callback, app_needs_user_action_data,
						     cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_install_apps_async);

	/* Queue a job to install the apps. */
	gs_worker_thread_queue (self->long_running_worker, get_priority_for_interactivity (interactive),
				install_apps_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
install_apps_thread_cb (GTask        *task,
                        gpointer      source_object,
                        gpointer      task_data,
                        GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPlugin *plugin = GS_PLUGIN (self);
	GsPluginInstallAppsData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE);
	g_autoptr(GHashTable) applist_by_flatpaks = NULL;
	GHashTableIter iter;
	gpointer key, value;
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	/* Mark all the apps as pending installation. While the op/progress
	 * handling code in #GsFlatpakTransaction does this more accurately and
	 * in more detail, we need to pre-emptively do it here, since multiple
	 * transactions are run sequentially below. That means that all the apps
	 * from the 2nd, 3rd, etc. transactions will not have their state
	 * updated until that transaction is prepared. That’s a long time for
	 * the apps to look like they’ve been left out of the install in the UI. */
	applist_by_flatpaks = _group_apps_by_installation (self, data->apps);

	g_hash_table_iter_init (&iter, applist_by_flatpaks);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GsAppList *apps_for_installation = GS_APP_LIST (value);

		for (guint i = 0; i < gs_app_list_length (apps_for_installation); i++) {
			GsApp *app = gs_app_list_index (apps_for_installation, i);

			gs_app_set_state (app, GS_APP_STATE_INSTALLING);
		}
	}

	/* build and run transaction for each flatpak installation */
	g_hash_table_iter_init (&iter, applist_by_flatpaks);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GsFlatpak *flatpak = GS_FLATPAK (key);
		GsAppList *list_tmp = GS_APP_LIST (value);
		g_autoptr(FlatpakTransaction) transaction = NULL;
		gpointer schedule_entry_handle = NULL;

		g_assert (GS_IS_FLATPAK (flatpak));
		g_assert (list_tmp != NULL);
		g_assert (gs_app_list_length (list_tmp) > 0);

		if (!interactive) {
			if (!(data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_NO_DOWNLOAD) &&
			    !gs_metered_block_app_list_on_download_scheduler (list_tmp, &schedule_entry_handle, cancellable, &local_error)) {
				g_warning ("Failed to block on download scheduler: %s",
					   local_error->message);
				g_clear_error (&local_error);
			}
		}

		gs_flatpak_set_busy (flatpak, TRUE);

		/* build */
		transaction = _build_transaction (self, flatpak, GS_FLATPAK_ERROR_MODE_STOP_ON_FIRST_ERROR, interactive, g_task_get_context (task), data->event_callback, data->event_user_data, cancellable, &local_error);
		if (transaction == NULL) {
			/* Reset the state of all the apps in this transaction. */
			for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
				GsApp *app = gs_app_list_index (list_tmp, i);
				gs_app_set_state_recover (app);
			}

			/* This can only fail if the repo doesn’t exist and can’t
			 * be created, which is unlikely. */
			gs_flatpak_error_convert (&local_error);

			if (data->event_callback != NULL) {
				g_autoptr(GsPluginEvent) event = NULL;

				event = gs_plugin_event_new ("error", local_error,
							     NULL);
				if (interactive)
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);

				event_callback_invoke_take (self, g_steal_pointer (&event), g_task_get_context (task),
							    data->event_callback, data->event_user_data);
			}

			g_clear_error (&local_error);

			remove_schedule_entry (schedule_entry_handle);
			gs_flatpak_set_busy (flatpak, FALSE);

			continue;
		}

		/* Apply flags to the transaction. */
		if (data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_NO_DOWNLOAD)
			flatpak_transaction_set_no_pull (transaction, TRUE);
		if (data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_NO_APPLY)
			flatpak_transaction_set_no_deploy (transaction, TRUE);

		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);

			/* queue for install if installation needs the network */
			if (!app_has_local_source (app) &&
			    !gs_plugin_get_network_available (plugin)) {
				gs_app_set_state (app, GS_APP_STATE_QUEUED_FOR_INSTALL);
				continue;
			}

			/* set the app scope */
			gs_plugin_flatpak_ensure_scope (plugin, app);

			/* not supported */
			if (gs_plugin_flatpak_get_handler (self, app) == NULL)
				continue;

			/* is a source, handled by dedicated function */
			g_assert (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY);

			/* add to the transaction cache for quick look up -- other unrelated
			 * refs will be matched using gs_plugin_flatpak_find_app_by_ref() */
			gs_flatpak_transaction_add_app (transaction, app);

			/* add flatpakref */
			if (gs_flatpak_app_get_file_kind (app) == GS_FLATPAK_APP_FILE_KIND_REF) {
				GFile *file = gs_app_get_local_file (app);

				if (file == NULL) {
					g_set_error (&local_error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_NOT_SUPPORTED,
						     "no local file set for bundle %s",
						     gs_app_get_unique_id (app));
				} else {
					g_autoptr(GBytes) blob = g_file_load_bytes (file, cancellable, NULL, &local_error);

					if (blob != NULL)
						flatpak_transaction_add_install_flatpakref (transaction, blob, &local_error);
				}

			/* add bundle */
			} else if (gs_flatpak_app_get_file_kind (app) == GS_FLATPAK_APP_FILE_KIND_BUNDLE) {
				GFile *file = gs_app_get_local_file (app);

				if (file == NULL) {
					g_set_error (&local_error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_NOT_SUPPORTED,
						     "no local file set for bundle %s",
						     gs_app_get_unique_id (app));
				} else {
					flatpak_transaction_add_install_bundle (transaction, file, NULL, &local_error);
				}

			/* add normal ref */
			} else {
				g_autofree gchar *ref = gs_flatpak_app_get_ref_display (app);

				if (!flatpak_transaction_add_install (transaction,
								      gs_app_get_origin (app),
								      ref, NULL, &local_error)) {
					/* Somehow, the app might already be installed. */
					if (g_error_matches (local_error, FLATPAK_ERROR,
							     FLATPAK_ERROR_ALREADY_INSTALLED)) {
						g_clear_error (&local_error);
					}
				}
			}

			/* Reset state if adding the app to the transaction failed. */
			if (local_error != NULL) {
				/* Reset the state of the failed app */
				gs_app_set_state_recover (app);

				gs_flatpak_error_convert (&local_error);

				if (data->event_callback != NULL) {
					g_autoptr(GsPluginEvent) event = NULL;

					event = gs_plugin_event_new ("error", local_error,
								     "app", app,
								     NULL);
					if (interactive)
						gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);

					event_callback_invoke_take (self, g_steal_pointer (&event), g_task_get_context (task),
								    data->event_callback, data->event_user_data);
				}

				g_clear_error (&local_error);

				continue;
			}

			gs_flatpak_cover_addons_in_transaction (self, transaction, app, GS_APP_STATE_INSTALLING, interactive,
								g_task_get_context (task),
								data->event_callback,
								data->event_user_data);
		}

		/* run transaction */
		/* FIXME: Link progress reporting from #FlatpakTransaction
		 * up to `data->progress_callback`. */
		if (!gs_flatpak_transaction_run (transaction, cancellable, &local_error)) {
			GsApp *error_app = NULL;

			gs_flatpak_transaction_get_error_operation (GS_FLATPAK_TRANSACTION (transaction), &error_app);

			/* Reset the state of all the apps in this transaction. */
			for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
				GsApp *app = gs_app_list_index (list_tmp, i);
				gs_app_set_state_recover (app);
			}

			/* Somehow, the app might already be installed. */
			if (g_error_matches (local_error, FLATPAK_ERROR,
					     FLATPAK_ERROR_ALREADY_INSTALLED)) {
				g_clear_error (&local_error);

				/* Set the app back to UNKNOWN so that refining it gets all the right details. */
				if (error_app != NULL) {
					g_debug ("App %s is already installed", gs_app_get_unique_id (error_app));
					gs_app_set_state (error_app, GS_APP_STATE_UNKNOWN);
				}
			} else {
				if (error_app != NULL &&
				    g_error_matches (local_error, FLATPAK_ERROR, FLATPAK_ERROR_REF_NOT_FOUND)) {
					const gchar *origin = gs_app_get_origin (error_app);
					if (origin != NULL) {
						g_autoptr(FlatpakRemote) remote = NULL;
						remote = flatpak_installation_get_remote_by_name (gs_flatpak_get_installation (flatpak, interactive),
												  origin, cancellable, NULL);
						if (remote != NULL) {
							g_autofree gchar *filter = flatpak_remote_get_filter (remote);
							if (filter != NULL && *filter != '\0') {
								/* It's a filtered remote, create a user friendly error message for it */
								g_autoptr(GError) error_tmp = NULL;
								g_set_error (&error_tmp, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
									     _("Remote “%s” doesn't allow install of “%s”, possibly due to its filter. Remove the filter and repeat the install. Detailed error: %s"),
									     flatpak_remote_get_title (remote),
									     gs_app_get_name (error_app),
									     local_error->message);
								g_clear_error (&local_error);
								local_error = g_steal_pointer (&error_tmp);
							}
						}
					}
				}

				gs_flatpak_error_convert (&local_error);

				if (data->event_callback != NULL) {
					g_autoptr(GsPluginEvent) event = NULL;

					event = gs_plugin_event_new ("error", local_error,
								     "app", error_app,
								     NULL);
					if (interactive)
						gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
					gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);

					event_callback_invoke_take (self, g_steal_pointer (&event), g_task_get_context (task),
								    data->event_callback, data->event_user_data);
				}

				g_clear_error (&local_error);

				remove_schedule_entry (schedule_entry_handle);
				gs_flatpak_set_busy (flatpak, FALSE);

				continue;
			}
		}

		remove_schedule_entry (schedule_entry_handle);

		/* Get any new state. Ignore failure and fall through to
		 * refining the apps, since refreshing is not an entirely
		 * necessary part of the install operation. */
		if (!(data->flags & GS_PLUGIN_INSTALL_APPS_FLAGS_NO_DOWNLOAD) &&
		    !gs_flatpak_refresh (flatpak, G_MAXUINT, interactive, data->event_callback, data->event_user_data, cancellable, &local_error)) {
			gs_flatpak_error_convert (&local_error);
			g_warning ("Error refreshing flatpak data for ‘%s’ after install: %s",
				   gs_flatpak_get_id (flatpak), local_error->message);
			g_clear_error (&local_error);
		}

		/* Refine all the installed apps to make sure they’re up to date
		 * in the UI. Ignore failure since it’s not an entirely
		 * necessary part of the install operation. */
		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);
			g_autofree gchar *ref = NULL;

			ref = gs_flatpak_app_get_ref_display (app);
			if (!gs_flatpak_refine_app (flatpak, app,
						    GS_PLUGIN_REFINE_REQUIRE_FLAGS_ID |
						    GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN |
						    GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION,
						    interactive, FALSE,
						    data->event_callback, data->event_user_data,
						    cancellable, &local_error)) {
				gs_flatpak_error_convert (&local_error);
				g_warning ("Error refining app ‘%s’ after install: %s", ref, local_error->message);
				g_clear_error (&local_error);
				continue;
			}

			gs_flatpak_refine_addons (flatpak,
						  app,
						  GS_PLUGIN_REFINE_REQUIRE_FLAGS_ID,
						  GS_APP_STATE_INSTALLING,
						  interactive,
						  data->event_callback,
						  data->event_user_data,
						  cancellable);
		}

		gs_flatpak_set_busy (flatpak, FALSE);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_flatpak_install_apps_finish (GsPlugin      *plugin,
                                       GAsyncResult  *result,
                                       GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static GsApp *
gs_plugin_flatpak_file_to_app_repo (GsPluginFlatpak  *self,
                                    GFile            *file,
                                    gboolean          interactive,
                                    GCancellable     *cancellable,
                                    GError          **error)
{
	g_autoptr(GsApp) app = NULL;

	/* parse the repo file */
	app = gs_flatpak_app_new_from_repo_file (file, cancellable, error);
	if (app == NULL)
		return NULL;

	/* already exists */
	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GsApp) app_tmp = NULL;
		app_tmp = gs_flatpak_find_repository_by_url (flatpak,
							     gs_flatpak_app_get_repo_url (app),
							     interactive,
							     cancellable, &error_local);
		if (app_tmp == NULL) {
			g_debug ("%s", error_local->message);
			continue;
		}
		if (g_strcmp0 (gs_flatpak_app_get_repo_filter (app), gs_flatpak_app_get_repo_filter (app_tmp)) != 0)
			continue;
		return g_steal_pointer (&app_tmp);
	}

	/* this is new */
	gs_app_set_management_plugin (app, GS_PLUGIN (self));
	return g_steal_pointer (&app);
}

static GsFlatpak *
gs_plugin_flatpak_create_temporary (GsPluginFlatpak  *self,
                                    GCancellable     *cancellable,
                                    GError          **error)
{
	g_autofree gchar *installation_path = NULL;
	g_autoptr(FlatpakInstallation) installation = NULL;
	g_autoptr(GFile) installation_file = NULL;

	/* create new per-user installation in a cache dir */
	installation_path = gs_utils_get_cache_filename ("flatpak",
							 "installation-tmp",
							 GS_UTILS_CACHE_FLAG_WRITEABLE |
							 GS_UTILS_CACHE_FLAG_ENSURE_EMPTY |
							 GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
							 error);
	if (installation_path == NULL)
		return NULL;
	installation_file = g_file_new_for_path (installation_path);
	installation = flatpak_installation_new_for_path (installation_file,
							  TRUE, /* user */
							  cancellable,
							  error);
	if (installation == NULL) {
		gs_flatpak_error_convert (error);
		return NULL;
	}
	return gs_flatpak_new (GS_PLUGIN (self), installation, GS_FLATPAK_FLAG_IS_TEMPORARY);
}

static GsApp *
gs_plugin_flatpak_file_to_app_bundle (GsPluginFlatpak  *self,
                                      GFile            *file,
                                      gboolean          interactive,
                                      GsApp            *alternate_of,
                                      GCancellable     *cancellable,
                                      GError          **error)
{
	g_autofree gchar *ref = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsApp) app_tmp = NULL;
	g_autoptr(GsFlatpak) flatpak_tmp = NULL;
	GsApp *runtime;

	/* only use the temporary GsFlatpak to avoid the auth dialog */
	flatpak_tmp = gs_plugin_flatpak_create_temporary (self, cancellable, error);
	if (flatpak_tmp == NULL)
		return NULL;

	/* First make a quick GsApp to get the ref */
	app = gs_flatpak_file_to_app_bundle (flatpak_tmp, file, TRUE /* unrefined */,
					     interactive, cancellable, error);
	if (app == NULL)
		return NULL;

	/* is this already installed or available in a configured remote */
	ref = gs_flatpak_app_get_ref_display (app);
	app_tmp = gs_plugin_flatpak_find_app_by_ref (self, ref, interactive, alternate_of, cancellable, NULL);
	if (app_tmp != NULL)
		return g_steal_pointer (&app_tmp);

	/* If not installed/available, make a fully refined GsApp */
	g_clear_object (&app);
	app = gs_flatpak_file_to_app_bundle (flatpak_tmp, file, FALSE /* unrefined */,
					     interactive, cancellable, error);
	if (app == NULL)
		return NULL;

	/* force this to be 'any' scope for installation */
	gs_app_set_scope (app, AS_COMPONENT_SCOPE_UNKNOWN);

	runtime = gs_app_get_runtime (app);
	if (runtime != NULL)
		gs_app_set_scope (runtime, AS_COMPONENT_SCOPE_UNKNOWN);

	/* this is new */
	return g_steal_pointer (&app);
}

static GsApp *
gs_plugin_flatpak_file_to_app_ref (GsPluginFlatpak        *self,
                                   GFile                  *file,
                                   gboolean                interactive,
                                   GsApp                  *alternate_of,
                                   GsPluginEventCallback   event_callback,
                                   void                   *event_user_data,
                                   GCancellable           *cancellable,
                                   GError                **error)
{
	GsApp *runtime;
	g_autofree gchar *ref = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsApp) app_tmp = NULL;
	g_autoptr(GsFlatpak) flatpak_tmp = NULL;

	/* only use the temporary GsFlatpak to avoid the auth dialog */
	flatpak_tmp = gs_plugin_flatpak_create_temporary (self, cancellable, error);
	if (flatpak_tmp == NULL)
		return NULL;

	/* First make a quick GsApp to get the ref */
	app = gs_flatpak_file_to_app_ref (flatpak_tmp, file, TRUE /* unrefined */,
					  interactive, event_callback, event_user_data, cancellable, error);
	if (app == NULL)
		return NULL;

	/* is this already installed or available in a configured remote */
	ref = gs_flatpak_app_get_ref_display (app);
	app_tmp = gs_plugin_flatpak_find_app_by_ref (self, ref, interactive, alternate_of, cancellable, NULL);
	if (app_tmp != NULL)
		return g_steal_pointer (&app_tmp);

	/* If not installed/available, make a fully refined GsApp */
	g_clear_object (&app);
	app = gs_flatpak_file_to_app_ref (flatpak_tmp, file, FALSE /* unrefined */,
					  interactive, event_callback, event_user_data, cancellable, error);
	if (app == NULL)
		return NULL;

	/* force this to be 'any' scope for installation */
	gs_app_set_scope (app, AS_COMPONENT_SCOPE_UNKNOWN);

	/* do we have a system runtime available */
	runtime = gs_app_get_runtime (app);
	if (runtime != NULL) {
		g_autoptr(GsApp) runtime_tmp = NULL;
		g_autofree gchar *runtime_ref = gs_flatpak_app_get_ref_display (runtime);
		runtime_tmp = gs_plugin_flatpak_find_app_by_ref (self,
								 runtime_ref,
								 interactive,
								 alternate_of,
								 cancellable,
								 NULL);
		if (runtime_tmp != NULL) {
			gs_app_set_runtime (app, runtime_tmp);
		} else {
			/* the new runtime is available from the RuntimeRepo */
			if (gs_flatpak_app_get_runtime_url (runtime) != NULL)
				gs_app_set_state (runtime, GS_APP_STATE_AVAILABLE);
		}
	}

	/* this is new */
	return g_steal_pointer (&app);
}

static GsApp * /* (transfer full) */
gs_plugin_flatpak_file_to_app (GsPluginFlatpak *self,
			       GFile *file,
			       gboolean interactive,
			       GsApp *alternate_of,
			       GsPluginEventCallback event_callback,
			       void *event_user_data,
			       GCancellable *cancellable,
			       GError **error)
{
	g_autofree gchar *content_type = NULL;
	GsApp *app = NULL;
	const gchar *mimetypes_bundle[] = {
		"application/vnd.flatpak",
		NULL };
	const gchar *mimetypes_repo[] = {
		"application/vnd.flatpak.repo",
		NULL };
	const gchar *mimetypes_ref[] = {
		"application/vnd.flatpak.ref",
		NULL };

	/* does this match any of the mimetypes we support */
	content_type = gs_utils_get_content_type (file, cancellable, error);
	if (content_type == NULL)
		return NULL;

	if (g_strv_contains (mimetypes_bundle, content_type))
		app = gs_plugin_flatpak_file_to_app_bundle (self, file, interactive, alternate_of, cancellable, error);
	else if (g_strv_contains (mimetypes_repo, content_type))
		app = gs_plugin_flatpak_file_to_app_repo (self, file, interactive, cancellable, error);
	else if (g_strv_contains (mimetypes_ref, content_type))
		app = gs_plugin_flatpak_file_to_app_ref (self, file, interactive, alternate_of, event_callback, event_user_data, cancellable, error);

	if (app != NULL) {
		GsApp *runtime = gs_app_get_runtime (app);
		/* Ensure the origin for the runtime is set */
		if (runtime != NULL && gs_app_get_origin (runtime) == NULL) {
			g_autoptr(GError) error_local = NULL;
			if (!gs_plugin_flatpak_refine_app (self, runtime, GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN, interactive, event_callback, event_user_data, cancellable, &error_local))
				g_debug ("Failed to refine runtime: %s", error_local->message);
		}
		gs_plugin_flatpak_ensure_scope (GS_PLUGIN (self), app);
		/* It can return a cached app when the app is available in one of the remotes.
		   Cached apps cannot have set the local file property. */
		if (gs_plugin_cache_lookup (GS_PLUGIN (self), gs_app_get_unique_id (app)) != app)
			gs_app_set_local_file (app, file);
	}

	return app;
}

static void
file_to_app_thread_cb (GTask *task,
		       gpointer source_object,
		       gpointer task_data,
		       GCancellable *cancellable)
{
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) local_error = NULL;
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPluginFileToAppData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_FILE_TO_APP_FLAGS_INTERACTIVE) != 0;

	app = gs_plugin_flatpak_file_to_app (self, data->file, interactive, NULL, data->event_callback, data->event_user_data, cancellable, &local_error);
	if (app != NULL) {
		g_autoptr(GsAppList) list = gs_app_list_new ();
		gs_app_list_add (list, app);
		g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
	} else if (local_error != NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
	} else {
		g_task_return_pointer (task, gs_app_list_new (), g_object_unref);
	}
}

static void
gs_plugin_flatpak_file_to_app_async (GsPlugin *plugin,
				     GFile *file,
				     GsPluginFileToAppFlags flags,
				     GsPluginEventCallback event_callback,
				     void *event_user_data,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer user_data)
{
	g_autoptr(GTask) task = NULL;
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	gboolean interactive = (flags & GS_PLUGIN_FILE_TO_APP_FLAGS_INTERACTIVE) != 0;

	task = gs_plugin_file_to_app_data_new_task (plugin, file, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_file_to_app_async);

	/* Queue a job to get the apps. */
	gs_worker_thread_queue (self->worker, get_priority_for_interactivity (interactive),
				file_to_app_thread_cb, g_steal_pointer (&task));
}

static GsAppList *
gs_plugin_flatpak_file_to_app_finish (GsPlugin      *plugin,
				      GAsyncResult  *result,
				      GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void refine_categories_thread_cb (GTask        *task,
                                         gpointer      source_object,
                                         gpointer      task_data,
                                         GCancellable *cancellable);

static void
gs_plugin_flatpak_refine_categories_async (GsPlugin                      *plugin,
                                           GPtrArray                     *list,
                                           GsPluginRefineCategoriesFlags  flags,
                                           GsPluginEventCallback          event_callback,
                                           void                          *event_user_data,
                                           GCancellable                  *cancellable,
                                           GAsyncReadyCallback            callback,
                                           gpointer                       user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_REFINE_CATEGORIES_FLAGS_INTERACTIVE);

	task = gs_plugin_refine_categories_data_new_task (plugin, list, flags,
							  event_callback, event_user_data,
							  cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_refine_categories_async);

	/* All we actually do is add the sizes of each category. If that’s
	 * not been requested, avoid queueing a worker job. */
	if (!(flags & GS_PLUGIN_REFINE_CATEGORIES_FLAGS_SIZE)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* Queue a job to get the apps. */
	gs_worker_thread_queue (self->worker, get_priority_for_interactivity (interactive),
				refine_categories_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
refine_categories_thread_cb (GTask        *task,
                             gpointer      source_object,
                             gpointer      task_data,
                             GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPluginRefineCategoriesData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_REFINE_CATEGORIES_FLAGS_INTERACTIVE);
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);

		if (!gs_flatpak_refine_category_sizes (flatpak, data->list, interactive, data->event_callback, data->event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_flatpak_refine_categories_finish (GsPlugin      *plugin,
                                            GAsyncResult  *result,
                                            GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void list_apps_thread_cb (GTask        *task,
                                 gpointer      source_object,
                                 gpointer      task_data,
                                 GCancellable *cancellable);

static void
gs_plugin_flatpak_list_apps_async (GsPlugin              *plugin,
                                   GsAppQuery            *query,
                                   GsPluginListAppsFlags  flags,
                                   GsPluginEventCallback  event_callback,
                                   void                  *event_user_data,
                                   GCancellable          *cancellable,
                                   GAsyncReadyCallback    callback,
                                   gpointer               user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE);

	task = gs_plugin_list_apps_data_new_task (plugin, query, flags,
						  event_callback, event_user_data,
						  cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_list_apps_async);

	/* Queue a job to get the apps. */
	gs_worker_thread_queue (self->worker, get_priority_for_interactivity (interactive),
				list_apps_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
list_apps_thread_cb (GTask        *task,
                     gpointer      source_object,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	g_autoptr(GsAppList) list = gs_app_list_new ();
	GsPluginListAppsData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE);
	GsPluginEventCallback event_callback = data->event_callback;
	void *event_user_data = data->event_user_data;
	GDateTime *released_since = NULL;
	GsAppQueryTristate is_curated = GS_APP_QUERY_TRISTATE_UNSET;
	GsAppQueryTristate is_featured = GS_APP_QUERY_TRISTATE_UNSET;
	GsCategory *category = NULL;
	GsAppQueryTristate is_installed = GS_APP_QUERY_TRISTATE_UNSET;
	GsAppQueryTristate is_for_update = GS_APP_QUERY_TRISTATE_UNSET;
	const AsComponentKind *component_kinds = NULL;
	guint64 age_secs = 0;
	const gchar * const *deployment_featured = NULL;
	const gchar *const *developers = NULL;
	const gchar * const *keywords = NULL;
	GsApp *alternate_of = NULL;
	const gchar *provides_tag = NULL;
	GsAppQueryProvidesType provides_type = GS_APP_QUERY_PROVIDES_UNKNOWN;
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	if (data->query != NULL) {
		released_since = gs_app_query_get_released_since (data->query);
		is_curated = gs_app_query_get_is_curated (data->query);
		is_featured = gs_app_query_get_is_featured (data->query);
		category = gs_app_query_get_category (data->query);
		is_installed = gs_app_query_get_is_installed (data->query);
		deployment_featured = gs_app_query_get_deployment_featured (data->query);
		developers = gs_app_query_get_developers (data->query);
		keywords = gs_app_query_get_keywords (data->query);
		alternate_of = gs_app_query_get_alternate_of (data->query);
		provides_type = gs_app_query_get_provides (data->query, &provides_tag);
		is_for_update = gs_app_query_get_is_for_update (data->query);
		component_kinds = gs_app_query_get_component_kinds (data->query);
	}

	if (released_since != NULL) {
		g_autoptr(GDateTime) now = g_date_time_new_now_local ();
		age_secs = g_date_time_difference (now, released_since) / G_TIME_SPAN_SECOND;
	}

	/* Currently only support a subset of query properties, and only one set at once.
	 * Also don’t currently support GS_APP_QUERY_TRISTATE_FALSE. */
	if ((released_since == NULL &&
	     is_curated == GS_APP_QUERY_TRISTATE_UNSET &&
	     is_featured == GS_APP_QUERY_TRISTATE_UNSET &&
	     category == NULL &&
	     is_installed == GS_APP_QUERY_TRISTATE_UNSET &&
	     deployment_featured == NULL &&
	     developers == NULL &&
	     keywords == NULL &&
	     alternate_of == NULL &&
	     provides_tag == NULL &&
	     is_for_update == GS_APP_QUERY_TRISTATE_UNSET &&
	     component_kinds == NULL) ||
	    is_curated == GS_APP_QUERY_TRISTATE_FALSE ||
	    is_featured == GS_APP_QUERY_TRISTATE_FALSE ||
	    is_installed == GS_APP_QUERY_TRISTATE_FALSE ||
	    is_for_update == GS_APP_QUERY_TRISTATE_FALSE ||
	    (component_kinds != NULL && !gs_component_kind_array_contains (component_kinds, AS_COMPONENT_KIND_REPOSITORY)) ||
	    gs_app_query_get_n_properties_set (data->query) != 1) {
		g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					 "Unsupported query");
		return;
	}

	if (alternate_of != NULL &&
	    gs_app_get_bundle_kind (alternate_of) == AS_BUNDLE_KIND_FLATPAK &&
	    gs_app_get_scope (alternate_of) != AS_COMPONENT_SCOPE_UNKNOWN &&
	    gs_app_get_local_file (alternate_of) != NULL) {
		g_autoptr(GsApp) app = NULL;
		GFile *file = gs_app_get_local_file (alternate_of);
		app = gs_plugin_flatpak_file_to_app (self, file, interactive, alternate_of, event_callback, event_user_data, cancellable, NULL);
		if (app != NULL && app != alternate_of) {
			gs_app_set_local_file (app, file);
			if (gs_app_get_scope (alternate_of) == AS_COMPONENT_SCOPE_SYSTEM)
				gs_app_set_scope (app, AS_COMPONENT_SCOPE_USER);
			else
				gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
			/* ensure both are considered */
			gs_app_list_add (list, alternate_of);
			gs_app_list_add (list, app);
		}
	}

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		const gchar * const provides_tag_strv[2] = { provides_tag, NULL };

		if (released_since != NULL &&
		    !gs_flatpak_add_recent (flatpak, list, age_secs, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (is_curated != GS_APP_QUERY_TRISTATE_UNSET &&
		    !gs_flatpak_add_popular (flatpak, list, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (is_featured != GS_APP_QUERY_TRISTATE_UNSET &&
		    !gs_flatpak_add_featured (flatpak, list, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (category != NULL &&
		    !gs_flatpak_add_category_apps (flatpak, category, list, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (is_installed != GS_APP_QUERY_TRISTATE_UNSET &&
		    !gs_flatpak_add_installed (flatpak, list, interactive, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (deployment_featured != NULL &&
		    !gs_flatpak_add_deployment_featured (flatpak, list, interactive, event_callback, event_user_data, deployment_featured, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (developers != NULL &&
		    !gs_flatpak_search_developer_apps (flatpak, developers, list, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (keywords != NULL &&
		    !gs_flatpak_search (flatpak, keywords, list, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (alternate_of != NULL &&
		    !gs_flatpak_add_alternates (flatpak, alternate_of, list, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		/* The @provides_type is deliberately ignored here, as flatpak
		 * wants to try and match anything. This could be changed in
		 * future. */
		if (provides_tag != NULL &&
		    provides_type != GS_APP_QUERY_PROVIDES_UNKNOWN &&
		    !gs_flatpak_search (flatpak, provides_tag_strv, list, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (is_for_update == GS_APP_QUERY_TRISTATE_TRUE) {
			g_autoptr(GError) local_error2 = NULL;
			if (!gs_flatpak_add_updates (flatpak, list, interactive, event_callback, event_user_data, cancellable, &local_error2))
				g_debug ("Failed to get updates for '%s': %s", gs_flatpak_get_id (flatpak), local_error2->message);
		}

		if (gs_component_kind_array_contains (component_kinds, AS_COMPONENT_KIND_REPOSITORY) &&
		    !gs_flatpak_add_repositories (flatpak, list, interactive, event_callback, event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

	if (is_for_update == GS_APP_QUERY_TRISTATE_TRUE)
		gs_plugin_cache_lookup_by_state (GS_PLUGIN (self), list, GS_APP_STATE_INSTALLING);

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_flatpak_list_apps_finish (GsPlugin      *plugin,
                                    GAsyncResult  *result,
                                    GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

typedef struct {
	gboolean interactive;
	GsPluginEventCallback event_callback;
	void *event_user_data;
	GFile *cache_file;  /* (owned) (not nullable) */
} UrlToAppDownloadData;

static void
url_to_app_download_data_free (UrlToAppDownloadData *data)
{
	g_clear_object (&data->cache_file);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (UrlToAppDownloadData, url_to_app_download_data_free)

static void url_to_app_download_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data);
static void url_to_app_file_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data);

static void
url_to_app_thread_cb (GTask *task,
		      gpointer source_object,
		      gpointer task_data,
		      GCancellable *cancellable)
{
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GError) local_error = NULL;
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsPluginUrlToAppData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_URL_TO_APP_FLAGS_INTERACTIVE) != 0;

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		if (!gs_flatpak_url_to_app (flatpak, list, data->url, interactive, data->event_callback, data->event_user_data, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static void
gs_plugin_flatpak_url_to_app_async (GsPlugin *plugin,
				    const gchar *url,
				    GsPluginUrlToAppFlags flags,
				    GsPluginEventCallback event_callback,
				    void *event_user_data,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	g_autoptr(GTask) task = NULL;
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	gboolean interactive = (flags & GS_PLUGIN_URL_TO_APP_FLAGS_INTERACTIVE) != 0;
	g_autofree char *scheme = NULL;

	task = gs_plugin_url_to_app_data_new_task (plugin, url, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_url_to_app_async);

	/* Firstly, try and support `flatpak+https` URIs. This needs to be done
	 * at the #GsPluginFlatpak level rather than the #GsFlatpak level,
	 * because we need to hand off to the plugin’s file-to-app code.
	 *
	 * The flatpak+https URI scheme points towards a .flatpakref file which
	 * we can download and then treat like a normal local file. This code
	 * actually also supports the URI pointing at a bundle or a repo file,
	 * since that also seems sensible to support.
	 *
	 * https://gitlab.gnome.org/GNOME/gnome-software/-/issues/2240#note_1787991 */
	scheme = gs_utils_get_url_scheme (url);

	if (g_strcmp0 (scheme, "flatpak+https") == 0) {
		g_autofree gchar *cache_filename = NULL;
		g_autoptr(GFile) cache_file = NULL;
		g_autoptr(SoupSession) soup_session = NULL;
		g_autoptr(GError) local_error = NULL;
		g_autoptr(UrlToAppDownloadData) data = NULL;

		/* Download and cache the file. */
		cache_filename = gs_utils_get_cache_filename ("flatpak-downloaded-refs",
							      url,
							      GS_UTILS_CACHE_FLAG_WRITEABLE |
							      GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
							      &local_error);
		if (cache_filename == NULL) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		cache_file = g_file_new_for_path (cache_filename);
		soup_session = gs_build_soup_session ();
		if (self->cache_files_to_delete == NULL)
			self->cache_files_to_delete = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		g_ptr_array_add (self->cache_files_to_delete, g_object_ref (cache_file));

		data = g_new0 (UrlToAppDownloadData, 1);
		data->interactive = interactive;
		data->event_callback = event_callback;
		data->event_user_data = event_user_data;
		data->cache_file = g_object_ref (cache_file);
		g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) url_to_app_download_data_free);

		gs_download_file_async (soup_session,
					url + strlen ("flatpak+"),
					cache_file,
					G_PRIORITY_DEFAULT,
					NULL, NULL,  /* progress */
					cancellable,
					url_to_app_download_cb,
					g_steal_pointer (&task));
	} else {
		/* If it’s not a file we need to download, queue a job to get the apps. */
		gs_worker_thread_queue (self->worker, get_priority_for_interactivity (interactive),
					url_to_app_thread_cb, g_steal_pointer (&task));
	}
}

static void
url_to_app_download_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
	SoupSession *soup_session = SOUP_SESSION (object);
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginFlatpak *self = g_task_get_source_object (task);
	UrlToAppDownloadData *data = g_task_get_task_data (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) local_error = NULL;

	if (!gs_download_file_finish (soup_session, result, &local_error) &&
	    !g_error_matches (local_error, GS_DOWNLOAD_ERROR, GS_DOWNLOAD_ERROR_NOT_MODIFIED)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_clear_error (&local_error);

	/* Now load and display the downloaded flatpakref file. */
	gs_plugin_flatpak_file_to_app_async (GS_PLUGIN (self),
					     data->cache_file,
					     data->interactive ? GS_PLUGIN_FILE_TO_APP_FLAGS_INTERACTIVE : GS_PLUGIN_FILE_TO_APP_FLAGS_NONE,
					     data->event_callback,
					     data->event_user_data,
					     cancellable,
					     url_to_app_file_cb, g_steal_pointer (&task));
}

static void
url_to_app_file_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginFlatpak *self = g_task_get_source_object (task);
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) local_error = NULL;

	list = gs_plugin_flatpak_file_to_app_finish (GS_PLUGIN (self), result, &local_error);

	if (list != NULL)
		g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

static GsAppList *
gs_plugin_flatpak_url_to_app_finish (GsPlugin      *plugin,
				     GAsyncResult  *result,
				     GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void install_repository_thread_cb (GTask        *task,
					  gpointer      source_object,
					  gpointer      task_data,
					  GCancellable *cancellable);

static void
gs_plugin_flatpak_install_repository_async (GsPlugin                     *plugin,
					    GsApp			 *repository,
                                            GsPluginManageRepositoryFlags flags,
                                            GsPluginEventCallback         event_callback,
                                            void                         *event_user_data,
                                            GCancellable		 *cancellable,
                                            GAsyncReadyCallback		  callback,
                                            gpointer			  user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE);

	task = gs_plugin_manage_repository_data_new_task (plugin, repository, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_install_repository_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (repository, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* is a source */
	g_assert (gs_app_get_kind (repository) == AS_COMPONENT_KIND_REPOSITORY);

	gs_worker_thread_queue (self->long_running_worker, get_priority_for_interactivity (interactive),
				install_repository_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
install_repository_thread_cb (GTask        *task,
			      gpointer      source_object,
			      gpointer      task_data,
			      GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsFlatpak *flatpak;
	GsPluginManageRepositoryData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE);
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	/* queue for install if installation needs the network */
	if (!app_has_local_source (data->repository) &&
	    !gs_plugin_get_network_available (GS_PLUGIN (self))) {
		gs_app_set_state (data->repository, GS_APP_STATE_QUEUED_FOR_INSTALL);
		g_task_return_boolean (task, TRUE);
		return;
	}

	gs_plugin_flatpak_ensure_scope (GS_PLUGIN (self), data->repository);

	flatpak = gs_plugin_flatpak_get_handler (self, data->repository);
	if (flatpak == NULL) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	if (gs_flatpak_add_repository_app (flatpak, data->repository, TRUE, interactive, cancellable, &local_error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

static gboolean
gs_plugin_flatpak_install_repository_finish (GsPlugin      *plugin,
					     GAsyncResult  *result,
					     GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void remove_repository_thread_cb (GTask        *task,
					 gpointer      source_object,
					 gpointer      task_data,
					 GCancellable *cancellable);

static void
gs_plugin_flatpak_remove_repository_async (GsPlugin                     *plugin,
					   GsApp			*repository,
                                           GsPluginManageRepositoryFlags flags,
                                           GsPluginEventCallback         event_callback,
                                           void                         *event_user_data,
                                           GCancellable		 	*cancellable,
                                           GAsyncReadyCallback		 callback,
                                           gpointer			 user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE);

	task = gs_plugin_manage_repository_data_new_task (plugin, repository, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_remove_repository_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (repository, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* is a source */
	g_assert (gs_app_get_kind (repository) == AS_COMPONENT_KIND_REPOSITORY);

	gs_worker_thread_queue (self->long_running_worker, get_priority_for_interactivity (interactive),
				remove_repository_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
remove_repository_thread_cb (GTask        *task,
			     gpointer      source_object,
			     gpointer      task_data,
			     GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsFlatpak *flatpak;
	GsPluginManageRepositoryData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE);
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	flatpak = gs_plugin_flatpak_get_handler (self, data->repository);
	if (flatpak == NULL) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	if (gs_flatpak_remove_repository_app (flatpak, data->repository, TRUE, interactive, cancellable, &local_error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

static gboolean
gs_plugin_flatpak_remove_repository_finish (GsPlugin      *plugin,
					    GAsyncResult  *result,
					    GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void enable_repository_thread_cb (GTask        *task,
					 gpointer      source_object,
					 gpointer      task_data,
					 GCancellable *cancellable);

static void
gs_plugin_flatpak_enable_repository_async (GsPlugin                     *plugin,
					   GsApp			*repository,
                                           GsPluginManageRepositoryFlags flags,
                                           GsPluginEventCallback         event_callback,
                                           void                         *event_user_data,
                                           GCancellable		 	*cancellable,
                                           GAsyncReadyCallback		 callback,
                                           gpointer			 user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE);

	task = gs_plugin_manage_repository_data_new_task (plugin, repository, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_enable_repository_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (repository, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* is a source */
	g_assert (gs_app_get_kind (repository) == AS_COMPONENT_KIND_REPOSITORY);

	gs_worker_thread_queue (self->long_running_worker, get_priority_for_interactivity (interactive),
				enable_repository_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
enable_repository_thread_cb (GTask        *task,
			     gpointer      source_object,
			     gpointer      task_data,
			     GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsFlatpak *flatpak;
	GsPluginManageRepositoryData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE);
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	flatpak = gs_plugin_flatpak_get_handler (self, data->repository);
	if (flatpak == NULL) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	if (gs_flatpak_add_repository_app (flatpak, data->repository, FALSE, interactive, cancellable, &local_error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

static gboolean
gs_plugin_flatpak_enable_repository_finish (GsPlugin      *plugin,
					    GAsyncResult  *result,
					    GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void disable_repository_thread_cb (GTask        *task,
					  gpointer      source_object,
					  gpointer      task_data,
					  GCancellable *cancellable);

static void
gs_plugin_flatpak_disable_repository_async (GsPlugin                     *plugin,
					    GsApp			 *repository,
                                            GsPluginManageRepositoryFlags flags,
                                            GsPluginEventCallback         event_callback,
                                            void                         *event_user_data,
                                            GCancellable	 	 *cancellable,
                                            GAsyncReadyCallback		  callback,
                                            gpointer			  user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE);

	task = gs_plugin_manage_repository_data_new_task (plugin, repository, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_disable_repository_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (repository, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* is a source */
	g_assert (gs_app_get_kind (repository) == AS_COMPONENT_KIND_REPOSITORY);

	gs_worker_thread_queue (self->long_running_worker, get_priority_for_interactivity (interactive),
				disable_repository_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
disable_repository_thread_cb (GTask        *task,
			      gpointer      source_object,
			      gpointer      task_data,
			      GCancellable *cancellable)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (source_object);
	GsFlatpak *flatpak;
	GsPluginManageRepositoryData *data = task_data;
	gboolean interactive = (data->flags & GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE);
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	flatpak = gs_plugin_flatpak_get_handler (self, data->repository);
	if (flatpak == NULL) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	if (gs_flatpak_remove_repository_app (flatpak, data->repository, FALSE, interactive, cancellable, &local_error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

static gboolean
gs_plugin_flatpak_disable_repository_finish (GsPlugin      *plugin,
					     GAsyncResult  *result,
					     GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_flatpak_class_init (GsPluginFlatpakClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_flatpak_dispose;

	plugin_class->adopt_app = gs_plugin_flatpak_adopt_app;
	plugin_class->setup_async = gs_plugin_flatpak_setup_async;
	plugin_class->setup_finish = gs_plugin_flatpak_setup_finish;
	plugin_class->shutdown_async = gs_plugin_flatpak_shutdown_async;
	plugin_class->shutdown_finish = gs_plugin_flatpak_shutdown_finish;
	plugin_class->refine_async = gs_plugin_flatpak_refine_async;
	plugin_class->refine_finish = gs_plugin_flatpak_refine_finish;
	plugin_class->list_apps_async = gs_plugin_flatpak_list_apps_async;
	plugin_class->list_apps_finish = gs_plugin_flatpak_list_apps_finish;
	plugin_class->refresh_metadata_async = gs_plugin_flatpak_refresh_metadata_async;
	plugin_class->refresh_metadata_finish = gs_plugin_flatpak_refresh_metadata_finish;
	plugin_class->install_repository_async = gs_plugin_flatpak_install_repository_async;
	plugin_class->install_repository_finish = gs_plugin_flatpak_install_repository_finish;
	plugin_class->remove_repository_async = gs_plugin_flatpak_remove_repository_async;
	plugin_class->remove_repository_finish = gs_plugin_flatpak_remove_repository_finish;
	plugin_class->enable_repository_async = gs_plugin_flatpak_enable_repository_async;
	plugin_class->enable_repository_finish = gs_plugin_flatpak_enable_repository_finish;
	plugin_class->disable_repository_async = gs_plugin_flatpak_disable_repository_async;
	plugin_class->disable_repository_finish = gs_plugin_flatpak_disable_repository_finish;
	plugin_class->refine_categories_async = gs_plugin_flatpak_refine_categories_async;
	plugin_class->refine_categories_finish = gs_plugin_flatpak_refine_categories_finish;
	plugin_class->install_apps_async = gs_plugin_flatpak_install_apps_async;
	plugin_class->install_apps_finish = gs_plugin_flatpak_install_apps_finish;
	plugin_class->uninstall_apps_async = gs_plugin_flatpak_uninstall_apps_async;
	plugin_class->uninstall_apps_finish = gs_plugin_flatpak_uninstall_apps_finish;
	plugin_class->update_apps_async = gs_plugin_flatpak_update_apps_async;
	plugin_class->update_apps_finish = gs_plugin_flatpak_update_apps_finish;
	plugin_class->launch_async = gs_plugin_flatpak_launch_async;
	plugin_class->launch_finish = gs_plugin_flatpak_launch_finish;
	plugin_class->file_to_app_async = gs_plugin_flatpak_file_to_app_async;
	plugin_class->file_to_app_finish = gs_plugin_flatpak_file_to_app_finish;
	plugin_class->url_to_app_async = gs_plugin_flatpak_url_to_app_async;
	plugin_class->url_to_app_finish = gs_plugin_flatpak_url_to_app_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_FLATPAK;
}
