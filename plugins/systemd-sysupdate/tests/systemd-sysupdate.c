/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (c) 2024 Codethink Limited
 * Copyright (c) 2024 GNOME Foundation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib/gstdio.h>
#include <libglib-testing/dbus-queue.h>
#include <libsoup/soup.h>

#include "gnome-software-private.h"
#include "gs-test.h"

#include "config.h"
#include "gs-systemd-sysupdated-generated.h"

/*
 * Here we do the integration test, which means we validate the
 * results indirectly from plugin-loader's point of view without
 * touching the plugin (code under test).
 */

static void mock_web_handler_cb (SoupServer        *server,
                                 SoupServerMessage *msg,
                                 const char        *path,
                                 GHashTable        *query,
                                 gpointer           user_data);

#define N_TARGETS_MAX 5  /* arbitrarily chosen to be big enough for any of the tests */

typedef struct {
	GtDBusQueue *queue;  /* (owned) */

	unsigned int target_object_ids[N_TARGETS_MAX];

	GsPluginLoader *plugin_loader;  /* (owned) */

	SoupServer *web;  /* (owned) */
	int web_port;
} BusFixture;

/**
 * UpdateTargetInfo:
 *
 * Fake update target info reported by the mocked service.
 */
typedef struct {
	const gchar *class;
	const gchar *name;
	const gchar *object_path;
	const gchar *current_version;
	const gchar *latest_version;
} UpdateTargetInfo;

/**
 * UpdateAppInfo:
 *
 * Expected app info to be created by the plugin.
 */
typedef struct {
	const gchar *id;
	const gchar *version;
	const GsAppState state;
	const AsComponentKind kind;
	/* metadata `SystemdSysupdated::Target`, this value must be the
	 * same as the name of the associated update target (assume app
	 * to target is one-to-one mapping) */
	const gchar *metadata_target;
} UpdateAppInfo;

/**
 * UpdateTarget:
 *
 * Wrapper of the target info and expected app.
 */
typedef struct {
	const UpdateTargetInfo target_info;
	const char *app_id;
} UpdateTarget;

/* Read-only data for setting up the initial test fixture state.
 *
 * The set of targets exposed by the mock sysupdated daemon may change from
 * what’s listed here throughout the test. */
typedef struct {
	const UpdateTarget *targets[N_TARGETS_MAX];
} TestData;

static const UpdateTarget *
assert_test_data_find_update_target_by_path (const TestData *test_data,
                                             const char     *object_path)
{
	for (size_t i = 0; test_data->targets[i] != NULL; i++) {
		if (g_str_equal (test_data->targets[i]->target_info.object_path, object_path))
			return test_data->targets[i];
	}

	g_assert_not_reached ();
}

static void
bus_set_up (BusFixture *fixture,
            const void *test_data_)
{
	const TestData *test_data = test_data_;
	g_autoptr(GError) local_error = NULL;
	const char * const allowlist[] = {
		"systemd-sysupdate",
		NULL,
	};
	g_autoslist(GUri) uris = NULL;

	fixture->queue = gt_dbus_queue_new ();

	gt_dbus_queue_add_activatable_name (fixture->queue, "org.freedesktop.sysupdate1", &local_error);
	g_assert_no_error (local_error);

	gt_dbus_queue_connect (fixture->queue, &local_error);
	g_assert_no_error (local_error);

	gt_dbus_queue_own_name (fixture->queue, "org.freedesktop.sysupdate1");

	/* Export the manager object */
	gt_dbus_queue_export_object (fixture->queue,
				     "/org/freedesktop/sysupdate1",
				     gs_systemd_sysupdate_manager_interface_info (),
				     &local_error);
	g_assert_no_error (local_error);

	/* And export the targets defined for this test */
	for (size_t i = 0; test_data->targets[i] != NULL; i++) {
		fixture->target_object_ids[i] =
			gt_dbus_queue_export_object (fixture->queue,
						     test_data->targets[i]->target_info.object_path,
						     gs_systemd_sysupdate_target_interface_info (),
						     &local_error);
		g_assert_no_error (local_error);
	}

	/* Although we only need to use the system bus in our test, the
	 * underlying `g_test_dbus_up()` will always override the environment
	 * variable `DBUS_SESSION_BUS_ADDRESS`. As a workaround, we also pass
	 * the connection created as the session bus to the `plugin-loader` to
	 * prevent it from setting up another session bus connection. */
	fixture->plugin_loader = gs_plugin_loader_new (gt_dbus_queue_get_client_connection (fixture->queue),
	                                               gt_dbus_queue_get_client_connection (fixture->queue));
	gs_plugin_loader_add_location (fixture->plugin_loader, LOCALPLUGINDIR);
	gs_plugin_loader_add_location (fixture->plugin_loader, LOCALPLUGINDIR_CORE);
	gs_plugin_loader_setup (fixture->plugin_loader,
	                        allowlist,
	                        NULL,
	                        NULL,
	                        &local_error);
	g_assert_no_error (local_error);

	/* Create the test web service. */
	fixture->web = soup_server_new (NULL, NULL);
	g_assert_nonnull (fixture->web);

	/* Connect on HTTP. */
	soup_server_listen_local (fixture->web, 0, 0, &local_error);
	g_assert_no_error (local_error);

	/* Get the allocated port. */
	uris = soup_server_get_uris (fixture->web);
	g_assert_nonnull (uris);
	g_assert_nonnull (uris->data);

	fixture->web_port = g_uri_get_port (uris->data);
	g_assert_cmpint (fixture->web_port, !=, -1);

	soup_server_add_handler (fixture->web, NULL, mock_web_handler_cb, NULL, NULL);
}

static void
bus_tear_down (BusFixture *fixture,
               const void *test_data_)
{
	/* stop test web server */
	g_clear_pointer (&fixture->web, g_object_unref);

	gs_test_flush_main_context ();

	gs_plugin_loader_shutdown (fixture->plugin_loader, NULL);
	g_assert_finalize_object (g_steal_pointer (&fixture->plugin_loader));

	gs_test_flush_main_context ();

	for (size_t i = 0; i < G_N_ELEMENTS (fixture->target_object_ids); i++) {
		if (fixture->target_object_ids[i] != 0)
			gt_dbus_queue_unexport_object (fixture->queue,
						       g_steal_handle_id (&fixture->target_object_ids[i]));
	}

	gt_dbus_queue_disconnect (fixture->queue, TRUE);
	g_clear_pointer (&fixture->queue, gt_dbus_queue_free);

	gs_test_flush_main_context ();
}

static void
async_result_cb (GObject      *source_object,
                 GAsyncResult *result,
                 void         *user_data)
{
	GAsyncResult **result_out = user_data;

	g_assert (*result_out == NULL);
	*result_out = g_object_ref (result);
	g_main_context_wakeup (NULL);
}

/* this function will get called every time a client attempts to connect */
static void
mock_web_handler_cb (SoupServer        *server,
                     SoupServerMessage *msg,
                     const char        *path,
                     GHashTable        *query,
                     gpointer           user_data)
{
	const gchar *mimetype = "application/xml";
#if AS_CHECK_VERSION(1, 0, 4)
	const gchar *bundle = "sysupdate";
#else
	const gchar *bundle = "package";
#endif
	const gchar *start = NULL;
	g_autofree gchar *id = NULL;
	g_autofree gchar *reply = NULL;
	size_t reply_size;

	if (soup_server_message_get_method (msg) != SOUP_METHOD_GET) {
		soup_server_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED, NULL);
		g_debug ("unexpected method");
		return;
	}

	if (!g_str_has_prefix (path, "/") && g_str_has_suffix (path, ".metainfo.xml")) {
		soup_server_message_set_status (msg, SOUP_STATUS_NOT_FOUND, NULL);
		g_debug ("unexpected appstream path = `%s`", path);
		return;
	}

	start = path + strlen ("/");
	id = g_strndup (start, strlen (start) - strlen (".metainfo.xml"));
	reply = g_strdup_printf ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	                         "<component type=\"operating-system\">\n"
	                         "  <id>%s</id>\n"
	                         "  <metadata_license>CC0-1.0</metadata_license>\n"
	                         "  <name>%s</name>\n"
	                         "  <summary>A target</summary>\n"
	                         "  <bundle type=\"%s\">systemd-sysupdate</bundle>\n"
	                         "</component>\n",
	                         id, id, bundle);
	reply_size = strlen (reply);

	soup_server_message_set_status (msg, SOUP_STATUS_OK, NULL);
	soup_server_message_set_response (msg, mimetype, SOUP_MEMORY_TAKE, g_steal_pointer (&reply), reply_size);
}

static const UpdateTarget target_host = {
	.target_info = {
		.class = "host",
		.name = "host",
		.object_path = "/org/freedesktop/sysupdate1/target/host",
		.current_version = "t.0",
		.latest_version = "t.1",
	},
	.app_id = "systemd-sysupdate.host",
};

static const UpdateTarget target_component_available = {
	.target_info = {
		.class = "component",
		.name = "available",
		.object_path = "/org/freedesktop/sysupdate1/target/component_available",
		.current_version = "",
		.latest_version = "t.1",
	},
	.app_id = "systemd-sysupdate.component-available",
};

static const UpdateTarget target_component_installed = {
	.target_info = {
		.class = "component",
		.name = "installed",
		.object_path = "/org/freedesktop/sysupdate1/target/component_installed",
		.current_version = "t.1",
		.latest_version = "",
	},
	.app_id = "systemd-sysupdate.component-installed",
};

static const UpdateTarget target_component_updatable = {
	.target_info = {
		.class = "component",
		.name = "updatable",
		.object_path = "/org/freedesktop/sysupdate1/target/component_updatable",
		.current_version = "t.0",
		.latest_version = "t.1",
	},
	.app_id = "systemd-sysupdate.component-updatable",
};

static const UpdateTarget target_component_updatable_v2 = {
	.target_info = {
		.class = "component",
		.name = "updatable",
		.object_path = "/org/freedesktop/sysupdate1/target/component_updatable",
		.current_version = "t.0",
		.latest_version = "t.2",
	},
	.app_id = "systemd-sysupdate.component-updatable",
};

/* Run a refresh metadata job on the plugin loader, and mock the interaction
 * between it and systemd-sysupdated. Assert that the operation succeeds. */
static void
assert_refresh_metadata_no_error (BusFixture     *fixture,
                                  const TestData *test_data)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_auto(GVariantBuilder) list_targets_builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("a(sso)"));
	g_autoptr(GDBusMethodInvocation) list_targets_invocation = NULL;
	g_autoptr(GAsyncResult) result = NULL;
	g_autoptr(GHashTable) remaining_targets = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);  /* (element-type utf8 UpdateTarget) */
	g_autoptr(GError) local_error = NULL;

	plugin_job = gs_plugin_job_refresh_metadata_new (0, /* always refresh */
	                                                 GS_PLUGIN_REFRESH_METADATA_FLAGS_NONE);
	gs_plugin_loader_job_process_async (fixture->plugin_loader, plugin_job, NULL, async_result_cb, &result);

	/* Expect a ListTargets() call. */
	list_targets_invocation =
		gt_dbus_queue_assert_pop_message (fixture->queue,
						  "/org/freedesktop/sysupdate1",
						  "org.freedesktop.sysupdate1.Manager",
						  "ListTargets",
						  "()");

	for (size_t i = 0; test_data->targets[i] != NULL; i++) {
		g_variant_builder_add (&list_targets_builder,
		                       "(sso)",
		                       test_data->targets[i]->target_info.class,
		                       test_data->targets[i]->target_info.name,
		                       test_data->targets[i]->target_info.object_path);
	}

	g_dbus_method_invocation_return_value (list_targets_invocation,
					       g_variant_new ("(@a(sso))",
							      g_variant_builder_end (&list_targets_builder)));

	/* And then, for each target (in any order), several calls: */
	for (size_t i = 0; test_data->targets[i] != NULL; i++)
		g_hash_table_insert (remaining_targets,
				     (void *) test_data->targets[i]->target_info.object_path,
				     (void *) test_data->targets[i]);

	while (g_hash_table_size (remaining_targets) > 0) {
		const UpdateTarget *target;
		g_autofree char *appstream_url = NULL;
		g_autoptr(GDBusMethodInvocation) get_app_stream_invocation = NULL, get_version_invocation = NULL, check_new_invocation = NULL;
		g_auto(GVariantBuilder) get_app_stream_builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("as"));

		/* A GetAppStream() call on the target. Pop this before asserting,
		 * so we can work out which target has been chosen by the client. */
		g_assert_true (gt_dbus_queue_pop_message (fixture->queue, &get_app_stream_invocation));
		g_assert_nonnull (get_app_stream_invocation);

		target = g_hash_table_lookup (remaining_targets, g_dbus_method_invocation_get_object_path (get_app_stream_invocation));
		g_assert_nonnull (target);

		g_assert_true (gt_dbus_queue_match_client_message (fixture->queue,
								   get_app_stream_invocation,
								   target->target_info.object_path,
								   "org.freedesktop.sysupdate1.Target",
								   "GetAppStream",
								   "()"));
		appstream_url = g_strdup_printf ("http://localhost:%d/%s.metainfo.xml",
						 fixture->web_port, target->app_id);
		g_variant_builder_add (&get_app_stream_builder, "s", appstream_url);
		g_dbus_method_invocation_return_value (get_app_stream_invocation,
						       g_variant_new ("(@as)",
								      g_variant_builder_end (&get_app_stream_builder)));

		/* And then a GetVersion() call on the target. */
		get_version_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  target->target_info.object_path,
							  "org.freedesktop.sysupdate1.Target",
							  "GetVersion",
							  "()");
		g_dbus_method_invocation_return_value (get_version_invocation,
						       g_variant_new ("(s)",
								      target->target_info.current_version));

		/* And finally a CheckNew() call on the target. */
		check_new_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  target->target_info.object_path,
							  "org.freedesktop.sysupdate1.Target",
							  "CheckNew",
							  "()");
		g_dbus_method_invocation_return_value (check_new_invocation,
						       g_variant_new ("(s)",
								      target->target_info.latest_version));

		g_hash_table_remove (remaining_targets, target->target_info.object_path);
	}

	/* Now wait for the job to finish */
	while (result == NULL)
		g_main_context_iteration (NULL, TRUE);

	gs_plugin_loader_job_process_finish (fixture->plugin_loader, result, NULL, &local_error);
	g_assert_no_error (local_error);
}

static void
assert_list_apps_for_update_no_error (BusFixture  *fixture,
                                      GsAppList  **out_updates_list)
{
	g_autoptr(GsAppQuery) query = NULL;
	g_autoptr(GsPluginJobListApps) list_apps_job = NULL;
	GsAppList *updates_list;
	g_autoptr(GError) local_error = NULL;

	query = gs_app_query_new ("is-for-update", GS_APP_QUERY_TRISTATE_TRUE,
	                          "refine-flags", GS_PLUGIN_REFINE_FLAGS_NONE,
	                          NULL);
	list_apps_job = GS_PLUGIN_JOB_LIST_APPS (gs_plugin_job_list_apps_new (query, GS_PLUGIN_LIST_APPS_FLAGS_NONE));

	gs_plugin_loader_job_process (fixture->plugin_loader, GS_PLUGIN_JOB (list_apps_job), NULL, &local_error);
	g_assert_no_error (local_error);

	updates_list = gs_plugin_job_list_apps_get_result_list (list_apps_job);
	g_assert_nonnull (updates_list);

	if (out_updates_list != NULL)
		*out_updates_list = g_object_ref (updates_list);
}

static void
test_plugin_enabled (BusFixture *fixture,
                     const void *test_data_)
{
	g_test_summary ("Checks that the plugin is enabled. If it isn't, it "
	                "could be because the org.freedesktop.sysupdate1 D-Bus "
	                "service isn't found. Given we mock it up for these "
	                "tests, not finding it is a bug.");

	g_assert_true (gs_plugin_loader_get_enabled (fixture->plugin_loader, "systemd-sysupdate"));
}

static void
test_distro_upgrade (BusFixture *fixture,
                     const void *test_data_)
{
	const TestData *test_data = test_data_;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	GsAppList *list_upgrades;
	g_autoptr(GError) local_error = NULL;

	g_test_summary ("Checks that the plugin doesn't do distro upgrades, as "
	                "for the moment it only handles updates, including for "
	                "the host target.");

	g_assert_true (gs_plugin_loader_get_enabled (fixture->plugin_loader, "systemd-sysupdate"));

	assert_refresh_metadata_no_error (fixture, test_data);

	plugin_job = gs_plugin_job_list_distro_upgrades_new (GS_PLUGIN_LIST_DISTRO_UPGRADES_FLAGS_NONE,
		                                             GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE);

	gs_plugin_loader_job_process (fixture->plugin_loader, plugin_job, NULL, &local_error);
	g_assert_error (local_error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NOT_SUPPORTED);

	list_upgrades = gs_plugin_job_list_distro_upgrades_get_result_list (GS_PLUGIN_JOB_LIST_DISTRO_UPGRADES (plugin_job));
	g_assert_null (list_upgrades);
}

static void
test_app_update_success (BusFixture *fixture,
                         const void *test_data_)
{
	const TestData *test_data = test_data_;
	g_autoptr(GsAppList) updates_list = NULL;
	GsApp *updatable_app;
	g_autoptr(GsPluginJobUpdateApps) update_apps_job = NULL;
	g_autoptr(GAsyncResult) result = NULL;
	const UpdateTarget *target;
	g_autoptr(GDBusMethodInvocation) acquire_invocation = NULL, acquire_job_get_all_invocation = NULL;
	g_autoptr(GDBusMethodInvocation) install_invocation = NULL, install_job_get_all_invocation = NULL;
	unsigned int acquire_job_object_id, install_job_object_id;
	const char *acquire_new_version, *install_new_version;
	uint64_t acquire_flags, install_flags;
	uint64_t acquire_job_id, install_job_id;
	const char *acquire_job_path, *install_job_path;
	int acquire_job_status, install_job_status;
	const char *get_all_interface;
	g_autoptr(GError) local_error = NULL;

	g_test_summary ("Checks that the plugin can handle app updates and tracks their progress");

	g_assert_true (gs_plugin_loader_get_enabled (fixture->plugin_loader, "systemd-sysupdate"));

	assert_refresh_metadata_no_error (fixture, test_data);

	/* Query for updates; this shouldn’t cause any D-Bus traffic as the
	 * data should have been cached after refreshing the metadata */
	assert_list_apps_for_update_no_error (fixture, &updates_list);
	g_assert_cmpuint (gs_app_list_length (updates_list), ==, 1);
	updatable_app = gs_app_list_index (updates_list, 0);
	g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_UPDATABLE);
	g_assert_cmpuint (gs_app_get_progress (updatable_app), ==, GS_APP_PROGRESS_UNKNOWN);

	/* Now update the app, asynchronously so we can inject the mock D-Bus
	 * traffic from the daemon */
	update_apps_job = GS_PLUGIN_JOB_UPDATE_APPS (gs_plugin_job_update_apps_new (updates_list,
										    GS_PLUGIN_UPDATE_APPS_FLAGS_NONE));

	gs_plugin_loader_job_process_async (fixture->plugin_loader, GS_PLUGIN_JOB (update_apps_job),
					    NULL, async_result_cb, &result);

	/* Expect an Acquire() call on the Target for the update. */
	target = assert_test_data_find_update_target_by_path (test_data, "/org/freedesktop/sysupdate1/target/component_updatable");

	acquire_invocation =
		gt_dbus_queue_assert_pop_message (fixture->queue,
						  target->target_info.object_path,
						  "org.freedesktop.sysupdate1.Target",
						  "Acquire",
						  "(&st)",
						  &acquire_new_version,
						  &acquire_flags);
	g_assert_cmpstr (acquire_new_version, ==, "");  /* always update to the latest version for now */
	g_assert_cmpuint (acquire_flags, ==, 0);  /* no flags are defined yet */

	/* Export a Job object to represent the Acquire operation, and return a
	 * path to it to the caller. */
	acquire_job_id = 2;
	acquire_job_path = "/org/freedesktop/sysupdate1/job/_2";

	acquire_job_object_id = gt_dbus_queue_export_object (fixture->queue,
							     acquire_job_path,
							     gs_systemd_sysupdate_job_interface_info (),
							     &local_error);
	g_assert_no_error (local_error);

	g_dbus_method_invocation_return_value (acquire_invocation,
					       g_variant_new ("(sto)",
							      target->target_info.latest_version,
							      acquire_job_id,
							      acquire_job_path));

	acquire_job_get_all_invocation =
		gt_dbus_queue_assert_pop_message (fixture->queue,
						  acquire_job_path,
						  "org.freedesktop.DBus.Properties",
						  "GetAll",
						  "(&s)",
						  &get_all_interface);
	g_assert_cmpstr (get_all_interface, ==, "org.freedesktop.sysupdate1.Job");
	g_dbus_method_invocation_return_value (acquire_job_get_all_invocation,
					       g_variant_new_parsed ("(@a{sv} { 'Id': <@t %t>, 'Type': <'acquire'>, 'Offline': <false>, 'Progress': <@u 0>},)",
								     acquire_job_id));

	/* Time passes, the update is being downloaded so let’s signal some
	 * progress on the Job */
	for (unsigned int progress_percentage = 0; progress_percentage <= 100; progress_percentage += 20) {
		const char *invalidated_properties[] = { NULL };
		g_auto(GVariantBuilder) properties_builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("a{sv}"));

		g_variant_builder_add (&properties_builder,
		                       "{sv}",
		                       "Progress",
		                       g_variant_new_uint32 (progress_percentage));

		gt_dbus_queue_emit_signal (fixture->queue,
		                           NULL,  /* broadcast */
		                           acquire_job_path,
		                           "org.freedesktop.DBus.Properties",
		                           "PropertiesChanged",
		                           g_variant_new ("(s@a{sv}@as)",
							  "org.freedesktop.sysupdate1.Job",
							  g_variant_builder_end (&properties_builder),
							  g_variant_new_strv (invalidated_properties, -1)),
		                           &local_error);
		g_assert_no_error (local_error);

		/* 50 percentage points in the app’s progress are allocated for
		 * Acquire(), and the other 50 for Install(). */
		while (gs_app_get_progress (updatable_app) != progress_percentage * 0.5)
			g_main_context_iteration (NULL, TRUE);

		g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_DOWNLOADING);
	}

	/* Time passes some more, the update download finishes and the Job now
	 * needs to signal as being complete. */
	acquire_job_status = 0;  /* success */
	gt_dbus_queue_emit_signal (fixture->queue,
	                           NULL,  /* broadcast */
	                           "/org/freedesktop/sysupdate1",
	                           "org.freedesktop.sysupdate1.Manager",
	                           "JobRemoved",
	                           g_variant_new ("(toi)",
						  acquire_job_id,
						  acquire_job_path,
						  acquire_job_status),
	                           &local_error);
	g_assert_no_error (local_error);

	gt_dbus_queue_unexport_object (fixture->queue, g_steal_handle_id (&acquire_job_object_id));

	/* Now expect an Install() call on the Target for the update. */
	install_invocation =
		gt_dbus_queue_assert_pop_message (fixture->queue,
						  target->target_info.object_path,
						  "org.freedesktop.sysupdate1.Target",
						  "Install",
						  "(&st)",
						  &install_new_version,
						  &install_flags);
	g_assert_cmpstr (install_new_version, ==, "");  /* always update to the latest version for now */
	g_assert_cmpuint (install_flags, ==, 0);  /* no flags are defined yet */

	/* Export a Job object to represent the Install operation, and return a
	 * path to it to the caller. */
	install_job_id = 3;
	install_job_path = "/org/freedesktop/sysupdate1/job/_3";

	install_job_object_id = gt_dbus_queue_export_object (fixture->queue,
							     install_job_path,
							     gs_systemd_sysupdate_job_interface_info (),
							     &local_error);
	g_assert_no_error (local_error);

	g_dbus_method_invocation_return_value (install_invocation,
					       g_variant_new ("(sto)",
							      target->target_info.latest_version,
							      install_job_id,
							      install_job_path));

	install_job_get_all_invocation =
		gt_dbus_queue_assert_pop_message (fixture->queue,
						  install_job_path,
						  "org.freedesktop.DBus.Properties",
						  "GetAll",
						  "(&s)",
						  &get_all_interface);
	g_assert_cmpstr (get_all_interface, ==, "org.freedesktop.sysupdate1.Job");
	g_dbus_method_invocation_return_value (install_job_get_all_invocation,
					       g_variant_new_parsed ("(@a{sv} { 'Id': <@t %t>, 'Type': <'install'>, 'Offline': <true>, 'Progress': <@u 0>},)",
								     install_job_id));

	/* Time passes, the update is being applied so let’s signal some
	 * progress on the Job */
	for (unsigned int progress_percentage = 0; progress_percentage <= 100; progress_percentage += 20) {
		const char *invalidated_properties[] = { NULL };
		g_auto(GVariantBuilder) properties_builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("a{sv}"));

		g_variant_builder_add (&properties_builder,
		                       "{sv}",
		                       "Progress",
		                       g_variant_new_uint32 (progress_percentage));

		gt_dbus_queue_emit_signal (fixture->queue,
		                           NULL,  /* broadcast */
		                           install_job_path,
		                           "org.freedesktop.DBus.Properties",
		                           "PropertiesChanged",
		                           g_variant_new ("(s@a{sv}@as)",
							  "org.freedesktop.sysupdate1.Job",
							  g_variant_builder_end (&properties_builder),
							  g_variant_new_strv (invalidated_properties, -1)),
		                           &local_error);
		g_assert_no_error (local_error);

		/* 50 percentage points in the app’s progress are allocated for
		 * Acquire(), and the other 50 for Install(). */
		while (gs_app_get_progress (updatable_app) != 50 + progress_percentage * 0.5)
			g_main_context_iteration (NULL, TRUE);

		g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_INSTALLING);
	}

	/* Time passes some more, the update is applied successfully and the Job
	 * now needs to signal as being complete. */
	install_job_status = 0;  /* success */
	gt_dbus_queue_emit_signal (fixture->queue,
	                           NULL,  /* broadcast */
	                           "/org/freedesktop/sysupdate1",
	                           "org.freedesktop.sysupdate1.Manager",
	                           "JobRemoved",
	                           g_variant_new ("(toi)",
						  install_job_id,
						  install_job_path,
						  install_job_status),
	                           &local_error);
	g_assert_no_error (local_error);

	gt_dbus_queue_unexport_object (fixture->queue, g_steal_handle_id (&install_job_object_id));

	/* Now wait for the job to finish */
	while (result == NULL)
		g_main_context_iteration (NULL, TRUE);

	gs_plugin_loader_job_process_finish (fixture->plugin_loader, result, NULL, &local_error);
	g_assert_no_error (local_error);

	/* Check that the app state changes on success */
	g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_INSTALLED);
}

static void
test_app_update_failure (BusFixture *fixture,
                         const void *test_data_)
{
	const TestData *test_data = test_data_;
	const struct {
		const char *acquire_error_name;  /* (nullable) */
		const char *acquire_error_message;  /* (nullable) */
		int acquire_job_status;
		const char *install_error_name;  /* (nullable) */
		const char *install_error_message;  /* (nullable) */
		int install_job_status;
		GsPluginError expected_error_code;
	} vectors[] = {
		{
			"org.freedesktop.DBus.Error.InvalidArgs",
			"Flags must be something new and unexpected",
			0, NULL, NULL, 0,
			GS_PLUGIN_ERROR_FAILED,
		},
		{
			"org.freedesktop.DBus.Error.AccessDenied",
			"Polkit said no",
			0, NULL, NULL, 0,
			GS_PLUGIN_ERROR_NO_SECURITY,
		},
		{
			"org.freedesktop.DBus.Error.Failed",
			"Internal error",
			0, NULL, NULL, 0,
			GS_PLUGIN_ERROR_FAILED,
		},
		{
			"org.freedesktop.DBus.Error.NoMemory",
			"Out of memory",
			0, NULL, NULL, 0,
			GS_PLUGIN_ERROR_FAILED,
		},
		{
			"org.freedesktop.DBus.Error.ObjectPathInUse",
			"Target x busy, ignoring job.",
			0, NULL, NULL, 0,
			GS_PLUGIN_ERROR_FAILED,
		},
		{
			"org.freedesktop.DBus.Error.ServiceUnknown",
			"Inexplicably we’ve got to this point but systemd-sysupdate is not installed",
			0, NULL, NULL, 0,
			GS_PLUGIN_ERROR_NOT_SUPPORTED,
		},
		{
			NULL, NULL,
			-1,  /* some arbitrary non-zero exit status */
			NULL, NULL, 0,
			GS_PLUGIN_ERROR_FAILED,
		},
		{
			NULL, NULL, 0,
			"org.freedesktop.DBus.Error.InvalidArgs",
			"Flags must be something new and unexpected",
			0,
		},
		{
			NULL, NULL, 0,
			"org.freedesktop.DBus.Error.AccessDenied",
			"Polkit said no",
			0,
			GS_PLUGIN_ERROR_NO_SECURITY,
		},
		{
			NULL, NULL, 0,
			"org.freedesktop.DBus.Error.Failed",
			"Internal error",
			0,
			GS_PLUGIN_ERROR_FAILED,
		},
		{
			NULL, NULL, 0,
			"org.freedesktop.DBus.Error.NoMemory",
			"Out of memory",
			0,
			GS_PLUGIN_ERROR_FAILED,
		},
		{
			NULL, NULL, 0,
			"org.freedesktop.DBus.Error.ObjectPathInUse",
			"Target x busy, ignoring job.",
			0,
			GS_PLUGIN_ERROR_FAILED,
		},
		{
			NULL, NULL, 0,
			NULL, NULL,
			-1,  /* some arbitrary non-zero exit status */
			GS_PLUGIN_ERROR_FAILED,
		},
	};

	g_test_summary ("Checks that the plugin can recover an app's state when its update failed in various ways");

	g_assert_true (gs_plugin_loader_get_enabled (fixture->plugin_loader, "systemd-sysupdate"));

	for (size_t i = 0; i < G_N_ELEMENTS (vectors); i++) {
		g_autoptr(GsAppList) updates_list = NULL;
		GsApp *updatable_app;
		g_autoptr(GsPluginJobUpdateApps) update_apps_job = NULL;
		g_autoptr(GAsyncResult) result = NULL;
		const UpdateTarget *target;
		g_autoptr(GDBusMethodInvocation) acquire_invocation = NULL, acquire_job_get_all_invocation = NULL;
		g_autoptr(GDBusMethodInvocation) install_invocation = NULL, install_job_get_all_invocation = NULL;
		unsigned int acquire_job_object_id, install_job_object_id;
		const char *acquire_new_version, *install_new_version;
		uint64_t acquire_flags, install_flags;
		uint64_t acquire_job_id, install_job_id;
		const char *acquire_job_path, *install_job_path;
		int acquire_job_status, install_job_status;
		const char *get_all_interface;
		g_autoptr(GError) local_error = NULL;
		g_autoptr(GPtrArray) events = NULL;
		GsPluginEvent *event;

		g_test_message ("Vector %" G_GSIZE_FORMAT, i);

		assert_refresh_metadata_no_error (fixture, test_data);

		/* Query for updates; this shouldn’t cause any D-Bus traffic as the
		 * data should have been cached after refreshing the metadata */
		assert_list_apps_for_update_no_error (fixture, &updates_list);
		g_assert_cmpuint (gs_app_list_length (updates_list), ==, 1);
		updatable_app = gs_app_list_index (updates_list, 0);
		g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_UPDATABLE);
		g_assert_cmpuint (gs_app_get_progress (updatable_app), ==, GS_APP_PROGRESS_UNKNOWN);

		/* There should be no events on the plugin loader yet. */
		events = gs_plugin_loader_get_events (fixture->plugin_loader);
		g_assert_cmpuint (events->len, ==, 0);
		g_clear_pointer (&events, g_ptr_array_unref);

		/* Now update the app, asynchronously so we can inject the mock D-Bus
		 * traffic from the daemon */
		update_apps_job = GS_PLUGIN_JOB_UPDATE_APPS (gs_plugin_job_update_apps_new (updates_list,
											    GS_PLUGIN_UPDATE_APPS_FLAGS_NONE));

		gs_plugin_loader_job_process_async (fixture->plugin_loader, GS_PLUGIN_JOB (update_apps_job),
						    NULL, async_result_cb, &result);

		/* Expect an Acquire() call on the Target for the update. */
		target = assert_test_data_find_update_target_by_path (test_data, "/org/freedesktop/sysupdate1/target/component_updatable");

		acquire_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  target->target_info.object_path,
							  "org.freedesktop.sysupdate1.Target",
							  "Acquire",
							  "(&st)",
							  &acquire_new_version,
							  &acquire_flags);

		/* Are we simulating an error in response to Acquire()? */
		if (vectors[i].acquire_error_name != NULL) {
			g_dbus_method_invocation_return_dbus_error (acquire_invocation,
								    vectors[i].acquire_error_name,
								    vectors[i].acquire_error_message);
			goto finished;
		}

		/* Export a Job object to represent the Acquire operation, and return a
		 * path to it to the caller. */
		acquire_job_id = 2;
		acquire_job_path = "/org/freedesktop/sysupdate1/job/_2";

		acquire_job_object_id = gt_dbus_queue_export_object (fixture->queue,
								     acquire_job_path,
								     gs_systemd_sysupdate_job_interface_info (),
								     &local_error);
		g_assert_no_error (local_error);

		g_dbus_method_invocation_return_value (acquire_invocation,
						       g_variant_new ("(sto)",
								      target->target_info.latest_version,
								      acquire_job_id,
								      acquire_job_path));

		acquire_job_get_all_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  acquire_job_path,
							  "org.freedesktop.DBus.Properties",
							  "GetAll",
							  "(&s)",
							  &get_all_interface);
		g_assert_cmpstr (get_all_interface, ==, "org.freedesktop.sysupdate1.Job");
		g_dbus_method_invocation_return_value (acquire_job_get_all_invocation,
						       g_variant_new_parsed ("(@a{sv} { 'Id': <@t %t>, 'Type': <'acquire'>, 'Offline': <false>, 'Progress': <@u 0>},)",
									     acquire_job_id));

		/* Time passes (ignore reporting progress), the update download
		 * finishes and the Job now needs to signal as being complete
		 * (either successfully or an error). */
		acquire_job_status = vectors[i].acquire_job_status;
		gt_dbus_queue_emit_signal (fixture->queue,
			                   NULL,  /* broadcast */
			                   "/org/freedesktop/sysupdate1",
			                   "org.freedesktop.sysupdate1.Manager",
			                   "JobRemoved",
			                   g_variant_new ("(toi)",
							  acquire_job_id,
							  acquire_job_path,
							  acquire_job_status),
			                   &local_error);
		g_assert_no_error (local_error);

		gt_dbus_queue_unexport_object (fixture->queue, g_steal_handle_id (&acquire_job_object_id));

		if (acquire_job_status != 0)
			goto finished;

		/* Now expect an Install() call on the Target for the update. */
		install_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  target->target_info.object_path,
							  "org.freedesktop.sysupdate1.Target",
							  "Install",
							  "(&st)",
							  &install_new_version,
							  &install_flags);

		/* Are we simulating an error in response to Install()? */
		if (vectors[i].install_error_name != NULL) {
			g_dbus_method_invocation_return_dbus_error (install_invocation,
								    vectors[i].install_error_name,
								    vectors[i].install_error_message);
			goto finished;
		}

		/* Export a Job object to represent the Install operation, and
		 * return a path to it to the caller. */
		install_job_id = 3;
		install_job_path = "/org/freedesktop/sysupdate1/job/_3";

		install_job_object_id = gt_dbus_queue_export_object (fixture->queue,
								     install_job_path,
								     gs_systemd_sysupdate_job_interface_info (),
								     &local_error);
		g_assert_no_error (local_error);

		g_dbus_method_invocation_return_value (install_invocation,
						       g_variant_new ("(sto)",
								      target->target_info.latest_version,
								      install_job_id,
								      install_job_path));

		install_job_get_all_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  install_job_path,
							  "org.freedesktop.DBus.Properties",
							  "GetAll",
							  "(&s)",
							  &get_all_interface);
		g_assert_cmpstr (get_all_interface, ==, "org.freedesktop.sysupdate1.Job");
		g_dbus_method_invocation_return_value (install_job_get_all_invocation,
						       g_variant_new_parsed ("(@a{sv} { 'Id': <@t %t>, 'Type': <'install'>, 'Offline': <true>, 'Progress': <@u 0>},)",
									     install_job_id));

		/* Time passes (ignore reporting progress), the update download
		 * finishes and the Job now needs to signal as being complete
		 * (either successfully or an error). */

		/* Time passes (ignore reporting progress), and this is the last
		 * opportunity we have to report an error. */
		install_job_status = vectors[i].install_job_status;
		g_assert (install_job_status != 0);

		gt_dbus_queue_emit_signal (fixture->queue,
			                   NULL,  /* broadcast */
			                   "/org/freedesktop/sysupdate1",
			                   "org.freedesktop.sysupdate1.Manager",
			                   "JobRemoved",
			                   g_variant_new ("(toi)",
							  install_job_id,
							  install_job_path,
							  install_job_status),
			                   &local_error);
		g_assert_no_error (local_error);

		gt_dbus_queue_unexport_object (fixture->queue, g_steal_handle_id (&install_job_object_id));

finished:
		/* Now wait for the job to finish. Currently, the plugin aborts
		 * updates of all targets if any of them fail. */
		while (result == NULL)
			g_main_context_iteration (NULL, TRUE);

		gs_plugin_loader_job_process_finish (fixture->plugin_loader, result, NULL, &local_error);
		g_assert_no_error (local_error);

		/* Check that the app state reflects the error */
		g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_UPDATABLE);

		/* And check that an event was emitted about the error */
		events = gs_plugin_loader_get_events (fixture->plugin_loader);
		g_assert_cmpuint (events->len, ==, 1);

		event = g_ptr_array_index (events, 0);
		g_assert_error (gs_plugin_event_get_error (event), GS_PLUGIN_ERROR,
				(int) vectors[i].expected_error_code);

		/* Clean up */
		gs_plugin_loader_remove_events (fixture->plugin_loader);
	}
}

/* Checks that the plugin can cancel app updates. */
static void
test_app_update_cancellation (BusFixture *fixture,
                              const void *test_data_)
{
	const TestData *test_data = test_data_;
	typedef enum {
		CANCELLATION_POINT_BEFORE_START,
		CANCELLATION_POINT_DURING_ACQUIRE,
		CANCELLATION_POINT_DURING_INSTALL,
	} CancellationPoint;

	g_test_summary ("Checks that the plugin can cancel updating an app");

	g_assert_true (gs_plugin_loader_get_enabled (fixture->plugin_loader, "systemd-sysupdate"));

	for (CancellationPoint cancellation_point = CANCELLATION_POINT_BEFORE_START;
	     cancellation_point < CANCELLATION_POINT_DURING_INSTALL + 1;
	     cancellation_point++) {
		g_autoptr(GCancellable) cancellable = g_cancellable_new ();
		g_autoptr(GsAppList) updates_list = NULL;
		GsApp *updatable_app;
		g_autoptr(GsPluginJobUpdateApps) update_apps_job = NULL;
		g_autoptr(GAsyncResult) result = NULL;
		const UpdateTarget *target;
		g_autoptr(GDBusMethodInvocation) acquire_invocation = NULL, acquire_job_get_all_invocation = NULL;
		g_autoptr(GDBusMethodInvocation) install_invocation = NULL, install_job_get_all_invocation = NULL;
		g_autoptr(GDBusMethodInvocation) cancel_invocation = NULL;
		unsigned int acquire_job_object_id, install_job_object_id;
		const char *acquire_new_version, *install_new_version;
		uint64_t acquire_flags, install_flags;
		uint64_t acquire_job_id, install_job_id;
		const char *acquire_job_path, *install_job_path;
		int acquire_job_status, install_job_status;
		const char *get_all_interface;
		g_autoptr(GError) local_error = NULL;

		g_test_message ("Cancellation point %u", cancellation_point);

		assert_refresh_metadata_no_error (fixture, test_data);

		/* Query for updates; this shouldn’t cause any D-Bus traffic as the
		 * data should have been cached after refreshing the metadata */
		assert_list_apps_for_update_no_error (fixture, &updates_list);
		g_assert_cmpuint (gs_app_list_length (updates_list), ==, 1);
		updatable_app = gs_app_list_index (updates_list, 0);
		g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_UPDATABLE);
		g_assert_cmpuint (gs_app_get_progress (updatable_app), ==, GS_APP_PROGRESS_UNKNOWN);

		/* Now update the app, asynchronously so we can inject the mock D-Bus
		 * traffic from the daemon */
		if (cancellation_point == CANCELLATION_POINT_BEFORE_START)
			g_cancellable_cancel (cancellable);

		update_apps_job = GS_PLUGIN_JOB_UPDATE_APPS (gs_plugin_job_update_apps_new (updates_list,
											    GS_PLUGIN_UPDATE_APPS_FLAGS_NONE));

		gs_plugin_loader_job_process_async (fixture->plugin_loader, GS_PLUGIN_JOB (update_apps_job),
						    cancellable, async_result_cb, &result);

		if (cancellation_point == CANCELLATION_POINT_BEFORE_START)
			goto assert_cancelled;

		/* Expect an Acquire() call on the Target for the update. */
		target = assert_test_data_find_update_target_by_path (test_data, "/org/freedesktop/sysupdate1/target/component_updatable");

		acquire_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  target->target_info.object_path,
							  "org.freedesktop.sysupdate1.Target",
							  "Acquire",
							  "(&st)",
							  &acquire_new_version,
							  &acquire_flags);
		g_assert_cmpstr (acquire_new_version, ==, "");  /* always update to the latest version for now */
		g_assert_cmpuint (acquire_flags, ==, 0);  /* no flags are defined yet */

		/* Export a Job object to represent the Acquire operation, and return a
		 * path to it to the caller. */
		acquire_job_id = 2;
		acquire_job_path = "/org/freedesktop/sysupdate1/job/_2";

		acquire_job_object_id = gt_dbus_queue_export_object (fixture->queue,
								     acquire_job_path,
								     gs_systemd_sysupdate_job_interface_info (),
								     &local_error);
		g_assert_no_error (local_error);

		g_dbus_method_invocation_return_value (acquire_invocation,
						       g_variant_new ("(sto)",
								      target->target_info.latest_version,
								      acquire_job_id,
								      acquire_job_path));

		acquire_job_get_all_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  acquire_job_path,
							  "org.freedesktop.DBus.Properties",
							  "GetAll",
							  "(&s)",
							  &get_all_interface);
		g_assert_cmpstr (get_all_interface, ==, "org.freedesktop.sysupdate1.Job");
		g_dbus_method_invocation_return_value (acquire_job_get_all_invocation,
						       g_variant_new_parsed ("(@a{sv} { 'Id': <@t %t>, 'Type': <'acquire'>, 'Offline': <false>, 'Progress': <@u 0>},)",
									     acquire_job_id));

		/* Time passes and the app starts to be downloaded. */
		if (cancellation_point == CANCELLATION_POINT_DURING_ACQUIRE) {
			g_cancellable_cancel (cancellable);

			/* Expect a Cancel() call on the Job */
			cancel_invocation =
				gt_dbus_queue_assert_pop_message (fixture->queue,
								  acquire_job_path,
								  "org.freedesktop.sysupdate1.Job",
								  "Cancel",
								  "()");

			/* Signal cancellation of the Job. */
			acquire_job_status = -1;  /* cancelled */
		} else {
			acquire_job_status = 0;  /* success */
		}

		gt_dbus_queue_emit_signal (fixture->queue,
			                   NULL,  /* broadcast */
			                   "/org/freedesktop/sysupdate1",
			                   "org.freedesktop.sysupdate1.Manager",
			                   "JobRemoved",
			                   g_variant_new ("(toi)",
							  acquire_job_id,
							  acquire_job_path,
							  acquire_job_status),
			                   &local_error);
		g_assert_no_error (local_error);

		gt_dbus_queue_unexport_object (fixture->queue, g_steal_handle_id (&acquire_job_object_id));

		if (acquire_job_status != 0)
			goto assert_cancelled;

		/* Now expect an Install() call on the Target for the update. */
		install_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  target->target_info.object_path,
							  "org.freedesktop.sysupdate1.Target",
							  "Install",
							  "(&st)",
							  &install_new_version,
							  &install_flags);
		g_assert_cmpstr (install_new_version, ==, "");  /* always update to the latest version for now */
		g_assert_cmpuint (install_flags, ==, 0);  /* no flags are defined yet */

		/* Export a Job object to represent the Install operation, and return a
		 * path to it to the caller. */
		install_job_id = 3;
		install_job_path = "/org/freedesktop/sysupdate1/job/_3";

		install_job_object_id = gt_dbus_queue_export_object (fixture->queue,
								     install_job_path,
								     gs_systemd_sysupdate_job_interface_info (),
								     &local_error);
		g_assert_no_error (local_error);

		g_dbus_method_invocation_return_value (install_invocation,
						       g_variant_new ("(sto)",
								      target->target_info.latest_version,
								      install_job_id,
								      install_job_path));

		install_job_get_all_invocation =
			gt_dbus_queue_assert_pop_message (fixture->queue,
							  install_job_path,
							  "org.freedesktop.DBus.Properties",
							  "GetAll",
							  "(&s)",
							  &get_all_interface);
		g_assert_cmpstr (get_all_interface, ==, "org.freedesktop.sysupdate1.Job");
		g_dbus_method_invocation_return_value (install_job_get_all_invocation,
						       g_variant_new_parsed ("(@a{sv} { 'Id': <@t %t>, 'Type': <'install'>, 'Offline': <true>, 'Progress': <@u 0>},)",
									     install_job_id));

		/* Time passes and the app starts to be applied. */
		if (cancellation_point == CANCELLATION_POINT_DURING_INSTALL) {
			g_cancellable_cancel (cancellable);

			/* Expect a Cancel() call on the Job */
			cancel_invocation =
				gt_dbus_queue_assert_pop_message (fixture->queue,
								  install_job_path,
								  "org.freedesktop.sysupdate1.Job",
								  "Cancel",
								  "()");

			/* Signal cancellation of the Job. */
			install_job_status = -1;  /* cancelled */
		} else {
			install_job_status = 0;  /* success */
		}

		gt_dbus_queue_emit_signal (fixture->queue,
			                   NULL,  /* broadcast */
			                   "/org/freedesktop/sysupdate1",
			                   "org.freedesktop.sysupdate1.Manager",
			                   "JobRemoved",
			                   g_variant_new ("(toi)",
							  install_job_id,
							  install_job_path,
							  install_job_status),
			                   &local_error);
		g_assert_no_error (local_error);

		gt_dbus_queue_unexport_object (fixture->queue, g_steal_handle_id (&install_job_object_id));

		if (install_job_status != 0)
			goto assert_cancelled;

assert_cancelled:
		if (cancel_invocation != NULL)
			g_dbus_method_invocation_return_value (cancel_invocation, NULL);

		/* Now wait for the job to finish */
		while (result == NULL)
			g_main_context_iteration (NULL, TRUE);

		gs_plugin_loader_job_process_finish (fixture->plugin_loader, result, NULL, &local_error);
		g_assert_error (local_error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED);

		/* Check that the app state reflects the error */
		g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_UPDATABLE);
	}
}

static void
test_app_update_split (BusFixture *fixture,
                       const void *test_data_)
{
	const TestData *test_data = test_data_;
	g_autoptr(GsAppList) updates_list = NULL;
	GsApp *updatable_app;
	g_autoptr(GsPluginJobUpdateApps) update_apps_job = NULL, update_apps_job2 = NULL;
	g_autoptr(GAsyncResult) result = NULL, result2 = NULL;
	const UpdateTarget *target;
	g_autoptr(GDBusMethodInvocation) acquire_invocation = NULL, acquire_job_get_all_invocation = NULL;
	g_autoptr(GDBusMethodInvocation) install_invocation = NULL, install_job_get_all_invocation = NULL;
	unsigned int acquire_job_object_id, install_job_object_id;
	const char *acquire_new_version, *install_new_version;
	uint64_t acquire_flags, install_flags;
	uint64_t acquire_job_id, install_job_id;
	const char *acquire_job_path, *install_job_path;
	int acquire_job_status, install_job_status;
	const char *get_all_interface;
	g_autoptr(GError) local_error = NULL;
	uint64_t size_download;

	g_test_summary ("Checks that the plugin can handle app updates with NO_DOWNLOAD and NO_APPLY flags");

	g_assert_true (gs_plugin_loader_get_enabled (fixture->plugin_loader, "systemd-sysupdate"));

	assert_refresh_metadata_no_error (fixture, test_data);

	/* Query for updates; this shouldn’t cause any D-Bus traffic as the
	 * data should have been cached after refreshing the metadata */
	assert_list_apps_for_update_no_error (fixture, &updates_list);
	g_assert_cmpuint (gs_app_list_length (updates_list), ==, 1);
	updatable_app = gs_app_list_index (updates_list, 0);
	g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_UPDATABLE);
	g_assert_cmpuint (gs_app_get_progress (updatable_app), ==, GS_APP_PROGRESS_UNKNOWN);

	/* Now update the app, asynchronously so we can inject the mock D-Bus
	 * traffic from the daemon. Only download the update, don’t apply it. */
	update_apps_job = GS_PLUGIN_JOB_UPDATE_APPS (gs_plugin_job_update_apps_new (updates_list,
										    GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY));

	gs_plugin_loader_job_process_async (fixture->plugin_loader, GS_PLUGIN_JOB (update_apps_job),
					    NULL, async_result_cb, &result);

	/* Expect an Acquire() call on the Target for the update. */
	target = assert_test_data_find_update_target_by_path (test_data, "/org/freedesktop/sysupdate1/target/component_updatable");

	acquire_invocation =
		gt_dbus_queue_assert_pop_message (fixture->queue,
						  target->target_info.object_path,
						  "org.freedesktop.sysupdate1.Target",
						  "Acquire",
						  "(&st)",
						  &acquire_new_version,
						  &acquire_flags);
	g_assert_cmpstr (acquire_new_version, ==, "");  /* always update to the latest version for now */
	g_assert_cmpuint (acquire_flags, ==, 0);  /* no flags are defined yet */

	/* Export a Job object to represent the Acquire operation, and return a
	 * path to it to the caller. */
	acquire_job_id = 2;
	acquire_job_path = "/org/freedesktop/sysupdate1/job/_2";

	acquire_job_object_id = gt_dbus_queue_export_object (fixture->queue,
							     acquire_job_path,
							     gs_systemd_sysupdate_job_interface_info (),
							     &local_error);
	g_assert_no_error (local_error);

	g_dbus_method_invocation_return_value (acquire_invocation,
					       g_variant_new ("(sto)",
							      target->target_info.latest_version,
							      acquire_job_id,
							      acquire_job_path));

	acquire_job_get_all_invocation =
		gt_dbus_queue_assert_pop_message (fixture->queue,
						  acquire_job_path,
						  "org.freedesktop.DBus.Properties",
						  "GetAll",
						  "(&s)",
						  &get_all_interface);
	g_assert_cmpstr (get_all_interface, ==, "org.freedesktop.sysupdate1.Job");
	g_dbus_method_invocation_return_value (acquire_job_get_all_invocation,
					       g_variant_new_parsed ("(@a{sv} { 'Id': <@t %t>, 'Type': <'acquire'>, 'Offline': <false>, 'Progress': <@u 0>},)",
								     acquire_job_id));

	/* Time passes, the update is being downloaded so let’s signal some
	 * progress on the Job */
	for (unsigned int progress_percentage = 0; progress_percentage <= 100; progress_percentage += 20) {
		const char *invalidated_properties[] = { NULL };
		g_auto(GVariantBuilder) properties_builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("a{sv}"));

		g_variant_builder_add (&properties_builder,
		                       "{sv}",
		                       "Progress",
		                       g_variant_new_uint32 (progress_percentage));

		gt_dbus_queue_emit_signal (fixture->queue,
		                           NULL,  /* broadcast */
		                           acquire_job_path,
		                           "org.freedesktop.DBus.Properties",
		                           "PropertiesChanged",
		                           g_variant_new ("(s@a{sv}@as)",
							  "org.freedesktop.sysupdate1.Job",
							  g_variant_builder_end (&properties_builder),
							  g_variant_new_strv (invalidated_properties, -1)),
		                           &local_error);
		g_assert_no_error (local_error);

		/* All the percentage points are allocated for Acquire(), since
		 * we’re using FLAGS_NO_APPLY. */
		while (gs_app_get_progress (updatable_app) != progress_percentage)
			g_main_context_iteration (NULL, TRUE);

		g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_DOWNLOADING);
	}

	/* Time passes some more, the update downloads and finishes and the Job
	 * now needs to signal as being complete. */
	acquire_job_status = 0;  /* success */
	gt_dbus_queue_emit_signal (fixture->queue,
	                           NULL,  /* broadcast */
	                           "/org/freedesktop/sysupdate1",
	                           "org.freedesktop.sysupdate1.Manager",
	                           "JobRemoved",
	                           g_variant_new ("(toi)",
						  acquire_job_id,
						  acquire_job_path,
						  acquire_job_status),
	                           &local_error);
	g_assert_no_error (local_error);

	gt_dbus_queue_unexport_object (fixture->queue, g_steal_handle_id (&acquire_job_object_id));

	/* Now wait for the job to finish */
	while (result == NULL)
		g_main_context_iteration (NULL, TRUE);

	gs_plugin_loader_job_process_finish (fixture->plugin_loader, result, NULL, &local_error);
	g_assert_no_error (local_error);

	/* Check that the app state changes on success */
	gt_dbus_queue_assert_no_messages (fixture->queue);
	g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_UPDATABLE);
	g_assert_cmpint (gs_app_get_size_download (updatable_app, &size_download), ==, GS_SIZE_TYPE_VALID);
	g_assert_cmpuint (size_download, ==, 0);

	/* Now apply the update asynchronously. */
	update_apps_job2 = GS_PLUGIN_JOB_UPDATE_APPS (gs_plugin_job_update_apps_new (updates_list,
										     GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD));

	gs_plugin_loader_job_process_async (fixture->plugin_loader, GS_PLUGIN_JOB (update_apps_job2),
					    NULL, async_result_cb, &result2);

	/* Now expect an Install() call on the Target for the update. */
	install_invocation =
		gt_dbus_queue_assert_pop_message (fixture->queue,
						  target->target_info.object_path,
						  "org.freedesktop.sysupdate1.Target",
						  "Install",
						  "(&st)",
						  &install_new_version,
						  &install_flags);
	g_assert_cmpstr (install_new_version, ==, "");  /* always update to the latest version for now */
	g_assert_cmpuint (install_flags, ==, 0);  /* no flags are defined yet */

	/* The app’s progress should be reset to 0% */
	g_assert_cmpuint (gs_app_get_progress (updatable_app), ==, 0);

	/* Export a Job object to represent the Install operation, and return a
	 * path to it to the caller. */
	install_job_id = 3;
	install_job_path = "/org/freedesktop/sysupdate1/job/_3";

	install_job_object_id = gt_dbus_queue_export_object (fixture->queue,
							     install_job_path,
							     gs_systemd_sysupdate_job_interface_info (),
							     &local_error);
	g_assert_no_error (local_error);

	g_dbus_method_invocation_return_value (install_invocation,
					       g_variant_new ("(sto)",
							      target->target_info.latest_version,
							      install_job_id,
							      install_job_path));

	install_job_get_all_invocation =
		gt_dbus_queue_assert_pop_message (fixture->queue,
						  install_job_path,
						  "org.freedesktop.DBus.Properties",
						  "GetAll",
						  "(&s)",
						  &get_all_interface);
	g_assert_cmpstr (get_all_interface, ==, "org.freedesktop.sysupdate1.Job");
	g_dbus_method_invocation_return_value (install_job_get_all_invocation,
					       g_variant_new_parsed ("(@a{sv} { 'Id': <@t %t>, 'Type': <'install'>, 'Offline': <true>, 'Progress': <@u 0>},)",
								     install_job_id));

	/* Time passes, the update is being applied so let’s signal some
	 * progress on the Job */
	for (unsigned int progress_percentage = 0; progress_percentage <= 100; progress_percentage += 20) {
		const char *invalidated_properties[] = { NULL };
		g_auto(GVariantBuilder) properties_builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("a{sv}"));

		g_variant_builder_add (&properties_builder,
		                       "{sv}",
		                       "Progress",
		                       g_variant_new_uint32 (progress_percentage));

		gt_dbus_queue_emit_signal (fixture->queue,
		                           NULL,  /* broadcast */
		                           install_job_path,
		                           "org.freedesktop.DBus.Properties",
		                           "PropertiesChanged",
		                           g_variant_new ("(s@a{sv}@as)",
							  "org.freedesktop.sysupdate1.Job",
							  g_variant_builder_end (&properties_builder),
							  g_variant_new_strv (invalidated_properties, -1)),
		                           &local_error);
		g_assert_no_error (local_error);

		/* All the percentage points are allocated for Install(), since
		 * we’re using FLAGS_NO_DOWNLOAD. */
		while (gs_app_get_progress (updatable_app) != progress_percentage)
			g_main_context_iteration (NULL, TRUE);

		g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_INSTALLING);
	}

	/* Time passes some more, the update is applied successfully and the Job
	 * now needs to signal as being complete. */
	install_job_status = 0;  /* success */
	gt_dbus_queue_emit_signal (fixture->queue,
	                           NULL,  /* broadcast */
	                           "/org/freedesktop/sysupdate1",
	                           "org.freedesktop.sysupdate1.Manager",
	                           "JobRemoved",
	                           g_variant_new ("(toi)",
						  install_job_id,
						  install_job_path,
						  install_job_status),
	                           &local_error);
	g_assert_no_error (local_error);

	gt_dbus_queue_unexport_object (fixture->queue, g_steal_handle_id (&install_job_object_id));

	/* Now wait for the second job to finish */
	while (result2 == NULL)
		g_main_context_iteration (NULL, TRUE);

	gs_plugin_loader_job_process_finish (fixture->plugin_loader, result2, NULL, &local_error);
	g_assert_no_error (local_error);

	/* Check that the app state changes on success */
	g_assert_cmpint (gs_app_get_state (updatable_app), ==, GS_APP_STATE_INSTALLED);
	g_assert_cmpuint (gs_app_get_progress (updatable_app), ==, GS_APP_PROGRESS_UNKNOWN);
}

static void
test_metadata_target_updatable (BusFixture *fixture,
                                const void *test_data_)
{
	const TestData *test_data = test_data_;
	const TestData test_data_updated = {
		.targets = {
			&target_component_updatable_v2,
			NULL
		},
	};
	g_autoptr(GsAppList) updates_list = NULL, updates_list2 = NULL;
	GsApp *updatable_app;

	/* This test currently only works with the expected test_data */
	g_assert (test_data->targets[0] == &target_component_updatable);

	g_test_summary ("Checks that the plugin can track a target’s latest "
	                "version by updating the currently stored target and app");

	g_assert_true (gs_plugin_loader_get_enabled (fixture->plugin_loader, "systemd-sysupdate"));

	/* The updatable target is currently exported on the bus, and this call
	 * will get the mock daemon to return the v1 information for it */
	assert_refresh_metadata_no_error (fixture, test_data);

	/* Query for updates; this shouldn’t cause any D-Bus traffic as the
	 * data should have been cached after refreshing the metadata */
	assert_list_apps_for_update_no_error (fixture, &updates_list);
	g_assert_cmpuint (gs_app_list_length (updates_list), ==, 1);
	updatable_app = gs_app_list_index (updates_list, 0);
	g_assert_cmpstr (gs_app_get_version (updatable_app), ==, "t.1");

	/* Now refresh again, but get the mock daemon to provide updated target
	 * information. */
	assert_refresh_metadata_no_error (fixture, &test_data_updated);

	/* Query for updates again; we should now see v2 */
	assert_list_apps_for_update_no_error (fixture, &updates_list2);
	g_assert_cmpuint (gs_app_list_length (updates_list2), ==, 1);
	updatable_app = gs_app_list_index (updates_list2, 0);
	g_assert_cmpstr (gs_app_get_version (updatable_app), ==, "t.2");
}

static void
test_metadata_target_removable (BusFixture *fixture,
                                const void *test_data_)
{
	const TestData *test_data = test_data_;
	const TestData test_data_updated = {
		.targets = {
			/* empty */
			NULL
		},
	};
	g_autoptr(GsAppList) updates_list = NULL, updates_list2 = NULL;
	GsApp *updatable_app;

	g_test_summary ("Checks that the plugin can remove a stored target if "
			"it has been removed from the configuration");

	g_assert_true (gs_plugin_loader_get_enabled (fixture->plugin_loader, "systemd-sysupdate"));

	/* Several targets are currently exported on the bus, and this call
	 * will get the mock daemon to return the information for them all */
	assert_refresh_metadata_no_error (fixture, test_data);

	/* Query for updates; this shouldn’t cause any D-Bus traffic as the
	 * data should have been cached after refreshing the metadata */
	assert_list_apps_for_update_no_error (fixture, &updates_list);
	g_assert_cmpuint (gs_app_list_length (updates_list), ==, 1);
	updatable_app = gs_app_list_index (updates_list, 0);
	g_assert_cmpstr (gs_app_get_version (updatable_app), ==, "t.1");

	/* Now refresh again, but get the mock daemon to provide no targets this
	 * time. */
	assert_refresh_metadata_no_error (fixture, &test_data_updated);

	/* Query for updates again; we should now see no apps for update */
	assert_list_apps_for_update_no_error (fixture, &updates_list2);
	g_assert_cmpuint (gs_app_list_length (updates_list2), ==, 0);
}

int
main (int argc, char **argv)
{
	const TestData standard_test_data = {
		.targets = {
			&target_host,
			NULL
		},
	};
	const TestData updatable_test_data = {
		.targets = {
			&target_component_available,
			&target_component_installed,
			&target_component_updatable,
			NULL
		},
	};
	const TestData only_updatable_test_data = {
		.targets = {
			&target_component_updatable,
			NULL
		},
	};

	gs_test_init (&argc, &argv);
	g_setenv ("GS_XMLB_VERBOSE", "1", TRUE);

	g_test_add ("/gnome-software/plugins/systemd-sysupdate/plugin-enabled",
	            BusFixture, &standard_test_data,
	            bus_set_up, test_plugin_enabled, bus_tear_down);
	g_test_add ("/gnome-software/plugins/systemd-sysupdate/distro-upgrade",
	            BusFixture, &standard_test_data,
	            bus_set_up, test_distro_upgrade, bus_tear_down);
	g_test_add ("/gnome-software/plugins/systemd-sysupdate/app-update/success",
	            BusFixture, &updatable_test_data,
	            bus_set_up, test_app_update_success, bus_tear_down);
	g_test_add ("/gnome-software/plugins/systemd-sysupdate/app-update/failure",
	            BusFixture, &updatable_test_data,
	            bus_set_up, test_app_update_failure, bus_tear_down);
	g_test_add ("/gnome-software/plugins/systemd-sysupdate/app-update/cancellation",
	            BusFixture, &updatable_test_data,
	            bus_set_up, test_app_update_cancellation, bus_tear_down);
	g_test_add ("/gnome-software/plugins/systemd-sysupdate/app-update/split",
	            BusFixture, &updatable_test_data,
	            bus_set_up, test_app_update_split, bus_tear_down);
	g_test_add ("/gnome-software/plugins/systemd-sysupdate/metadata-target/updatable",
	            BusFixture, &only_updatable_test_data,
	            bus_set_up, test_metadata_target_updatable, bus_tear_down);
	g_test_add ("/gnome-software/plugins/systemd-sysupdate/metadata-target/removable",
	            BusFixture, &updatable_test_data,
	            bus_set_up, test_metadata_target_removable, bus_tear_down);

	return g_test_run ();
}
