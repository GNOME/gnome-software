/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>

#include "packagekit-common.h"

#include <gnome-software.h>

#include "gs-plugin-systemd-updates.h"

/*
 * Mark previously downloaded packages as zero size, and also allow
 * scheduling the offline update.
 */

struct _GsPluginSystemdUpdates {
	GsPlugin		 parent;

	GFileMonitor		*monitor;
	GFileMonitor		*monitor_trigger;
	GPermission		*permission;
	gboolean		 is_triggered;
	GHashTable		*hash_prepared;
	GMutex			 hash_prepared_mutex;
};

G_DEFINE_TYPE (GsPluginSystemdUpdates, gs_plugin_systemd_updates, GS_TYPE_PLUGIN)

static void
gs_plugin_systemd_updates_init (GsPluginSystemdUpdates *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit-refresh");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "generic-updates");

	g_mutex_init (&self->hash_prepared_mutex);
	self->hash_prepared = g_hash_table_new_full (g_str_hash, g_str_equal,
						     g_free, NULL);
}

static void
gs_plugin_systemd_updates_dispose (GObject *object)
{
	GsPluginSystemdUpdates *self = GS_PLUGIN_SYSTEMD_UPDATES (object);

	g_clear_pointer (&self->hash_prepared, g_hash_table_unref);
	g_clear_object (&self->monitor);
	g_clear_object (&self->monitor_trigger);

	G_OBJECT_CLASS (gs_plugin_systemd_updates_parent_class)->dispose (object);
}

static void
gs_plugin_systemd_updates_finalize (GObject *object)
{
	GsPluginSystemdUpdates *self = GS_PLUGIN_SYSTEMD_UPDATES (object);

	g_mutex_clear (&self->hash_prepared_mutex);

	G_OBJECT_CLASS (gs_plugin_systemd_updates_parent_class)->finalize (object);
}

/* Run in the main thread. */
static void
gs_plugin_systemd_updates_permission_cb (GPermission *permission,
					 GParamSpec *pspec,
					 gpointer data)
{
	GsPlugin *plugin = GS_PLUGIN (data);
	gboolean ret = g_permission_get_allowed (permission) ||
			g_permission_get_can_acquire (permission);
	gs_plugin_set_allow_updates (plugin, ret);
}

static gboolean
gs_plugin_systemd_update_cache (GsPluginSystemdUpdates  *self,
                                GError                 **error)
{
	g_autoptr(GError) error_local = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&self->hash_prepared_mutex);

	/* invalidate */
	g_hash_table_remove_all (self->hash_prepared);

	/* get new list of package-ids. This loads a local file, so should be
	 * just about fast enough to be sync. */
	package_ids = pk_offline_get_prepared_ids (&error_local);
	if (package_ids == NULL) {
		if (g_error_matches (error_local,
				     PK_OFFLINE_ERROR,
				     PK_OFFLINE_ERROR_NO_DATA)) {
			return TRUE;
		}
		gs_plugin_packagekit_error_convert (&error_local);
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "Failed to get prepared IDs: %s",
			     error_local->message);
		return FALSE;
	}
	for (guint i = 0; package_ids[i] != NULL; i++) {
		g_hash_table_insert (self->hash_prepared,
				     g_strdup (package_ids[i]),
				     GUINT_TO_POINTER (1));
	}
	return TRUE;
}

/* Run in the main thread. */
static void
gs_plugin_systemd_updates_changed_cb (GFileMonitor *monitor,
				      GFile *file, GFile *other_file,
				      GFileMonitorEvent event_type,
				      gpointer user_data)
{
	GsPluginSystemdUpdates *self = GS_PLUGIN_SYSTEMD_UPDATES (user_data);

	/* update UI */
	gs_plugin_systemd_update_cache (self, NULL);
	gs_plugin_updates_changed (GS_PLUGIN (self));
}

static void
gs_plugin_systemd_updates_refresh_is_triggered (GsPluginSystemdUpdates *self,
                                                GCancellable           *cancellable)
{
	g_autoptr(GFile) file_trigger = NULL;
	file_trigger = g_file_new_for_path ("/system-update");
	self->is_triggered = g_file_query_exists (file_trigger, NULL);
	g_debug ("offline trigger is now %s",
		 self->is_triggered ? "enabled" : "disabled");
}

/* Run in the main thread. */
static void
gs_plugin_systemd_trigger_changed_cb (GFileMonitor *monitor,
				      GFile *file, GFile *other_file,
				      GFileMonitorEvent event_type,
				      gpointer user_data)
{
	GsPluginSystemdUpdates *self = GS_PLUGIN_SYSTEMD_UPDATES (user_data);

	gs_plugin_systemd_updates_refresh_is_triggered (self, NULL);
}

static void
gs_plugin_systemd_refine_app (GsPluginSystemdUpdates *self,
                              GsApp                  *app)
{
	const gchar *package_id;
	g_autoptr(GMutexLocker) locker = NULL;

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
		return;

	/* the package is already downloaded */
	package_id = gs_app_get_source_id_default (app);
	if (package_id == NULL)
		return;
	locker = g_mutex_locker_new (&self->hash_prepared_mutex);
	if (g_hash_table_lookup (self->hash_prepared, package_id) != NULL)
		gs_app_set_size_download (app, 0);
}

static void
gs_plugin_systemd_updates_refine_async (GsPlugin            *plugin,
                                        GsAppList           *list,
                                        GsPluginRefineFlags  flags,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
	GsPluginSystemdUpdates *self = GS_PLUGIN_SYSTEMD_UPDATES (plugin);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;

	task = gs_plugin_refine_data_new_task (plugin, list, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_updates_refine_async);

	/* not now */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) == 0) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* re-read /var/lib/PackageKit/prepared-update */
	if (!gs_plugin_systemd_update_cache (self, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsAppList *related = gs_app_get_related (app);
		/* refine the app itself */
		gs_plugin_systemd_refine_app (self, app);
		/* and anything related for proxy apps */
		for (guint j = 0; j < gs_app_list_length (related); j++) {
			GsApp *app_related = gs_app_list_index (related, j);
			gs_plugin_systemd_refine_app (self, app_related);
		}
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_systemd_updates_refine_finish (GsPlugin      *plugin,
                                         GAsyncResult  *result,
                                         GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void get_permission_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data);

static void
gs_plugin_systemd_updates_setup_async (GsPlugin            *plugin,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
	GsPluginSystemdUpdates *self = GS_PLUGIN_SYSTEMD_UPDATES (plugin);
	g_autoptr(GFile) file_trigger = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_updates_setup_async);

	/* watch the prepared file */
	self->monitor = pk_offline_get_prepared_monitor (cancellable, &local_error);
	if (self->monitor == NULL) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_signal_connect (self->monitor, "changed",
			  G_CALLBACK (gs_plugin_systemd_updates_changed_cb),
			  plugin);

	/* watch the trigger file */
	file_trigger = g_file_new_for_path ("/system-update");
	self->monitor_trigger = g_file_monitor_file (file_trigger,
						     G_FILE_MONITOR_NONE,
						     NULL,
						     &local_error);
	if (self->monitor_trigger == NULL) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_signal_connect (self->monitor_trigger, "changed",
			  G_CALLBACK (gs_plugin_systemd_trigger_changed_cb),
			  plugin);

	/* check if we have permission to trigger the update */
	gs_utils_get_permission_async ("org.freedesktop.packagekit.trigger-offline-update",
				       cancellable, get_permission_cb, g_steal_pointer (&task));
}

static void
get_permission_cb (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginSystemdUpdates *self = g_task_get_source_object (task);
	g_autoptr(GError) local_error = NULL;

	self->permission = gs_utils_get_permission_finish (result, &local_error);
	if (self->permission != NULL) {
		g_signal_connect (self->permission, "notify",
				  G_CALLBACK (gs_plugin_systemd_updates_permission_cb),
				  self);
	}

	/* get the list of currently downloaded packages */
	if (!gs_plugin_systemd_update_cache (self, &local_error))
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_systemd_updates_setup_finish (GsPlugin      *plugin,
                                        GAsyncResult  *result,
                                        GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

#ifdef HAVE_PK_OFFLINE_WITH_FLAGS

static PkOfflineFlags
gs_systemd_get_offline_flags (GsPlugin *plugin)
{
	if (gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE))
		return PK_OFFLINE_FLAGS_INTERACTIVE;
	return PK_OFFLINE_FLAGS_NONE;
}

static gboolean
gs_systemd_call_trigger (GsPlugin *plugin,
			 PkOfflineAction action,
			 GCancellable *cancellable,
			 GError **error)
{
	return pk_offline_trigger_with_flags (action,
					      gs_systemd_get_offline_flags (plugin),
					      cancellable, error);
}

static gboolean
gs_systemd_call_cancel (GsPlugin *plugin,
			GCancellable *cancellable,
			GError **error)
{
	return pk_offline_cancel_with_flags (gs_systemd_get_offline_flags (plugin), cancellable, error);
}

static gboolean
gs_systemd_call_trigger_upgrade (GsPlugin *plugin,
				 PkOfflineAction action,
				 GCancellable *cancellable,
				 GError **error)
{
	return pk_offline_trigger_upgrade_with_flags (action,
						      gs_systemd_get_offline_flags (plugin),
						      cancellable, error);
}

#else /* HAVE_PK_OFFLINE_WITH_FLAGS */

static GDBusCallFlags
gs_systemd_get_gdbus_call_flags (GsPlugin *plugin)
{
	if (gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE))
		return G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION;
	return G_DBUS_CALL_FLAGS_NONE;
}

static gboolean
gs_systemd_call_trigger (GsPlugin *plugin,
			 PkOfflineAction action,
			 GCancellable *cancellable,
			 GError **error)
{
	const gchar *tmp;
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) res = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
	if (connection == NULL)
		return FALSE;
	tmp = pk_offline_action_to_string (action);
	res = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Offline",
					   "Trigger",
					   g_variant_new ("(s)", tmp),
					   NULL,
					   gs_systemd_get_gdbus_call_flags (plugin),
					   -1,
					   cancellable,
					   error);
	if (res == NULL)
		return FALSE;
	return TRUE;
}

static gboolean
gs_systemd_call_cancel (GsPlugin *plugin,
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) res = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
	if (connection == NULL)
		return FALSE;
	res = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Offline",
					   "Cancel",
					   NULL,
					   NULL,
					   gs_systemd_get_gdbus_call_flags (plugin),
					   -1,
					   cancellable,
					   error);
	if (res == NULL)
		return FALSE;
	return TRUE;
}

static gboolean
gs_systemd_call_trigger_upgrade (GsPlugin *plugin,
				 PkOfflineAction action,
				 GCancellable *cancellable,
				 GError **error)
{
	const gchar *tmp;
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) res = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
	if (connection == NULL)
		return FALSE;
	tmp = pk_offline_action_to_string (action);
	res = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Offline",
					   "TriggerUpgrade",
					   g_variant_new ("(s)", tmp),
					   NULL,
					   gs_systemd_get_gdbus_call_flags (plugin),
					   -1,
					   cancellable,
					   error);
	if (res == NULL)
		return FALSE;
	return TRUE;
}

#endif /* HAVE_PK_OFFLINE_WITH_FLAGS */

static gboolean
_systemd_trigger_app (GsPluginSystemdUpdates  *self,
                      GsApp                   *app,
                      GCancellable            *cancellable,
                      GError                 **error)
{
	/* if we can process this online do not require a trigger */
	if (gs_app_get_state (app) != GS_APP_STATE_UPDATABLE)
		return TRUE;

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, GS_PLUGIN (self)))
		return TRUE;

	/* already in correct state */
	if (self->is_triggered)
		return TRUE;

	/* trigger offline update */
	if (!gs_systemd_call_trigger (GS_PLUGIN (self), PK_OFFLINE_ACTION_REBOOT, cancellable, error)) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}

	/* don't rely on the file monitor */
	gs_plugin_systemd_updates_refresh_is_triggered (self, cancellable);

	/* success */
	return TRUE;
}

gboolean
gs_plugin_update (GsPlugin *plugin,
		  GsAppList *list,
		  GCancellable *cancellable,
		  GError **error)
{
	GsPluginSystemdUpdates *self = GS_PLUGIN_SYSTEMD_UPDATES (plugin);

	/* any are us? */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsAppList *related = gs_app_get_related (app);

		/* try to trigger this app */
		if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_PROXY)) {
			if (!_systemd_trigger_app (self, app, cancellable, error))
				return FALSE;
			continue;
		}

		/* try to trigger each related app */
		for (guint j = 0; j < gs_app_list_length (related); j++) {
			GsApp *app_tmp = gs_app_list_index (related, j);
			if (!_systemd_trigger_app (self, app_tmp, cancellable, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
gs_plugin_update_cancel (GsPlugin *plugin,
			 GsApp *app,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginSystemdUpdates *self = GS_PLUGIN_SYSTEMD_UPDATES (plugin);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin))
		return TRUE;

	/* already in correct state */
	if (!self->is_triggered)
		return TRUE;

	/* cancel offline update */
	if (!gs_systemd_call_cancel (plugin, cancellable, error)) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}

	/* don't rely on the file monitor */
	gs_plugin_systemd_updates_refresh_is_triggered (self, cancellable);

	/* success! */
	return TRUE;
}

gboolean
gs_plugin_app_upgrade_trigger (GsPlugin *plugin,
                               GsApp *app,
                               GCancellable *cancellable,
                               GError **error)
{
	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin))
		return TRUE;

	if (!gs_systemd_call_trigger_upgrade (plugin, PK_OFFLINE_ACTION_REBOOT, cancellable, error)) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}
	return TRUE;
}

static void
gs_plugin_systemd_updates_class_init (GsPluginSystemdUpdatesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_systemd_updates_dispose;
	object_class->finalize = gs_plugin_systemd_updates_finalize;

	plugin_class->setup_async = gs_plugin_systemd_updates_setup_async;
	plugin_class->setup_finish = gs_plugin_systemd_updates_setup_finish;
	plugin_class->refine_async = gs_plugin_systemd_updates_refine_async;
	plugin_class->refine_finish = gs_plugin_systemd_updates_refine_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_SYSTEMD_UPDATES;
}
