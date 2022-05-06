/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Joaquim Rocha <jrocha@endlessm.com>
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017-2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/*
 * SECTION:
 * Exposes flatpaks from the user and system repositories.
 *
 * All GsApp's created have management-plugin set to flatpak
 * Some GsApp's created have have flatpak::kind of app or runtime
 * The GsApp:origin is the remote name, e.g. test-repo
 *
 * The plugin has a worker thread which all operations are delegated to, as the
 * libflatpak API is entirely synchronous (and thread-safe). * Message passing
 * to the worker thread is by gs_worker_thread_queue().
 *
 * FIXME: It may speed things up in future to have one worker thread *per*
 * `FlatpakInstallation`, all operating in parallel.
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
#include "gs-worker-thread.h"

#include "gs-plugin-flatpak.h"

struct _GsPluginFlatpak
{
	GsPlugin		 parent;

	GsWorkerThread		*worker;  /* (owned) */

	GPtrArray		*installations;  /* (element-type GsFlatpak) (owned); may be NULL before setup or after shutdown */
	gboolean		 has_system_helper;
	const gchar		*destdir_for_tests;
};

G_DEFINE_TYPE (GsPluginFlatpak, gs_plugin_flatpak, GS_TYPE_PLUGIN)

#define assert_in_worker(self) \
	g_assert (gs_worker_thread_is_in_worker_context (self->worker))

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

	g_clear_pointer (&self->installations, g_ptr_array_unref);
	g_clear_object (&self->worker);

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

	/* set name of MetaInfo file */
	gs_plugin_set_appstream_id (plugin, "org.gnome.Software.Plugin.Flatpak");

	/* used for self tests */
	self->destdir_for_tests = g_getenv ("GS_SELF_TEST_FLATPAK_DATADIR");
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

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
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

	/* create and set up */
	flatpak = gs_flatpak_new (GS_PLUGIN (self), installation, GS_FLATPAK_FLAG_NONE);
	if (!gs_flatpak_setup (flatpak, cancellable, error))
		return FALSE;
	g_debug ("successfully set up %s", gs_flatpak_get_id (flatpak));

	/* add objects that set up correctly */
	g_ptr_array_add (self->installations, g_steal_pointer (&flatpak));
	return TRUE;
}

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
	gs_plugin_event_add_flag (event,
				  GS_PLUGIN_EVENT_FLAG_WARNING);
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

	/* Start up a worker thread to process all the plugin’s function calls. */
	self->worker = gs_worker_thread_new ("gs-plugin-flatpak");

	/* Queue a job to find and set up the installations. */
	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				setup_thread_cb, g_steal_pointer (&task));
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
		if (self->has_system_helper) {
			installations = flatpak_get_system_installations (cancellable,
									  &error_local);

			if (installations == NULL) {
				gs_plugin_flatpak_report_warning (plugin, &error_local);
				g_clear_error (&error_local);
			}
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

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_shutdown_async);

	/* Stop the worker thread. */
	gs_worker_thread_shutdown_async (self->worker, cancellable, shutdown_cb, g_steal_pointer (&task));
}

static void
shutdown_cb (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	GsPluginFlatpak *self = g_task_get_source_object (task);
	g_autoptr(GsWorkerThread) worker = NULL;
	g_autoptr(GError) local_error = NULL;

	worker = g_steal_pointer (&self->worker);

	if (!gs_worker_thread_shutdown_finish (worker, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
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

gboolean
gs_plugin_add_sources (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		if (!gs_flatpak_add_sources (flatpak, list, interactive, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		if (!gs_flatpak_add_updates (flatpak, list, interactive, cancellable, error))
			return FALSE;
	}
	gs_plugin_cache_lookup_by_state (plugin, list, GS_APP_STATE_INSTALLING);
	return TRUE;
}

static void refresh_metadata_thread_cb (GTask        *task,
                                        gpointer      source_object,
                                        gpointer      task_data,
                                        GCancellable *cancellable);

static void
gs_plugin_flatpak_refresh_metadata_async (GsPlugin                     *plugin,
                                          guint64                       cache_age_secs,
                                          GsPluginRefreshMetadataFlags  flags,
                                          GCancellable                 *cancellable,
                                          GAsyncReadyCallback           callback,
                                          gpointer                      user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE);

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_flatpak_refresh_metadata_async);
	g_task_set_task_data (task, gs_plugin_refresh_metadata_data_new (cache_age_secs, flags), (GDestroyNotify) gs_plugin_refresh_metadata_data_free);

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
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);

		if (!gs_flatpak_refresh (flatpak, data->cache_age_secs, interactive, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
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
gs_plugin_flatpak_refine_app (GsPluginFlatpak      *self,
                              GsApp                *app,
                              GsPluginRefineFlags   flags,
                              gboolean              interactive,
                              GCancellable         *cancellable,
                              GError              **error)
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
			if (gs_flatpak_refine_app_state (flatpak_tmp, app, interactive,
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
	return gs_flatpak_refine_app (flatpak, app, flags, interactive, cancellable, error);
}


static gboolean
refine_app (GsPluginFlatpak      *self,
            GsApp                *app,
            GsPluginRefineFlags   flags,
            gboolean              interactive,
            GCancellable         *cancellable,
            GError              **error)
{
	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
		return TRUE;

	/* get the runtime first */
	if (!gs_plugin_flatpak_refine_app (self, app, flags, interactive, cancellable, error))
		return FALSE;

	/* the runtime might be installed in a different scope */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME) {
		GsApp *runtime = gs_app_get_runtime (app);
		if (runtime != NULL) {
			if (!gs_plugin_flatpak_refine_app (self, app,
							   flags,
							   interactive,
							   cancellable,
							   error)) {
				return FALSE;
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
gs_plugin_flatpak_refine_async (GsPlugin            *plugin,
                                GsAppList           *list,
                                GsPluginRefineFlags  flags,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = gs_plugin_has_flags (GS_PLUGIN (self), GS_PLUGIN_FLAGS_INTERACTIVE);

	task = gs_plugin_refine_data_new_task (plugin, list, flags, cancellable, callback, user_data);
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
	GsPluginRefineFlags flags = data->flags;
	gboolean interactive = gs_plugin_has_flags (GS_PLUGIN (self), GS_PLUGIN_FLAGS_INTERACTIVE);
	g_autoptr(GsAppList) app_list = NULL;
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (self, app, flags, interactive, cancellable, &local_error)) {
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

	for (guint j = 0; j < gs_app_list_length (app_list); j++) {
		GsApp *app = gs_app_list_index (app_list, j);

		if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;

		for (guint i = 0; i < self->installations->len; i++) {
			GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);

			if (!gs_flatpak_refine_wildcard (flatpak, app, list, flags, interactive,
							 cancellable, &local_error)) {
				g_task_return_error (task, g_steal_pointer (&local_error));
				return;
			}
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

gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	GsFlatpak *flatpak = gs_plugin_flatpak_get_handler (GS_PLUGIN_FLATPAK (plugin), app);
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	if (flatpak == NULL)
		return TRUE;

	return gs_flatpak_launch (flatpak, app, interactive, cancellable, error);
}

/* ref full */
static GsApp *
gs_plugin_flatpak_find_app_by_ref (GsPluginFlatpak  *self,
                                   const gchar      *ref,
                                   gboolean          interactive,
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
						  gs_plugin_has_flags (GS_PLUGIN (self), GS_PLUGIN_FLAGS_INTERACTIVE),
						  NULL, NULL);
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

static gboolean
_webflow_start (FlatpakTransaction *transaction,
                const char *remote,
                const char *url,
                GVariant *options,
                guint id,
                GsPlugin *plugin)
{
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
			g_autoptr(GsPluginEvent) event = NULL;

			g_warning ("Failed to start browser %s: %s", browser, error_local->message);

			gs_flatpak_error_convert (&error_local);

			event = gs_plugin_event_new ("error", error_local,
						     NULL);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			gs_plugin_report_event (plugin, event);

			return FALSE;
		}
	} else {
		if (!g_app_info_launch_default_for_uri (url, NULL, &error_local)) {
			g_autoptr(GsPluginEvent) event = NULL;

			g_warning ("Failed to show url: %s", error_local->message);

			gs_flatpak_error_convert (&error_local);

			event = gs_plugin_event_new ("error", error_local,
						     NULL);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			gs_plugin_report_event (plugin, event);

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

static FlatpakTransaction *
_build_transaction (GsPlugin *plugin, GsFlatpak *flatpak,
		    gboolean interactive,
		    GCancellable *cancellable, GError **error)
{
	FlatpakInstallation *installation;
	g_autoptr(FlatpakInstallation) installation_clone = NULL;
	g_autoptr(FlatpakTransaction) transaction = NULL;

	installation = gs_flatpak_get_installation (flatpak, interactive);

	installation_clone = g_object_ref (installation);

	/* create transaction */
	transaction = gs_flatpak_transaction_new (installation_clone, cancellable, error);
	if (transaction == NULL) {
		g_prefix_error (error, "failed to build transaction: ");
		gs_flatpak_error_convert (error);
		return NULL;
	}

	/* Let flatpak know if it is a background operation */
	flatpak_transaction_set_no_interaction (transaction, !interactive);

	/* connect up signals */
	g_signal_connect (transaction, "ref-to-app",
			  G_CALLBACK (_ref_to_app), plugin);
	g_signal_connect (transaction, "basic-auth-start",
			  G_CALLBACK (_basic_auth_start), plugin);
	g_signal_connect (transaction, "webflow-start",
			  G_CALLBACK (_webflow_start), plugin);
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

gboolean
gs_plugin_download (GsPlugin *plugin, GsAppList *list,
		    GCancellable *cancellable, GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GHashTable) applist_by_flatpaks = NULL;
	GHashTableIter iter;
	gpointer key, value;
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	/* build and run transaction for each flatpak installation */
	applist_by_flatpaks = _group_apps_by_installation (self, list);
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
			g_autoptr(GError) error_local = NULL;

			if (!gs_metered_block_app_list_on_download_scheduler (list_tmp, &schedule_entry_handle, cancellable, &error_local)) {
				g_warning ("Failed to block on download scheduler: %s",
					   error_local->message);
				g_clear_error (&error_local);
			}
		}

		/* build and run non-deployed transaction */
		transaction = _build_transaction (plugin, flatpak, interactive, cancellable, error);
		if (transaction == NULL) {
			gs_flatpak_error_convert (error);
			return FALSE;
		}

		flatpak_transaction_set_no_deploy (transaction, TRUE);

		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);
			g_autofree gchar *ref = NULL;
			g_autoptr(GError) error_local = NULL;

			ref = gs_flatpak_app_get_ref_display (app);
			if (flatpak_transaction_add_update (transaction, ref, NULL, NULL, &error_local))
				continue;

			/* Errors about missing remotes are not fatal, as that’s
			 * a not-uncommon situation. */
			if (g_error_matches (error_local, FLATPAK_ERROR, FLATPAK_ERROR_REMOTE_NOT_FOUND)) {
				g_autoptr(GsPluginEvent) event = NULL;

				g_warning ("Skipping update for ‘%s’: %s", ref, error_local->message);

				gs_flatpak_error_convert (&error_local);

				event = gs_plugin_event_new ("error", error_local,
							     NULL);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
				gs_plugin_report_event (plugin, event);
			} else {
				gs_flatpak_error_convert (&error_local);
				g_propagate_error (error, g_steal_pointer (&error_local));
				return FALSE;
			}
		}

		if (!gs_flatpak_transaction_run (transaction, cancellable, error)) {
			gs_flatpak_error_convert (error);
			remove_schedule_entry (schedule_entry_handle);
			return FALSE;
		}

		remove_schedule_entry (schedule_entry_handle);

		/* Traverse over the GsAppList again and set that the update has been already downloaded
		 * for the apps. */
		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);
			gs_app_set_is_update_downloaded (app, TRUE);
		}
	}

	return TRUE;
}

static void
gs_flatpak_cover_addons_in_transaction (GsPlugin *plugin,
					FlatpakTransaction *transaction,
					GsApp *parent_app,
					GsAppState state)
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

	if (errors) {
		g_autoptr(GsPluginEvent) event = NULL;
		g_autoptr(GError) error_local = g_error_new_literal (GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			errors->str);

		event = gs_plugin_event_new ("error", error_local,
					     NULL);
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
		gs_plugin_report_event (plugin, event);
	}
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	GsFlatpak *flatpak;
	g_autoptr(FlatpakTransaction) transaction = NULL;
	g_autofree gchar *ref = NULL;
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	/* not supported */
	flatpak = gs_plugin_flatpak_get_handler (self, app);
	if (flatpak == NULL)
		return TRUE;

	/* is a source, handled by dedicated function */
	g_return_val_if_fail (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY, FALSE);

	/* build and run transaction */
	transaction = _build_transaction (plugin, flatpak, gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE), cancellable, error);
	if (transaction == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	/* add to the transaction cache for quick look up -- other unrelated
	 * refs will be matched using gs_plugin_flatpak_find_app_by_ref() */
	gs_flatpak_transaction_add_app (transaction, app);

	ref = gs_flatpak_app_get_ref_display (app);
	if (!flatpak_transaction_add_uninstall (transaction, ref, error)) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	gs_flatpak_cover_addons_in_transaction (plugin, transaction, app, GS_APP_STATE_REMOVING);

	/* run transaction */
	gs_app_set_state (app, GS_APP_STATE_REMOVING);
	if (!gs_flatpak_transaction_run (transaction, cancellable, error)) {
		gs_flatpak_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* get any new state */
	gs_app_set_size_download (app, GS_SIZE_TYPE_UNKNOWN, 0);
	gs_app_set_size_installed (app, GS_SIZE_TYPE_UNKNOWN, 0);

	if (!gs_flatpak_refresh (flatpak, G_MAXUINT, interactive, cancellable, error)) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	if (!gs_flatpak_refine_app (flatpak, app,
				    GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID,
				    interactive,
				    cancellable, error)) {
		g_prefix_error (error, "failed to run refine for %s: ", ref);
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	gs_flatpak_refine_addons (flatpak,
				  app,
				  GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID,
				  GS_APP_STATE_REMOVING,
				  interactive,
				  cancellable);

	return TRUE;
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

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	GsFlatpak *flatpak;
	g_autoptr(FlatpakTransaction) transaction = NULL;
	g_autoptr(GError) error_local = NULL;
	gpointer schedule_entry_handle = NULL;
	gboolean already_installed = FALSE;
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	/* queue for install if installation needs the network */
	if (!app_has_local_source (app) &&
	    !gs_plugin_get_network_available (plugin)) {
		gs_app_set_state (app, GS_APP_STATE_QUEUED_FOR_INSTALL);
		return TRUE;
	}

	/* set the app scope */
	gs_plugin_flatpak_ensure_scope (plugin, app);

	/* not supported */
	flatpak = gs_plugin_flatpak_get_handler (self, app);
	if (flatpak == NULL)
		return TRUE;

	/* is a source, handled by dedicated function */
	g_return_val_if_fail (gs_app_get_kind (app) != AS_COMPONENT_KIND_REPOSITORY, FALSE);

	/* build */
	transaction = _build_transaction (plugin, flatpak, interactive, cancellable, error);
	if (transaction == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	/* add to the transaction cache for quick look up -- other unrelated
	 * refs will be matched using gs_plugin_flatpak_find_app_by_ref() */
	gs_flatpak_transaction_add_app (transaction, app);

	/* add flatpakref */
	if (gs_flatpak_app_get_file_kind (app) == GS_FLATPAK_APP_FILE_KIND_REF) {
		GFile *file = gs_app_get_local_file (app);
		g_autoptr(GBytes) blob = NULL;
		if (file == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no local file set for bundle %s",
				     gs_app_get_unique_id (app));
			return FALSE;
		}
		blob = g_file_load_bytes (file, cancellable, NULL, error);
		if (blob == NULL) {
			gs_flatpak_error_convert (error);
			return FALSE;
		}
		if (!flatpak_transaction_add_install_flatpakref (transaction, blob, error)) {
			gs_flatpak_error_convert (error);
			return FALSE;
		}

	/* add bundle */
	} else if (gs_flatpak_app_get_file_kind (app) == GS_FLATPAK_APP_FILE_KIND_BUNDLE) {
		GFile *file = gs_app_get_local_file (app);
		if (file == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no local file set for bundle %s",
				     gs_app_get_unique_id (app));
			return FALSE;
		}
		if (!flatpak_transaction_add_install_bundle (transaction, file,
							     NULL, error)) {
			gs_flatpak_error_convert (error);
			return FALSE;
		}

	/* add normal ref */
	} else {
		g_autofree gchar *ref = gs_flatpak_app_get_ref_display (app);
		if (!flatpak_transaction_add_install (transaction,
						      gs_app_get_origin (app),
						      ref, NULL, &error_local)) {
			/* Somehow, the app might already be installed. */
			if (g_error_matches (error_local, FLATPAK_ERROR,
					     FLATPAK_ERROR_ALREADY_INSTALLED)) {
				already_installed = TRUE;
				g_clear_error (&error_local);
			} else {
				g_propagate_error (error, g_steal_pointer (&error_local));
				gs_flatpak_error_convert (error);
				return FALSE;
			}
		}
	}

	gs_flatpak_cover_addons_in_transaction (plugin, transaction, app, GS_APP_STATE_INSTALLING);

	if (!interactive) {
		/* FIXME: Add additional details here, especially the download
		 * size bounds (using `size-minimum` and `size-maximum`, both
		 * type `t`). */
		if (!gs_metered_block_app_on_download_scheduler (app, &schedule_entry_handle, cancellable, &error_local)) {
			g_warning ("Failed to block on download scheduler: %s",
				   error_local->message);
			g_clear_error (&error_local);
		}
	}

	/* run transaction */
	if (!already_installed) {
		gs_app_set_state (app, GS_APP_STATE_INSTALLING);
		if (!gs_flatpak_transaction_run (transaction, cancellable, &error_local)) {
			/* Somehow, the app might already be installed. */
			if (g_error_matches (error_local, FLATPAK_ERROR,
					     FLATPAK_ERROR_ALREADY_INSTALLED)) {
				already_installed = TRUE;
				g_clear_error (&error_local);
			} else {
				g_propagate_error (error, g_steal_pointer (&error_local));
				gs_flatpak_error_convert (error);
				gs_app_set_state_recover (app);
				remove_schedule_entry (schedule_entry_handle);
				return FALSE;
			}
		}
	}

	if (already_installed) {
		/* Set the app back to UNKNOWN so that refining it gets all the right details. */
		g_debug ("App %s is already installed", gs_app_get_unique_id (app));
		gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
	}

	remove_schedule_entry (schedule_entry_handle);

	/* get any new state */
	if (!gs_flatpak_refresh (flatpak, G_MAXUINT, interactive, cancellable, error)) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	if (!gs_flatpak_refine_app (flatpak, app,
				    GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID,
				    interactive,
				    cancellable, error)) {
		g_prefix_error (error, "failed to run refine for %s: ",
				gs_app_get_unique_id (app));
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	gs_flatpak_refine_addons (flatpak,
				  app,
				  GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID,
				  GS_APP_STATE_INSTALLING,
				  interactive,
				  cancellable);

	return TRUE;
}

static gboolean
gs_plugin_flatpak_update (GsPlugin *plugin,
			  GsFlatpak *flatpak,
			  GsAppList *list_tmp,
			  gboolean interactive,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autoptr(FlatpakTransaction) transaction = NULL;
	gboolean is_update_downloaded = TRUE;
	gpointer schedule_entry_handle = NULL;

	if (!interactive) {
		g_autoptr(GError) error_local = NULL;

		if (!gs_metered_block_app_list_on_download_scheduler (list_tmp, &schedule_entry_handle, cancellable, &error_local)) {
			g_warning ("Failed to block on download scheduler: %s",
				   error_local->message);
			g_clear_error (&error_local);
		}
	}

	/* build and run transaction */
	transaction = _build_transaction (plugin, flatpak, interactive, cancellable, error);
	if (transaction == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
		GsApp *app = gs_app_list_index (list_tmp, i);
		g_autofree gchar *ref = NULL;
		g_autoptr(GError) error_local = NULL;

		ref = gs_flatpak_app_get_ref_display (app);
		if (flatpak_transaction_add_update (transaction, ref, NULL, NULL, error)) {
			/* add to the transaction cache for quick look up -- other unrelated
			 * refs will be matched using gs_plugin_flatpak_find_app_by_ref() */
			gs_flatpak_transaction_add_app (transaction, app);

			continue;
		}

		/* Errors about missing remotes are not fatal, as that’s
		 * a not-uncommon situation. */
		if (g_error_matches (error_local, FLATPAK_ERROR, FLATPAK_ERROR_REMOTE_NOT_FOUND)) {
			g_autoptr(GsPluginEvent) event = NULL;

			g_warning ("Skipping update for ‘%s’: %s", ref, error_local->message);

			gs_flatpak_error_convert (&error_local);

			event = gs_plugin_event_new ("error", error_local,
						     NULL);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			gs_plugin_report_event (plugin, event);
		} else {
			gs_flatpak_error_convert (&error_local);
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
	}

	/* run transaction */
	for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
		GsApp *app = gs_app_list_index (list_tmp, i);
		gs_app_set_state (app, GS_APP_STATE_INSTALLING);

		/* If all apps' update are previously downloaded and available locally,
		 * FlatpakTransaction should run with no-pull flag. This is the case
		 * for apps' autoupdates. */
		is_update_downloaded &= gs_app_get_is_update_downloaded (app);
	}

	if (is_update_downloaded) {
		flatpak_transaction_set_no_pull (transaction, TRUE);
	}

	/* automatically clean up unused EOL runtimes when updating */
	flatpak_transaction_set_include_unused_uninstall_ops (transaction, TRUE);

	if (!gs_flatpak_transaction_run (transaction, cancellable, error)) {
		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);
			gs_app_set_state_recover (app);
		}
		gs_flatpak_error_convert (error);
		remove_schedule_entry (schedule_entry_handle);
		return FALSE;
	} else {
		/* Reset the state to have it updated */
		for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
			GsApp *app = gs_app_list_index (list_tmp, i);
			gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
		}
	}

	remove_schedule_entry (schedule_entry_handle);
	gs_plugin_updates_changed (plugin);

	/* get any new state */
	if (!gs_flatpak_refresh (flatpak, G_MAXUINT, interactive, cancellable, error)) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < gs_app_list_length (list_tmp); i++) {
		GsApp *app = gs_app_list_index (list_tmp, i);
		g_autofree gchar *ref = NULL;

		ref = gs_flatpak_app_get_ref_display (app);
		if (!gs_flatpak_refine_app (flatpak, app,
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME,
					    interactive,
					    cancellable, error)) {
			g_prefix_error (error, "failed to run refine for %s: ", ref);
			gs_flatpak_error_convert (error);
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
gs_plugin_update (GsPlugin *plugin,
                  GsAppList *list,
                  GCancellable *cancellable,
                  GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GHashTable) applist_by_flatpaks = NULL;
	GHashTableIter iter;
	gpointer key, value;
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	/* build and run transaction for each flatpak installation */
	applist_by_flatpaks = _group_apps_by_installation (self, list);
	g_hash_table_iter_init (&iter, applist_by_flatpaks);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GsFlatpak *flatpak = GS_FLATPAK (key);
		GsAppList *list_tmp = GS_APP_LIST (value);
		gboolean success;

		g_assert (GS_IS_FLATPAK (flatpak));
		g_assert (list_tmp != NULL);
		g_assert (gs_app_list_length (list_tmp) > 0);

		gs_flatpak_set_busy (flatpak, TRUE);
		success = gs_plugin_flatpak_update (plugin, flatpak, list_tmp, interactive, cancellable, error);
		gs_flatpak_set_busy (flatpak, FALSE);
		if (!success)
			return FALSE;
	}
	return TRUE;
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
		app_tmp = gs_flatpak_find_source_by_url (flatpak,
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
                                      GCancellable     *cancellable,
                                      GError          **error)
{
	g_autofree gchar *ref = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsApp) app_tmp = NULL;
	g_autoptr(GsFlatpak) flatpak_tmp = NULL;

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
	app_tmp = gs_plugin_flatpak_find_app_by_ref (self, ref, interactive, cancellable, NULL);
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

	/* this is new */
	return g_steal_pointer (&app);
}

static GsApp *
gs_plugin_flatpak_file_to_app_ref (GsPluginFlatpak  *self,
                                   GFile            *file,
                                   gboolean          interactive,
                                   GCancellable     *cancellable,
                                   GError          **error)
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
					  interactive, cancellable, error);
	if (app == NULL)
		return NULL;

	/* is this already installed or available in a configured remote */
	ref = gs_flatpak_app_get_ref_display (app);
	app_tmp = gs_plugin_flatpak_find_app_by_ref (self, ref, interactive, cancellable, NULL);
	if (app_tmp != NULL)
		return g_steal_pointer (&app_tmp);

	/* If not installed/available, make a fully refined GsApp */
	g_clear_object (&app);
	app = gs_flatpak_file_to_app_ref (flatpak_tmp, file, FALSE /* unrefined */,
					  interactive, cancellable, error);
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

gboolean
gs_plugin_file_to_app (GsPlugin *plugin,
		       GsAppList *list,
		       GFile *file,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autofree gchar *content_type = NULL;
	g_autoptr(GsApp) app = NULL;
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);
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
		return FALSE;
	if (g_strv_contains (mimetypes_bundle, content_type)) {
		app = gs_plugin_flatpak_file_to_app_bundle (self, file, interactive,
							    cancellable, error);
		if (app == NULL)
			return FALSE;
	} else if (g_strv_contains (mimetypes_repo, content_type)) {
		app = gs_plugin_flatpak_file_to_app_repo (self, file, interactive,
							  cancellable, error);
		if (app == NULL)
			return FALSE;
	} else if (g_strv_contains (mimetypes_ref, content_type)) {
		app = gs_plugin_flatpak_file_to_app_ref (self, file, interactive,
							 cancellable, error);
		if (app == NULL)
			return FALSE;
	}
	if (app != NULL) {
		GsApp *runtime = gs_app_get_runtime (app);
		/* Ensure the origin for the runtime is set */
		if (runtime != NULL && gs_app_get_origin (runtime) == NULL) {
			g_autoptr(GError) error_local = NULL;
			if (!gs_plugin_flatpak_refine_app (self, runtime, GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN, interactive, cancellable, &error_local))
				g_debug ("Failed to refine runtime: %s", error_local->message);
		}
		gs_app_list_add (list, app);
	}
	return TRUE;
}

static gboolean
gs_plugin_flatpak_do_search (GsPlugin *plugin,
			     gchar **values,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		if (!gs_flatpak_search (flatpak, (const gchar * const *) values, list,
					interactive, cancellable, error)) {
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	return gs_plugin_flatpak_do_search (plugin, values, list, cancellable, error);
}

gboolean
gs_plugin_add_search_what_provides (GsPlugin *plugin,
				    gchar **search,
				    GsAppList *list,
				    GCancellable *cancellable,
				    GError **error)
{
	return gs_plugin_flatpak_do_search (plugin, search, list, cancellable, error);
}

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		if (!gs_flatpak_add_categories (flatpak, list, interactive, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_add_alternates (GsPlugin *plugin,
			  GsApp *app,
			  GsAppList *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		if (!gs_flatpak_add_alternates (flatpak, app, list, interactive, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

static void list_apps_thread_cb (GTask        *task,
                                 gpointer      source_object,
                                 gpointer      task_data,
                                 GCancellable *cancellable);

static void
gs_plugin_flatpak_list_apps_async (GsPlugin              *plugin,
                                   GsAppQuery            *query,
                                   GsPluginListAppsFlags  flags,
                                   GCancellable          *cancellable,
                                   GAsyncReadyCallback    callback,
                                   gpointer               user_data)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (flags & GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE);

	task = gs_plugin_list_apps_data_new_task (plugin, query, flags,
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
	GDateTime *released_since = NULL;
	GsAppQueryTristate is_curated = GS_APP_QUERY_TRISTATE_UNSET;
	GsAppQueryTristate is_featured = GS_APP_QUERY_TRISTATE_UNSET;
	GsCategory *category = NULL;
	GsAppQueryTristate is_installed = GS_APP_QUERY_TRISTATE_UNSET;
	guint64 age_secs = 0;
	const gchar * const *deployment_featured = NULL;
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	if (data->query != NULL) {
		released_since = gs_app_query_get_released_since (data->query);
		is_curated = gs_app_query_get_is_curated (data->query);
		is_featured = gs_app_query_get_is_featured (data->query);
		category = gs_app_query_get_category (data->query);
		is_installed = gs_app_query_get_is_installed (data->query);
		deployment_featured = gs_app_query_get_deployment_featured (data->query);
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
	     deployment_featured == NULL) ||
	    is_curated == GS_APP_QUERY_TRISTATE_FALSE ||
	    is_featured == GS_APP_QUERY_TRISTATE_FALSE ||
	    is_installed == GS_APP_QUERY_TRISTATE_FALSE ||
	    gs_app_query_get_n_properties_set (data->query) != 1) {
		g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					 "Unsupported query");
		return;
	}

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);

		if (released_since != NULL &&
		    !gs_flatpak_add_recent (flatpak, list, age_secs, interactive, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (is_curated != GS_APP_QUERY_TRISTATE_UNSET &&
		    !gs_flatpak_add_popular (flatpak, list, interactive, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (is_featured != GS_APP_QUERY_TRISTATE_UNSET &&
		    !gs_flatpak_add_featured (flatpak, list, interactive, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (category != NULL &&
		    !gs_flatpak_add_category_apps (flatpak, category, list, interactive, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (is_installed != GS_APP_QUERY_TRISTATE_UNSET &&
		    !gs_flatpak_add_installed (flatpak, list, interactive, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		if (deployment_featured != NULL &&
		    !gs_flatpak_add_deployment_featured (flatpak, list, interactive, deployment_featured, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_flatpak_list_apps_finish (GsPlugin      *plugin,
                                    GAsyncResult  *result,
                                    GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

gboolean
gs_plugin_url_to_app (GsPlugin *plugin,
		      GsAppList *list,
		      const gchar *url,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	for (guint i = 0; i < self->installations->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (self->installations, i);
		if (!gs_flatpak_url_to_app (flatpak, list, url, interactive, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_install_repo (GsPlugin *plugin,
			GsApp *repo,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	GsFlatpak *flatpak;
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	/* queue for install if installation needs the network */
	if (!app_has_local_source (repo) &&
	    !gs_plugin_get_network_available (plugin)) {
		gs_app_set_state (repo, GS_APP_STATE_QUEUED_FOR_INSTALL);
		return TRUE;
	}

	gs_plugin_flatpak_ensure_scope (plugin, repo);

	flatpak = gs_plugin_flatpak_get_handler (self, repo);
	if (flatpak == NULL)
		return TRUE;

	/* is a source */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	return gs_flatpak_app_install_source (flatpak, repo, TRUE, interactive, cancellable, error);
}

gboolean
gs_plugin_remove_repo (GsPlugin *plugin,
		       GsApp *repo,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	GsFlatpak *flatpak;
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	flatpak = gs_plugin_flatpak_get_handler (self, repo);
	if (flatpak == NULL)
		return TRUE;

	/* is a source */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	return gs_flatpak_app_remove_source (flatpak, repo, TRUE, interactive, cancellable, error);
}

gboolean
gs_plugin_enable_repo (GsPlugin *plugin,
		       GsApp *repo,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	GsFlatpak *flatpak;
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	flatpak = gs_plugin_flatpak_get_handler (self, repo);
	if (flatpak == NULL)
		return TRUE;

	/* is a source */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	return gs_flatpak_app_install_source (flatpak, repo, FALSE, interactive, cancellable, error);
}

gboolean
gs_plugin_disable_repo (GsPlugin *plugin,
			GsApp *repo,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginFlatpak *self = GS_PLUGIN_FLATPAK (plugin);
	GsFlatpak *flatpak;
	gboolean interactive = gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);

	flatpak = gs_plugin_flatpak_get_handler (self, repo);
	if (flatpak == NULL)
		return TRUE;

	/* is a source */
	g_return_val_if_fail (gs_app_get_kind (repo) == AS_COMPONENT_KIND_REPOSITORY, FALSE);

	return gs_flatpak_app_remove_source (flatpak, repo, FALSE, interactive, cancellable, error);
}

static void
gs_plugin_flatpak_class_init (GsPluginFlatpakClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_flatpak_dispose;

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
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_FLATPAK;
}
