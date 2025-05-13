/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (c) 2024 Codethink Limited
 * Copyright (c) 2024 GNOME Foundation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <glib/gstdio.h>
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

/* While g_auto(GMutex) and g_auto(GCond) are available, we can't use them as
 * our mutexes and conds are in variables. This works around that limitation by
 * allowing us to automate initializing and clearing any GMutex and GCond. */

/**
 * GsMutexGuard:
 *
 * Helps ensuring a #GMutex is usable during a given scope thanks to autocleanup
 * functions.
 */
typedef void GsMutexGuard;

static inline GsMutexGuard *
gs_mutex_guard_new (GMutex *mutex)
{
	g_mutex_init (mutex);
	return (GsMutexGuard *) mutex;
}

static inline void
gs_mutex_guard_free (GsMutexGuard *guard)
{
	g_mutex_clear ((GMutex *) guard);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsMutexGuard, gs_mutex_guard_free)

#define GS_MUTEX_AUTO_GUARD(mutex, var) \
  g_autoptr (GsMutexGuard) G_GNUC_UNUSED var = gs_mutex_guard_new (mutex)

/**
 * GsCondGuard:
 *
 * Helps ensuring a #GCond is usable during a given scope thanks to autocleanup
 * functions.
 */
typedef void GsCondGuard;

static inline GsCondGuard *
gs_cond_guard_new (GCond *cond)
{
	g_cond_init (cond);
	return (GsCondGuard *) cond;
}

static inline void
gs_cond_guard_free (GsCondGuard *guard)
{
	g_cond_clear ((GCond *) guard);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsCondGuard, gs_cond_guard_free)

#define GS_COND_AUTO_GUARD(cond, var) \
  g_autoptr (GsCondGuard) G_GNUC_UNUSED var = gs_cond_guard_new (cond)

/**
 * GsMonitor:
 *
 * A mutex and a cond paired together as the monitor synchronization pattern.
 */
typedef struct {
	GMutex lock;
	GCond cond;
} GsMonitor;

/**
 * GsThreadedRunner:
 *
 * Runs a #GMainContext in a dedicated thread with its own main loop.
 */
typedef struct {
	GMainContext *context;
	GMainLoop *loop;
	GThread *thread;
} GsThreadedRunner;

static gpointer
gs_threaded_runner_thread_cb (GsThreadedRunner *threaded_runner)
{
	g_main_context_push_thread_default (threaded_runner->context);
	{
		g_main_loop_run (threaded_runner->loop);
	}
	g_main_context_pop_thread_default (threaded_runner->context);
	return NULL;
}

static void
gs_threaded_runner_init (GsThreadedRunner *threaded_runner,
                         const gchar      *name,
                         GMainContext     *context)
{
	/* push mock systemd-sysupdated service to server thread */
	threaded_runner->context = g_main_context_ref (context);
	threaded_runner->loop = g_main_loop_new (context, FALSE);
	threaded_runner->thread = g_thread_new (name,
	                                        (GThreadFunc) gs_threaded_runner_thread_cb,
	                                        threaded_runner);
}

static gboolean
gs_threaded_runner_is_running_cb (gpointer user_data)
{
	GsMonitor *monitor = user_data;
	G_MUTEX_AUTO_LOCK (&monitor->lock, locker);
	g_cond_signal (&monitor->cond);
	return G_SOURCE_REMOVE;
}

static void
gs_threaded_runner_clear (GsThreadedRunner *threaded_runner)
{
	/* Ensure the thread's main loop is running before trying to
	 * quit it, otherwise we would deadlock trying to join a
	 * never-ending thread. */
	{
		GsMonitor monitor;
		g_autoptr(GSource) source = g_idle_source_new ();
		GS_MUTEX_AUTO_GUARD (&monitor.lock, lock);
		GS_COND_AUTO_GUARD (&monitor.cond, cond);
		G_MUTEX_AUTO_LOCK (&monitor.lock, locker);

		g_source_set_callback (source, gs_threaded_runner_is_running_cb, &monitor, NULL);
		g_source_attach (source, threaded_runner->context);
		g_cond_wait (&monitor.cond, &monitor.lock);
		g_main_loop_quit (threaded_runner->loop);
	}

	g_clear_pointer (&threaded_runner->thread, g_thread_join);
	g_clear_pointer (&threaded_runner->loop, g_main_loop_unref);
	g_clear_pointer (&threaded_runner->context, g_main_context_unref);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(GsThreadedRunner, gs_threaded_runner_clear)

/* this function will get called everytime a client attempts to connect */
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
	const UpdateAppInfo app_info;
} UpdateTarget;

static const UpdateTarget target_host = {
	.target_info = {
		.class = "host",
		.name = "host",
		.object_path = "/org/freedesktop/sysupdate1/target/host",
		.current_version = "t.0",
		.latest_version = "t.1",
	},
	.app_info = {
		.id = "systemd-sysupdate.host",
		.version = "t.1",
		.state = GS_APP_STATE_AVAILABLE,
		.kind = AS_COMPONENT_KIND_OPERATING_SYSTEM,
		.metadata_target = "host",
	},
};

static const UpdateTarget target_component_available = {
	.target_info = {
		.class = "component",
		.name = "available",
		.object_path = "/org/freedesktop/sysupdate1/target/component_available",
		.current_version = "",
		.latest_version = "t.1",
	},
	.app_info = {
		.id = "systemd-sysupdate.component-available",
		.version = "t.1",
		.state = GS_APP_STATE_AVAILABLE,
		.kind = AS_COMPONENT_KIND_OPERATING_SYSTEM,
		.metadata_target = "available",
	},
};

static const UpdateTarget target_component_installed = {
	.target_info = {
		.class = "component",
		.name = "installed",
		.object_path = "/org/freedesktop/sysupdate1/target/component_installed",
		.current_version = "t.1",
		.latest_version = "",
	},
	.app_info = {
		.id = "systemd-sysupdate.component-installed",
		.version = "t.1",
		.state = GS_APP_STATE_AVAILABLE,
		.kind = AS_COMPONENT_KIND_OPERATING_SYSTEM,
		.metadata_target = "installed",
	},
};

static const UpdateTarget target_component_updatable = {
	.target_info = {
		.class = "component",
		.name = "updatable",
		.object_path = "/org/freedesktop/sysupdate1/target/component_updatable",
		.current_version = "t.0",
		.latest_version = "t.1",
	},
	.app_info = {
		.id = "systemd-sysupdate.component-updatable",
		.version = "t.1",
		.state = GS_APP_STATE_UPDATABLE,
		.kind = AS_COMPONENT_KIND_OPERATING_SYSTEM,
		.metadata_target = "updatable",
	},
};

static const UpdateTarget target_component_updatable_v2 = {
	.target_info = {
		.class = "component",
		.name = "updatable",
		.object_path = "/org/freedesktop/sysupdate1/target/component_updatable",
		.current_version = "t.0",
		.latest_version = "t.2",
	},
	.app_info = {
		.id = "systemd-sysupdate.component-updatable",
		.version = "t.2",
		.state = GS_APP_STATE_UPDATABLE,
		.kind = AS_COMPONENT_KIND_OPERATING_SYSTEM,
		.metadata_target = "updatable",
	},
};

/**
 * MockSysupdatedCallData:
 *
 * Holds data to be used by the interface method call function implementations.
 */
typedef struct {
	gint web_port;
	const UpdateTarget **targets;
	GMutex lock; /* used in `Target.Update()` to check if code-under-test starts to wait for signal JobRemoved() */
	GCond cond;
} MockSysupdatedCallData;

static void
mock_sysupdated_reply_method_call_manager_introspect (GDBusConnection       *connection,
                                                      const gchar           *sender,
                                                      const gchar           *object_path,
                                                      const gchar           *interface_name,
                                                      const gchar           *method_name,
                                                      GVariant              *parameters,
                                                      GDBusMethodInvocation *invocation,
                                                      gpointer               user_data)
{
	g_autoptr(GVariant) reply = NULL;

	reply = g_variant_new ("(s)", "<fake-xml-data>");
	g_dbus_method_invocation_return_value (invocation, g_steal_pointer (&reply));
}

static void
mock_sysupdated_reply_method_call_manager_list_targets (GDBusConnection       *connection,
                                                        const gchar           *sender,
                                                        const gchar           *object_path,
                                                        const gchar           *interface_name,
                                                        const gchar           *method_name,
                                                        GVariant              *parameters,
                                                        GDBusMethodInvocation *invocation,
                                                        gpointer               user_data)
{
	MockSysupdatedCallData *call_data = (MockSysupdatedCallData *) user_data;
	const UpdateTarget **targets = (const UpdateTarget **) call_data->targets;
	g_autoptr(GVariant) reply = NULL;
	GVariantBuilder builder;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sso)"));
	for (guint i = 0; targets[i] != NULL; i++) {
		g_variant_builder_add (&builder,
		                       "(sso)",
		                       targets[i]->target_info.class,
		                       targets[i]->target_info.name,
		                       targets[i]->target_info.object_path);
	}
	reply = g_variant_new ("(@a(sso))",
	                       g_variant_builder_end (&builder)); /* Also clears the builder up. */
	g_dbus_method_invocation_return_value (invocation, g_steal_pointer (&reply));
}

static void
mock_sysupdated_reply_method_call_target_properties_get_all (GDBusConnection       *connection,
                                                             const gchar           *sender,
                                                             const gchar           *object_path,
                                                             const gchar           *interface_name,
                                                             const gchar           *method_name,
                                                             GVariant              *parameters,
                                                             GDBusMethodInvocation *invocation,
                                                             gpointer               user_data)
{
	MockSysupdatedCallData *call_data = (MockSysupdatedCallData *) user_data;
	const UpdateTarget **targets = (const UpdateTarget **) call_data->targets;

	for (guint i = 0; targets[i] != NULL; i++) {
		if (g_strcmp0 (targets[i]->target_info.object_path, object_path) == 0) {
			g_autoptr(GVariant) reply = NULL;
			const gchar *interface = NULL;

			g_assert_true (g_str_has_prefix (object_path, "/org/freedesktop/sysupdate1/target/"));

			g_variant_get (parameters, "(&s)", &interface);
			g_assert_true (g_str_equal (interface, "org.freedesktop.sysupdate1.Target") ||
			               g_str_equal (interface, "org.freedesktop.DBus.Properties"));

			reply = g_variant_new_parsed ("({'Version': <%s>},)",
			                              targets[i]->target_info.current_version);
			g_dbus_method_invocation_return_value (invocation, g_steal_pointer (&reply));
			return;
		}
	}

	if (g_strcmp0 ("/org/freedesktop/sysupdate1/job/_2", object_path) == 0) {
		g_autoptr(GVariant) reply = NULL;
		const gchar *interface = NULL;

		g_variant_get (parameters, "(&s)", &interface);
		g_assert_cmpstr (interface, ==, "org.freedesktop.sysupdate1.Job");

		reply = g_variant_new_parsed ("({'': <%s>},)", "");
		g_dbus_method_invocation_return_value (invocation, g_steal_pointer (&reply));
		return;
	}

	g_debug ("unexpected object_path = `%s`", object_path);
	g_assert_not_reached ();
};

static void
mock_sysupdated_reply_method_call_target_check_new (GDBusConnection       *connection,
                                                    const gchar           *sender,
                                                    const gchar           *object_path,
                                                    const gchar           *interface_name,
                                                    const gchar           *method_name,
                                                    GVariant              *parameters,
                                                    GDBusMethodInvocation *invocation,
                                                    gpointer               user_data)
{
	MockSysupdatedCallData *call_data = (MockSysupdatedCallData *) user_data;
	const UpdateTarget **targets = (const UpdateTarget **) call_data->targets;

	for (guint i = 0; targets[i] != NULL; i++) {
		if (g_strcmp0 (targets[i]->target_info.object_path, object_path) == 0) {
			g_autoptr(GVariant) reply = NULL;

			reply = g_variant_new ("(s)",
			                       targets[i]->target_info.latest_version);
			g_dbus_method_invocation_return_value (invocation, g_steal_pointer (&reply));
			return;
		}
	}

	g_debug ("unexpected object_path = `%s`", object_path);
	g_assert_not_reached ();
}

static void
mock_sysupdated_reply_method_call_target_describe (GDBusConnection       *connection,
                                                   const gchar           *sender,
                                                   const gchar           *object_path,
                                                   const gchar           *interface_name,
                                                   const gchar           *method_name,
                                                   GVariant              *parameters,
                                                   GDBusMethodInvocation *invocation,
                                                   gpointer               user_data)
{
	MockSysupdatedCallData *call_data = (MockSysupdatedCallData *) user_data;
	const UpdateTarget **targets = (const UpdateTarget **) call_data->targets;

	for (guint i = 0; targets[i] != NULL; i++) {
		if (g_strcmp0 (targets[i]->target_info.object_path, object_path) == 0) {
			g_autoptr(GVariant) reply = NULL;
			const gchar *version = NULL;
			gboolean offline = FALSE;
			gboolean is_latest = FALSE;
			g_autofree gchar *json = NULL;

			g_variant_get (parameters, "(&sb)", &version, &offline);
			g_assert_cmpstr (version, ==, targets[i]->app_info.version);
			g_assert_false (offline);

			is_latest = g_strcmp0 (version, targets[i]->target_info.latest_version) == 0;
			json = g_strdup_printf ("{\"version\":\"%s\",\"newest\":%s,\"available\":%s,\"installed\":%s,\"obsolete\":%s,\"protected\":false,\"changelog_urls\":[],\"contents\":[]}",
			                        version,
			                        is_latest ? "true" : "false",
			                        targets[i]->app_info.state == GS_APP_STATE_AVAILABLE ? "true" : "false",
			                        targets[i]->app_info.state == GS_APP_STATE_INSTALLED ? "true" : "false",
			                        !is_latest ? "true" : "false");

			reply = g_variant_new ("(s)", json);
			g_dbus_method_invocation_return_value (invocation, g_steal_pointer (&reply));
			return;
		}
	}

	g_debug ("unexpected object_path = `%s`", object_path);
	g_assert_not_reached ();
}

static void
mock_sysupdated_reply_method_call_target_get_app_stream (GDBusConnection       *connection,
                                                         const gchar           *sender,
                                                         const gchar           *object_path,
                                                         const gchar           *interface_name,
                                                         const gchar           *method_name,
                                                         GVariant              *parameters,
                                                         GDBusMethodInvocation *invocation,
                                                         gpointer               user_data)
{
	MockSysupdatedCallData *call_data = (MockSysupdatedCallData *) user_data;
	const UpdateTarget **targets = (const UpdateTarget **) call_data->targets;

	for (guint i = 0; targets[i] != NULL; i++) {
		if (g_strcmp0 (targets[i]->target_info.object_path, object_path) == 0) {
			GVariantBuilder builder;
			g_autoptr(GVariant) reply = NULL;
			g_autofree gchar *appstream_url = NULL;

			g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
			appstream_url = g_strdup_printf ("http://localhost:%d/%s.metainfo.xml", call_data->web_port, targets[i]->app_info.id);
			g_variant_builder_add (&builder, "s", appstream_url);
			reply = g_variant_new ("(as)", &builder);
			g_dbus_method_invocation_return_value (invocation, g_steal_pointer (&reply));
			return;
		}
	}

	g_debug ("unexpected object_path = `%s`", object_path);
	g_assert_not_reached ();
}

static void
mock_sysupdated_reply_method_call_target_get_version (GDBusConnection       *connection,
                                                      const gchar           *sender,
                                                      const gchar           *object_path,
                                                      const gchar           *interface_name,
                                                      const gchar           *method_name,
                                                      GVariant              *parameters,
                                                      GDBusMethodInvocation *invocation,
                                                      gpointer               user_data)
{
	MockSysupdatedCallData *call_data = (MockSysupdatedCallData *) user_data;
	const UpdateTarget **targets = (const UpdateTarget **) call_data->targets;

	for (guint i = 0; targets[i] != NULL; i++) {
		if (g_strcmp0 (targets[i]->target_info.object_path, object_path) == 0) {
			g_autoptr(GVariant) reply = NULL;

			reply = g_variant_new ("(s)",
			                       targets[i]->target_info.current_version);
			g_dbus_method_invocation_return_value (invocation, g_steal_pointer (&reply));
			return;
		}
	}

	g_debug ("unexpected object_path = `%s`", object_path);
	g_assert_not_reached ();
}

static void
mock_sysupdated_reply_method_call_target_update (GDBusConnection       *connection,
                                                 const gchar           *sender,
                                                 const gchar           *object_path,
                                                 const gchar           *interface_name,
                                                 const gchar           *method_name,
                                                 GVariant              *parameters,
                                                 GDBusMethodInvocation *invocation,
                                                 gpointer               user_data)
{
	MockSysupdatedCallData *call_data = (MockSysupdatedCallData *) user_data;
	const UpdateTarget **targets = (const UpdateTarget **) call_data->targets;

	for (guint i = 0; targets[i] != NULL; i++) {
		if (g_strcmp0 (targets[i]->target_info.object_path, object_path) == 0) {
			g_autoptr(GVariant) reply = NULL;
			const gchar *version = NULL;
			guint64 flags = 0;
			G_MUTEX_AUTO_LOCK (&call_data->lock, locker);

			g_variant_get (parameters, "(&st)", &version, &flags);
			g_assert_cmpstr (version, ==, ""); /* always update to the latest version for now */
			g_assert_cmpuint (flags, ==, 0); /* no flags are defined yet */

			reply = g_variant_new ("(sto)",
			                       targets[i]->target_info.latest_version,
			                       2,
			                       "/org/freedesktop/sysupdate1/job/_2");
			g_dbus_method_invocation_return_value (invocation, g_steal_pointer (&reply));

			/* signal the test code that it has already replyed to
			 * the method_call `Target.Update()`, which means plugin
			 * should now start to wait for the signal
			 * `JobRemoved()` */
			g_cond_signal (&call_data->cond);
			return;
		}
	}

	g_debug ("unexpected object_path = `%s`", object_path);
	g_assert_not_reached ();
}

static void
mock_sysupdated_reply_method_call_job_cancel (GDBusConnection       *connection,
                                              const gchar           *sender,
                                              const gchar           *object_path,
                                              const gchar           *interface_name,
                                              const gchar           *method_name,
                                              GVariant              *parameters,
                                              GDBusMethodInvocation *invocation,
                                              gpointer               user_data)
{
	MockSysupdatedCallData *call_data = (MockSysupdatedCallData *) user_data;
	G_MUTEX_AUTO_LOCK (&call_data->lock, locker);

	/* no parameters */
	g_dbus_method_invocation_return_value (invocation, NULL);

	/* signal test code that cancel has been replied and it can move
	 * on to emit signal JobRemoved() */
	g_cond_signal (&call_data->cond);
}

static void
mock_sysupdated_server_method_call (GDBusConnection       *connection,
                                    const gchar           *sender,
                                    const gchar           *object_path,
                                    const gchar           *interface_name,
                                    const gchar           *method_name,
                                    GVariant              *parameters,
                                    GDBusMethodInvocation *invocation,
                                    gpointer               user_data)
{
	GDBusInterfaceMethodCallFunc handle_method_call_reply = NULL;

	if (g_strcmp0 (interface_name, "org.freedesktop.DBus.Introspectable") == 0) {
		if (g_strcmp0 (method_name, "Introspect") == 0) {
			handle_method_call_reply = mock_sysupdated_reply_method_call_manager_introspect;
		}
	} else if (g_strcmp0 (interface_name, "org.freedesktop.DBus.Properties") == 0) {
		if (g_strcmp0 (method_name, "GetAll") == 0) {
			handle_method_call_reply = mock_sysupdated_reply_method_call_target_properties_get_all;
		}
	} else if (g_strcmp0 (interface_name, "org.freedesktop.sysupdate1.Manager") == 0) {
		if (g_strcmp0 (method_name, "ListTargets") == 0) {
			handle_method_call_reply = mock_sysupdated_reply_method_call_manager_list_targets;
		}
	} else if (g_strcmp0 (interface_name, "org.freedesktop.sysupdate1.Target") == 0) {
		if (g_strcmp0 (method_name, "CheckNew") == 0) {
			handle_method_call_reply = mock_sysupdated_reply_method_call_target_check_new;
		}
		else if (g_strcmp0 (method_name, "Describe") == 0) {
			handle_method_call_reply = mock_sysupdated_reply_method_call_target_describe;
		}
		else if (g_strcmp0 (method_name, "GetAppStream") == 0) {
			handle_method_call_reply = mock_sysupdated_reply_method_call_target_get_app_stream;
		}
		else if (g_strcmp0 (method_name, "GetVersion") == 0) {
			handle_method_call_reply = mock_sysupdated_reply_method_call_target_get_version;
		}
		else if (g_strcmp0 (method_name, "Update") == 0) {
			handle_method_call_reply = mock_sysupdated_reply_method_call_target_update;
		}
	} else if (g_strcmp0 (interface_name, "org.freedesktop.sysupdate1.Job") == 0) {
		if (g_strcmp0 (method_name, "Cancel") == 0) {
			handle_method_call_reply = mock_sysupdated_reply_method_call_job_cancel;
		}
	}

	if (handle_method_call_reply == NULL) {
		g_debug ("mock systemd-sysupdated service does not implement reply to `%s.%s()`",
		         interface_name,
		         method_name);
		g_assert_not_reached ();
	}

	handle_method_call_reply (connection,
	                          sender,
	                          object_path,
	                          interface_name,
	                          method_name,
	                          parameters,
	                          invocation,
	                          user_data);
}

static GVariant *
mock_sysupdated_server_get_property (GDBusConnection  *connection,
                                     const gchar      *sender,
                                     const gchar      *object_path,
                                     const gchar      *interface_name,
                                     const gchar      *property_name,
                                     GError          **error,
                                     gpointer          user_data)
{
	if (g_strcmp0 (interface_name, "org.freedesktop.sysupdate1.Job") == 0) {
		if (g_strcmp0 (property_name, "Id") == 0) {
			return g_variant_new ("t", 0);
		} else if (g_strcmp0 (property_name, "Type") == 0) {
			return g_variant_new ("s", "");
		} else if (g_strcmp0 (property_name, "Offline") == 0) {
			return g_variant_new ("b", FALSE);
		} else if (g_strcmp0 (property_name, "Progress") == 0) {
			return g_variant_new ("u", 0);
		}
	}

	g_debug ("mock systemd-sysupdated service does not implement getting property `%s.%s()`",
	         interface_name,
	         property_name);
	g_assert_not_reached ();
}

static gboolean
mock_sysupdated_server_set_property (GDBusConnection  *connection,
                                     const gchar      *sender,
                                     const gchar      *object_path,
                                     const gchar      *interface_name,
                                     const gchar      *property_name,
                                     GVariant         *value,
                                     GError          **error,
                                     gpointer          user_data)
{
	g_debug ("mock systemd-sysupdated service does not implement setting property `%s.%s()`",
	         interface_name,
	         property_name);
	g_assert_not_reached ();
}

static const GDBusInterfaceVTable mock_sysupdated_server_vtable =
{
  .method_call = mock_sysupdated_server_method_call,
  .get_property = mock_sysupdated_server_get_property,
  .set_property = mock_sysupdated_server_set_property,
};

/**
 * MockSysupdatedHandle:
 *
 * A handle to manipulate the mockup-up systemd sysupdate service.
 */
typedef struct {
	GDBusConnection *connection;
	GMainContext *context;
} MockSysupdatedHandle;

/**
 * MockSysupdatedService:
 *
 * The mocked-up systemd-sysupdate D-Bus service.
 */
typedef struct {
	SoupServer *web;
	gint web_port;
	MockSysupdatedHandle handle;
	GTestDBus *bus;
	guint owner_id;
	guint registration_id;
	GsThreadedRunner runner;
} MockSysupdatedService;

/**
 * TestData:
 *
 * Data passed to the tests.
 */
typedef struct {
	MockSysupdatedHandle handle;
	gint web_port;
	/* can only load once per process */
	GsPluginLoader *plugin_loader;
} TestData;

/**
 * EmitSignalData:
 *
 * Holds data to pass to a g_dbus_connection_emit_signal() call.
 */
typedef struct {
	GDBusConnection *connection;
	const gchar *sender;
	const gchar *object_path;
	const gchar *interface_name;
	const gchar *signal_name;
	GVariant *parameters;

	GMutex lock;
	GCond cond;
} EmitSignalData;

static gboolean
emit_signal_cb (gpointer user_data)
{
	EmitSignalData *data = (EmitSignalData *) user_data;
	g_autoptr(GError) error = NULL;
	G_MUTEX_AUTO_LOCK (&data->lock, locker);

	g_dbus_connection_emit_signal (data->connection,
	                               data->sender,
	                               data->object_path,
	                               data->interface_name,
	                               data->signal_name,
	                               g_steal_pointer (&data->parameters),
	                               &error);
	g_assert_no_error (error);

	g_dbus_connection_flush_sync (data->connection, NULL, &error);
	g_assert_no_error (error);

	g_cond_signal (&data->cond);

	return G_SOURCE_REMOVE;
}

/* Append an event to the server's context to emit the signal, and wait for the
 * server's thread to emit it. */
static void
mock_sysupdated_emit_signal_job_removed (MockSysupdatedHandle *handle,
                                         gint                  job_status)
{
	EmitSignalData data = {
		.connection = handle->connection,
		.sender = "org.freedesktop.sysupdate1",
		.object_path = "/org/freedesktop/sysupdate1",
		.interface_name = "org.freedesktop.sysupdate1.Manager",
		.signal_name = "JobRemoved",
		/* The D-Bus message will take ownership of the floating reference. */
		.parameters = g_variant_new ("(toi)", 2, "/org/freedesktop/sysupdate1/job/_2", job_status),
	};
	GS_MUTEX_AUTO_GUARD (&data.lock, lock);
	GS_COND_AUTO_GUARD (&data.cond, cond);
	G_MUTEX_AUTO_LOCK (&data.lock, locker);

	gs_test_flush_main_context ();

	g_main_context_invoke (handle->context, emit_signal_cb, &data);
	g_cond_wait (&data.cond, &data.lock);

	/* this is a workaround for we want to wait until the signal
	 * emitted has been dispatched and is received by the plugin.
	 * we are using the main context here due to currently the
	 * signal subscriptions are done in the `setup()` and was run on
	 * the main context in the test `main()`. */
	g_main_context_iteration (NULL, TRUE);
}

/* Append an event to the server's context to emit the signal, and wait for the
 * server's thread to emit it. */
static void
mock_sysupdated_emit_signal_properties_changed (MockSysupdatedHandle *handle,
                                                guint                 progress_percentage)
{
	EmitSignalData data = {
		.connection = handle->connection,
		.sender = "org.freedesktop.sysupdate1",
		.object_path = "/org/freedesktop/sysupdate1/job/_2",
		.interface_name = "org.freedesktop.DBus.Properties",
		.signal_name = "PropertiesChanged",
		.parameters = NULL,
	};
	const gchar *invalidated_properties[] = {NULL};
	GVariantBuilder builder;
	GS_MUTEX_AUTO_GUARD (&data.lock, lock);
	GS_COND_AUTO_GUARD (&data.cond, cond);
	G_MUTEX_AUTO_LOCK (&data.lock, locker);

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (&builder,
	                       "{sv}",
	                       "Progress",
	                       g_variant_new_uint32 (progress_percentage));
	/* The D-Bus message will take ownership of the floating reference. */
	data.parameters = g_variant_new ("(s@a{sv}@as)",
	                                 "org.freedesktop.sysupdate1.Job",
	                                 g_variant_builder_end (&builder), /* Also clears the builder up. */
	                                 g_variant_new_strv (invalidated_properties, -1)),

	gs_test_flush_main_context ();

	g_main_context_invoke (handle->context, emit_signal_cb, &data);
	g_cond_wait (&data.cond, &data.lock);

	/* the same as the `mock_sysupdated_emit_signal_job_removed()` */
	g_main_context_iteration (NULL, TRUE);
}

/**
 * MockSysupdatedRegistrar:
 *
 * Holds a register of D-Bus objects.
 */
typedef struct {
	MockSysupdatedHandle handle;
	GSList *ids;
	MockSysupdatedCallData call_data;
} MockSysupdatedRegistrar;

/**
 * RegisterObjectData:
 *
 * Holds data to pass to a g_dbus_connection_register_object() call.
 */
typedef struct {
	GDBusConnection *connection;
	const gchar *object_path;
	GDBusInterfaceInfo *interface_info;
	gpointer user_data;
	guint registration_id;

	GMutex lock;
	GCond cond;
} RegisterObjectData;

static gboolean
mock_sysupdated_registrar_register_object_cb (gpointer user_data)
{
	RegisterObjectData *data = (RegisterObjectData *) user_data;
	g_autoptr(GError) error = NULL;
	G_MUTEX_AUTO_LOCK (&data->lock, locker);

	data->registration_id = g_dbus_connection_register_object (data->connection,
	                                                           data->object_path,
	                                                           data->interface_info,
	                                                           &mock_sysupdated_server_vtable,
	                                                           data->user_data,
	                                                           NULL,
	                                                           &error);
	g_assert_no_error (error);
	g_assert (data->registration_id > 0);
	g_cond_signal (&data->cond);

	return G_SOURCE_REMOVE;
}

static void
mock_sysupdated_registrar_register_object (MockSysupdatedRegistrar *registrar,
                                           const gchar             *object_path,
                                           GDBusInterfaceInfo      *interface_info,
                                           gpointer                 user_data)
{
	RegisterObjectData data = {
		.connection = registrar->handle.connection,
		.object_path = object_path,
		.interface_info = interface_info,
		.user_data = user_data,
		.registration_id = 0,
	};
	GS_MUTEX_AUTO_GUARD (&data.lock, lock);
	GS_COND_AUTO_GUARD (&data.cond, cond);
	G_MUTEX_AUTO_LOCK (&data.lock, locker);

	g_main_context_invoke (registrar->handle.context,
	                       mock_sysupdated_registrar_register_object_cb,
	                       &data);
	g_cond_wait (&data.cond, &data.lock);

	registrar->ids = g_slist_append (registrar->ids,
	                                 GUINT_TO_POINTER (data.registration_id));
}

/**
 * UnregisterObjectData:
 *
 * Holds data to pass to a g_dbus_connection_unregister_object() call.
 */
typedef struct {
	GDBusConnection *connection;
	guint registration_id;

	GMutex lock;
	GCond cond;
} UnregisterObjectData;

static gboolean
mock_sysupdated_registrar_unregister_object_cb (gpointer user_data)
{
	UnregisterObjectData *data = (UnregisterObjectData *) user_data;
	G_MUTEX_AUTO_LOCK (&data->lock, locker);

	g_dbus_connection_unregister_object (data->connection,
	                                     data->registration_id);
	g_cond_signal (&data->cond);

	return G_SOURCE_REMOVE;
}

static void
mock_sysupdated_registrar_unregister_object (MockSysupdatedRegistrar *registrar,
                                             guint                    registration_id)
{
	UnregisterObjectData data = {
		.connection = registrar->handle.connection,
		.registration_id = registration_id,
	};
	GS_MUTEX_AUTO_GUARD (&data.lock, lock);
	GS_COND_AUTO_GUARD (&data.cond, cond);
	G_MUTEX_AUTO_LOCK (&data.lock, locker);

	g_main_context_invoke (registrar->handle.context,
	                       mock_sysupdated_registrar_unregister_object_cb,
	                       &data);
	g_cond_wait (&data.cond, &data.lock);

	registrar->ids = g_slist_remove (registrar->ids,
	                                 GUINT_TO_POINTER (registration_id));
}

static void
mock_sysupdated_registrar_init (MockSysupdatedRegistrar  *registrar,
                                gint                      web_port,
                                MockSysupdatedHandle     *handle,
                                const UpdateTarget      **targets)
{
	/* Configure mock `systemd-sysupdated` server's reply based on
	 * the given `user_data` */

	registrar->call_data.web_port = web_port;
	registrar->call_data.targets = targets;

	g_mutex_init (&registrar->call_data.lock);
	g_cond_init (&registrar->call_data.cond);

	g_set_object (&registrar->handle.connection, handle->connection);
	registrar->handle.context = g_main_context_ref (handle->context);

	/* since the server thread already started running on a
	 * different context, we now need to invoke the object
	 * registration on the thread context */

	/* register manager object */
	{
		/* org.freedesktop.sysupdate1.Manager */
		mock_sysupdated_registrar_register_object (registrar,
		                                           "/org/freedesktop/sysupdate1",
		                                           gs_systemd_sysupdate_manager_interface_info (),
		                                           &registrar->call_data);
	}

	/* register target objects */
	for (guint i = 0; targets[i] != NULL; i++) {
		/* org.freedesktop.DBus.Properties */
		mock_sysupdated_registrar_register_object (registrar,
		                                           targets[i]->target_info.object_path,
		                                           gs_systemd_sysupdate_org_freedesktop_dbus_properties_interface_info (),
		                                           &registrar->call_data);

		/* org.freedesktop.sysupdate1.Target */
		mock_sysupdated_registrar_register_object (registrar,
		                                           targets[i]->target_info.object_path,
		                                           gs_systemd_sysupdate_target_interface_info (),
		                                           &registrar->call_data);
	}

	/* register job objects. here we use the same job ID hard-coded
	 * everywhere in this file */
	{
		/* org.freedesktop.sysupdate1.Job */
		mock_sysupdated_registrar_register_object (registrar,
		                                           "/org/freedesktop/sysupdate1/job/_2",
		                                           gs_systemd_sysupdate_job_interface_info (),
		                                           &registrar->call_data);
	}
}

static void
mock_sysupdated_registrar_clear (MockSysupdatedRegistrar *registrar)
{
	/* clean-up all objects registered to the test bus */
	while (registrar->ids != NULL) {
		mock_sysupdated_registrar_unregister_object (registrar,
		                                             GPOINTER_TO_UINT (registrar->ids->data));
	}
	g_clear_pointer (&registrar->ids, g_slist_free);
	g_clear_object (&registrar->handle.connection);
	g_clear_pointer (&registrar->handle.context, g_main_context_unref);

	g_cond_clear (&registrar->call_data.cond);
	g_mutex_clear (&registrar->call_data.lock);
}

#define MOCK_SYSUPDATED_REGISTRAR_INIT {0}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(MockSysupdatedRegistrar, mock_sysupdated_registrar_clear)

static void
mock_sysupdated_service_init (MockSysupdatedService *service)
{
	service->handle.context = g_main_context_new ();

	g_main_context_push_thread_default (service->handle.context);
	{
		g_autofree gchar *relative = NULL;
		g_autofree gchar *servicesdir = NULL;
		g_autoptr(GError) error = NULL;
		GSList *uris;

		/* Create the test web service. */
		service->web = soup_server_new (NULL, NULL);
		g_assert_nonnull (service->web);

		/* Connect on HTTP. */
		soup_server_listen_local (service->web, 0, 0, &error);
		g_assert_no_error (error);

		/* Get the allocated port. */
		uris = soup_server_get_uris (service->web);
		g_assert_nonnull (uris);
		g_assert_nonnull (uris->data);

		service->web_port = g_uri_get_port (uris->data);
		g_assert_cmpint (service->web_port, !=, -1);

		g_slist_free_full (uris, (GDestroyNotify) g_uri_unref);

		soup_server_add_handler (service->web, NULL, mock_web_handler_cb, NULL, NULL);

		/* Create the global dbus-daemon for this test suite. */
		service->bus = g_test_dbus_new (G_TEST_DBUS_NONE);

		/* Add the private directory with our in-tree service files. */
		relative = g_test_build_filename (G_TEST_BUILT, "services", NULL);
		servicesdir = g_canonicalize_filename (relative, NULL);
		g_test_dbus_add_service_dir (service->bus, servicesdir);

		/* Start the private D-Bus daemon. */
		g_test_dbus_up (service->bus);

		/* create bus connection */
		service->handle.connection = g_dbus_connection_new_for_address_sync (g_test_dbus_get_bus_address (service->bus),
		                                                                     G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
		                                                                     G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
		                                                                     NULL, NULL, &error);
		g_assert_no_error (error);

		/* we need at least the manager to reply to the plugin's
		* self-disable query in the constructor */
		service->owner_id = g_bus_own_name_on_connection (service->handle.connection,
		                                                  "org.freedesktop.sysupdate1",
		                                                  G_BUS_NAME_OWNER_FLAGS_NONE,
		                                                  NULL, NULL, NULL, NULL);
		service->registration_id = g_dbus_connection_register_object (service->handle.connection,
		                                                              "/org/freedesktop/sysupdate1",
		                                                              gs_systemd_sysupdate_org_freedesktop_dbus_introspectable_interface_info (),
		                                                              &mock_sysupdated_server_vtable,
		                                                              NULL, NULL, &error);
		g_assert_no_error (error);
	}
	g_main_context_pop_thread_default (service->handle.context);

	gs_threaded_runner_init (&service->runner,
	                         "mock systemd-sysupdated service",
	                         service->handle.context);
}

static void
mock_sysupdated_service_clear (MockSysupdatedService *service)
{
	gs_threaded_runner_clear (&service->runner);

	g_main_context_push_thread_default (service->handle.context);
	{
		/* clean-up bus connection */
		g_dbus_connection_unregister_object (service->handle.connection,
		                                     service->registration_id);
		g_bus_unown_name (service->owner_id);
		if (service->handle.connection != NULL) {
			g_dbus_connection_close_sync (service->handle.connection, NULL, NULL);
		}

		/* stop test D-Bus daemon */
		g_test_dbus_down (service->bus);
		g_clear_pointer (&service->bus, g_object_unref);

		/* stop test web server */
		g_clear_pointer (&service->web, g_object_unref);
	}
	g_main_context_pop_thread_default (service->handle.context);
	g_clear_pointer (&service->handle.context, g_main_context_unref);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(MockSysupdatedService, mock_sysupdated_service_clear)

static gint
compare_apps_by_name (GsApp *app1, GsApp *app2, gpointer user_data)
{
	/* Negative value if a < b; zero if a = b; positive value if a > b. */
	return g_ascii_strcasecmp (gs_app_get_name (app1),
	                           gs_app_get_name (app2));
}

static void
invoke_plugin_loader_refresh_metadata_assert_no_error (GsPluginLoader *plugin_loader)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GError) error = NULL;
	gboolean ret;

	plugin_job = gs_plugin_job_refresh_metadata_new (0, /* always refresh */
	                                                 GS_PLUGIN_REFRESH_METADATA_FLAGS_NONE);
	ret = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();

	g_assert_no_error (error);
	g_assert_true (ret);
}

static GsAppList *
invoke_plugin_loader_list_upgrades_assert_no_error (GsPluginLoader *plugin_loader)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	GsAppList *list;
	g_autoptr(GError) error = NULL;

	plugin_job = gs_plugin_job_list_distro_upgrades_new (GS_PLUGIN_LIST_DISTRO_UPGRADES_FLAGS_NONE,
	                                                     GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE);
	gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	list = gs_plugin_job_list_distro_upgrades_get_result_list (GS_PLUGIN_JOB_LIST_DISTRO_UPGRADES (plugin_job));
	gs_test_flush_main_context ();

	g_assert_no_error (error);
	g_assert_nonnull (list);

	gs_app_list_sort (list, (GsAppListSortFunc) compare_apps_by_name, NULL);
	return g_steal_pointer (&list);
}

static GsAppList *
invoke_plugin_loader_list_apps_for_update_assert_no_error (GsPluginLoader *plugin_loader)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GsAppQuery) query = NULL;
	GsAppList *list;
	g_autoptr(GError) error = NULL;

	query = gs_app_query_new ("is-for-update", GS_APP_QUERY_TRISTATE_TRUE,
	                          "refine-flags", GS_PLUGIN_REFINE_FLAGS_NONE,
	                          NULL);
	plugin_job = gs_plugin_job_list_apps_new (query, GS_PLUGIN_LIST_APPS_FLAGS_NONE);
	gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
	gs_test_flush_main_context ();

	g_assert_no_error (error);
	g_assert_nonnull (list);

	gs_app_list_sort (list, (GsAppListSortFunc) compare_apps_by_name, NULL);
	return g_steal_pointer (&list);
}

/**
 * RunPluginJobActionData:
 *
 * Holds data to pass to a gs_plugin_loader_job_process() call.
 */
typedef struct {
	GsPluginLoader *plugin_loader;
	GsPluginJob *plugin_job;
	GCancellable *cancellable;
	GError *error;
	gboolean ret;

	GThread *plugin_thread;
} RunPluginJobActionData;

static gpointer
run_plugin_job_action_thread_cb (gpointer user_data)
{
	RunPluginJobActionData *data = (RunPluginJobActionData *) user_data;

	data->ret = gs_plugin_loader_job_process (data->plugin_loader,
	                                          data->plugin_job,
	                                          data->cancellable,
	                                          &data->error);

	return NULL;
}

static void
invoke_plugin_loader_upgrade_trigger_end_assert_no_error (RunPluginJobActionData *data)
{
	g_clear_pointer (&data->plugin_thread, g_thread_join);

	g_assert_no_error (data->error);
	g_assert_true (data->ret);

	g_clear_pointer (&data->plugin_job, g_object_unref);
	g_slice_free (RunPluginJobActionData, data);
}

static void
invoke_plugin_loader_upgrade_trigger_end_assert_error (RunPluginJobActionData *data,
                                                       GQuark                  domain,
                                                       gint                    code)
{
	g_clear_pointer (&data->plugin_thread, g_thread_join);

	g_assert_error (data->error, domain, code);
	g_assert_false (data->ret);

	g_clear_error (&data->error);
	g_clear_pointer (&data->plugin_job, g_object_unref);
	g_slice_free (RunPluginJobActionData, data);
}

static RunPluginJobActionData *
invoke_plugin_loader_update_apps_begin (GsPluginLoader *plugin_loader,
                                        GsAppList      *list_updates)
{
	RunPluginJobActionData *data = g_slice_new (RunPluginJobActionData);

	data->plugin_loader = plugin_loader;
	data->plugin_job = gs_plugin_job_update_apps_new (list_updates,
	                                                  GS_PLUGIN_UPDATE_APPS_FLAGS_NONE);
	data->cancellable = g_cancellable_new ();
	data->error = NULL;
	data->ret = FALSE;

	data->plugin_thread = g_thread_new ("invoke-plugin-loader-update-apps-background",
	                                    (GThreadFunc) run_plugin_job_action_thread_cb,
	                                    data);
	return g_steal_pointer (&data);
}

static void
invoke_plugin_loader_update_apps_end_assert_no_error (RunPluginJobActionData *data)
{
	invoke_plugin_loader_upgrade_trigger_end_assert_no_error (data);
}

static void
invoke_plugin_loader_update_apps_end_assert_error (RunPluginJobActionData *data,
                                                   GQuark                  domain,
                                                   gint                    code)
{
	invoke_plugin_loader_upgrade_trigger_end_assert_error (data, domain, code);
}

/* Checks that the plugin is enabled. If it isn't, it could be because the
 * org.freedesktop.sysupdate1 D-Bus service isn't found. Given we mock it up for
 * these tests, not finding it is a bug. */
static void
gs_plugin_systemd_sysupdate_plugin_enabled_func (TestData *test_data)
{
	GsPluginLoader *plugin_loader = test_data->plugin_loader;

	g_assert_true (gs_plugin_loader_get_enabled (plugin_loader, "systemd-sysupdate"));
}

/* Checks that the plugin doesn't do distro upgrades, as for the moment it only
 * handles updates, including for the host target. */
static void
gs_plugin_systemd_sysupdate_distro_upgrade_func (TestData *test_data)
{
	GsPluginLoader *plugin_loader = test_data->plugin_loader;
	const UpdateTarget *targets[] = {
		&target_host,
		NULL
	};
	g_auto(MockSysupdatedRegistrar) registrar = MOCK_SYSUPDATED_REGISTRAR_INIT;

	if (!gs_plugin_loader_get_enabled (plugin_loader, "systemd-sysupdate")) {
		g_test_skip ("not enabled");
		return;
	}

	mock_sysupdated_registrar_init (&registrar, test_data->web_port, &test_data->handle, targets);
	{
		g_autoptr(GsAppList) list_upgrades = NULL;

		invoke_plugin_loader_refresh_metadata_assert_no_error (plugin_loader);
		list_upgrades = invoke_plugin_loader_list_upgrades_assert_no_error (plugin_loader);
		g_assert_cmpint (gs_app_list_length (list_upgrades), ==, 0);
	}
}

/* Checks that the plugin can handle app updates. */
static void
gs_plugin_systemd_sysupdate_app_update_updatable_func (TestData *test_data)
{
	GsPluginLoader *plugin_loader = test_data->plugin_loader;
	const UpdateTarget *targets[] = {
		&target_component_available,
		&target_component_installed,
		&target_component_updatable,
		NULL
	};
	g_auto(MockSysupdatedRegistrar) registrar = MOCK_SYSUPDATED_REGISTRAR_INIT;

	if (!gs_plugin_loader_get_enabled (plugin_loader, "systemd-sysupdate")) {
		g_test_skip ("not enabled");
		return;
	}

	mock_sysupdated_registrar_init (&registrar, test_data->web_port, &test_data->handle, targets);
	{
		g_autoptr(GsAppList) list_updates = NULL;

		invoke_plugin_loader_refresh_metadata_assert_no_error (plugin_loader);
		list_updates = invoke_plugin_loader_list_apps_for_update_assert_no_error (plugin_loader);
		g_assert_cmpint (gs_app_list_length (list_updates), ==, 1);

		{
			RunPluginJobActionData *data = NULL;
			G_MUTEX_AUTO_LOCK (&registrar.call_data.lock, locker);

			data = invoke_plugin_loader_update_apps_begin (plugin_loader, list_updates);
			for (guint i = 0; i < gs_app_list_length (list_updates); i++) {
				/* Wait for the plugin thread to handle `Target.Update()`. */
				g_cond_wait (&registrar.call_data.cond, &registrar.call_data.lock);

				/* emit `job_status` = `0` as update success */
				mock_sysupdated_emit_signal_job_removed (&test_data->handle, 0);
			}
			invoke_plugin_loader_update_apps_end_assert_no_error (g_steal_pointer (&data));
		}

		/* app state changes on update succeed */
		for (guint i = 0; i < gs_app_list_length (list_updates); i++) {
			GsApp *app = gs_app_list_index (list_updates, i);
			g_assert_cmpint (gs_app_get_state (app), ==, GS_APP_STATE_INSTALLED);
		}
	}
}

/* Checks that the plugin reports the progress of app updates. */
static void
gs_plugin_systemd_sysupdate_app_update_trackable_func (TestData *test_data)
{
	GsPluginLoader *plugin_loader = test_data->plugin_loader;
	const UpdateTarget *targets[] = {
		&target_component_available,
		&target_component_installed,
		&target_component_updatable,
		NULL
	};
	g_auto(MockSysupdatedRegistrar) registrar = MOCK_SYSUPDATED_REGISTRAR_INIT;

	if (!gs_plugin_loader_get_enabled (plugin_loader, "systemd-sysupdate")) {
		g_test_skip ("not enabled");
		return;
	}

	/* use only one app update (component) here since the plugin
	 * does not control the app update order in the app list */
	mock_sysupdated_registrar_init (&registrar, test_data->web_port, &test_data->handle, targets);
	{
		g_autoptr(GsAppList) list_updates = NULL;
		GsApp *app = NULL;

		invoke_plugin_loader_refresh_metadata_assert_no_error (plugin_loader);
		list_updates = invoke_plugin_loader_list_apps_for_update_assert_no_error (plugin_loader);
		g_assert_cmpint (gs_app_list_length (list_updates), ==, 1);

		app = gs_app_list_index (list_updates, 0);
		{
			RunPluginJobActionData *data = NULL;
			G_MUTEX_AUTO_LOCK (&registrar.call_data.lock, locker);

			data = invoke_plugin_loader_update_apps_begin (plugin_loader, list_updates);
			/* Wait for the plugin thread to handle `Target.Update()`. */
			g_cond_wait (&registrar.call_data.cond, &registrar.call_data.lock);

			/* The mock server can only return the default value for
			 * properties, so we need to wait for the plugin to
			 * retrieve the default progress value before emitting
			 * its updated value. */
			while (gs_app_get_progress (app) == GS_APP_PROGRESS_UNKNOWN) {
				g_usleep (100);
			}

			/* Signal the update has progressed. */
			mock_sysupdated_emit_signal_properties_changed (&test_data->handle, 50);
			/* Wait for the plugin thread to handle the update. */
			while (gs_app_get_progress (app) != 50) {
				g_usleep (100);
			}
			g_assert_cmpint (gs_app_get_progress (app), ==, 50);

			/* emit job-removed to end the job */
			mock_sysupdated_emit_signal_job_removed (&test_data->handle, 0);

			invoke_plugin_loader_update_apps_end_assert_no_error (g_steal_pointer (&data));
		}

		/* app state changes on update succeed */
		g_assert_cmpint (gs_app_get_state (app), ==, GS_APP_STATE_INSTALLED);
	}
}

/* Checks that the plugin can recover an app's state when its update failed. */
static void
gs_plugin_systemd_sysupdate_app_update_recoverable_func (TestData *test_data)
{
	GsPluginLoader *plugin_loader = test_data->plugin_loader;
	const UpdateTarget *targets[] = {
		&target_component_available,
		&target_component_installed,
		&target_component_updatable,
		NULL
	};
	g_auto(MockSysupdatedRegistrar) registrar = MOCK_SYSUPDATED_REGISTRAR_INIT;

	if (!gs_plugin_loader_get_enabled (plugin_loader, "systemd-sysupdate")) {
		g_test_skip ("not enabled");
		return;
	}

	/* it might be just a choice, currently in the plugin, the
	 * update chain stops on any of the update failure happenes */
	mock_sysupdated_registrar_init (&registrar, test_data->web_port, &test_data->handle, targets);
	{
		g_autoptr(GsAppList) list_updates = NULL;

		invoke_plugin_loader_refresh_metadata_assert_no_error (plugin_loader);
		list_updates = invoke_plugin_loader_list_apps_for_update_assert_no_error (plugin_loader);
		g_assert_cmpint (gs_app_list_length (list_updates), ==, 1);

		{
			RunPluginJobActionData *data = NULL;
			G_MUTEX_AUTO_LOCK (&registrar.call_data.lock, locker);

			data = invoke_plugin_loader_update_apps_begin (plugin_loader, list_updates);
			/* Wait for the plugin thread to handle `Target.Update()`. */
			g_cond_wait (&registrar.call_data.cond, &registrar.call_data.lock);

			/* emit `job_status` = non-zero as update failure */
			mock_sysupdated_emit_signal_job_removed (&test_data->handle, -2);

			/* as the 1st job failed, the 2nd job will not run
			 * based on the plugin's current implementation */
			invoke_plugin_loader_update_apps_end_assert_no_error (g_steal_pointer (&data));
		}

		/* if the 2nd job is somehow triggered, this test case will
		 * fail because of the timeout. as a result, we only need to
		 * check both apps are not installed here */
		for (guint i = 0; i < gs_app_list_length (list_updates); i++) {
			GsApp *app = gs_app_list_index (list_updates, i);
			g_assert_cmpint (gs_app_get_state (app), !=, GS_APP_STATE_INSTALLED);
		}
	}
}

/* Checks that the plugin can cancel app updates. */
static void
gs_plugin_systemd_sysupdate_app_update_cancellable_func (TestData *test_data)
{
	GsPluginLoader *plugin_loader = test_data->plugin_loader;
	const UpdateTarget *targets[] = {
		&target_component_available,
		&target_component_installed,
		&target_component_updatable,
		NULL
	};
	g_auto(MockSysupdatedRegistrar) registrar = MOCK_SYSUPDATED_REGISTRAR_INIT;

	if (!gs_plugin_loader_get_enabled (plugin_loader, "systemd-sysupdate")) {
		g_test_skip ("not enabled");
		return;
	}

	mock_sysupdated_registrar_init (&registrar, test_data->web_port, &test_data->handle, targets);
	{
		g_autoptr(GsAppList) list_updates = NULL;

		invoke_plugin_loader_refresh_metadata_assert_no_error (plugin_loader);
		list_updates = invoke_plugin_loader_list_apps_for_update_assert_no_error (plugin_loader);
		g_assert_cmpint (gs_app_list_length (list_updates), ==, 1);

		{
			RunPluginJobActionData *data = NULL;
			G_MUTEX_AUTO_LOCK (&registrar.call_data.lock, locker);

			data = invoke_plugin_loader_update_apps_begin (plugin_loader, list_updates);
			/* Wait for the plugin thread to handle `Target.Update()`. */
			g_cond_wait (&registrar.call_data.cond, &registrar.call_data.lock);

			/* cancel the job, error should be set automatically */
			g_cancellable_cancel (data->cancellable);
			/* Wait for the plugin thread to handle `Job.Cancel()`. */
			g_cond_wait (&registrar.call_data.cond, &registrar.call_data.lock);

			/* emit `job_status` = -1 as what real service returns */
			mock_sysupdated_emit_signal_job_removed (&test_data->handle, -1);

			invoke_plugin_loader_update_apps_end_assert_error (g_steal_pointer (&data),
			                                                   G_IO_ERROR,
			                                                   G_IO_ERROR_CANCELLED);
		}

		for (guint i = 0; i < gs_app_list_length (list_updates); i++) {
			GsApp *app = gs_app_list_index (list_updates, i);
			g_assert_cmpint (gs_app_get_state (app), !=, GS_APP_STATE_INSTALLED);
		}
	}
}

/* Checks that the plugin can track a target's latest version by updating the
 * currently stored target and app. */
static void
gs_plugin_systemd_sysupdate_metadata_target_updatable_func (TestData *test_data)
{
	GsPluginLoader *plugin_loader = test_data->plugin_loader;
	const UpdateTarget *targets[] = {
		&target_component_updatable,
		NULL
	};

	if (!gs_plugin_loader_get_enabled (plugin_loader, "systemd-sysupdate")) {
		g_test_skip ("not enabled");
		return;
	}

	/* latest version = v1 */
	{
		g_auto(MockSysupdatedRegistrar) registrar = MOCK_SYSUPDATED_REGISTRAR_INIT;
		g_autoptr(GsAppList) list_updates = NULL;

		mock_sysupdated_registrar_init (&registrar, test_data->web_port, &test_data->handle, targets);
		invoke_plugin_loader_refresh_metadata_assert_no_error (plugin_loader);
		list_updates = invoke_plugin_loader_list_apps_for_update_assert_no_error (plugin_loader);
		g_assert_cmpint (gs_app_list_length (list_updates), ==, 1);

		g_assert_cmpstr (gs_app_get_version (gs_app_list_index (list_updates, 0)),
		                 ==, "t.1");
	}

	/* latest version = v2 */
	targets[0] = &target_component_updatable_v2;
	targets[1] = NULL;
	{
		g_auto(MockSysupdatedRegistrar) registrar = MOCK_SYSUPDATED_REGISTRAR_INIT;
		g_autoptr(GsAppList) list_updates = NULL;

		mock_sysupdated_registrar_init (&registrar, test_data->web_port, &test_data->handle, targets);
		invoke_plugin_loader_refresh_metadata_assert_no_error (plugin_loader);
		list_updates = invoke_plugin_loader_list_apps_for_update_assert_no_error (plugin_loader);
		g_assert_cmpint (gs_app_list_length (list_updates), ==, 1);

		g_assert_cmpstr (gs_app_get_version (gs_app_list_index (list_updates, 0)),
		                 ==, "t.2");
	}
}

/* Checks that the plugin can remove a stored target if it has been removed from
 * the configuration. */
static void
gs_plugin_systemd_sysupdate_metadata_target_removable_func (TestData *test_data)
{
	GsPluginLoader *plugin_loader = test_data->plugin_loader;
	const UpdateTarget *targets[] = {
		&target_component_available,
		&target_component_installed,
		&target_component_updatable,
		NULL
	};

	if (!gs_plugin_loader_get_enabled (plugin_loader, "systemd-sysupdate")) {
		g_test_skip ("not enabled");
		return;
	}

	/* 1st setup, after refresh metadata there should have one app
	 * in the list */
	{
		g_auto(MockSysupdatedRegistrar) registrar = MOCK_SYSUPDATED_REGISTRAR_INIT;
		g_autoptr(GsAppList) list_updates = NULL;

		mock_sysupdated_registrar_init (&registrar, test_data->web_port, &test_data->handle, targets);
		invoke_plugin_loader_refresh_metadata_assert_no_error (plugin_loader);
		list_updates = invoke_plugin_loader_list_apps_for_update_assert_no_error (plugin_loader);
		g_assert_cmpint (gs_app_list_length (list_updates), ==, 1);
	}

	/* 2nd setup, after refresh metadata the list should become
	 * empty now */
	targets[0] = NULL;
	{
		g_auto(MockSysupdatedRegistrar) registrar = MOCK_SYSUPDATED_REGISTRAR_INIT;
		g_autoptr(GsAppList) list_updates = NULL;

		mock_sysupdated_registrar_init (&registrar, test_data->web_port, &test_data->handle, targets);
		invoke_plugin_loader_refresh_metadata_assert_no_error (plugin_loader);
		list_updates = invoke_plugin_loader_list_apps_for_update_assert_no_error (plugin_loader);
		g_assert_cmpint (gs_app_list_length (list_updates), ==, 0);
	}
}

int
main (int argc, char **argv)
{
	g_auto(MockSysupdatedService) service;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	TestData test_data;
	g_autoptr(GError) error = NULL;
	gboolean ret;
	const gchar * const allowlist[] = {
		"systemd-sysupdate",
		NULL,
	};

	gs_test_init (&argc, &argv);
	g_setenv ("GS_XMLB_VERBOSE", "1", TRUE);

	/* setup test D-Bus, mock systemd-sysupdate service */
	mock_sysupdated_service_init (&service);

	/* We can only load this once per process.
	 *
	 * Although we only need to use the system bus in our test, the
	 * underlying `g_test_dbus_up()` will always override the environment
	 * variable `DBUS_SESSION_BUS_ADDRESS`. As a workaround, we also pass
	 * the connection created as the session bus to the `plugin-loader` to
	 * prevent it from setting up another session bus connection. */
	plugin_loader = gs_plugin_loader_new (service.handle.connection,
	                                      service.handle.connection);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR_CORE);
	ret = gs_plugin_loader_setup (plugin_loader,
	                              allowlist,
	                              NULL,
	                              NULL,
	                              &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	test_data.handle = service.handle;
	test_data.web_port = service.web_port;
	test_data.plugin_loader = plugin_loader;

	/* plugin tests go here */

	g_test_add_data_func ("/gnome-software/plugins/systemd-sysupdate/plugin-enabled",
	                      &test_data,
	                      (GTestDataFunc) gs_plugin_systemd_sysupdate_plugin_enabled_func);
	g_test_add_data_func ("/gnome-software/plugins/systemd-sysupdate/distro-upgrade",
	                      &test_data,
	                      (GTestDataFunc) gs_plugin_systemd_sysupdate_distro_upgrade_func);
	g_test_add_data_func ("/gnome-software/plugins/systemd-sysupdate/app-update-updatable",
	                      &test_data,
	                      (GTestDataFunc) gs_plugin_systemd_sysupdate_app_update_updatable_func);
	g_test_add_data_func ("/gnome-software/plugins/systemd-sysupdate/app-update-trackable",
	                      &test_data,
	                      (GTestDataFunc) gs_plugin_systemd_sysupdate_app_update_trackable_func);
	g_test_add_data_func ("/gnome-software/plugins/systemd-sysupdate/app-update-recoverable",
	                      &test_data,
	                      (GTestDataFunc) gs_plugin_systemd_sysupdate_app_update_recoverable_func);
	g_test_add_data_func ("/gnome-software/plugins/systemd-sysupdate/app-update-cancellable",
	                      &test_data,
	                      (GTestDataFunc) gs_plugin_systemd_sysupdate_app_update_cancellable_func);
	g_test_add_data_func ("/gnome-software/plugins/systemd-sysupdate/metadata-target-updatable",
	                      &test_data,
	                      (GTestDataFunc) gs_plugin_systemd_sysupdate_metadata_target_updatable_func);
	g_test_add_data_func ("/gnome-software/plugins/systemd-sysupdate/metadata-target-removable",
	                      &test_data,
	                      (GTestDataFunc) gs_plugin_systemd_sysupdate_metadata_target_removable_func);

	return g_test_run ();
}
