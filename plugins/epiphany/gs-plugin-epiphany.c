/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021-2022 Matthew Leeds <mwleeds@protonmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>
#include <glib/gi18n.h>
#include <gnome-software.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#include "gs-epiphany-generated.h"
#include "gs-plugin-epiphany.h"
#include "gs-plugin-private.h"

/*
 * SECTION:
 * This plugin uses Epiphany to install, launch, and uninstall web applications.
 *
 * If the org.gnome.Epiphany.WebAppProvider D-Bus interface is not present or
 * the DynamicLauncher portal is not available then it self-disables. This
 * should work with both Flatpak'd and not Flatpak'd Epiphany, for new enough
 * versions of Epiphany.
 *
 * Since: 43
 */

struct _GsPluginEpiphany
{
	GsPlugin parent;

	GsWorkerThread *worker;  /* (owned) */

	GsEphyWebAppProvider *epiphany_proxy;  /* (owned) */
	GDBusProxy *launcher_portal_proxy;  /* (owned) */
	GFileMonitor *monitor; /* (owned) */
	guint changed_id;
	GMutex installed_apps_mutex; /* protects the plugin cache */
};

G_DEFINE_TYPE (GsPluginEpiphany, gs_plugin_epiphany, GS_TYPE_PLUGIN)

#define assert_in_worker(self) \
	g_assert (gs_worker_thread_is_in_worker_context (self->worker))

static void
gs_epiphany_error_convert (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return;

	/* parse remote epiphany-webapp-provider error */
	if (g_dbus_error_is_remote_error (error)) {
		g_autofree gchar *remote_error = g_dbus_error_get_remote_error (error);

		g_dbus_error_strip_remote_error (error);

		if (g_str_equal (remote_error, "org.freedesktop.DBus.Error.ServiceUnknown")) {
			error->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
		} else if (g_str_has_prefix (remote_error, "org.gnome.Epiphany.WebAppProvider.Error")) {
			error->code = GS_PLUGIN_ERROR_FAILED;
		} else {
			g_warning ("Can’t reliably fixup remote error ‘%s’", remote_error);
			error->code = GS_PLUGIN_ERROR_FAILED;
		}
		error->domain = GS_PLUGIN_ERROR;
		return;
	}

	/* this is allowed for low-level errors */
	if (gs_utils_error_convert_gio (perror))
		return;

	/* this is allowed for low-level errors */
	if (gs_utils_error_convert_gdbus (perror))
		return;
}

/* Run in the main thread. */
static void
gs_plugin_epiphany_changed_cb (GFileMonitor      *monitor,
                               GFile             *file,
                               GFile             *other_file,
                               GFileMonitorEvent  event_type,
                               gpointer           user_data)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (user_data);

	/* FIXME: With the current API this is the only way to reload the list
	 * of installed apps.
	 */
	gs_plugin_reload (GS_PLUGIN (self));
}

static void setup_thread_cb (GTask        *task,
                             gpointer      source_object,
                             gpointer      task_data,
                             GCancellable *cancellable);

static void
gs_plugin_epiphany_setup_async (GsPlugin            *plugin,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (plugin);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autofree char *portal_apps_path = NULL;
	g_autoptr(GFile) portal_apps_file = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_epiphany_setup_async);

	g_debug ("%s", G_STRFUNC);

	/* Watch for changes to the set of installed apps in the main thread.
	 * This will also trigger when other apps' dynamic launchers are
	 * installed or removed but that is expected to be infrequent.
	 */
	portal_apps_path = g_build_filename (g_get_user_data_dir (), "xdg-desktop-portal", "applications", NULL);
	portal_apps_file = g_file_new_for_path (portal_apps_path);
	/* Monitoring the directory works even if it doesn't exist yet */
	self->monitor = g_file_monitor_directory (portal_apps_file, G_FILE_MONITOR_WATCH_MOVES,
			                          cancellable, &local_error);
	if (self->monitor == NULL) {
		gs_epiphany_error_convert (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	self->changed_id = g_signal_connect (self->monitor, "changed",
					     G_CALLBACK (gs_plugin_epiphany_changed_cb), self);

	/* Start up a worker thread to process all the plugin’s function calls. */
	self->worker = gs_worker_thread_new ("gs-plugin-epiphany");

	/* Queue a job to set up D-Bus proxies */
	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				setup_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker */
static void
setup_thread_cb (GTask        *task,
		 gpointer      source_object,
		 gpointer      task_data,
		 GCancellable *cancellable)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (source_object);
	g_autofree gchar *name_owner = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GVariant) version = NULL;
	GDBusConnection *connection;

	assert_in_worker (self);

	connection = gs_plugin_get_session_bus_connection (GS_PLUGIN (self));
	g_assert (connection != NULL);

	/* Check that the proxy exists (and is owned; it should auto-start) so
	 * we can disable the plugin for systems which don’t have new enough
	 * Epiphany.
	 */
	self->epiphany_proxy = gs_ephy_web_app_provider_proxy_new_sync (connection,
									G_DBUS_PROXY_FLAGS_NONE,
									"org.gnome.Epiphany.WebAppProvider",
									"/org/gnome/Epiphany/WebAppProvider",
									g_task_get_cancellable (task),
									&local_error);
	if (self->epiphany_proxy == NULL) {
		gs_epiphany_error_convert (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (self->epiphany_proxy));
	if (name_owner == NULL) {
		g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "Couldn’t create Epiphany WebAppProvider proxy: couldn’t get name owner");
		return;
	}

	/* Check if the dynamic launcher portal is available and disable otherwise */
	self->launcher_portal_proxy = g_dbus_proxy_new_sync (connection,
							     G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
							     NULL,
							     "org.freedesktop.portal.Desktop",
							     "/org/freedesktop/portal/desktop",
							     "org.freedesktop.portal.DynamicLauncher",
							     g_task_get_cancellable (task),
							     &local_error);
	if (self->launcher_portal_proxy == NULL) {
		gs_epiphany_error_convert (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	version = g_dbus_proxy_get_cached_property (self->launcher_portal_proxy, "version");
	if (version == NULL) {
		g_task_return_new_error (task, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "Dynamic launcher portal not available");
		return;
	} else {
		g_debug ("Found version %" G_GUINT32_FORMAT " of the dynamic launcher portal",
			 g_variant_get_uint32 (version));
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_epiphany_setup_finish (GsPlugin      *plugin,
                                 GAsyncResult  *result,
                                 GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void shutdown_cb (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data);

static void
gs_plugin_epiphany_shutdown_async (GsPlugin            *plugin,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_epiphany_shutdown_async);

	/* Stop the worker thread. */
	gs_worker_thread_shutdown_async (self->worker, cancellable, shutdown_cb, g_steal_pointer (&task));
}

static void
shutdown_cb (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	GsPluginEpiphany *self = g_task_get_source_object (task);
	g_autoptr(GsWorkerThread) worker = NULL;
	g_autoptr(GError) local_error = NULL;

	worker = g_steal_pointer (&self->worker);

	if (!gs_worker_thread_shutdown_finish (worker, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_epiphany_shutdown_finish (GsPlugin      *plugin,
                                    GAsyncResult  *result,
                                    GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_epiphany_init (GsPluginEpiphany *self)
{
	/* set name of MetaInfo file */
	gs_plugin_set_appstream_id (GS_PLUGIN (self), "org.gnome.Software.Plugin.Epiphany");

	/* need help from appstream */
	gs_plugin_add_rule (GS_PLUGIN (self), GS_PLUGIN_RULE_RUN_AFTER, "appstream");

	/* prioritize over packages */
	gs_plugin_add_rule (GS_PLUGIN (self), GS_PLUGIN_RULE_BETTER_THAN, "packagekit");
}

static void
gs_plugin_epiphany_dispose (GObject *object)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (object);

	if (self->changed_id > 0) {
		g_signal_handler_disconnect (self->monitor, self->changed_id);
		self->changed_id = 0;
	}

	g_clear_object (&self->epiphany_proxy);
	g_clear_object (&self->launcher_portal_proxy);
	g_clear_object (&self->monitor);
	g_clear_object (&self->worker);

	G_OBJECT_CLASS (gs_plugin_epiphany_parent_class)->dispose (object);
}

static void
gs_plugin_epiphany_finalize (GObject *object)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (object);

	g_mutex_clear (&self->installed_apps_mutex);

	G_OBJECT_CLASS (gs_plugin_epiphany_parent_class)->finalize (object);
}

void
gs_plugin_adopt_app (GsPlugin *plugin,
		     GsApp    *app)
{
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_WEB_APP &&
	    gs_app_get_bundle_kind (app) != AS_BUNDLE_KIND_PACKAGE) {
		gs_app_set_management_plugin (app, plugin);
	}
}

static void list_installed_apps_thread_cb (GTask        *task,
                                           gpointer      source_object,
                                           gpointer      task_data,
                                           GCancellable *cancellable);

static void
gs_plugin_epiphany_list_installed_apps_async (GsPlugin                       *plugin,
					      GsPluginListInstalledAppsFlags  flags,
					      GCancellable                   *cancellable,
					      GAsyncReadyCallback             callback,
					      gpointer                        user_data)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_epiphany_list_installed_apps_async);

	/* Queue a job to get the installed apps. */
	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				list_installed_apps_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker */
static void
set_license_from_hostname (GsApp      *app,
			   const char *hostname)
{
	const struct {
		const gchar *hostname;
		const gchar *license_spdx;
	} app_licenses[] = {
	/* Keep in alphabetical order by hostname */
	{ "app.diagrams.net", "Apache-2.0" },
	{ "devdocs.io", "MPL-2.0" },
	{ "discourse.flathub.org", "GPL-2.0-or-later" },
	{ "discourse.gnome.org", "GPL-2.0-or-later" },
	{ "excalidraw.com", "MIT" },
	{ "pinafore.social", "AGPL-3.0-only" },
	{ "snapdrop.net", "GPL-3.0-only" },
	{ "stackedit.io", "Apache-2.0" },
	{ "squoosh.app", "Apache-2.0" },
	};

	g_return_if_fail (GS_IS_APP (app));

	if (hostname == NULL || *hostname == '\0')
		return;

	/* Hard-code the licenses as it's hard to get them programmatically. We
	 * can move them to an AppStream file if needed.
	 */
	if (gs_app_get_license (app) == NULL) {
		for (gsize i = 0; i < G_N_ELEMENTS (app_licenses); i++) {
			if (g_str_equal (hostname, app_licenses[i].hostname)) {
				gs_app_set_license (app, GS_APP_QUALITY_NORMAL,
						    app_licenses[i].license_spdx);
				break;
			}
		}
	}
}

/* Run in @worker */
static GsApp *
gs_epiphany_create_app (GsPluginEpiphany *self,
			const char       *id)
{
	g_autoptr(GsApp) app_cached = NULL;
	g_autoptr(GsApp) tmp_app = NULL;

	assert_in_worker (self);

	tmp_app = gs_app_new (id);
	gs_app_set_management_plugin (tmp_app, GS_PLUGIN (self));
	gs_app_set_origin (tmp_app, "gnome-web");
	gs_app_set_origin_ui (tmp_app, _("GNOME Web"));
	gs_app_set_kind (tmp_app, AS_COMPONENT_KIND_WEB_APP);
	gs_app_set_scope (tmp_app, AS_COMPONENT_SCOPE_USER);

	app_cached = gs_plugin_cache_lookup (GS_PLUGIN (self), id);
	if (app_cached != NULL)
		return g_steal_pointer (&app_cached);

	gs_plugin_cache_add (GS_PLUGIN (self), id, tmp_app);
	return g_steal_pointer (&tmp_app);
}

/* Run in @worker */
static void
list_installed_apps_thread_cb (GTask        *task,
                               gpointer      source_object,
                               gpointer      task_data,
                               GCancellable *cancellable)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (source_object);
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GsAppList) installed_cache = gs_app_list_new ();
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GVariant) webapps_v = NULL;
	g_auto(GStrv) webapps = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&self->installed_apps_mutex);
	guint n_webapps;

	assert_in_worker (self);

	if (!gs_ephy_web_app_provider_call_get_installed_apps_sync (self->epiphany_proxy,
								    &webapps,
								    cancellable,
								    &local_error)) {
		gs_epiphany_error_convert (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	n_webapps = g_strv_length (webapps);
	g_debug ("%s: epiphany-webapp-provider returned %u installed web apps", G_STRFUNC, n_webapps);
	for (guint i = 0; i < n_webapps; i++) {
		const gchar *desktop_file_id = webapps[i];
		const gchar *desktop_path;
		const gchar *name;
		const gchar *url = NULL;
		g_autofree char *icon_path = NULL;
		const gchar *exec;
		const gchar *host;
		int argc;
		g_auto(GStrv) argv = NULL;
		guint64 install_date = 0;
		goffset desktop_size = 0, icon_size = 0;
		g_autoptr(GsApp) app = NULL;
		g_autoptr(GDesktopAppInfo) desktop_info = NULL;
		g_autoptr(GFileInfo) file_info = NULL;
		g_autoptr(GFile) desktop_file = NULL;
		g_autoptr(GUri) uri = NULL;

		g_debug ("%s: Working on installed web app %s", G_STRFUNC, desktop_file_id);

		desktop_info = g_desktop_app_info_new (desktop_file_id);

		if (desktop_info == NULL) {
			g_warning ("Epiphany returned a non-existent or invalid desktop ID %s", desktop_file_id);
			continue;
		}

		name = g_app_info_get_name (G_APP_INFO (desktop_info));

		/* This way of getting the URL is a bit hacky but it's what
		 * Epiphany does, specifically in
		 * ephy_web_application_for_profile_directory() which lives in
		 * https://gitlab.gnome.org/GNOME/epiphany/-/blob/master/lib/ephy-web-app-utils.c
		 */
		exec = g_app_info_get_commandline (G_APP_INFO (desktop_info));
		if (g_shell_parse_argv (exec, &argc, &argv, NULL)) {
			g_assert (argc > 0);
			url = argv[argc - 1];
		}
		if (!url || !(uri = g_uri_parse (url, G_URI_FLAGS_NONE, NULL))) {
			g_warning ("Failed to parse URL for web app %s", desktop_file_id);
			continue;
		}

		icon_path = g_desktop_app_info_get_string (desktop_info, "Icon");

		desktop_path = g_desktop_app_info_get_filename (desktop_info);
		g_assert (desktop_path);
		desktop_file = g_file_new_for_path (desktop_path);

		file_info = g_file_query_info (desktop_file,
					       G_FILE_ATTRIBUTE_TIME_CREATED "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
					       0, NULL, NULL);
		if (file_info) {
			install_date = g_file_info_get_attribute_uint64 (file_info, G_FILE_ATTRIBUTE_TIME_CREATED);
			desktop_size = g_file_info_get_size (file_info);
		}

		app = gs_epiphany_create_app (self, desktop_file_id);
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, name);
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, url);
		gs_app_set_permissions (app, GS_APP_PERMISSIONS_NETWORK);

		/* Use the domain name as a fallback summary.
		 * FIXME: Fetch the summary from the site's webapp manifest.
		 */
		host = g_uri_get_host (uri);
		gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, host ? host : url);

		set_license_from_hostname (app, host);

		if (icon_path) {
			g_autoptr(GFile) icon_file = g_file_new_for_path (icon_path);
			g_autoptr(GIcon) icon = g_file_icon_new (icon_file);
			g_autofree char *icon_dir = g_path_get_dirname (icon_path);
			g_autofree char *icon_dir_basename = g_path_get_basename (icon_dir);
			const char *x;
			guint64 size = 0;

			g_debug ("%s: finding size for icon %s", G_STRFUNC, icon_path);

			g_clear_object (&file_info);
			file_info = g_file_query_info (icon_file,
						       G_FILE_ATTRIBUTE_STANDARD_SIZE,
						       0, NULL, NULL);
			if (file_info)
				icon_size = g_file_info_get_size (file_info);

			/* dir should be either scalable or e.g. 512x512 */
			if (g_strcmp0 (icon_dir_basename, "scalable") == 0) {
				/* Ensure scalable icons are preferred */
				size = 4096;
			} else if ((x = strchr (icon_dir_basename, 'x')) != NULL) {
				g_ascii_string_to_unsigned (x + 1, 10, 1, G_MAXINT, &size, NULL);
			}
			if (size > 0 && size <= 4096) {
				gs_icon_set_width (icon, size);
				gs_icon_set_height (icon, size);
			} else {
				g_warning ("Unexpectedly unable to determine size of icon %s", icon_path);
			}

			gs_app_add_icon (app, icon);
		}
		if (install_date) {
			gs_app_set_install_date (app, install_date);
		}
		if (desktop_size > 0 || icon_size > 0) {
			gs_app_set_size_installed (app, GS_SIZE_TYPE_VALID, desktop_size + icon_size);
		}
		gs_app_list_add (list, app);
	}

	/* Update the state on any apps that were uninstalled outside
	 * gnome-software
	 */
	gs_plugin_cache_lookup_by_state (GS_PLUGIN (self), installed_cache, GS_APP_STATE_INSTALLED);
	for (guint i = 0; i < gs_app_list_length (installed_cache); i++) {
		GsApp *app = gs_app_list_index (installed_cache, i);
		const char *app_id = gs_app_get_id (app);
		g_autoptr(GsApp) app_cached = NULL;

		if (g_strv_contains ((const char * const *)webapps, app_id))
			continue;

		gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
		gs_plugin_cache_remove (GS_PLUGIN (self), app_id);
	}

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_epiphany_list_installed_apps_finish (GsPlugin      *plugin,
                                               GAsyncResult  *result,
                                               GError       **error)
{
	g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == gs_plugin_epiphany_list_installed_apps_async, FALSE);
	return g_task_propagate_pointer (G_TASK (result), error);
}

static GVariant *
get_serialized_icon (GsApp *app,
		     GIcon *icon)
{
	g_autofree char *icon_path = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GIcon) bytes_icon = NULL;
	g_autoptr(GVariant) icon_v = NULL;

	/* Note: GsRemoteIcon will work on this GFileIcon code path.
	 * The icons plugin should have called
	 * gs_app_ensure_icons_downloaded() for us.
	 */
	if (!G_IS_FILE_ICON (icon))
		return NULL;

	icon_path = g_file_get_path (g_file_icon_get_file (G_FILE_ICON (icon)));
	if (!g_str_has_suffix (icon_path, ".png") &&
	    !g_str_has_suffix (icon_path, ".svg") &&
	    !g_str_has_suffix (icon_path, ".jpeg") &&
	    !g_str_has_suffix (icon_path, ".jpg")) {
		g_warning ("Icon for app %s has unsupported file extension: %s",
			   gs_app_get_id (app), icon_path);
		return NULL;
	}

	/* Serialize the icon as a #GBytesIcon since that's
	 * what the dynamic launcher portal requires.
	 */
	stream = g_loadable_icon_load (G_LOADABLE_ICON (icon), 0, NULL, NULL, NULL);

	/* Icons are usually smaller than 1 MiB. Set a 10 MiB
	 * limit so we can't use a huge amount of memory or hit
	 * the D-Bus message size limit
	 */
	if (stream)
		bytes = g_input_stream_read_bytes (stream, 10485760 /* 10 MiB */, NULL, NULL);
	if (bytes)
		bytes_icon = g_bytes_icon_new (bytes);
	if (bytes_icon)
		icon_v = g_icon_serialize (bytes_icon);

	return g_steal_pointer (&icon_v);
}

gboolean
gs_plugin_app_install (GsPlugin      *plugin,
		       GsApp         *app,
		       GCancellable  *cancellable,
		       GError       **error)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (plugin);
	const char *url;
	const char *name;
	const char *token = NULL;
	g_autofree char *installed_desktop_id = NULL;
	g_autoptr(GVariant) token_v = NULL;
	g_autoptr(GVariant) icon_v = NULL;
	GVariantBuilder opt_builder;
	const int icon_sizes[] = {512, 192, 128, 1};

	if (!gs_app_has_management_plugin (app, plugin))
		return TRUE;

	url = gs_app_get_url (app, AS_URL_KIND_HOMEPAGE);
	if (url == NULL || *url == '\0') {
		g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			     "Can't install web app %s without url",
			     gs_app_get_id (app));
		return FALSE;
	}
	name = gs_app_get_name (app);
	if (name == NULL || *name == '\0') {
		g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			     "Can't install web app %s without name",
			     gs_app_get_id (app));
		return FALSE;
	}
	for (guint i = 0; i < G_N_ELEMENTS (icon_sizes); i++) {
		GIcon *icon = gs_app_get_icon_for_size (app, icon_sizes[i], 1, NULL);
		if (icon != NULL)
			icon_v = get_serialized_icon (app, icon);
		if (icon_v != NULL)
			break;
	}
	if (icon_v == NULL) {
		g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			     "Can't install web app %s without icon",
			     gs_app_get_id (app));
		return FALSE;
	}

	gs_app_set_state (app, GS_APP_STATE_INSTALLING);
	/* First get a token from xdg-desktop-portal so Epiphany can do the
	 * installation without user confirmation
	 */
	g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
	token_v = g_dbus_proxy_call_sync (self->launcher_portal_proxy,
					  "RequestInstallToken",
					  g_variant_new ("(sva{sv})",
							 name, icon_v, &opt_builder),
					  G_DBUS_CALL_FLAGS_NONE,
					  -1, cancellable, error);
	if (token_v == NULL) {
		gs_epiphany_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* Then pass the token to Epiphany which will use xdg-desktop-portal to
	 * complete the installation
	 */
	g_variant_get (token_v, "(&s)", &token);
	if (!gs_ephy_web_app_provider_call_install_sync (self->epiphany_proxy,
							 url, name, token,
							 &installed_desktop_id,
							 cancellable,
							 error)) {
		gs_epiphany_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_launchable (app, AS_LAUNCHABLE_KIND_DESKTOP_ID, installed_desktop_id);
	gs_app_set_state (app, GS_APP_STATE_INSTALLED);

	return TRUE;
}

gboolean
gs_plugin_app_remove (GsPlugin      *plugin,
		      GsApp         *app,
		      GCancellable  *cancellable,
		      GError       **error)
{
	GsPluginEpiphany *self = GS_PLUGIN_EPIPHANY (plugin);

	if (!gs_app_has_management_plugin (app, plugin))
		return TRUE;

	gs_app_set_state (app, GS_APP_STATE_REMOVING);
	if (!gs_ephy_web_app_provider_call_uninstall_sync (self->epiphany_proxy,
							   gs_app_get_id (app),
							   cancellable,
							   error)) {
		gs_epiphany_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE);

	return TRUE;
}

gboolean
gs_plugin_launch (GsPlugin      *plugin,
		  GsApp         *app,
		  GCancellable  *cancellable,
		  GError       **error)
{
	if (!gs_app_has_management_plugin (app, plugin))
		return TRUE;

	return gs_plugin_app_launch (plugin, app, error);
}

static void
gs_plugin_epiphany_class_init (GsPluginEpiphanyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_epiphany_dispose;
	object_class->finalize = gs_plugin_epiphany_finalize;

	plugin_class->setup_async = gs_plugin_epiphany_setup_async;
	plugin_class->setup_finish = gs_plugin_epiphany_setup_finish;
	plugin_class->shutdown_async = gs_plugin_epiphany_shutdown_async;
	plugin_class->shutdown_finish = gs_plugin_epiphany_shutdown_finish;
	plugin_class->list_installed_apps_async = gs_plugin_epiphany_list_installed_apps_async;
	plugin_class->list_installed_apps_finish = gs_plugin_epiphany_list_installed_apps_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_EPIPHANY;
}
