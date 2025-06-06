/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (c) 2024 Codethink Limited
 * Copyright (c) 2024 GNOME Foundation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <malloc.h>
#include <xmlb.h>

#include <gnome-software.h>

#include "gs-appstream.h"
#include "gs-external-appstream-utils.h"
#include "gs-metered.h"
#include "gs-plugin-systemd-sysupdate.h"
#include "gs-systemd-sysupdated-generated.h"

/*
 * Plugin to allow system updates using `systemd-sysupdated`.
 *
 * This plugin only works when systemd-sysupdated's org.freedesktop.sysupdate1
 * D-Bus service is available on the system. For more information see the
 * following links:
 * - https://github.com/systemd/systemd/blob/main/docs/APPSTREAM_BUNDLE.md
 * - https://github.com/systemd/systemd/blob/main/man/org.freedesktop.sysupdate1.xml
 * - https://github.com/systemd/systemd/blob/main/man/systemd-sysupdated.service.xml
 * - https://github.com/systemd/systemd/blob/main/man/systemd-sysupdate.xml
 * - https://github.com/systemd/systemd/blob/main/man/sysupdate.d.xml
 * - https://github.com/systemd/systemd/blob/main/man/sysupdate.features.xml
 * - https://github.com/systemd/systemd/blob/main/man/updatectl.xml
 *
 * `systemd-sysupdated` provides a D-Bus interface, so this plugin runs
 * asynchronously in the main thread, acting as a thin wrapper over that D-Bus
 * interface. It doesn’t need to do any locking.
 */

#define FREEDESKTOP_DBUS_LIST_ACTIVATABLE_NAMES_TIMEOUT_MS (200)
#define SYSUPDATED_JOB_CANCEL_TIMEOUT_MS (1000)
#define SYSUPDATED_MANAGER_LIST_TARGET_TIMEOUT_MS (1000)
#define SYSUPDATED_TARGET_CHECK_NEW_TIMEOUT_MS (10000)
#define SYSUPDATED_TARGET_DESCRIBE_TIMEOUT_MS (1000)
#define SYSUPDATED_TARGET_GET_APP_STREAM_TIMEOUT_MS (1000)
#define SYSUPDATED_TARGET_GET_VERSION_TIMEOUT_MS (1000)
#define SYSUPDATED_TARGET_UPDATE_TIMEOUT_MS (-1)

/* See the org.freedesktop.sysupdate1 manual for a list of flags. */
#define SYSUPDATED_TARGET_DESCRIBE_FLAGS_NONE ((guint64) 0)
#define SYSUPDATED_TARGET_DESCRIBE_FLAGS_OFFLINE ((guint64) (1 << 0))
#define SYSUPDATED_TARGET_UPDATE_FLAGS_NONE ((guint64) 0)

/* Structure stores the `target` information reported by
 * `systemd-sysupdated` */
typedef struct {
	GsSystemdSysupdateTarget *proxy;
	gboolean is_valid;
	gchar *id; /* (owned) (not nullable) */
	gchar *class; /* (owned) (not nullable) */
	gchar *name; /* (owned) (not nullable) */
	gchar *object_path; /* (owned) (not nullable) */
	gchar *current_version; /* (owned) (nullable) */
	gchar *latest_version; /* (owned) (nullable) */
	gchar *cache_hash; /* (owned) (nullable) */
	gchar *xml_cache_kind; /* (owned) (nullable) */
	GFile *xml_blob; /* (owned) (nullable) */
	XbSilo *silo; /* (owned) (nullable) */
} TargetItem;

static TargetItem *
target_item_new (const gchar *class, const gchar *name, const gchar *object_path)
{
	TargetItem *target = g_new0 (TargetItem, 1);
	target->is_valid = TRUE; /* default to true on creation */
	if (g_strcmp0 (class, "host") == 0) {
		target->id = g_strdup ("host");
	} else {
		target->id = g_strdup_printf ("%s-%s", class, name);
	}
	target->class = g_strdup (class);
	target->name = g_strdup (name);
	target->object_path = g_strdup (object_path);
	return target;
}

static void
target_item_free (TargetItem *target)
{
	target->is_valid = FALSE;
	g_clear_object (&target->proxy);
	g_clear_pointer (&target->id, g_free);
	g_clear_pointer (&target->class, g_free);
	g_clear_pointer (&target->name, g_free);
	g_clear_pointer (&target->object_path, g_free);
	g_clear_pointer (&target->current_version, g_free);
	g_clear_pointer (&target->latest_version, g_free);
	g_clear_pointer (&target->cache_hash, g_free);
	g_clear_pointer (&target->xml_cache_kind, g_free);
	g_clear_object (&target->xml_blob);
	g_clear_object (&target->silo);
	g_free (target);
}

static const gchar *
target_item_get_id (TargetItem *target)
{
	return target->id;
}

static gboolean
target_item_is_available (TargetItem *target)
{
	return target->latest_version != NULL;
}

static gboolean
target_item_is_installed (TargetItem *target)
{
	return target->current_version != NULL;
}

static gboolean
target_item_is_updatable (TargetItem *target)
{
	return target_item_is_available (target) && target_item_is_installed (target);
}

static gboolean
target_item_matches_keywords (TargetItem         *target,
                              const gchar *const *keywords)
{
	return g_strv_contains (keywords, "sysupdate") ||
	       g_strv_contains (keywords, target->class) ||
	       g_strv_contains (keywords, target->name);
}

static const gchar *
target_item_get_cache_hash (TargetItem  *target,
                            GError     **error)
{
	if (target->cache_hash != NULL) {
		return target->cache_hash;
	}

	target->cache_hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1, target->object_path, -1);
	if (target->cache_hash == NULL) {
		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_FAILED,
		             "Failed to hash object path ‘%s’",
		             target->object_path);
		return NULL;
	}

	return target->cache_hash;
}

static const gchar *
target_item_get_xml_cache_kind (TargetItem  *target,
                                GsPlugin    *plugin,
                                GError     **error)
{
	const gchar *cache_hash;

	if (target->xml_cache_kind != NULL) {
		return target->xml_cache_kind;
	}

	cache_hash = target_item_get_cache_hash (target, error);
	if (cache_hash == NULL) {
		return NULL;
	}

	target->xml_cache_kind = g_build_filename (gs_plugin_get_name (plugin), cache_hash, "xml", NULL);

	return target->xml_cache_kind;
}

static GFile *
target_item_get_xml_blob (TargetItem  *target,
                          GsPlugin    *plugin,
                          GError     **error)
{
	const gchar *cache_hash;
	g_autofree gchar *cache_kind = NULL;
	g_autofree gchar *xml_blob_path = NULL;

	if (target->xml_blob != NULL) {
		return target->xml_blob;
	}

	cache_hash = target_item_get_cache_hash (target, error);
	if (cache_hash == NULL) {
		return NULL;
	}

	cache_kind = g_build_filename (gs_plugin_get_name (plugin), cache_hash, NULL);
	xml_blob_path = gs_utils_get_cache_filename (cache_kind,
	                                             "components.xmlb",
	                                             GS_UTILS_CACHE_FLAG_WRITEABLE | GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
	                                             error);
	if (xml_blob_path == NULL) {
		return NULL;
	}

	target->xml_blob = g_file_new_for_path (xml_blob_path);

	return target->xml_blob;
}

static XbSilo *
target_item_ensure_silo_for_appstream_paths (TargetItem    *target,
                                             GsPlugin      *plugin,
                                             GStrv          appstream_paths,
                                             GCancellable  *cancellable,
                                             GError       **error)
{
	GFile* xml_blob = NULL;
	g_autoptr(XbBuilder) builder = NULL;
	g_autoptr(XbSilo) silo = NULL;

	builder = xb_builder_new ();

	/* Verbose profiling. */
	if (g_getenv ("GS_XMLB_VERBOSE") != NULL) {
		xb_builder_set_profile_flags (builder,
					      XB_SILO_PROFILE_FLAG_XPATH |
					      XB_SILO_PROFILE_FLAG_DEBUG);
	}

	gs_appstream_add_current_locales (builder);

	for (GStrv appstream_paths_l = appstream_paths; appstream_paths_l != NULL && *appstream_paths_l != NULL; appstream_paths_l++) {
		g_autoptr(XbBuilderSource) source = NULL;
		g_autoptr(GFile) appstream_file = NULL;
		g_autoptr(XbBuilderNode) info = NULL;

		source = xb_builder_source_new ();
		appstream_file = g_file_new_for_path (*appstream_paths_l);
		if (!xb_builder_source_load_file (source,
		                                  appstream_file,
		                                  XB_BUILDER_SOURCE_FLAG_WATCH_FILE | XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
		                                  cancellable,
		                                  error)) {
			return NULL;
		}

		/* Add metadata. */
		info = xb_builder_node_insert (NULL, "info", NULL);
		xb_builder_node_insert_text (info, "scope", as_component_scope_to_string (AS_COMPONENT_SCOPE_SYSTEM), NULL);
		xb_builder_source_set_info (source, info);

		xb_builder_import_source (builder, source);
	}

	/* Regenerate with each minor release. */
	xb_builder_append_guid (builder, PACKAGE_VERSION);

	xml_blob = target_item_get_xml_blob (target, plugin, error);
	if (xml_blob == NULL) {
		return NULL;
	}

	silo = xb_builder_ensure (builder, xml_blob,
	                          XB_BUILDER_COMPILE_FLAG_IGNORE_INVALID | XB_BUILDER_COMPILE_FLAG_SINGLE_LANG,
	                          cancellable,
	                          error);
#ifdef __GLIBC__
	/* https://gitlab.gnome.org/GNOME/gnome-software/-/issues/941 
	 * libxmlb <= 0.3.22 makes lots of temporary heap allocations parsing large XMLs
	 * trim the heap after parsing to control RSS growth. */
	malloc_trim (0);
#endif

	if (silo == NULL) {
		return NULL;
	}

	g_clear_object (&target->silo);
	target->silo = g_steal_pointer (&silo);

	return target->silo;
}

/* Structure stores the `targets` whose information to be updated in
 * queue and the current working `target` */
typedef struct {
	GQueue *queue; /* (owned) (not nullable) (element-type TargetItem) */
	GsPluginRefreshMetadataFlags flags;
} GsPluginSystemdSysupdateRefreshMetadataData;

/* Takes ownership of @queue */
static GsPluginSystemdSysupdateRefreshMetadataData *
gs_plugin_systemd_sysupdate_refresh_metadata_data_new (GQueue                       *queue,
                                                       GsPluginRefreshMetadataFlags  flags)
{
	GsPluginSystemdSysupdateRefreshMetadataData *data = g_new0 (GsPluginSystemdSysupdateRefreshMetadataData, 1);
	data->queue = g_steal_pointer (&queue);
	data->flags = flags;
	return data;
}

static void
gs_plugin_systemd_sysupdate_refresh_metadata_data_free (GsPluginSystemdSysupdateRefreshMetadataData *data)
{
	g_clear_pointer (&data->queue, g_queue_free);
	data->flags = 0;
	g_free (data);
}

/* Structure stores the `targets` whose information to be updated in
 * queue and the current working `target` */
typedef struct {
	TargetItem *target;  /* (not owned) (nullable) */
	GsPluginRefreshMetadataFlags flags;
} GsPluginSystemdSysupdateTargetRefreshMetadataData;

static GsPluginSystemdSysupdateTargetRefreshMetadataData *
gs_plugin_systemd_sysupdate_target_refresh_metadata_data_new (TargetItem                   *target,
                                                              GsPluginRefreshMetadataFlags  flags)
{
	GsPluginSystemdSysupdateTargetRefreshMetadataData *data = g_new0 (GsPluginSystemdSysupdateTargetRefreshMetadataData, 1);
	data->target = target;
	data->flags = flags;
	return data;
}

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_data_free (GsPluginSystemdSysupdateTargetRefreshMetadataData *data)
{
	data->target = NULL;
	data->flags = 0;
	g_free (data);
}

/* Structure stores the `targets` whose information to be refined in
 * queue and the current working `target` */
typedef struct {
	GQueue *queue; /* (owned) (not nullable) (element-type TargetItem) */
	GsPluginRefineFlags job_flags;
	GsPluginRefineRequireFlags require_flags;
} GsPluginSystemdSysupdateRefineData;

static GsPluginSystemdSysupdateRefineData *
gs_plugin_systemd_sysupdate_refine_data_new (GQueue                     *queue,
                                             GsPluginRefineFlags         job_flags,
                                             GsPluginRefineRequireFlags  require_flags)
{
	GsPluginSystemdSysupdateRefineData *data = g_new0 (GsPluginSystemdSysupdateRefineData, 1);
	data->queue = g_steal_pointer (&queue);
	data->job_flags = job_flags;
	data->require_flags = require_flags;
	return data;
}

static void
gs_plugin_systemd_sysupdate_refine_data_free (GsPluginSystemdSysupdateRefineData *data)
{
	if (data->queue != NULL) {
		g_queue_free_full (data->queue, g_object_unref);
		data->queue = NULL;
	}
	data->job_flags = 0;
	data->require_flags = 0;
	g_free (data);
}

typedef struct {
	GsApp *app; /* (owned) (not nullable) */
} GsPluginSystemdSysupdateRefineAppData;

static GsPluginSystemdSysupdateRefineAppData *
gs_plugin_systemd_sysupdate_refine_app_data_new (GsApp *app)
{
	GsPluginSystemdSysupdateRefineAppData *data = g_new0 (GsPluginSystemdSysupdateRefineAppData, 1);
	data->app = g_object_ref (app);
	return data;
}

static void
gs_plugin_systemd_sysupdate_refine_app_data_free (GsPluginSystemdSysupdateRefineAppData *data)
{
	g_clear_object (&data->app);
	g_free (data);
}

typedef struct {
	GQueue *queue; /* (owned) (not nullable) (element-type GsApp) */
	GsPluginProgressCallback progress_callback;
	gpointer progress_user_data;
	GsPluginAppNeedsUserActionCallback app_needs_user_action_callback;
	gpointer app_needs_user_action_data;
	GCancellable *cancellable; /* (owned) (nullable) */
	GsPluginUpdateAppsFlags flags;
} GsPluginSystemdSysupdateUpdateAppsData;

static GsPluginSystemdSysupdateUpdateAppsData *
gs_plugin_systemd_sysupdate_update_apps_data_new (GQueue                             *queue,
                                                  GsPluginProgressCallback            progress_callback,
                                                  gpointer                            progress_user_data,
                                                  GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                                  gpointer                            app_needs_user_action_data,
                                                  GCancellable                       *cancellable,
                                                  GsPluginUpdateAppsFlags             flags)
{
	GsPluginSystemdSysupdateUpdateAppsData *data = g_new0 (GsPluginSystemdSysupdateUpdateAppsData, 1);
	data->queue = g_steal_pointer (&queue);
	data->progress_callback = progress_callback;
	data->progress_user_data = progress_user_data;
	data->app_needs_user_action_callback = app_needs_user_action_callback;
	data->app_needs_user_action_data = app_needs_user_action_data;
	g_set_object (&data->cancellable, cancellable);
	data->flags = flags;
	return data;
}

static void
gs_plugin_systemd_sysupdate_update_apps_data_free (GsPluginSystemdSysupdateUpdateAppsData *data)
{
	if (data->queue != NULL) {
		g_queue_free_full (data->queue, g_object_unref);
		data->queue = NULL;
	}
	data->progress_callback = NULL;
	data->progress_user_data = NULL;
	data->app_needs_user_action_callback = NULL;
	data->app_needs_user_action_data = NULL;
	g_clear_object (&data->cancellable);
	data->flags = 0;
	g_free (data);
}

typedef struct {
	GsApp *app; /* (owned) (not nullable) */
	GCancellable *cancellable; /* (owned) (nullable) */
	gulong cancelled_id;
	gboolean interactive;
	gpointer schedule_entry_handle;
} GsPluginSystemdSysupdateUpdateAppData;

static void
gs_plugin_systemd_sysupdate_update_app_data_remove_from_download_scheduler_cb (GObject      *source_object,
                                                                               GAsyncResult *result,
                                                                               gpointer      schedule_entry_handle);

static GsPluginSystemdSysupdateUpdateAppData *
gs_plugin_systemd_sysupdate_update_app_data_new (GsApp        *app,
                                                 GCancellable *cancellable,
                                                 gulong        cancelled_id,
                                                 gboolean      interactive)
{
	GsPluginSystemdSysupdateUpdateAppData *data = g_new0 (GsPluginSystemdSysupdateUpdateAppData, 1);
	data->app = g_object_ref (app);
	g_set_object (&data->cancellable, cancellable);
	data->cancelled_id = cancelled_id;
	data->interactive = interactive;
	return data;
}

static void
gs_plugin_systemd_sysupdate_update_app_data_free (GsPluginSystemdSysupdateUpdateAppData *data)
{
	g_cancellable_disconnect (data->cancellable, data->cancelled_id);

	g_clear_object (&data->app);
	g_clear_object (&data->cancellable);
	data->cancelled_id = 0;
	data->interactive = FALSE;
	g_assert (data->schedule_entry_handle == NULL);
	g_free (data);
}

static void
gs_plugin_systemd_sysupdate_update_app_data_remove_from_download_scheduler (GsPluginSystemdSysupdateUpdateAppData *data)
{
	if (data->schedule_entry_handle == NULL) {
		return;
	}

	gs_metered_remove_from_download_scheduler_async (data->schedule_entry_handle,
	                                                 NULL,
	                                                 gs_plugin_systemd_sysupdate_update_app_data_remove_from_download_scheduler_cb,
	                                                 data->schedule_entry_handle);
	data->schedule_entry_handle = NULL;
}

static void
gs_plugin_systemd_sysupdate_update_app_data_remove_from_download_scheduler_cb (GObject      *source_object,
                                                                               GAsyncResult *result,
                                                                               gpointer      schedule_entry_handle)
{
	g_autoptr(GError) local_error = NULL;

	if (!gs_metered_remove_from_download_scheduler_finish (schedule_entry_handle, result, &local_error)) {
		g_warning ("Failed to remove from download scheduler: %s",
		           local_error->message);
		g_clear_error (&local_error);
	}
}

/* Plugin object */
struct _GsPluginSystemdSysupdate {
	GsPlugin parent;

	gchar *os_pretty_name; /* (owned) (not nullable) */
	gchar *os_version; /* (owned) (not nullable) */

	GsSystemdSysupdateManager *manager_proxy; /* (owned) (nullable) */
	GHashTable *target_item_map; /* (owned) (not nullable) (element-type utf8 TargetItem) */
	GHashTable *job_task_map; /* (owned) (not nullable) (element-type utf8 GTask) */
	GHashTable *job_to_remove_status_map; /* (owned) (not nullable) (element-type utf8 int32) */
	GHashTable *job_to_cancel_task_map; /* (owned) (not nullable) (element-type utf8 GTask) */
	gboolean is_metadata_refresh_ongoing;
	guint64 cache_age_secs;
};

/* Plugin private methods, and their callbacks. */

static void
gs_plugin_systemd_sysupdate_remove_job_apply (GsPluginSystemdSysupdate *self,
                                              GTask                    *task,
                                              const gchar              *job_path,
                                              gint32                    job_status);

static void
gs_plugin_systemd_sysupdate_cancel_job_cancel_cb (GObject      *source_object,
                                                  GAsyncResult *result,
                                                  gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_cancel_job_cb (GObject      *source_object,
                                           GAsyncResult *result,
                                           gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_cancel_job_revoke (GsPluginSystemdSysupdate *self,
                                               const gchar              *job_path);

static void
gs_plugin_systemd_sysupdate_update_target_proxy_new_cb (GObject      *source_object,
                                                        GAsyncResult *result,
                                                        gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_update_target_update_cb (GObject      *source_object,
                                                     GAsyncResult *result,
                                                     gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_update_target_job_proxy_new_cb (GObject      *source_object,
                                                            GAsyncResult *result,
                                                            gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_update_target_notify_progress_cb (gpointer user_data);

/* Plugin overridden virtual methods, and their callbacks. */

static void
gs_plugin_systemd_sysupdate_setup_list_activatable_names_cb (GObject      *source_object,
                                                             GAsyncResult *result,
                                                             gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_setup_proxy_new_cb (GObject      *source_object,
                                                GAsyncResult *result,
                                                gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_refine_iter (GObject      *source_object,
                                         GAsyncResult *result,
                                         gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_refine_app_async (GsPlugin                   *plugin,
                                              GsApp                      *app,
                                              GsPluginRefineFlags         job_flags,
                                              GsPluginRefineRequireFlags  require_flags,
                                              GCancellable               *cancellable,
                                              GAsyncReadyCallback         callback,
                                              gpointer                    user_data);

static void
gs_plugin_systemd_sysupdate_refine_app_proxy_new_cb (GObject      *source_object,
                                                 GAsyncResult *result,
                                                 gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_refine_app_describe_cb (GObject      *source_object,
                                                GAsyncResult *result,
                                                gpointer      user_data);

static gboolean
gs_plugin_systemd_sysupdate_refine_app_finish (GsPlugin      *plugin,
                                               GAsyncResult  *result,
                                               GError       **error);

static void
gs_plugin_systemd_sysupdate_refresh_metadata_list_targets_cb (GObject      *source_object,
                                                              GAsyncResult *result,
                                                              gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_refresh_metadata_iter (GObject      *source_object,
                                                   GAsyncResult *result,
                                                   gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_async (GsPlugin                     *plugin,
                                                           TargetItem                   *target,
                                                           GsPluginRefreshMetadataFlags  flags,
                                                           GCancellable                 *cancellable,
                                                           GAsyncReadyCallback           callback,
                                                           gpointer                      user_data);

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_proxy_new_cb (GObject      *source_object,
                                                                  GAsyncResult *result,
                                                                  gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_get_app_stream_cb (GObject      *source_object,
                                                                       GAsyncResult *result,
                                                                       gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_external_appstream_refresh_cb (GObject      *source_object,
                                                                                   GAsyncResult *result,
                                                                                   gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_get_version_cb (GObject      *source_object,
                                                                    GAsyncResult *result,
                                                                    gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_check_new_cb (GObject      *source_object,
                                                                  GAsyncResult *result,
                                                                  gpointer      user_data);

static gboolean
gs_plugin_systemd_sysupdate_target_refresh_metadata_finish (GsPlugin      *plugin,
                                                            GAsyncResult  *result,
                                                            GError       **error);

static void
gs_plugin_systemd_sysupdate_update_apps_iter (GObject      *source_object,
                                              GAsyncResult *result,
                                              gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_update_app_async (GsPlugin                           *plugin,
                                              GsApp                              *app,
                                              gboolean                            interactive,
                                              GsPluginProgressCallback            progress_callback,
                                              gpointer                            progress_user_data,
                                              GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                              gpointer                            app_needs_user_action_data,
                                              GCancellable                       *cancellable,
                                              GAsyncReadyCallback                 callback,
                                              gpointer                            user_data);
static void
gs_plugin_systemd_sysupdate_update_app_download_scheduler_cb (GObject      *source_object,
                                                              GAsyncResult *result,
                                                              gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_update_app_update_target_cb (GObject      *source_object,
                                                         GAsyncResult *result,
                                                         gpointer      user_data);

static void
gs_plugin_systemd_sysupdate_update_app_remove_from_download_scheduler_cb (GObject      *source_object,
                                                                          GAsyncResult *result,
                                                                          gpointer      user_data);

static gboolean
gs_plugin_systemd_sysupdate_update_app_finish (GsPlugin      *plugin,
                                               GAsyncResult  *result,
                                               GError       **error);

G_DEFINE_TYPE (GsPluginSystemdSysupdate, gs_plugin_systemd_sysupdate, GS_TYPE_PLUGIN)

static TargetItem *
lookup_target_by_app (GsPluginSystemdSysupdate *self,
                      GsApp                    *app)
{
	/* Helper to get the associated `target` of the given `app`
	 */
	return g_hash_table_lookup (self->target_item_map, gs_app_get_metadata_item (app, "SystemdSysupdated::Target"));
}

static GsApp *
create_app_for_target_appstream (GsPluginSystemdSysupdate  *self,
                                 TargetItem                *target,
                                 GError                   **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) info_filename = NULL;
	const gchar *silo_filename = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) local_error = NULL;

	if (target->silo == NULL) {
		g_set_error_literal (error,
		                     GS_PLUGIN_ERROR,
		                     GS_PLUGIN_ERROR_INVALID_FORMAT,
		                     "No metadata available");
		return NULL;
	}

	component = xb_silo_query_first (target->silo, "/component", NULL);
	if (component == NULL) {
		g_set_error_literal (error,
		                     GS_PLUGIN_ERROR,
		                     GS_PLUGIN_ERROR_INVALID_FORMAT,
		                     "No component available in metadata");
		return NULL;
	}

	info_filename = xb_silo_query_first (target->silo, "/info/filename", NULL);
	if (info_filename != NULL) {
		silo_filename = xb_node_get_text (info_filename);
	}

	if (silo_filename == NULL) {
		silo_filename = "";
	}

	app = gs_appstream_create_app (plugin, target->silo, component, silo_filename, AS_COMPONENT_SCOPE_SYSTEM, &local_error);
	if (local_error != NULL) {
		g_propagate_error (error, g_steal_pointer (&local_error));
		return NULL;
	} else if (app == NULL) {
		g_set_error_literal (error,
		                     GS_PLUGIN_ERROR,
		                     GS_PLUGIN_ERROR_FAILED,
		                     "Couldn't create an application via appstream");
		return NULL;
	}

	/* store target name to look up target info. */
	gs_app_set_metadata (app, "SystemdSysupdated::Target", target->name);
	gs_app_set_metadata (app, "SystemdSysupdated::Class", target->class);

	/* own the app we created */
	gs_app_set_management_plugin (app, plugin);

	return g_steal_pointer (&app);
}

static GsApp *
create_app_for_target_fallback (GsPluginSystemdSysupdate  *self,
                                TargetItem                *target,
                                GError                   **error)
{
	/* Create an app upgrade (os-upgrade) for the target `host` or an app
	 * update for the target `component`
	 */
	g_autoptr(GsApp) app = NULL;
	g_autofree gchar *app_id = NULL;
	const gchar *app_name = NULL;
#if AS_CHECK_VERSION(1, 0, 4)
	AsBundleKind bundle_kind = AS_BUNDLE_KIND_SYSUPDATE;
#else
	AsBundleKind bundle_kind = AS_BUNDLE_KIND_UNKNOWN;
#endif
	const gchar *app_summary = NULL;
	GsAppQuirk app_quirk = GS_APP_QUIRK_NEEDS_REBOOT
	                     | GS_APP_QUIRK_PROVENANCE
	                     | GS_APP_QUIRK_NOT_REVIEWABLE;

	if (g_strcmp0 (target->class, "host") == 0) {
		app_name = self->os_pretty_name;
#if !AS_CHECK_VERSION(1, 0, 4)
		bundle_kind = AS_BUNDLE_KIND_PACKAGE;
#endif
		/* TRANSLATORS: this is the system OS upgrade */
		app_summary = _("System");
	} else if (g_strcmp0 (target->class, "component") == 0) {
		app_name = target_item_get_id (target);
		/* TRANSLATORS: this is the system component update */
		app_summary = _("System component");
		app_quirk |= GS_APP_QUIRK_COMPULSORY;
	} else {
		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_NOT_SUPPORTED,
		             "Unsupported target class `%s`",
		             target->class);
		return NULL;
	}

	app_id = g_strdup_printf ("%s.%s",
	                          gs_plugin_get_name (GS_PLUGIN (self)),
	                          target_item_get_id (target));

	/* We explicitly don't set the license as we don't have it with the
	 * current version of the sysupdate D-Bus API.
	 */
	app = gs_app_new (app_id);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, app_name);
	gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
	gs_app_set_kind (app, AS_COMPONENT_KIND_OPERATING_SYSTEM);
	gs_app_set_bundle_kind (app, bundle_kind);
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, app_summary);
	gs_app_set_size_installed (app, GS_SIZE_TYPE_UNKNOWABLE, 0);
	gs_app_set_size_download (app, GS_SIZE_TYPE_UNKNOWABLE, 0);
	gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
	gs_app_set_progress (app, GS_APP_PROGRESS_UNKNOWN);
	gs_app_set_allow_cancel (app, TRUE);

	/* store target name to look up target info. */
	gs_app_set_metadata (app, "SystemdSysupdated::Target", target->name);
	gs_app_set_metadata (app, "SystemdSysupdated::Class", target->class);

	gs_app_add_quirk (app, app_quirk);

	/* own the app we created */
	gs_app_set_management_plugin (app, GS_PLUGIN (self));

	/* store app to the per-plugin cache */
	gs_plugin_cache_add (GS_PLUGIN (self), target_item_get_id (target), app);

	return g_steal_pointer (&app);
}

static GsApp *
create_app_for_target (GsPluginSystemdSysupdate  *self,
                       TargetItem                *target,
                       GError                   **error)
{
	/* Valid metadata are required for all but the host target. If we can't
	 * create an application from the appstream metainfo, we create a
	 * fallback application to avoid blocking host updates. */
	if (g_strcmp0 (target->class, "host") == 0) {
		g_autoptr(GsApp) app = NULL;
		g_autoptr(GError) local_error = NULL;

		app = create_app_for_target_appstream (self, target, &local_error);
		if (app != NULL) {
			return g_steal_pointer (&app);
		}

		g_debug ("Couldn't create app for host target, creating fallback: %s", local_error->message);

		return create_app_for_target_fallback (self, target, error);
	} else if (g_strcmp0 (target->class, "component") == 0) {
		return create_app_for_target_appstream (self, target, error);
	} else {
		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_NOT_SUPPORTED,
		             "Unsupported target class `%s`",
		             target->class);
		return NULL;
	}
}

static GsApp *
get_or_create_app_for_target (GsPluginSystemdSysupdate  *self,
                              TargetItem                *target,
                              GError                   **error)
{
	/* Get or create an app when there is no existing one in cache
	 * for the given target */

	g_autoptr(GsApp) app = NULL;

	/* find in the per-plugin cache */
	app = gs_plugin_cache_lookup (GS_PLUGIN (self), target_item_get_id (target));
	if (app != NULL) {
		return g_steal_pointer (&app);
	}

	return create_app_for_target (self, target, error);
}

/* This plugin explicitly only allows updating already installed targets. It is
 * expected for targets to be installed through other means. */
static void
update_app_for_target (GsPluginSystemdSysupdate *self,
                       GsApp                    *app,
                       TargetItem               *target)
{
	const gchar *app_version = NULL;
	GsAppState app_state = GS_APP_STATE_UNKNOWN;

	if (target_item_is_updatable (target)) {
		app_version = target->latest_version;
		app_state = GS_APP_STATE_UPDATABLE;
	} else if (target_item_is_installed (target)) {
		if (g_strcmp0 (target->class, "host") == 0) {
			app_version = self->os_version;
		} else {
			app_version = target->current_version;
		}
		app_state = GS_APP_STATE_INSTALLED;
	}

	gs_app_set_version (app, app_version);
	gs_app_set_state (app, app_state);
}

/* Wrapper methods for async. target update
 *
 * The goal of the method `gs_plugin_systemd_sysupdate_update_target_async()`
 * is to wrap the specific target update as a single async. call.
 * By design, there are two D-Bus method calls and two D-Bus signals
 * involved in one 'target update' progress:
 *  1) D-Bus method `Target.Update()`
 *  2) D-Bus method `Job.Cancel()`
 *  3) D-Bus signal `Job.PropertiesChanged()`
 *  4) D-Bus signal `Manager.JobRemoved()`
 *
 * Assumes there is only one job created dynamically in the runtime
 * by `systemd-sysupdated` is associated to the `Target.Update()`.
 * Here we create a subtask for each individual target update, and
 * hide the 'target to job' mapping from the caller by maintaining
 * the relationships internally within a look-up table. */
typedef struct {
	GsApp *app; /* (owned) (not nullable) */
	GsSystemdSysupdateJob *job_proxy; /* (owned) (nullable) */
	gchar *target_path; /* (owned) (not nullable) */
	gchar *job_path; /* (owned) (nullable) */
	gboolean interactive;
} GsPluginSystemdSysupdateUpdateTargetData;

static GsPluginSystemdSysupdateUpdateTargetData *
gs_plugin_systemd_sysupdate_update_target_data_new (GsApp       *app,
                                                    const gchar *target_path,
                                                    gboolean     interactive)
{
	GsPluginSystemdSysupdateUpdateTargetData *data = g_new0 (GsPluginSystemdSysupdateUpdateTargetData, 1);
	data->app = g_object_ref (app);
	data->target_path = g_strdup (target_path);
	data->interactive = interactive;
	return data;
}

static void
gs_plugin_systemd_sysupdate_update_target_data_free (GsPluginSystemdSysupdateUpdateTargetData *data)
{
	g_clear_object (&data->app);
	g_clear_object (&data->job_proxy);
	g_clear_pointer (&data->target_path, g_free);
	g_clear_pointer (&data->job_path, g_free);
	data->interactive = FALSE;
	g_free (data);
}

/* Remove the given job. It is called when the server notified us a job
 * terminated.
 *
 * Because of the async nature of of the application, we can receive job removal
 * notifications from the server after we requested the update jobs but before
 * we finished preparing them. To handle job removal notifications correctly, we
 * may need to store them until we are ready. */
static void
gs_plugin_systemd_sysupdate_remove_job (GsPluginSystemdSysupdate *self,
                                        const gchar              *job_path,
                                        gint32                    job_status)
{
	GTask *task = NULL;

	if (g_hash_table_contains (self->job_to_remove_status_map, job_path)) {
		g_debug ("Job already filed for removal: %s", job_path);
		return;
	}

	task = g_hash_table_lookup (self->job_task_map, job_path);
	if (task == NULL) {
		g_debug ("Couldn´t remove task for job `%s`, no task found, storing for later removal", job_path);
		g_hash_table_insert (self->job_to_remove_status_map, g_strdup (job_path), GINT_TO_POINTER (job_status));
		/* The job terminated, there is nothing to cancel anymore. */
		gs_plugin_systemd_sysupdate_cancel_job_revoke (self, job_path);
		return;
	}

	gs_plugin_systemd_sysupdate_remove_job_apply (self, task, job_path, job_status);
}

static void
gs_plugin_systemd_sysupdate_remove_job_apply (GsPluginSystemdSysupdate *self,
                                              GTask                    *task,
                                              const gchar              *job_path,
                                              gint32                    job_status)
{
	GsPluginSystemdSysupdateUpdateTargetData *data = NULL;
	const gchar *target_class = NULL;
	gboolean target_is_host = FALSE;

	g_debug ("Removing task found for job `%s`", job_path);
	/* pass the parameters to the callback */
	data = g_task_get_task_data (task);
	target_class = gs_app_get_metadata_item (data->app, "SystemdSysupdated::Class");
	target_is_host = g_strcmp0 (target_class, "host") == 0;

	/* The `systemd-sysupdate` job returns zero on success, any other number
	 * represents a failure. A positive number is an exit code, and a
	 * negative number is an errno code that gives us more information about
	 * the failure. */
	if (job_status == 0) {
		gs_app_set_progress (data->app, GS_APP_PROGRESS_UNKNOWN);
		/* The `host` target should have its state left as `updatable`. */
		if (target_is_host) {
			gs_app_set_state (data->app, GS_APP_STATE_UPDATABLE);
			gs_app_set_size_download (data->app, GS_SIZE_TYPE_VALID, 0);
		} else {
			gs_app_set_state (data->app, GS_APP_STATE_INSTALLED);
		}
	} else {
		gs_app_set_progress (data->app, GS_APP_PROGRESS_UNKNOWN);
		/* The `host` target has the non-transient `updatable` state, so
		 * to recover back to the `available` state, we have to set it
		 * explicitly. */
		if (target_is_host) {
			gs_app_set_state (data->app, GS_APP_STATE_AVAILABLE);
		} else {
			gs_app_set_state_recover (data->app);
		}
	}

	/* remove task from the hashmap and return the job status */
	g_hash_table_remove (self->job_task_map, job_path);
	g_hash_table_remove (self->job_to_remove_status_map, job_path);
	/* The job terminated, there is nothing to cancel anymore. */
	gs_plugin_systemd_sysupdate_cancel_job_revoke (self, job_path);

	if (job_status == 0) {
		g_task_return_boolean (task, TRUE);
	} else {
		g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
		                         _("Removing sysupdate job '%s' failed with status %i"),
		                         job_path, job_status);
	}
}

static void
gs_plugin_systemd_sysupdate_remove_job_revoke (GsPluginSystemdSysupdate *self,
                                               const gchar              *job_path)
{
	g_hash_table_remove (self->job_to_remove_status_map, job_path);
}

/* Request systemd-sysupdate to cancel the given job. It is called when the
 * plugin's update job has been cancelled.
 *
 * Because of the async nature of the application, we can receive job
 * cancellation requests from the application after we requested the update jobs
 * but before we finished preparing them. To handle job cancellation requests
 * correctly, we may need to store them until we are ready. */
static void
gs_plugin_systemd_sysupdate_cancel_job (GsPluginSystemdSysupdate *self,
                                        GsApp                    *app,
                                        gboolean                  interactive)
{
	TargetItem *target = NULL;
	const gchar *job_path = NULL;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	g_autoptr(GCancellable) cancellable = NULL;
	g_autoptr(GTask) task = NULL;
	GTask *update_task = NULL;
	GsPluginSystemdSysupdateUpdateTargetData *update_data = NULL;
	GDBusCallFlags call_flags = G_DBUS_CALL_FLAGS_NONE;

	target = lookup_target_by_app (self, app);
	if (target == NULL) {
		g_debug ("Couldn´t cancel the update: no target found");
		return;
	}

	/* iterate over the on-going tasks to find the job */
	g_hash_table_iter_init (&iter, self->job_task_map);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GsPluginSystemdSysupdateUpdateTargetData *job_data = g_task_get_task_data (value);
		if (job_data != NULL &&
		    g_strcmp0 (job_data->target_path, target->object_path) == 0) {
			job_path = key;
			update_task = G_TASK (value);
			break;
		}
	}
	if (job_path == NULL) {
		g_debug ("Couldn´t cancel the update: no job found for target `%s`", target->object_path);
		return;
	}

	if (g_hash_table_contains (self->job_to_cancel_task_map, job_path)) {
		g_debug ("Job already filed for cancellation: %s", job_path);
		return;
	}

	if (g_hash_table_contains (self->job_to_remove_status_map, job_path)) {
		g_debug ("Job already filed for removal: %s", job_path);
		return;
	}

	cancellable = g_cancellable_new ();
	task = g_task_new (self, cancellable, gs_plugin_systemd_sysupdate_cancel_job_cb, NULL);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_cancel_job);
	g_task_set_task_data (task, g_strdup (job_path), (GDestroyNotify)g_free);

	if (update_task == NULL) {
		g_debug ("Couldn´t cancel task for job `%s`, no task found, storing for later cancellation", job_path);
		g_hash_table_insert (self->job_to_cancel_task_map, g_strdup (job_path), g_steal_pointer (&task));
		return;
	}

	update_data = g_task_get_task_data (update_task);

	if (update_data->interactive) {
		call_flags |= G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION;
	}

	gs_systemd_sysupdate_job_call_cancel (update_data->job_proxy,
	                                      call_flags,
	                                      SYSUPDATED_JOB_CANCEL_TIMEOUT_MS,
	                                      cancellable,
	                                      gs_plugin_systemd_sysupdate_cancel_job_cancel_cb,
	                                      g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_cancel_job_cancel_cb (GObject      *source_object,
                                                  GAsyncResult *result,
                                                  gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;

	if (!gs_systemd_sysupdate_job_call_cancel_finish (GS_SYSTEMD_SYSUPDATE_JOB (source_object),
	                                                  result,
	                                                  &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	if (g_task_return_error_if_cancelled (task)) {
		return;
	}

	g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_systemd_sysupdate_cancel_job_cb (GObject      *source_object,
                                           GAsyncResult *result,
                                           gpointer      user_data)
{
	GTask *task = G_TASK (result);
	const gchar *job_path = NULL;
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	g_autoptr(GError) local_error = NULL;

	job_path = g_task_get_task_data (task);
	g_hash_table_remove (self->job_to_cancel_task_map, job_path);

	if (!g_task_propagate_boolean (task, &local_error)) {
		g_debug ("Couldn´t cancel the update: %s", local_error->message);
		return;
	}

	g_debug ("Cancelled update job `%s` successfully", job_path);
}

static void
gs_plugin_systemd_sysupdate_cancel_job_revoke (GsPluginSystemdSysupdate *self,
                                               const gchar              *job_path)
{
	GTask *task = NULL;
	GCancellable *cancellable = NULL;

	task = G_TASK (g_hash_table_lookup (self->job_to_cancel_task_map, job_path));
	if (!task) {
		return;
	}

	cancellable = g_task_get_cancellable (task);
	if (!cancellable) {
		return;
	}

	g_cancellable_cancel (cancellable);
}

static void
gs_plugin_systemd_sysupdate_update_target_async (GsPluginSystemdSysupdate *self,
                                                 GsApp                    *app,
                                                 const gchar              *target_path,
                                                 const gchar              *target_version,
                                                 gboolean                  interactive,
                                                 GCancellable             *cancellable,
                                                 GAsyncReadyCallback       callback,
                                                 gpointer                  user_data)
{
	GsPluginSystemdSysupdateUpdateTargetData *data = NULL;
	g_autoptr(GTask) task = NULL;
	TargetItem *target = NULL;
	GsPlugin *plugin = GS_PLUGIN (self);

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_update_target_async);

	data = gs_plugin_systemd_sysupdate_update_target_data_new (app,
	                                                           target_path,
	                                                           interactive);
	g_task_set_task_data (task, data, (GDestroyNotify)gs_plugin_systemd_sysupdate_update_target_data_free);

	target = lookup_target_by_app (self, data->app);
	if (target == NULL) {
		g_task_return_new_error (task, G_IO_ERROR, GS_PLUGIN_ERROR_FAILED,
		                         "cannot find target for app: %s", gs_app_get_name (data->app));
		return;
	}

	/* currently two actions `download file` and `deploy changes`
	 * are bound together as one method in `Target.Update()`.
	 * This method will trigger the update to start and return
	 * immediately. Results should be waited and handled within the
	 * signal `Manager.JobRemoved()` */
	gs_systemd_sysupdate_target_proxy_new (gs_plugin_get_system_bus_connection (plugin),
	                                       G_DBUS_PROXY_FLAGS_NONE,
	                                       "org.freedesktop.sysupdate1",
	                                       target_path,
	                                       cancellable,
	                                       gs_plugin_systemd_sysupdate_update_target_proxy_new_cb,
	                                       g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_update_target_proxy_new_cb (GObject      *source_object,
                                                        GAsyncResult *result,
                                                        gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GsSystemdSysupdateTarget) proxy = NULL;
	g_autofree gchar *job_path = NULL;
	GsPluginSystemdSysupdateUpdateTargetData *data = NULL;
	GDBusCallFlags call_flags = G_DBUS_CALL_FLAGS_NONE;

	proxy = gs_systemd_sysupdate_target_proxy_new_finish (result, &local_error);
	if (proxy == NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	data = g_task_get_task_data (task);

	if (data->interactive) {
		call_flags |= G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION;
	}

	gs_systemd_sysupdate_target_call_update (proxy,
	                                         "", /* left empty as the latest version */
	                                         SYSUPDATED_TARGET_UPDATE_FLAGS_NONE,
	                                         call_flags,
	                                         SYSUPDATED_TARGET_UPDATE_TIMEOUT_MS,
	                                         NULL, /* Makes the call explicitly non-cancellable so we can get the job path and cancel it correctly. */
	                                         gs_plugin_systemd_sysupdate_update_target_update_cb,
	                                         g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_update_target_update_cb (GObject      *source_object,
                                                     GAsyncResult *result,
                                                     gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	g_autofree gchar *job_path = NULL;
	GsPlugin *plugin = GS_PLUGIN (self);
	GsPluginSystemdSysupdateUpdateTargetData *data = NULL;

	if (!gs_systemd_sysupdate_target_call_update_finish (GS_SYSTEMD_SYSUPDATE_TARGET (source_object),
	                                                     NULL,
	                                                     NULL,
	                                                     &job_path,
	                                                     result,
	                                                     &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	data = g_task_get_task_data (task);
	g_set_str (&data->job_path, job_path);

	gs_systemd_sysupdate_job_proxy_new (gs_plugin_get_system_bus_connection (plugin),
	                                    G_DBUS_PROXY_FLAGS_NONE,
	                                    "org.freedesktop.sysupdate1",
	                                    job_path,
	                                    NULL, /* Makes the call explicitly non-cancellable so we can get the job path and cancel it correctly. */
	                                    gs_plugin_systemd_sysupdate_update_target_job_proxy_new_cb,
	                                    g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_update_target_job_proxy_new_cb (GObject      *source_object,
                                                            GAsyncResult *result,
                                                            gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GsSystemdSysupdateJob) proxy = NULL;
	GCancellable *cancellable = g_task_get_cancellable (task);
	GsPluginSystemdSysupdateUpdateTargetData *data = NULL;
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	GDBusCallFlags call_flags = G_DBUS_CALL_FLAGS_NONE;

	data = g_task_get_task_data (task);

	proxy = gs_systemd_sysupdate_job_proxy_new_finish (result, &local_error);
	if (proxy == NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		/* The job's preparation failed, we can't act on it, revoke any
		 * removal or cancellation request that we filed during its
		 * preparation. */
		gs_plugin_systemd_sysupdate_remove_job_revoke (self, data->job_path);
		gs_plugin_systemd_sysupdate_cancel_job_revoke (self, data->job_path);
		return;
	}

	g_set_object (&data->job_proxy, proxy);

	g_signal_connect_object (proxy, "notify::progress",
	                         G_CALLBACK (gs_plugin_systemd_sysupdate_update_target_notify_progress_cb),
	                         g_object_ref (task), G_CONNECT_SWAPPED);

	gs_plugin_systemd_sysupdate_update_target_notify_progress_cb (task);

	/* job path to task mapping, easier for the callbacks to use the
	 * object path to find it's related task */
	g_hash_table_insert (self->job_task_map,
	                     g_strdup (data->job_path),
	                     g_object_ref (task));

	/* We don't chain up or return here, the task will be terminated when
	 * systemd-sysupdate notifies us that the job is removed, or by
	 * cancelling the task. */

	/* If the update job has been filed for removal during its preparation,
	 * we need to resume the removal request. This will also revoke any
	 * cancellation request. */
	if (g_hash_table_contains (self->job_to_remove_status_map, data->job_path)) {
		gint32 job_status = GPOINTER_TO_INT (g_hash_table_lookup (self->job_to_remove_status_map, data->job_path));
		gs_plugin_systemd_sysupdate_remove_job_apply (self, task, data->job_path, job_status);
		return;
	}

	/* If the update job has been filed for cancellation during its
	 * preparation, we need to resume the cancellation request. */
	if (g_hash_table_contains (self->job_to_cancel_task_map, data->job_path)) {
		GTask *cancel_task = g_hash_table_lookup (self->job_to_cancel_task_map, data->job_path);

		if (data->interactive) {
			call_flags |= G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION;
		}

		gs_systemd_sysupdate_job_call_cancel (data->job_proxy,
		                                      call_flags,
		                                      SYSUPDATED_JOB_CANCEL_TIMEOUT_MS,
		                                      g_task_get_cancellable (cancel_task),
		                                      gs_plugin_systemd_sysupdate_cancel_job_cancel_cb,
		                                      g_steal_pointer (&cancel_task));
		return;
	}

	/* If the task has been cancelled during its preparation, we need to ask
	 * systemd-sysdupdate to cancel it. */
	if (g_cancellable_is_cancelled (cancellable)) {
		gs_plugin_systemd_sysupdate_cancel_job (self, data->app, data->interactive);
	}
}

static void
gs_plugin_systemd_sysupdate_update_target_notify_progress_cb (gpointer user_data)
{
	GTask *task = G_TASK (user_data);
	GsPluginSystemdSysupdateUpdateTargetData *data = g_task_get_task_data (task);
	guint progress = gs_systemd_sysupdate_job_get_progress (data->job_proxy);

	gs_app_set_state (data->app, GS_APP_STATE_DOWNLOADING);
	gs_app_set_progress (data->app, progress);
}

static gboolean
gs_plugin_systemd_sysupdate_update_target_finish (GsPluginSystemdSysupdate  *self,
                                                  GAsyncResult              *result,
                                                  GError                   **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

/* Plugin methods */
static void
gs_plugin_systemd_sysupdate_init (GsPluginSystemdSysupdate *self)
{
	/* Plugin constructor
	 */
}

static void
gs_plugin_systemd_sysupdate_dispose (GObject *object)
{
	GsPluginSystemdSysupdate *self = GS_PLUGIN_SYSTEMD_SYSUPDATE (object);

	g_clear_object (&self->manager_proxy);

	G_OBJECT_CLASS (gs_plugin_systemd_sysupdate_parent_class)->dispose (object);
}

static void
gs_plugin_systemd_sysupdate_finalize (GObject *object)
{
	GsPluginSystemdSysupdate *self = GS_PLUGIN_SYSTEMD_SYSUPDATE (object);

	g_clear_pointer (&self->os_pretty_name, g_free);
	g_clear_pointer (&self->os_version, g_free);
	g_clear_pointer (&self->target_item_map, g_hash_table_destroy);
	g_clear_pointer (&self->job_task_map, g_hash_table_destroy);
	g_clear_pointer (&self->job_to_remove_status_map, g_hash_table_destroy);
	g_clear_pointer (&self->job_to_cancel_task_map, g_hash_table_destroy);
	self->is_metadata_refresh_ongoing = FALSE;
	self->cache_age_secs = 0;

	G_OBJECT_CLASS (gs_plugin_systemd_sysupdate_parent_class)->finalize (object);
}

static void
gs_plugin_systemd_sysupdate_setup_async (GsPlugin            *plugin,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
	/* Plugin object init. before runtime operations
	 */
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_setup_async);

	/* Check that the proxies exist (and are owned; they should auto-start)
	 * so we can disable the plugin for systems which don’t have
	 * systemd-sysupdate. */
	g_dbus_connection_call (gs_plugin_get_system_bus_connection (plugin),
	                        "org.freedesktop.DBus",
	                        "/org/freedesktop/DBus",
	                        "org.freedesktop.DBus",
	                        "ListActivatableNames",
	                        NULL,
	                        (const GVariantType *) "(as)",
	                        G_DBUS_CALL_FLAGS_NONE,
	                        FREEDESKTOP_DBUS_LIST_ACTIVATABLE_NAMES_TIMEOUT_MS,
	                        cancellable,
	                        gs_plugin_systemd_sysupdate_setup_list_activatable_names_cb,
	                        g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_setup_list_activatable_names_cb (GObject      *source_object,
                                                             GAsyncResult *result,
                                                             gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GCancellable *cancellable = g_task_get_cancellable (task);
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	GsPlugin *plugin = GS_PLUGIN (self);
	g_autoptr(GVariant) ret_val = NULL;
	g_autofree gchar **activatable_names = NULL;
	g_autoptr(GError) local_error = NULL;

	ret_val = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), result, &local_error);
	if (ret_val == NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_variant_get (ret_val, "(^a&s)", &activatable_names);
	if (!g_strv_contains ((const gchar *const *) activatable_names, "org.freedesktop.sysupdate1")) {
		g_task_return_new_error_literal (task, GS_PLUGIN_ERROR,
		                                 GS_PLUGIN_ERROR_PLUGIN_DEPSOLVE_FAILED,
		                                 "D-Bus service org.freedesktop.sysupdate1 unavailable");
		return;
	}

	gs_systemd_sysupdate_manager_proxy_new (gs_plugin_get_system_bus_connection (plugin),
	                                        G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION,
	                                        "org.freedesktop.sysupdate1",
	                                        "/org/freedesktop/sysupdate1",
	                                        cancellable,
	                                        gs_plugin_systemd_sysupdate_setup_proxy_new_cb,
	                                        g_steal_pointer (&task));
}

static void
manager_proxy_job_removed_cb (GsPluginSystemdSysupdate       *self,
                              guint64                         job_id,
                              const gchar                    *job_path,
                              gint32                          job_status,
                              GsSystemdSysupdateManagerProxy  proxy)
{
	gs_plugin_systemd_sysupdate_remove_job (self, job_path, job_status);
}

static void
gs_plugin_systemd_sysupdate_setup_proxy_new_cb (GObject      *source_object,
                                                GAsyncResult *result,
                                                gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	g_autoptr(GsOsRelease) os_release = NULL;
	const gchar *os_pretty_name = NULL;
	const gchar *os_version = NULL;

	self->manager_proxy = gs_systemd_sysupdate_manager_proxy_new_finish (result, &local_error);
	if (self->manager_proxy == NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* read os-release */
	os_release = gs_os_release_new (&local_error);
	if (local_error) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	os_pretty_name = gs_os_release_get_pretty_name (os_release);
	if (os_pretty_name == NULL) {
		os_pretty_name = "unknown";
	}

	os_version = gs_os_release_get_version (os_release);
	if (os_version == NULL) {
		os_version = "unknown";
	}

	/* `systemd-sysupdated` signal subscription */
	g_signal_connect_object (self->manager_proxy,
	                         "job-removed",
	                         G_CALLBACK (manager_proxy_job_removed_cb),
	                         self,
	                         G_CONNECT_SWAPPED);

	/* plugin object attributes init. */
	self->os_pretty_name = g_strdup (os_pretty_name);
	self->os_version = g_strdup (os_version);
	self->target_item_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)target_item_free);
	self->job_task_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)NULL);
	self->job_to_remove_status_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)NULL);
	self->job_to_cancel_task_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)NULL);
	self->cache_age_secs = 0;

	/* on success */
	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_systemd_sysupdate_setup_finish (GsPlugin      *plugin,
                                          GAsyncResult  *result,
                                          GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_systemd_sysupdate_refine_async (GsPlugin                   *plugin,
                                          GsAppList                  *list,
                                          GsPluginRefineFlags         job_flags,
                                          GsPluginRefineRequireFlags  require_flags,
                                          GsPluginEventCallback       event_callback,
                                          void                       *event_user_data,
                                          GCancellable               *cancellable,
                                          GAsyncReadyCallback         callback,
                                          gpointer                    user_data)
{
	GsPluginSystemdSysupdateRefineData *data = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GQueue) queue = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_refine_async);

	/* put apps to be refined in queue */
	queue = g_queue_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		/* only process this app if was created by this plugin */
		if (!gs_app_has_management_plugin (app, plugin))
			continue;

		g_queue_push_tail (queue, g_object_ref (app));
	}

	/* put apps in queue to task data */
	data = gs_plugin_systemd_sysupdate_refine_data_new (g_steal_pointer (&queue), job_flags, require_flags);
	g_task_set_task_data (task, data, (GDestroyNotify)gs_plugin_systemd_sysupdate_refine_data_free);

	/* invoke the first target */
	gs_plugin_systemd_sysupdate_refine_iter (NULL, NULL, g_steal_pointer (&task));
}

/* Iterate over the elements of the queue one-by-one.
 */
static void
gs_plugin_systemd_sysupdate_refine_iter (GObject      *source_object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginSystemdSysupdateRefineData *data = g_task_get_task_data (task);
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) local_error = NULL;
	g_autoptr (GsApp) app = NULL;

	if (result != NULL &&
	    !gs_plugin_systemd_sysupdate_refine_app_finish (GS_PLUGIN (self), result, &local_error)) {
		g_debug ("Failed to refine app: %s", local_error->message);
		g_clear_error (&local_error);
	}

	app = g_queue_pop_head (data->queue);
	if (app == NULL) {
		/* We reached the end of the queue. */
		g_task_return_boolean (task, TRUE);
		return;
	}

	if (g_task_return_error_if_cancelled (task)) {
		g_debug ("%s: Cancelled", G_STRFUNC);
		return;
	}

	gs_plugin_systemd_sysupdate_refine_app_async (GS_PLUGIN (self),
	                                              app,
	                                              data->job_flags,
	                                              data->require_flags,
	                                              cancellable,
	                                              gs_plugin_systemd_sysupdate_refine_iter,
	                                              g_steal_pointer (&task));
}

static gboolean
gs_plugin_systemd_sysupdate_refine_finish (GsPlugin      *plugin,
                                           GAsyncResult  *result,
                                           GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_systemd_sysupdate_refine_app_async (GsPlugin                   *plugin,
                                              GsApp                      *app,
                                              GsPluginRefineFlags         job_flags,
                                              GsPluginRefineRequireFlags  require_flags,
                                              GCancellable               *cancellable,
                                              GAsyncReadyCallback         callback,
                                              gpointer                    user_data)
{
	GsPluginSystemdSysupdateRefineAppData *data = NULL;
	GsPluginSystemdSysupdate *self = GS_PLUGIN_SYSTEMD_SYSUPDATE (plugin);
	g_autoptr(GTask) task = NULL;
	TargetItem *target = NULL;
	const gchar *target_path = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_refine_app_async);

	data = gs_plugin_systemd_sysupdate_refine_app_data_new (app);
	g_task_set_task_data (task, data, (GDestroyNotify) gs_plugin_systemd_sysupdate_refine_app_data_free);

	target = lookup_target_by_app (self, data->app);
	if (target == NULL) {
		g_task_return_new_error (task, G_IO_ERROR, GS_PLUGIN_ERROR_FAILED,
		                         "cannot find target for app: %s", gs_app_get_name (data->app));
		return;
	}

	target_path = target->object_path;

	gs_systemd_sysupdate_target_proxy_new (gs_plugin_get_system_bus_connection (plugin),
	                                       G_DBUS_PROXY_FLAGS_NONE,
	                                       "org.freedesktop.sysupdate1",
	                                       target_path,
	                                       cancellable,
	                                       gs_plugin_systemd_sysupdate_refine_app_proxy_new_cb,
	                                       g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_refine_app_proxy_new_cb (GObject      *source_object,
                                                     GAsyncResult *result,
                                                     gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	GCancellable *cancellable = g_task_get_cancellable (task);
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	g_autofree gchar *job_path = NULL;
	g_autoptr(GsSystemdSysupdateTarget) proxy = NULL;
	GsPluginSystemdSysupdateRefineAppData *data = g_task_get_task_data (task);
	TargetItem *target = NULL;
	const gchar *version = NULL;

	proxy = gs_systemd_sysupdate_target_proxy_new_finish (result, &local_error);
	if (proxy == NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	target = lookup_target_by_app (self, data->app);
	if (target == NULL) {
		g_task_return_new_error (task, G_IO_ERROR, GS_PLUGIN_ERROR_FAILED,
		                         "cannot find target for app: %s", gs_app_get_name (data->app));
		return;
	}

	version = target->latest_version != NULL ? target->latest_version
	                                         : target->current_version;

	/* if the version is not available, it will result an error
	 * later in the callback */
	gs_systemd_sysupdate_target_call_describe (proxy,
	                                           version,
	                                           SYSUPDATED_TARGET_DESCRIBE_FLAGS_NONE,
	                                           G_DBUS_CALL_FLAGS_NONE,
	                                           SYSUPDATED_TARGET_DESCRIBE_TIMEOUT_MS,
	                                           cancellable,
	                                           gs_plugin_systemd_sysupdate_refine_app_describe_cb,
	                                           g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_refine_app_describe_cb (GObject      *source_object,
                                                    GAsyncResult *result,
                                                    gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *json = NULL;

	/* `systemd-sysupdated` also returns error when the given
	 * version is not available (case both no version installed and
	 * no available version in the server). we ignore the error here
	 * and always move on to the next target */
	if (!gs_systemd_sysupdate_target_call_describe_finish (GS_SYSTEMD_SYSUPDATE_TARGET (source_object),
	                                                       &json,
	                                                       result,
	                                                       &local_error)) {
		g_debug ("Describe target error ignored, error = `%s`", local_error->message);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_systemd_sysupdate_refine_app_finish (GsPlugin      *plugin,
                                               GAsyncResult  *result,
                                               GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_systemd_sysupdate_list_apps_async (GsPlugin              *plugin,
                                             GsAppQuery            *query,
                                             GsPluginListAppsFlags  flags,
                                             GsPluginEventCallback  event_callback,
                                             void                  *event_user_data,
                                             GCancellable          *cancellable,
                                             GAsyncReadyCallback    callback,
                                             gpointer               user_data)
{
	/* Return managed apps filtered by the given query
	 */
	GsPluginSystemdSysupdate *self = GS_PLUGIN_SYSTEMD_SYSUPDATE (plugin);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GsAppList) list = gs_app_list_new ();
	GsAppQueryTristate is_installed = GS_APP_QUERY_TRISTATE_UNSET;
	GsAppQueryTristate is_for_update = GS_APP_QUERY_TRISTATE_UNSET;
	const gchar * const *keywords = NULL;
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_list_apps_async);

	/* here we report the system updates as individual apps, so user
	 * can easily search and update a specific target */

	if (query != NULL) {
		is_installed = gs_app_query_get_is_installed (query);
		is_for_update = gs_app_query_get_is_for_update (query);
		keywords = gs_app_query_get_keywords (query);
	}

	/* currently only support a subset of query properties, and only
	 * one set at once */
	if ((is_installed == GS_APP_QUERY_TRISTATE_UNSET &&
	     is_for_update == GS_APP_QUERY_TRISTATE_UNSET &&
	     keywords == NULL) ||
	    gs_app_query_get_n_properties_set (query) != 1) {
		g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                         "Unsupported query");
		return;
	}

	/* iterate over our targets, after `refresh_metadata()` we
	 * should have target and its corresponding app created and
	 * stored in the per-plugin cache */
	g_hash_table_iter_init (&iter, self->target_item_map);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		TargetItem *target = (TargetItem *)value;
		g_autoptr(GsApp) app = NULL;
		g_autoptr(GError) local_error = NULL;

		/* get or create app for the target */
		app = get_or_create_app_for_target (self, target, &local_error);
		if (app == NULL) {
			g_debug ("Couldn't list app for target %s: %s", target_item_get_id (target), local_error->message);
			continue;
		}

		if (keywords != NULL && !target_item_matches_keywords (target, keywords)) {
			continue;
		}

		/* We support updating installed targets only. */
		if (is_for_update == GS_APP_QUERY_TRISTATE_TRUE && !target_item_is_updatable (target)) {
			continue;
		}


		if ((is_installed == GS_APP_QUERY_TRISTATE_TRUE && !target_item_is_installed (target)) ||
		    (is_installed == GS_APP_QUERY_TRISTATE_FALSE && target_item_is_installed (target))) {
			continue;
		}

		gs_app_list_add (list, app);
	}

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_systemd_sysupdate_list_apps_finish (GsPlugin      *plugin,
                                              GAsyncResult  *result,
                                              GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void
gs_plugin_systemd_sysupdate_refresh_metadata_async (GsPlugin                     *plugin,
                                                    guint64                       cache_age_secs,
                                                    GsPluginRefreshMetadataFlags  flags,
                                                    GsPluginEventCallback         event_callback,
                                                    void                         *event_user_data,
                                                    GCancellable                 *cancellable,
                                                    GAsyncReadyCallback           callback,
                                                    gpointer                      user_data)
{
	/* Periodically update the targets saved
	 */
	GsPluginSystemdSysupdate *self = GS_PLUGIN_SYSTEMD_SYSUPDATE (plugin);
	g_autoptr(GTask) task = NULL;
	GsPluginSystemdSysupdateRefreshMetadataData *data = NULL;
	g_autoptr(GQueue) queue = NULL;
	GDBusCallFlags call_flags = G_DBUS_CALL_FLAGS_NONE;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_refresh_metadata_async);

	queue = g_queue_new ();

	/* put apps in queue to task data */
	data = gs_plugin_systemd_sysupdate_refresh_metadata_data_new (g_steal_pointer (&queue),
	                                                              flags);
	g_task_set_task_data (task, data, (GDestroyNotify)gs_plugin_systemd_sysupdate_refresh_metadata_data_free);

	if (self->is_metadata_refresh_ongoing) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	self->is_metadata_refresh_ongoing = TRUE; /* update immediately to block continuous refreshes */
	self->cache_age_secs = cache_age_secs;

	if (data->flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE) {
		call_flags |= G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION;
	}

	/* here we ask `systemd-sysupdated` to list all available
	 * targets and enumerate the targets reported in the callback. */
	gs_systemd_sysupdate_manager_call_list_targets (self->manager_proxy,
	                                                call_flags,
	                                                SYSUPDATED_MANAGER_LIST_TARGET_TIMEOUT_MS,
	                                                cancellable,
	                                                gs_plugin_systemd_sysupdate_refresh_metadata_list_targets_cb,
	                                                g_steal_pointer (&task));
}

static gboolean
check_to_be_removed (gpointer key, gpointer value, gpointer user_data)
{
	TargetItem *target = (TargetItem *) value;
	return !target->is_valid;
}

static void
gs_plugin_systemd_sysupdate_refresh_metadata_list_targets_cb (GObject      *source_object,
                                                              GAsyncResult *result,
                                                              gpointer      user_data)
{
	GsPluginSystemdSysupdateRefreshMetadataData *data = NULL;
	TargetItem *target = NULL;
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GVariant) ret_targets = NULL;
	g_autoptr(GVariantIter) variant_iter = NULL;
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	const gchar *class = NULL;
	const gchar *name = NULL;
	const gchar *object_path = NULL;
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	if (!gs_systemd_sysupdate_manager_call_list_targets_finish (GS_SYSTEMD_SYSUPDATE_MANAGER (source_object),
	                                                            &ret_targets,
	                                                            result,
	                                                            &local_error)) {
		self->is_metadata_refresh_ongoing = FALSE;
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	data = g_task_get_task_data (task);

	/* mark all targets saved as invalid to detect removals */
	g_hash_table_iter_init (&iter, self->target_item_map);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		target = (TargetItem *)value;
		target->is_valid = FALSE;
	}

	/* iterate over targets and save to the target hashmap */
	g_variant_get (ret_targets, "a(sso)", &variant_iter);
	while (g_variant_iter_loop (variant_iter, "(&s&s&o)", &class, &name, &object_path)) {
		g_hash_table_insert (self->target_item_map,
		                     (gpointer) g_strdup (name),
		                     (gpointer) target_item_new (class, name, object_path)); /* overwrite value */
	}

	/* remove targets no-longer exist and their apps */
	g_hash_table_iter_init (&iter, self->target_item_map);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		if (check_to_be_removed (key, value, NULL)) {
			gs_plugin_cache_remove (GS_PLUGIN (self), key);
		}
	}
	g_hash_table_foreach_remove (self->target_item_map, (GHRFunc) check_to_be_removed, NULL);

	/* push all targets to queue. Make 'host' the first target if it
	 * exists, so other targets can point to it if it needs to */
	g_hash_table_iter_init (&iter, self->target_item_map);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		target = (TargetItem *)value;
		if (g_strcmp0 (target->class, "host") == 0) {
			g_queue_push_head (data->queue, value);
		} else {
			g_queue_push_tail (data->queue, value);
		}
	}

	/* invoke the first target */
	gs_plugin_systemd_sysupdate_refresh_metadata_iter (NULL, NULL, g_steal_pointer (&task));
}

/* Iterate over the elements of the queue one-by-one.
 */
static void
gs_plugin_systemd_sysupdate_refresh_metadata_iter (GObject      *source_object,
                                                   GAsyncResult *result,
                                                   gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginSystemdSysupdateRefreshMetadataData *data = g_task_get_task_data (task);
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) local_error = NULL;
	TargetItem *target;

	if (result != NULL &&
	    !gs_plugin_systemd_sysupdate_target_refresh_metadata_finish (GS_PLUGIN (self), result, &local_error)) {
		g_debug ("Failed to refresh metadata: %s", local_error->message);
		g_clear_error (&local_error);
	}

	target = g_queue_pop_head (data->queue);
	if (target == NULL) {
		self->is_metadata_refresh_ongoing = FALSE;
		/* We reached the end of the queue. */
		g_task_return_boolean (task, TRUE);
		return;
	}

	if (g_task_return_error_if_cancelled (task)) {
		self->is_metadata_refresh_ongoing = FALSE;
		g_debug ("%s: Cancelled", G_STRFUNC);
		return;
	}

	gs_plugin_systemd_sysupdate_target_refresh_metadata_async (GS_PLUGIN (self),
	                                                           target,
	                                                           data->flags,
	                                                           cancellable,
	                                                           gs_plugin_systemd_sysupdate_refresh_metadata_iter,
	                                                           g_steal_pointer (&task));
}

static gboolean
gs_plugin_systemd_sysupdate_refresh_metadata_finish (GsPlugin      *plugin,
                                                     GAsyncResult  *result,
                                                     GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_async (GsPlugin                     *plugin,
                                                           TargetItem                   *target,
                                                           GsPluginRefreshMetadataFlags  flags,
                                                           GCancellable                 *cancellable,
                                                           GAsyncReadyCallback           callback,
                                                           gpointer                      user_data)
{
	/* Periodically update the targets saved
	 */
	g_autoptr(GTask) task = NULL;
	GsPluginSystemdSysupdateTargetRefreshMetadataData *data = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_target_refresh_metadata_async);

	/* put apps in queue to task data */
	data = gs_plugin_systemd_sysupdate_target_refresh_metadata_data_new (target, flags);
	g_task_set_task_data (task, data, (GDestroyNotify)gs_plugin_systemd_sysupdate_target_refresh_metadata_data_free);

	gs_systemd_sysupdate_target_proxy_new (gs_plugin_get_system_bus_connection (plugin),
	                                       G_DBUS_PROXY_FLAGS_NONE,
	                                       "org.freedesktop.sysupdate1",
	                                       data->target->object_path,
	                                       cancellable,
	                                       gs_plugin_systemd_sysupdate_target_refresh_metadata_proxy_new_cb,
	                                       g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_proxy_new_cb (GObject      *source_object,
                                                                  GAsyncResult *result,
                                                                  gpointer      user_data)
{
	GsPluginSystemdSysupdateTargetRefreshMetadataData *data = NULL;
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GsSystemdSysupdateTarget) proxy = NULL;
	GCancellable *cancellable = g_task_get_cancellable (task);
	GDBusCallFlags call_flags = G_DBUS_CALL_FLAGS_NONE;

	proxy = gs_systemd_sysupdate_target_proxy_new_finish (result, &local_error);
	if (proxy == NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	data = g_task_get_task_data (task);
	g_set_object (&data->target->proxy, proxy);

	if (data->flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE) {
		call_flags |= G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION;
	}

	gs_systemd_sysupdate_target_call_get_app_stream (data->target->proxy,
	                                                 call_flags,
	                                                 SYSUPDATED_TARGET_GET_APP_STREAM_TIMEOUT_MS,
	                                                 cancellable,
	                                                 gs_plugin_systemd_sysupdate_target_refresh_metadata_get_app_stream_cb,
	                                                 g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_get_app_stream_cb (GObject      *source_object,
                                                                       GAsyncResult *result,
                                                                       gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	g_auto(GStrv) appstream_urls = NULL;
	GsPluginSystemdSysupdateTargetRefreshMetadataData *data = NULL;
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	GsPlugin *plugin = GS_PLUGIN (self);
	GCancellable *cancellable = g_task_get_cancellable (task);
	const gchar *cache_kind = NULL;

	if (!gs_systemd_sysupdate_target_call_get_app_stream_finish (GS_SYSTEMD_SYSUPDATE_TARGET (source_object),
	                                                             &appstream_urls,
	                                                             result,
	                                                             &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	data = g_task_get_task_data (task);

	cache_kind = target_item_get_xml_cache_kind (data->target, plugin, &local_error);
	if (cache_kind == NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	gs_external_appstream_refresh_async (cache_kind,
	                                     appstream_urls,
	                                     self->cache_age_secs,
	                                     NULL,
	                                     NULL,
	                                     cancellable,
	                                     gs_plugin_systemd_sysupdate_target_refresh_metadata_external_appstream_refresh_cb,
	                                     g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_external_appstream_refresh_cb (GObject      *source_object,
                                                                                   GAsyncResult *result,
                                                                                   gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	GCancellable *cancellable = g_task_get_cancellable (task);
	GsPluginSystemdSysupdateTargetRefreshMetadataData *data = NULL;
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	GsPlugin *plugin = GS_PLUGIN (self);
	g_auto(GStrv) appstream_paths = NULL;
	XbSilo *silo;
	GDBusCallFlags call_flags = G_DBUS_CALL_FLAGS_NONE;

	/* FIXME Should return which files were updated and which weren't so we can know which ones to reload. */
	if (!gs_external_appstream_refresh_finish (result, &appstream_paths, &local_error)) {
		/* Intentionally ignore errors to avoid blocking host updates
		 * just because metadata failed to be updated. */
		g_debug ("Failed to refresh appstream: %s", local_error->message);
		g_clear_error (&local_error);
	}

	/* TODO Clear unused cached XML files for this target. */

	data = g_task_get_task_data (task);

	silo = target_item_ensure_silo_for_appstream_paths (data->target, plugin, appstream_paths, cancellable, &local_error);
	if (silo == NULL) {
		/* We don't want to block updates for the host target because we
		 * couldn't get appstream metadata as this is how fixes to the
		 * update process are delivered. */
		if (g_strcmp0 (data->target->class, "host") == 0) {
			g_debug ("Failed to get the XML blob for host target: %s", local_error->message);
			g_clear_error (&local_error);
		} else {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

	if (data->flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE) {
		call_flags |= G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION;
	}

	gs_systemd_sysupdate_target_call_get_version (data->target->proxy,
	                                              call_flags,
	                                              SYSUPDATED_TARGET_GET_VERSION_TIMEOUT_MS,
	                                              cancellable,
	                                              gs_plugin_systemd_sysupdate_target_refresh_metadata_get_version_cb,
	                                              g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_get_version_cb (GObject      *source_object,
                                                                    GAsyncResult *result,
                                                                    gpointer      user_data)
{
	GsPluginSystemdSysupdateTargetRefreshMetadataData *data = NULL;
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *current_version = NULL;
	GCancellable *cancellable = g_task_get_cancellable (task);
	GDBusCallFlags call_flags = G_DBUS_CALL_FLAGS_NONE;

	if (!gs_systemd_sysupdate_target_call_get_version_finish (GS_SYSTEMD_SYSUPDATE_TARGET (source_object),
	                                                          &current_version,
	                                                          result,
	                                                          &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	data = g_task_get_task_data (task);

	/* Ensure version strings are NULL rather than empty. */
	g_clear_pointer (&data->target->current_version, g_free);
	if (current_version != NULL && *current_version != '\0') {
		data->target->current_version = g_steal_pointer (&current_version);
	}

	if (data->flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE) {
		call_flags |= G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION;
	}

	/* move on to check new version */
	gs_systemd_sysupdate_target_call_check_new (data->target->proxy,
	                                            call_flags,
	                                            SYSUPDATED_TARGET_CHECK_NEW_TIMEOUT_MS,
	                                            cancellable,
	                                            gs_plugin_systemd_sysupdate_target_refresh_metadata_check_new_cb,
	                                            g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_target_refresh_metadata_check_new_cb (GObject      *source_object,
                                                                  GAsyncResult *result,
                                                                  gpointer      user_data)
{
	GsPluginSystemdSysupdateTargetRefreshMetadataData *data = NULL;
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	g_autoptr(GsApp) app = NULL;
	g_autofree gchar *latest_version = NULL;

	/* currently, the returned result contains only one string
	 * representing the latest version found in the server. However,
	 * it can possibly be an empty string representing no newer
	 * version available */
	if (!gs_systemd_sysupdate_target_call_check_new_finish (GS_SYSTEMD_SYSUPDATE_TARGET (source_object),
	                                                        &latest_version,
	                                                        result,
	                                                        &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	data = g_task_get_task_data (task);

	/* Ensure version strings are NULL rather than empty. */
	g_clear_pointer (&data->target->latest_version, g_free);
	if (latest_version != NULL && *latest_version != '\0') {
		data->target->latest_version = g_steal_pointer (&latest_version);
	}

	/* update app state base on the target's new version */
	app = get_or_create_app_for_target (self, data->target, &local_error);
	if (app == NULL) {
		g_debug ("Couldn't refresh app for target %s: %s", target_item_get_id (data->target), local_error->message);
		g_clear_error (&local_error);
	} else {
		update_app_for_target (self, app, data->target);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_systemd_sysupdate_target_refresh_metadata_finish (GsPlugin      *plugin,
                                                            GAsyncResult  *result,
                                                            GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_systemd_sysupdate_update_apps_async (GsPlugin                           *plugin,
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
	/* Install the given system updates
	 */
	GsPluginSystemdSysupdateUpdateAppsData *data = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GQueue) queue = NULL;

	/* TODO Report progress */

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_update_apps_async);

	/* It's forbidden to mix these flags, but let's check just in case. */
	if (flags & GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD &&
	    flags & GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* The download and apply steps are merged into a single operation in
	 * systemd-sysupdate, meaning we can't download the update without
	 * downloading and vice versa. In the meantime, let's do as the
	 * eos-updater plugin by completing the task successfully on
	 * NO_DOWNLOAD and by ignoring NO_APPLY. */
	/* TODO Split the download and apply steps once systemd-sysupdate allows
	 * it: https://github.com/systemd/systemd/issues/34814 */
	if (flags & GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	queue = g_queue_new ();
	for (guint i = 0; i < gs_app_list_length (apps); i++) {
		GsApp *app = gs_app_list_index (apps, i);

		/* only process this app if was created by this plugin */
		if (!gs_app_has_management_plugin (app, plugin)) {
			continue;
		}

		/* only update the app if it is source available */
		if (gs_app_get_state (app) != GS_APP_STATE_AVAILABLE &&
		    gs_app_get_state (app) != GS_APP_STATE_AVAILABLE_LOCAL &&
		    gs_app_get_state (app) != GS_APP_STATE_UPDATABLE &&
		    gs_app_get_state (app) != GS_APP_STATE_UPDATABLE_LIVE &&
		    gs_app_get_state (app) != GS_APP_STATE_QUEUED_FOR_INSTALL) {
			continue;
		}

		g_queue_push_head (queue, g_object_ref (app));
	}

	/* put apps in queue to task data */
	data = gs_plugin_systemd_sysupdate_update_apps_data_new (g_steal_pointer (&queue),
	                                                         progress_callback,
	                                                         progress_user_data,
	                                                         app_needs_user_action_callback,
	                                                         app_needs_user_action_data,
	                                                         cancellable,
	                                                         flags);
	g_task_set_task_data (task, data, (GDestroyNotify)gs_plugin_systemd_sysupdate_update_apps_data_free);

	gs_plugin_systemd_sysupdate_update_apps_iter (NULL, NULL, g_steal_pointer (&task));
}

/* Iterate over the elements of the queue one-by-one.
 *
 * While the typical use case is to have only a single update target, there
 * could be multiple ones, so this could be improved in the future by applying
 * the updates in parallel.
 */
static void
gs_plugin_systemd_sysupdate_update_apps_iter (GObject      *source_object,
                                              GAsyncResult *result,
                                              gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginSystemdSysupdateUpdateAppsData *data = g_task_get_task_data (task);
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	g_autoptr(GError) local_error = NULL;
	g_autoptr (GsApp) app = NULL;

	if (result != NULL &&
	    !gs_plugin_systemd_sysupdate_update_app_finish (GS_PLUGIN (self), result, &local_error)) {
		g_debug ("Failed to update app: %s", local_error->message);
		g_clear_error (&local_error);
	}

	app = g_queue_pop_head (data->queue);
	if (app == NULL) {
		/* We reached the end of the queue. */
		g_task_return_boolean (task, TRUE);
		return;
	}

	if (g_task_return_error_if_cancelled (task)) {
		g_debug ("%s: Cancelled", G_STRFUNC);
		return;
	}

	gs_plugin_systemd_sysupdate_update_app_async (GS_PLUGIN (self),
	                                              app,
	                                              data->flags & GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE,
	                                              data->progress_callback,
	                                              data->progress_user_data,
	                                              data->app_needs_user_action_callback,
	                                              data->app_needs_user_action_data,
	                                              data->cancellable,
	                                              gs_plugin_systemd_sysupdate_update_apps_iter,
	                                              g_steal_pointer (&task));
}

static gboolean
gs_plugin_systemd_sysupdate_update_apps_finish (GsPlugin      *plugin,
                                                GAsyncResult  *result,
                                                GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
update_app_cancelled_cb (GCancellable *cancellable,
                         gpointer      user_data)
{
	GTask *task = G_TASK (user_data);
	GsPluginSystemdSysupdate *self = NULL;
	GsPluginSystemdSysupdateUpdateAppData *data = NULL;

	if (!g_cancellable_is_cancelled (cancellable)) {
		return;
	}

	self = g_task_get_source_object (task);
	data = g_task_get_task_data (task);
	gs_plugin_systemd_sysupdate_cancel_job (self, data->app, data->interactive);
}

static void
gs_plugin_systemd_sysupdate_update_app_async (GsPlugin                           *plugin,
                                              GsApp                              *app,
                                              gboolean                            interactive,
                                              GsPluginProgressCallback            progress_callback,
                                              gpointer                            progress_user_data,
                                              GsPluginAppNeedsUserActionCallback  app_needs_user_action_callback,
                                              gpointer                            app_needs_user_action_data,
                                              GCancellable                       *cancellable,
                                              GAsyncReadyCallback                 callback,
                                              gpointer                            user_data)
{
	/* Install the given system updates
	 */
	GsPluginSystemdSysupdateUpdateAppData *data = NULL;
	g_autoptr(GTask) task = NULL;
	gulong cancelled_id = 0;

	/* TODO Report progress */

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_update_apps_async);

	/* connect to cancellation signal */
	if (cancellable != NULL) {
		cancelled_id = g_cancellable_connect (cancellable,
		                                      G_CALLBACK (update_app_cancelled_cb),
		                                      (gpointer)task,
		                                      (GDestroyNotify)NULL);
	}

	data = gs_plugin_systemd_sysupdate_update_app_data_new (app,
	                                                        cancellable,
	                                                        cancelled_id,
	                                                        interactive);
	g_task_set_task_data (task, data, (GDestroyNotify) gs_plugin_systemd_sysupdate_update_app_data_free);

	if (!interactive) {
		gs_metered_block_on_download_scheduler_async (gs_metered_build_scheduler_parameters_for_app (data->app),
		                                              cancellable,
		                                              gs_plugin_systemd_sysupdate_update_app_download_scheduler_cb,
		                                              g_steal_pointer (&task));
	} else {
		gs_plugin_systemd_sysupdate_update_app_download_scheduler_cb (NULL, NULL, g_steal_pointer (&task));
	}
}

static void
gs_plugin_systemd_sysupdate_update_app_download_scheduler_cb (GObject      *source_object,
                                                              GAsyncResult *result,
                                                              gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginSystemdSysupdateUpdateAppData *data = g_task_get_task_data (task);
	g_autoptr(GError) local_error = NULL;
	TargetItem *target = NULL;
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);

	if (result != NULL &&
	    !gs_metered_block_on_download_scheduler_finish (result, &data->schedule_entry_handle, &local_error)) {
		g_warning ("Failed to block on download scheduler: %s",
		           local_error->message);
		g_clear_error (&local_error);
	}

	/* find the target associated to the app */
	target = lookup_target_by_app (self, data->app);
	if (target == NULL) {
		gs_plugin_systemd_sysupdate_update_app_data_remove_from_download_scheduler (data);
		g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
		                         "Can´t find target for app: %s", gs_app_get_name (data->app));
		return;
	}

	/* update the 'target' to specific version */
	gs_plugin_systemd_sysupdate_update_target_async (self,
	                                                 data->app,
	                                                 target->object_path,
	                                                 gs_app_get_version (data->app),
	                                                 data->interactive,
	                                                 cancellable,
	                                                 gs_plugin_systemd_sysupdate_update_app_update_target_cb,
	                                                 g_steal_pointer (&task));
}

static void
gs_plugin_systemd_sysupdate_update_app_update_target_cb (GObject      *source_object,
                                                         GAsyncResult *result,
                                                         gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	g_autoptr(GError) local_error = NULL;
	GsPluginSystemdSysupdateUpdateAppData *data = g_task_get_task_data (task);
	GsPluginSystemdSysupdate *self = g_task_get_source_object (task);
	GCancellable *cancellable = g_task_get_cancellable (task);

	if (!gs_plugin_systemd_sysupdate_update_target_finish (self, result, &local_error)) {
		gs_plugin_systemd_sysupdate_update_app_data_remove_from_download_scheduler (data);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	if (data->schedule_entry_handle != NULL) {
		gs_metered_remove_from_download_scheduler_async (data->schedule_entry_handle,
		                                                 cancellable,
		                                                 gs_plugin_systemd_sysupdate_update_app_remove_from_download_scheduler_cb,
		                                                 g_steal_pointer (&task));
	} else {
		gs_plugin_systemd_sysupdate_update_app_remove_from_download_scheduler_cb (NULL, NULL, g_steal_pointer (&task));
	}
}

static void
gs_plugin_systemd_sysupdate_update_app_remove_from_download_scheduler_cb (GObject      *source_object,
                                                                          GAsyncResult *result,
                                                                          gpointer      user_data)
{
	g_autoptr(GTask) task = g_steal_pointer (&user_data);
	GsPluginSystemdSysupdateUpdateAppData *data = g_task_get_task_data (task);
	g_autoptr(GError) local_error = NULL;

	if (result != NULL &&
	    !gs_metered_remove_from_download_scheduler_finish (g_steal_pointer (&data->schedule_entry_handle), result, &local_error)) {
		g_warning ("Failed to remove from download scheduler: %s",
		           local_error->message);
		g_clear_error (&local_error);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_systemd_sysupdate_update_app_finish (GsPlugin      *plugin,
                                               GAsyncResult  *result,
                                               GError       **error)
{
	GsPluginSystemdSysupdateUpdateAppData *data = NULL;
	GTask *task = G_TASK (result);
	GCancellable *cancellable = g_task_get_cancellable (task);

	data = g_task_get_task_data (task);

	/* disconnect cancellation signal */
	if (data != NULL) {
		g_cancellable_disconnect (cancellable, data->cancelled_id);
		data->cancelled_id = 0;
	}

	return g_task_propagate_boolean (g_steal_pointer (&task), error);
}

static void
gs_plugin_systemd_sysupdate_install_apps_async (GsPlugin                           *plugin,
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
	GsPluginUpdateAppsFlags update_flags = GS_PLUGIN_UPDATE_APPS_FLAGS_NONE;

	if (flags & GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE)
		update_flags |= GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE;
	if (flags & GS_PLUGIN_INSTALL_APPS_FLAGS_NO_DOWNLOAD)
		update_flags |= GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD;
	if (flags & GS_PLUGIN_INSTALL_APPS_FLAGS_NO_APPLY)
		update_flags |= GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY;

	gs_plugin_systemd_sysupdate_update_apps_async (plugin,
	                                               apps,
	                                               update_flags,
	                                               progress_callback,
	                                               progress_user_data,
	                                               event_callback,
	                                               event_user_data,
	                                               app_needs_user_action_callback,
	                                               app_needs_user_action_data,
	                                               cancellable,
	                                               callback,
	                                               user_data);
}

static gboolean
gs_plugin_systemd_sysupdate_install_apps_finish (GsPlugin      *plugin,
                                                 GAsyncResult  *result,
                                                 GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_systemd_sysupdate_download_upgrade_async (GsPlugin                     *plugin,
                                                    GsApp                        *app,
                                                    GsPluginDownloadUpgradeFlags  flags,
                                                    GsPluginEventCallback         event_callback,
                                                    void                         *event_user_data,
                                                    GCancellable                 *cancellable,
                                                    GAsyncReadyCallback           callback,
                                                    gpointer                      user_data)
{
	/* Flag specific distro upgrade as downloadable and installable
	 */
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_systemd_sysupdate_download_upgrade_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* only update the app if it is source available */
	if (gs_app_get_state (app) != GS_APP_STATE_AVAILABLE &&
	    gs_app_get_state (app) != GS_APP_STATE_AVAILABLE_LOCAL) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_systemd_sysupdate_download_upgrade_finish (GsPlugin      *plugin,
                                                     GAsyncResult  *result,
                                                     GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_systemd_sysupdate_trigger_upgrade_async (GsPlugin                    *plugin,
                                                   GsApp                       *app,
                                                   GsPluginTriggerUpgradeFlags  flags,
                                                   GCancellable                *cancellable,
                                                   GAsyncReadyCallback          callback,
                                                   gpointer                     user_data)
{
	/* Download and install specific distro upgrade
	 */
	g_autoptr(GsAppList) apps = NULL;

	apps = gs_app_list_new ();
	gs_app_list_add (apps, app);

	gs_plugin_systemd_sysupdate_update_apps_async (plugin, apps,
	                                               GS_PLUGIN_UPDATE_APPS_FLAGS_NONE,
	                                               NULL, NULL,
	                                               NULL, NULL,
	                                               NULL, NULL,
	                                               cancellable,
	                                               callback, user_data);
}

static gboolean
gs_plugin_systemd_sysupdate_trigger_upgrade_finish (GsPlugin      *plugin,
                                                    GAsyncResult  *result,
                                                    GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_systemd_sysupdate_adopt_app (GsPlugin *plugin,
                                       GsApp    *app)
{
	/* Adopt app originally discovered by other plugins
	 */

#if AS_CHECK_VERSION(1, 0, 4)
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_SYSUPDATE) {
		gs_app_set_management_plugin (app, plugin);
	}
#endif
}

static void
gs_plugin_systemd_sysupdate_class_init (GsPluginSystemdSysupdateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_systemd_sysupdate_dispose;
	object_class->finalize = gs_plugin_systemd_sysupdate_finalize;

	plugin_class->setup_async = gs_plugin_systemd_sysupdate_setup_async;
	plugin_class->setup_finish = gs_plugin_systemd_sysupdate_setup_finish;
	plugin_class->adopt_app = gs_plugin_systemd_sysupdate_adopt_app;
	plugin_class->refine_async = gs_plugin_systemd_sysupdate_refine_async;
	plugin_class->refine_finish = gs_plugin_systemd_sysupdate_refine_finish;
	plugin_class->list_apps_async = gs_plugin_systemd_sysupdate_list_apps_async;
	plugin_class->list_apps_finish = gs_plugin_systemd_sysupdate_list_apps_finish;
	plugin_class->refresh_metadata_async = gs_plugin_systemd_sysupdate_refresh_metadata_async;
	plugin_class->refresh_metadata_finish = gs_plugin_systemd_sysupdate_refresh_metadata_finish;
	plugin_class->update_apps_async = gs_plugin_systemd_sysupdate_update_apps_async;
	plugin_class->update_apps_finish = gs_plugin_systemd_sysupdate_update_apps_finish;
	plugin_class->install_apps_async = gs_plugin_systemd_sysupdate_install_apps_async;
	plugin_class->install_apps_finish = gs_plugin_systemd_sysupdate_install_apps_finish;
	plugin_class->download_upgrade_async = gs_plugin_systemd_sysupdate_download_upgrade_async;
	plugin_class->download_upgrade_finish = gs_plugin_systemd_sysupdate_download_upgrade_finish;
	plugin_class->trigger_upgrade_async = gs_plugin_systemd_sysupdate_trigger_upgrade_async;
	plugin_class->trigger_upgrade_finish = gs_plugin_systemd_sysupdate_trigger_upgrade_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_SYSTEMD_SYSUPDATE;
}

