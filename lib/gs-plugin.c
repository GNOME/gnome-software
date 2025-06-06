/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-plugin
 * @title: GsPlugin Helpers
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: Runtime-loaded modules providing functionality
 *
 * Plugins are modules that are loaded at runtime to provide information
 * about requests and to service user actions like installing, removing
 * and updating.
 * This allows different distributions to pick and choose how the
 * application installer gathers data.
 *
 * Plugins also have a priority system where the largest number gets
 * run first. That means if one plugin requires some property or
 * metadata set by another plugin then it **must** depend on the other
 * plugin to be run in the correct order.
 *
 * As a general rule, try to make plugins as small and self-contained
 * as possible and remember to cache as much data as possible for speed.
 * Memory is cheap, time less so.
 */

#include "config.h"

#include <gio/gdesktopappinfo.h>
#include <gdk/gdk.h>
#include <string.h>

#include "gs-app-list-private.h"
#include "gs-download-utils.h"
#include "gs-enums.h"
#include "gs-os-release.h"
#include "gs-plugin-private.h"
#include "gs-plugin.h"
#include "gs-utils.h"

typedef struct
{
	GHashTable		*cache;
	GMutex			 cache_mutex;
	GModule			*module;
	GPtrArray		*rules[GS_PLUGIN_RULE_LAST];
	GHashTable		*vfuncs;		/* string:pointer */
	GMutex			 vfuncs_mutex;
	gboolean		 enabled;
	gchar			*language;		/* allow-none */
	gchar			*name;
	guint			 scale;
	guint			 order;
	guint			 priority;
	guint			 timer_id;
	GMutex			 timer_mutex;
	GNetworkMonitor		*network_monitor;

	GDBusConnection		*session_bus_connection;  /* (owned) (not nullable) */
	GDBusConnection		*system_bus_connection;  /* (owned) (not nullable) */
} GsPluginPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsPlugin, gs_plugin, G_TYPE_OBJECT)

G_DEFINE_QUARK (gs-plugin-error-quark, gs_plugin_error)

typedef enum {
	PROP_NAME = 1,
	PROP_SCALE,
	PROP_SESSION_BUS_CONNECTION,
	PROP_SYSTEM_BUS_CONNECTION,
} GsPluginProperty;

static GParamSpec *obj_props[PROP_SYSTEM_BUS_CONNECTION + 1] = { NULL, };

enum {
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_RELOAD,
	SIGNAL_REPORT_EVENT,
	SIGNAL_ALLOW_UPDATES,
	SIGNAL_BASIC_AUTH_START,
	SIGNAL_REPOSITORY_CHANGED,
	SIGNAL_ASK_UNTRUSTED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_plugin_create:
 * @filename: an absolute filename
 * @session_bus_connection: (not nullable) (transfer none): a session bus
 *   connection to use
 * @system_bus_connection: (not nullable) (transfer none): a system bus
 *   connection to use
 * @error: a #GError, or %NULL
 *
 * Creates a new plugin from an external module.
 *
 * Returns: (transfer full): the #GsPlugin, or %NULL on error
 *
 * Since: 43
 **/
GsPlugin *
gs_plugin_create (const gchar      *filename,
                  GDBusConnection  *session_bus_connection,
                  GDBusConnection  *system_bus_connection,
                  GError          **error)
{
	GsPlugin *plugin = NULL;
	GsPluginPrivate *priv;
	g_autofree gchar *basename = NULL;
	GModule *module = NULL;
	GType (*query_type_function) (void) = NULL;
	GType plugin_type;
	const char *library_prefix = "libgs_plugin_";

	/* get the plugin name from the basename */
	basename = g_path_get_basename (filename);
	if (!g_str_has_prefix (basename, library_prefix)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "plugin filename has wrong prefix: %s",
			     filename);
		return NULL;
	}
	g_strdelimit (basename, ".", '\0');

	/* create new plugin */
	module = g_module_open (filename, 0);
	if (module == NULL ||
	    !g_module_symbol (module, "gs_plugin_query_type", (gpointer *) &query_type_function)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to open plugin %s: %s",
			     filename, g_module_error ());
		if (module != NULL)
			g_module_close (module);
		return NULL;
	}

	/* Make the module resident so it can’t be unloaded: without using a
	 * full #GTypePlugin implementation for the modules, it’s not safe to
	 * re-load a module and re-register its types with GObject, as that will
	 * confuse the GType system. */
	g_module_make_resident (module);

	plugin_type = query_type_function ();
	g_assert (g_type_is_a (plugin_type, GS_TYPE_PLUGIN));

	plugin = g_object_new (plugin_type,
			       "session-bus-connection", session_bus_connection,
			       "system-bus-connection", system_bus_connection,
			       "name", basename + strlen (library_prefix),
			       NULL);
	priv = gs_plugin_get_instance_private (plugin);
	priv->module = g_steal_pointer (&module);

	return plugin;
}

static void
gs_plugin_dispose (GObject *object)
{
	GsPlugin *plugin = GS_PLUGIN (object);
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	g_clear_object (&priv->session_bus_connection);
	g_clear_object (&priv->system_bus_connection);

	G_OBJECT_CLASS (gs_plugin_parent_class)->dispose (object);
}

static void
gs_plugin_finalize (GObject *object)
{
	GsPlugin *plugin = GS_PLUGIN (object);
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	guint i;

	for (i = 0; i < GS_PLUGIN_RULE_LAST; i++)
		g_ptr_array_unref (priv->rules[i]);

	if (priv->timer_id > 0)
		g_source_remove (priv->timer_id);
	g_free (priv->name);
	g_free (priv->language);
	if (priv->network_monitor != NULL)
		g_object_unref (priv->network_monitor);
	g_hash_table_unref (priv->cache);
	g_hash_table_unref (priv->vfuncs);
	g_mutex_clear (&priv->cache_mutex);
	g_mutex_clear (&priv->timer_mutex);
	g_mutex_clear (&priv->vfuncs_mutex);
	if (priv->module != NULL)
		g_module_close (priv->module);

	G_OBJECT_CLASS (gs_plugin_parent_class)->finalize (object);
}

/**
 * gs_plugin_get_enabled:
 * @plugin: a #GsPlugin
 *
 * Gets if the plugin is enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 3.22
 **/
gboolean
gs_plugin_get_enabled (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->enabled;
}

/**
 * gs_plugin_set_enabled:
 * @plugin: a #GsPlugin
 * @enabled: the enabled state
 *
 * Enables or disables a plugin.
 * This is normally only called from the init function for a #GsPlugin instance.
 *
 * Since: 3.22
 **/
void
gs_plugin_set_enabled (GsPlugin *plugin, gboolean enabled)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->enabled = enabled;
}

/**
 * gs_plugin_get_name:
 * @plugin: a #GsPlugin
 *
 * Gets the plugin name.
 *
 * Returns: a string, e.g. "fwupd"
 *
 * Since: 3.22
 **/
const gchar *
gs_plugin_get_name (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->name;
}

/**
 * gs_plugin_get_scale:
 * @plugin: a #GsPlugin
 *
 * Gets the window scale factor.
 *
 * Returns: the factor, usually 1 for standard screens or 2 for HiDPI
 *
 * Since: 3.22
 **/
guint
gs_plugin_get_scale (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->scale;
}

/**
 * gs_plugin_set_scale:
 * @plugin: a #GsPlugin
 * @scale: the window scale factor, usually 1 for standard screens or 2 for HiDPI
 *
 * Sets the window scale factor.
 *
 * Since: 3.22
 **/
void
gs_plugin_set_scale (GsPlugin *plugin, guint scale)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	if (priv->scale != scale) {
		priv->scale = scale;
		g_object_notify_by_pspec (G_OBJECT (plugin), obj_props[PROP_SCALE]);
	}
}

/**
 * gs_plugin_get_order:
 * @plugin: a #GsPlugin
 *
 * Gets the plugin order, where higher numbers are run after lower
 * numbers.
 *
 * Returns: the integer value
 *
 * Since: 3.22
 **/
guint
gs_plugin_get_order (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->order;
}

/**
 * gs_plugin_set_order:
 * @plugin: a #GsPlugin
 * @order: a integer value
 *
 * Sets the plugin order, where higher numbers are run after lower
 * numbers.
 *
 * Since: 3.22
 **/
void
gs_plugin_set_order (GsPlugin *plugin, guint order)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->order = order;
}

/**
 * gs_plugin_get_priority:
 * @plugin: a #GsPlugin
 *
 * Gets the plugin priority, where higher values will be chosen where
 * multiple #GsApp's match a specific rule.
 *
 * Returns: the integer value
 *
 * Since: 3.22
 **/
guint
gs_plugin_get_priority (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->priority;
}

/**
 * gs_plugin_set_priority:
 * @plugin: a #GsPlugin
 * @priority: a integer value
 *
 * Sets the plugin priority, where higher values will be chosen where
 * multiple #GsApp's match a specific rule.
 *
 * Since: 3.22
 **/
void
gs_plugin_set_priority (GsPlugin *plugin, guint priority)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->priority = priority;
}

/**
 * gs_plugin_get_language:
 * @plugin: a #GsPlugin
 *
 * Gets the user language from the locale. This is the first component of the
 * locale.
 *
 * Typically you should use the full locale rather than the language, as the
 * same language can be used quite differently in different territories.
 *
 * Returns: the language string, e.g. `fr`
 *
 * Since: 3.22
 **/
const gchar *
gs_plugin_get_language (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->language;
}

/**
 * gs_plugin_set_language:
 * @plugin: a #GsPlugin
 * @language: a language string, e.g. "fr"
 *
 * Sets the plugin language.
 *
 * Since: 3.22
 **/
void
gs_plugin_set_language (GsPlugin *plugin, const gchar *language)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_free (priv->language);
	priv->language = g_strdup (language);
}

/**
 * gs_plugin_set_network_monitor:
 * @plugin: a #GsPlugin
 * @monitor: a #GNetworkMonitor
 *
 * Sets the network monitor so that plugins can check the state of the network.
 *
 * Since: 3.28
 **/
void
gs_plugin_set_network_monitor (GsPlugin *plugin, GNetworkMonitor *monitor)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_set_object (&priv->network_monitor, monitor);
}

/**
 * gs_plugin_get_network_available:
 * @plugin: a #GsPlugin
 *
 * Gets whether a network connectivity is available.
 *
 * Returns: %TRUE if a network is available.
 *
 * Since: 3.28
 **/
gboolean
gs_plugin_get_network_available (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	if (priv->network_monitor == NULL) {
		g_debug ("no network monitor, so returning network-available=TRUE");
		return TRUE;
	}
	return g_network_monitor_get_network_available (priv->network_monitor);
}

/**
 * gs_plugin_add_rule:
 * @plugin: a #GsPlugin
 * @rule: a #GsPluginRule, e.g. %GS_PLUGIN_RULE_CONFLICTS
 * @name: a plugin name, e.g. "appstream"
 *
 * If the plugin name is found, the rule will be used to sort the plugin list,
 * for example the plugin specified by @name will be ordered after this plugin
 * when %GS_PLUGIN_RULE_RUN_AFTER is used.
 *
 * NOTE: The depsolver is iterative and may not solve overly-complicated rules;
 * If depsolving fails then gnome-software will not start.
 *
 * Since: 3.22
 **/
void
gs_plugin_add_rule (GsPlugin *plugin, GsPluginRule rule, const gchar *name)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_ptr_array_add (priv->rules[rule], g_strdup (name));
}

/**
 * gs_plugin_get_rules:
 * @plugin: a #GsPlugin
 * @rule: a #GsPluginRule, e.g. %GS_PLUGIN_RULE_CONFLICTS
 *
 * Gets the plugin IDs that should be run after this plugin.
 *
 * Returns: (element-type utf8) (transfer none): the list of plugin names, e.g. ['appstream']
 *
 * Since: 3.22
 **/
GPtrArray *
gs_plugin_get_rules (GsPlugin *plugin, GsPluginRule rule)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->rules[rule];
}

/**
 * gs_plugin_adopt_app:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 *
 * Called when the @app has not been claimed (i.e. a management plugin has not
 * been set), using GsPluginClass.adopt_app() if set. This does nothing if
 * the @plugin does not implement the function.
 *
 * A claimed app means other plugins will not try to perform actions
 * such as install, remove or update. Most apps are claimed when they
 * are created.
 *
 * If a plugin can adopt this app then it should call
 * gs_app_set_management_plugin() on @app.
 *
 * Since: 49
 */
void
gs_plugin_adopt_app (GsPlugin *plugin,
		     GsApp *app)
{
	GsPluginClass *plugin_class;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP (app));

	plugin_class = GS_PLUGIN_GET_CLASS (plugin);
	if (plugin_class->adopt_app != NULL)
		plugin_class->adopt_app (plugin, app);
}

/**
 * gs_plugin_check_distro_id:
 * @plugin: a #GsPlugin
 * @distro_id: a distro ID, e.g. "fedora"
 *
 * Checks if the distro is compatible.
 *
 * Returns: %TRUE if compatible
 *
 * Since: 3.22
 **/
gboolean
gs_plugin_check_distro_id (GsPlugin *plugin, const gchar *distro_id)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;
	const gchar *id = NULL;

	/* load /etc/os-release */
	os_release = gs_os_release_new (&error);
	if (os_release == NULL) {
		g_debug ("could not parse os-release: %s", error->message);
		return FALSE;
	}

	/* check that we are running on Fedora */
	id = gs_os_release_get_id (os_release);
	if (id == NULL) {
		g_debug ("could not get distro ID");
		return FALSE;
	}
	if (g_strcmp0 (id, distro_id) != 0)
		return FALSE;
	return TRUE;
}

typedef struct {
	GsPlugin	*plugin;
	gchar		*remote;
	gchar		*realm;
	GCallback	 callback;
	gpointer	 user_data;
} GsPluginBasicAuthHelper;

static gboolean
gs_plugin_basic_auth_start_cb (gpointer user_data)
{
	GsPluginBasicAuthHelper *helper = user_data;
	g_signal_emit (helper->plugin,
		       signals[SIGNAL_BASIC_AUTH_START], 0,
		       helper->remote,
		       helper->realm,
		       helper->callback,
		       helper->user_data);
	g_free (helper->remote);
	g_free (helper->realm);
	g_slice_free (GsPluginBasicAuthHelper, helper);
	return FALSE;
}

/**
 * gs_plugin_basic_auth_start:
 * @plugin: a #GsPlugin
 * @remote: a string
 * @realm: a string
 * @callback: callback to invoke to submit the user/password
 * @user_data: callback data to pass to the callback
 *
 * Emit the basic-auth-start signal in the main thread.
 *
 * Since: 3.38
 **/
void
gs_plugin_basic_auth_start (GsPlugin *plugin,
                            const gchar *remote,
                            const gchar *realm,
                            GCallback callback,
                            gpointer user_data)
{
	GsPluginBasicAuthHelper *helper;
	g_autoptr(GSource) idle_source = NULL;

	helper = g_slice_new0 (GsPluginBasicAuthHelper);
	helper->plugin = plugin;
	helper->remote = g_strdup (remote);
	helper->realm = g_strdup (realm);
	helper->callback = callback;
	helper->user_data = user_data;

	idle_source = g_idle_source_new ();
	g_source_set_callback (idle_source, gs_plugin_basic_auth_start_cb, helper, NULL);
	g_source_attach (idle_source, NULL);
}

static const gchar *
get_desktop_id_to_launch (GsApp *app)
{
	const gchar *desktop_id = gs_app_get_launchable (app, AS_LAUNCHABLE_KIND_DESKTOP_ID);
	if (desktop_id == NULL)
		desktop_id = gs_app_get_id (app);
	return desktop_id;
}

static gboolean
launch_app_info (GAppInfo *appinfo,
		 GError **error)
{
	GdkDisplay *display;
	g_autoptr(GAppLaunchContext) context = NULL;

	g_assert (appinfo != NULL);

	display = gdk_display_get_default ();
	context = G_APP_LAUNCH_CONTEXT (gdk_display_get_app_launch_context (display));

	return g_app_info_launch (appinfo, NULL, context, error);
}

/**
 * gs_plugin_app_launch_async:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @flags: a bit-or of #GsPluginLaunchFlags
 * @cancellable: a #GCancellable, or %NULL
 * @callback: (not nullable): a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: (closure callback) (scope async): data to pass to @callback
 *
 * Asynchronously launches the application using #GAppInfo.
 * Finish the call with gs_plugin_app_launch_finish().
 *
 * The function also verifies whether the @plugin can handle the @app,
 * in a sense of gs_app_has_management_plugin(), and if not then does
 * nothing.
 *
 * Since: 47
 **/
void
gs_plugin_app_launch_async (GsPlugin *plugin,
			    GsApp *app,
			    GsPluginLaunchFlags flags,
			    GCancellable *cancellable,
			    GAsyncReadyCallback callback,
			    gpointer user_data)
{
	const gchar *desktop_id;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GAppInfo) appinfo = NULL;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (callback != NULL);

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_app_launch_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin)) {
		g_task_return_pointer (task, NULL, NULL);
		return;
	}

	desktop_id = get_desktop_id_to_launch (app);
	if (desktop_id == NULL) {
		g_task_return_new_error (task, GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no desktop file for app: %s",
			     gs_app_get_name (app));
		return;
	}
	appinfo = G_APP_INFO (gs_utils_get_desktop_app_info (desktop_id));
	if (appinfo == NULL) {
		g_task_return_new_error (task, GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no such desktop file: %s",
			     desktop_id);
		return;
	}

	/* the actual launch happens in the _finish() function,
	   which should be in the main thread */
	g_task_return_pointer (task, g_steal_pointer (&appinfo), g_object_unref);
}

/**
 * gs_plugin_app_launch_finish:
 * @plugin: a #GsPlugin
 * @result: an async result
 * @error: a #GError or %NULL
 *
 * Finishes operation started by gs_plugin_app_launch_async().
 * This function should be called from the main thread.
 *
 * Returns: whether succeeded
 *
 * Since: 47
 **/
gboolean
gs_plugin_app_launch_finish (GsPlugin *plugin,
			     GAsyncResult *result,
			     GError **error)
{
	g_autoptr(GAppInfo) appinfo = NULL;

	g_return_val_if_fail (g_task_is_valid (G_TASK (result), plugin), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, gs_plugin_app_launch_async), FALSE);

	appinfo = g_task_propagate_pointer (G_TASK (result), error);
	if (appinfo == NULL)
		return TRUE;

	return launch_app_info (appinfo, error);
}

static GDesktopAppInfo *
check_directory_for_desktop_file (GsPlugin *plugin,
				  GsApp *app,
				  GsPluginPickDesktopFileCallback cb,
				  gpointer user_data,
				  const gchar *desktop_id,
				  const gchar *data_dir)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(GKeyFile) key_file = NULL;
	gboolean found, any_found = FALSE;

	filename = g_build_filename (data_dir, "applications", desktop_id, NULL);
	key_file = g_key_file_new ();

	found = g_key_file_load_from_file (key_file, filename, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	if (found && cb (plugin, app, filename, key_file, user_data)) {
		g_autoptr(GDesktopAppInfo) appinfo = NULL;
		g_debug ("Found '%s' for app '%s' and picked it", filename, desktop_id);
		/* use the filename, not the key_file, to enable bus activation from the .desktop file */
		appinfo = g_desktop_app_info_new_from_filename (filename);
		if (appinfo != NULL)
			return g_steal_pointer (&appinfo);
		g_debug ("Failed to load '%s' as a GDesktopAppInfo", filename);
		return NULL;
	} else if (found) {
		g_debug ("Found '%s' for app '%s', but did not pick it", filename, desktop_id);
		any_found = TRUE;
	}

	if (!g_str_has_suffix (desktop_id, ".desktop")) {
		g_autofree gchar *desktop_filename = g_strconcat (filename, ".desktop", NULL);
		found = g_key_file_load_from_file (key_file, desktop_filename, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
		if (found && cb (plugin, app, desktop_filename, key_file, user_data)) {
			g_autoptr(GDesktopAppInfo) appinfo = NULL;
			g_debug ("Found '%s' for app '%s' and picked it", desktop_filename, desktop_id);
			/* use the filename, not the key_file, to enable bus activation from the .desktop file */
			appinfo = g_desktop_app_info_new_from_filename (desktop_filename);
			if (appinfo != NULL)
				return g_steal_pointer (&appinfo);
			g_debug ("Failed to load '%s' as a GDesktopAppInfo", desktop_filename);
			return NULL;
		} else if (found) {
			g_debug ("Found '%s' for app '%s', but did not pick it", desktop_filename, desktop_id);
			any_found = TRUE;
		}
	}

	if (!any_found)
		g_debug ("Did not find any appropriate .desktop file for '%s' in '%s/applications/'", desktop_id, data_dir);
	return NULL;
}

typedef struct {
	GsApp *app; /* (owned) */
	GsPluginPickDesktopFileCallback cb;
	gpointer cb_user_data;
	GAppInfo *appinfo; /* (owned) (nullable) (out) */
} LaunchFilteredData;

static void
launch_filtered_data_free (LaunchFilteredData *data)
{
	g_clear_object (&data->app);
	g_clear_object (&data->appinfo);
	g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LaunchFilteredData, launch_filtered_data_free)

static void
launch_filtered_thread (GTask *task,
			gpointer source_object,
			gpointer task_data,
			GCancellable *cancellable)
{
	g_autoptr(GDesktopAppInfo) appinfo = NULL;
	GsPlugin *plugin = GS_PLUGIN (source_object);
	LaunchFilteredData *data = task_data;
	const gchar *desktop_id;

	desktop_id = get_desktop_id_to_launch (data->app);
	/* the caller verified it's set */
	g_assert (desktop_id != NULL);

	/* First, the configs.  Highest priority: the user's ~/.config */
	appinfo = check_directory_for_desktop_file (plugin, data->app, data->cb, data->cb_user_data, desktop_id, g_get_user_config_dir ());

	if (appinfo == NULL) {
		/* Next, the system configs (/etc/xdg, and so on). */
		const gchar * const *dirs;
		dirs = g_get_system_config_dirs ();
		for (guint i = 0; dirs[i] && appinfo == NULL; i++) {
			appinfo = check_directory_for_desktop_file (plugin, data->app, data->cb, data->cb_user_data, desktop_id, dirs[i]);
		}
	}

	if (appinfo == NULL) {
		/* Now the data.  Highest priority: the user's ~/.local/share/applications */
		appinfo = check_directory_for_desktop_file (plugin, data->app, data->cb, data->cb_user_data, desktop_id, g_get_user_data_dir ());
	}

	if (appinfo == NULL) {
		/* Following that, XDG_DATA_DIRS/applications, in order */
		const gchar * const *dirs;
		dirs = g_get_system_data_dirs ();
		for (guint i = 0; dirs[i] && appinfo == NULL; i++) {
			appinfo = check_directory_for_desktop_file (plugin, data->app, data->cb, data->cb_user_data, desktop_id, dirs[i]);
		}
	}

	if (appinfo == NULL) {
		g_task_return_new_error (task, GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "no appropriate desktop file found: %s",
					 desktop_id);
		return;
	}

	/* the actual launch happens in the _finish() function,
	   which should be in the main thread */
	data->appinfo = (GAppInfo *) g_steal_pointer (&appinfo);
	g_task_return_boolean (task, TRUE);
}

/**
 * gs_plugin_app_launch_filtered_async:
 * @plugin: a #GsPlugin
 * @app: a #GsApp to launch
 * @flags: a bit-or of #GsPluginLaunchFlags
 * @cb: a callback to pick the correct .desktop file
 * @cb_user_data: (closure cb) (scope async): user data for the @cb
 * @cancellable: a #GCancellable or %NULL
 * @async_callback: (not nullable): async call ready callback
 * @async_user_data: (closure async_callback) (scope async): user data for the @async_callback
 *
 * Asynchronosuly launches @app, using the .desktop file picked by the @cb.
 * This can help in case multiple versions of the @app are installed
 * in the system (like a Flatpak and RPM versions).
 * Finish the call with gs_plugin_app_launch_filtered_finish().
 *
 * The function also verifies whether the @plugin can handle the @app,
 * in a sense of gs_app_has_management_plugin(), and if not then does
 * nothing.
 *
 * Since: 47
 **/
void
gs_plugin_app_launch_filtered_async (GsPlugin *plugin,
				     GsApp *app,
				     GsPluginLaunchFlags flags,
				     GsPluginPickDesktopFileCallback cb,
				     gpointer cb_user_data,
				     GCancellable *cancellable,
				     GAsyncReadyCallback async_callback,
				     gpointer async_user_data)
{
	const gchar *desktop_id;
	g_autoptr(GTask) task = NULL;
	g_autoptr(LaunchFilteredData) data = NULL;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (cb != NULL);
	g_return_if_fail (async_callback != NULL);

	task = g_task_new (plugin, cancellable, async_callback, async_user_data);
	g_task_set_source_tag (task, gs_plugin_app_launch_filtered_async);

	/* only process this app if was created by this plugin */
	if (!gs_app_has_management_plugin (app, plugin)) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	desktop_id = get_desktop_id_to_launch (app);
	if (desktop_id == NULL) {
		g_task_return_new_error (task, GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no desktop file for app: %s",
			     gs_app_get_name (app));
		return;
	}

	data = g_new0 (LaunchFilteredData, 1);
	data->app = g_object_ref (app);
	data->cb = cb;
	data->cb_user_data = cb_user_data;

	g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) launch_filtered_data_free);
	g_task_run_in_thread (task, launch_filtered_thread);
}

/**
 * gs_plugin_app_launch_filtered_finish:
 * @plugin: a #GsPlugin
 * @result: an async result
 * @error: a #GError or %NULL
 *
 * Finishes operation started by gs_plugin_app_launch_finltered_async().
 * This function should be called from the main thread.
 *
 * Returns: whether succeeded
 *
 * Since: 47
 **/
gboolean
gs_plugin_app_launch_filtered_finish (GsPlugin *plugin,
				      GAsyncResult *result,
				      GError **error)
{
	GTask *task = G_TASK (result);
	LaunchFilteredData *data = NULL;

	g_return_val_if_fail (g_task_is_valid (task, plugin), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, gs_plugin_app_launch_filtered_async), FALSE);

	if (!g_task_propagate_boolean (task, error))
		return FALSE;

	data = g_task_get_task_data (task);
	/* the plugin does not manage the provided app */
	if (data == NULL)
		return TRUE;

	return launch_app_info (data->appinfo, error);
}

static void
weak_ref_free (GWeakRef *weak)
{
	g_weak_ref_clear (weak);
	g_free (weak);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GWeakRef, weak_ref_free)

/* @obj is a gpointer rather than a GObject* to avoid the need for casts */
static GWeakRef *
weak_ref_new (gpointer obj)
{
	g_autoptr(GWeakRef) weak = g_new0 (GWeakRef, 1);
	g_weak_ref_init (weak, obj);
	return g_steal_pointer (&weak);
}

static gboolean
gs_plugin_updates_changed_cb (gpointer user_data)
{
	GWeakRef *plugin_weak = user_data;
	g_autoptr(GsPlugin) plugin = NULL;

	plugin = g_weak_ref_get (plugin_weak);
	if (plugin != NULL)
		g_signal_emit (plugin, signals[SIGNAL_UPDATES_CHANGED], 0);

	return G_SOURCE_REMOVE;
}

/**
 * gs_plugin_updates_changed:
 * @plugin: a #GsPlugin
 *
 * Emit a signal that tells the plugin loader that the list of updates
 * may have changed.
 *
 * Since: 3.22
 **/
void
gs_plugin_updates_changed (GsPlugin *plugin)
{
	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, gs_plugin_updates_changed_cb,
			 weak_ref_new (plugin), (GDestroyNotify) weak_ref_free);
}

static gboolean
gs_plugin_reload_cb (gpointer user_data)
{
	GWeakRef *plugin_weak = user_data;
	g_autoptr(GsPlugin) plugin = NULL;

	plugin = g_weak_ref_get (plugin_weak);
	if (plugin != NULL)
		g_signal_emit (plugin, signals[SIGNAL_RELOAD], 0);

	return G_SOURCE_REMOVE;
}

/**
 * gs_plugin_reload:
 * @plugin: a #GsPlugin
 *
 * Plugins that call this function should expect that all panels will
 * reload after a small delay, causing mush flashing, wailing and
 * gnashing of teeth.
 *
 * Plugins should not call this unless absolutely required.
 *
 * Since: 3.22
 **/
void
gs_plugin_reload (GsPlugin *plugin)
{
	g_debug ("emitting %s::reload in idle", gs_plugin_get_name (plugin));
	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, gs_plugin_reload_cb,
			 weak_ref_new (plugin), (GDestroyNotify) weak_ref_free);
}

/**
 * gs_plugin_cache_lookup:
 * @plugin: a #GsPlugin
 * @key: a string
 *
 * Looks up an application object from the per-plugin cache
 *
 * Returns: (transfer full) (nullable): the #GsApp, or %NULL
 *
 * Since: 3.22
 **/
GsApp *
gs_plugin_cache_lookup (GsPlugin *plugin, const gchar *key)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	GsApp *app;
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	locker = g_mutex_locker_new (&priv->cache_mutex);
	app = g_hash_table_lookup (priv->cache, key);
	if (app == NULL)
		return NULL;
	return g_object_ref (app);
}

/**
 * gs_plugin_cache_lookup_by_state:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList to add applications to
 * @state: a #GsAppState
 *
 * Adds each cached #GsApp with state @state into the @list.
 * When the state is %GS_APP_STATE_UNKNOWN, then adds all
 * cached applications.
 *
 * Since: 40
 **/
void
gs_plugin_cache_lookup_by_state (GsPlugin *plugin,
				 GsAppList *list,
				 GsAppState state)
{
	GsPluginPrivate *priv;
	GHashTableIter iter;
	gpointer value;
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP_LIST (list));

	priv = gs_plugin_get_instance_private (plugin);
	locker = g_mutex_locker_new (&priv->cache_mutex);

	g_hash_table_iter_init (&iter, priv->cache);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GsApp *app = value;

		if (state == GS_APP_STATE_UNKNOWN ||
		    state == gs_app_get_state (app))
			gs_app_list_add (list, app);
	}
}

/**
 * gs_plugin_cache_remove:
 * @plugin: a #GsPlugin
 * @key: a key which matches
 *
 * Removes an application from the per-plugin cache.
 *
 * Since: 3.22
 **/
void
gs_plugin_cache_remove (GsPlugin *plugin, const gchar *key)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (key != NULL);

	locker = g_mutex_locker_new (&priv->cache_mutex);
	g_hash_table_remove (priv->cache, key);
}

/**
 * gs_plugin_cache_add:
 * @plugin: a #GsPlugin
 * @key: a string, or %NULL if the unique ID should be used
 * @app: a #GsApp
 *
 * Adds an application to the per-plugin cache. This is optional,
 * and the plugin can use the cache however it likes.
 *
 * Since: 3.22
 **/
void
gs_plugin_cache_add (GsPlugin *plugin, const gchar *key, GsApp *app)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->cache_mutex);

	/* the user probably doesn't want to do this */
	if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD)) {
		g_warning ("adding wildcard app %s to plugin cache",
			   gs_app_get_unique_id (app));
	}

	/* default */
	if (key == NULL)
		key = gs_app_get_unique_id (app);

	g_return_if_fail (key != NULL);

	if (g_hash_table_lookup (priv->cache, key) == app)
		return;
	g_hash_table_insert (priv->cache, g_strdup (key), g_object_ref (app));
}

/**
 * gs_plugin_cache_invalidate:
 * @plugin: a #GsPlugin
 *
 * Invalidate the per-plugin cache by marking all entries as invalid.
 * This is optional, and the plugin can evict the cache whenever it
 * likes. Using this function may mean the front-end and the plugin
 * may be operating on a different GsApp with the same cache ID.
 *
 * Most plugins do not need to call this function; if a suitable cache
 * key is being used the old cache item can remain.
 *
 * Since: 3.22
 **/
void
gs_plugin_cache_invalidate (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_PLUGIN (plugin));

	locker = g_mutex_locker_new (&priv->cache_mutex);
	g_hash_table_remove_all (priv->cache);
}

/**
 * gs_plugin_list_cached:
 * @plugin: a #GsPlugin
 *
 * Lists all apps cached by the @plugin.
 *
 * Returns: (transfer full): a #GsAppList with all currently cached apps
 *
 * Since: 46
 **/
GsAppList *
gs_plugin_list_cached (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	GsAppList *list = NULL;
	GHashTableIter iter;
	gpointer value = NULL;
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), NULL);

	locker = g_mutex_locker_new (&priv->cache_mutex);
	list = gs_app_list_new ();

	g_hash_table_iter_init (&iter, priv->cache);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GsApp *app = value;
		gs_app_list_add (list, app);
	}

	return list;
}

/**
 * gs_plugin_report_event:
 * @plugin: a #GsPlugin
 * @event: a #GsPluginEvent
 *
 * Report a non-fatal event to the UI. Plugins should not assume that a
 * specific event is actually shown to the user as it may be ignored
 * automatically.
 *
 * Since: 3.24
 **/
void
gs_plugin_report_event (GsPlugin *plugin, GsPluginEvent *event)
{
	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_PLUGIN_EVENT (event));
	g_signal_emit (plugin, signals[SIGNAL_REPORT_EVENT], 0, event);
}

/**
 * gs_plugin_set_allow_updates:
 * @plugin: a #GsPlugin
 * @allow_updates: boolean
 *
 * This allows plugins to inhibit the showing of the updates panel.
 * This will typically be used when the required permissions are not possible
 * to obtain, or when a LiveUSB image is low on space.
 *
 * By default, the updates panel is shown so plugins do not need to call this
 * function unless they called gs_plugin_set_allow_updates() with %FALSE.
 *
 * Since: 3.24
 **/
void
gs_plugin_set_allow_updates (GsPlugin *plugin, gboolean allow_updates)
{
	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_signal_emit (plugin, signals[SIGNAL_ALLOW_UPDATES], 0, allow_updates);
}

/**
 * gs_plugin_error_to_string:
 * @error: a #GsPluginError, e.g. %GS_PLUGIN_ERROR_NO_NETWORK
 *
 * Converts the enumerated error to a string.
 *
 * Returns: a string, or %NULL for invalid
 **/
const gchar *
gs_plugin_error_to_string (GsPluginError error)
{
	if (error == GS_PLUGIN_ERROR_FAILED)
		return "failed";
	if (error == GS_PLUGIN_ERROR_NOT_SUPPORTED)
		return "not-supported";
	if (error == GS_PLUGIN_ERROR_CANCELLED)
		return "cancelled";
	if (error == GS_PLUGIN_ERROR_NO_NETWORK)
		return "no-network";
	if (error == GS_PLUGIN_ERROR_NO_SECURITY)
		return "no-security";
	if (error == GS_PLUGIN_ERROR_NO_SPACE)
		return "no-space";
	if (error == GS_PLUGIN_ERROR_AUTH_REQUIRED)
		return "auth-required";
	if (error == GS_PLUGIN_ERROR_AUTH_INVALID)
		return "auth-invalid";
	if (error == GS_PLUGIN_ERROR_PLUGIN_DEPSOLVE_FAILED)
		return "plugin-depsolve-failed";
	if (error == GS_PLUGIN_ERROR_DOWNLOAD_FAILED)
		return "download-failed";
	if (error == GS_PLUGIN_ERROR_WRITE_FAILED)
		return "write-failed";
	if (error == GS_PLUGIN_ERROR_INVALID_FORMAT)
		return "invalid-format";
	if (error == GS_PLUGIN_ERROR_DELETE_FAILED)
		return "delete-failed";
	if (error == GS_PLUGIN_ERROR_RESTART_REQUIRED)
		return "restart-required";
	if (error == GS_PLUGIN_ERROR_AC_POWER_REQUIRED)
		return "ac-power-required";
	if (error == GS_PLUGIN_ERROR_BATTERY_LEVEL_TOO_LOW)
		return "battery-level-too-low";
	if (error == GS_PLUGIN_ERROR_TIMED_OUT)
		return "timed-out";
	return NULL;
}

/**
 * gs_plugin_refine_flags_to_string:
 * @refine_flags: some #GsPluginRefineFlags, e.g. %GS_PLUGIN_REFINE_FLAGS_INTERACTIVE
 *
 * Converts the refine flags to a string.
 *
 * Returns: a string
 * Since: 49
 */
gchar *
gs_plugin_refine_flags_to_string (GsPluginRefineFlags refine_flags)
{
	g_autoptr(GPtrArray) cstrs = g_ptr_array_new ();
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_INTERACTIVE) != 0)
		g_ptr_array_add (cstrs, (gpointer) "interactive");
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES) != 0)
		g_ptr_array_add (cstrs, (gpointer) "allow-packages");
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_DISABLE_FILTERING) != 0)
		g_ptr_array_add (cstrs, (gpointer) "disable-filtering");
	if (cstrs->len == 0)
		return g_strdup ("none");
	g_ptr_array_add (cstrs, NULL);
	return g_strjoinv (",", (gchar**) cstrs->pdata);
}

/**
 * gs_plugin_refine_require_flags_to_string:
 * @require_flags: some #GsPluginRefineRequireFlags, e.g. %GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE
 *
 * Converts the flags to a string.
 *
 * Returns: a string
 * Since: 49
 **/
gchar *
gs_plugin_refine_require_flags_to_string (GsPluginRefineRequireFlags require_flags)
{
	g_autoptr(GPtrArray) cstrs = g_ptr_array_new ();
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_ID)
		g_ptr_array_add (cstrs, (gpointer) "require-id");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_LICENSE)
		g_ptr_array_add (cstrs, (gpointer) "require-license");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_URL)
		g_ptr_array_add (cstrs, (gpointer) "require-url");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_DESCRIPTION)
		g_ptr_array_add (cstrs, (gpointer) "require-description");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE)
		g_ptr_array_add (cstrs, (gpointer) "require-size");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_RATING)
		g_ptr_array_add (cstrs, (gpointer) "require-rating");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_VERSION)
		g_ptr_array_add (cstrs, (gpointer) "require-version");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_HISTORY)
		g_ptr_array_add (cstrs, (gpointer) "require-history");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION)
		g_ptr_array_add (cstrs, (gpointer) "require-setup-action");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_DETAILS)
		g_ptr_array_add (cstrs, (gpointer) "require-update-details");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN)
		g_ptr_array_add (cstrs, (gpointer) "require-origin");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_RELATED)
		g_ptr_array_add (cstrs, (gpointer) "require-related");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_ADDONS)
		g_ptr_array_add (cstrs, (gpointer) "require-addons");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_SEVERITY)
		g_ptr_array_add (cstrs, (gpointer) "require-update-severity");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPGRADE_REMOVED)
		g_ptr_array_add (cstrs, (gpointer) "require-upgrade-removed");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_PROVENANCE)
		g_ptr_array_add (cstrs, (gpointer) "require-provenance");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEWS)
		g_ptr_array_add (cstrs, (gpointer) "require-reviews");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEW_RATINGS)
		g_ptr_array_add (cstrs, (gpointer) "require-review-ratings");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_ICON)
		g_ptr_array_add (cstrs, (gpointer) "require-icon");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_PERMISSIONS)
		g_ptr_array_add (cstrs, (gpointer) "require-permissions");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN_HOSTNAME)
		g_ptr_array_add (cstrs, (gpointer) "require-origin-hostname");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN_UI)
		g_ptr_array_add (cstrs, (gpointer) "require-origin-ui");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_RUNTIME)
		g_ptr_array_add (cstrs, (gpointer) "require-runtime");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_SCREENSHOTS)
		g_ptr_array_add (cstrs, (gpointer) "require-screenshots");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_CATEGORIES)
		g_ptr_array_add (cstrs, (gpointer) "require-categories");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_PROJECT_GROUP)
		g_ptr_array_add (cstrs, (gpointer) "require-project-group");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_DEVELOPER_NAME)
		g_ptr_array_add (cstrs, (gpointer) "require-developer-name");
	if (require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_KUDOS)
		g_ptr_array_add (cstrs, (gpointer) "require-kudos");
	if (cstrs->len == 0)
		return g_strdup ("none");
	g_ptr_array_add (cstrs, NULL);
	return g_strjoinv (",", (gchar**) cstrs->pdata);
}

static void
gs_plugin_constructed (GObject *object)
{
	GsPlugin *plugin = GS_PLUGIN (object);
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	G_OBJECT_CLASS (gs_plugin_parent_class)->constructed (object);

	/* Check all required properties have been set. */
	g_assert (priv->session_bus_connection != NULL);
	g_assert (priv->system_bus_connection != NULL);
}

static void
gs_plugin_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsPlugin *plugin = GS_PLUGIN (object);
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	switch ((GsPluginProperty) prop_id) {
	case PROP_NAME:
		/* Construct only */
		g_assert (priv->name == NULL);
		priv->name = g_value_dup_string (value);
		break;
	case PROP_SCALE:
		gs_plugin_set_scale (plugin, g_value_get_uint (value));
		break;
	case PROP_SESSION_BUS_CONNECTION:
		/* Construct only */
		g_assert (priv->session_bus_connection == NULL);
		priv->session_bus_connection = g_value_dup_object (value);
		break;
	case PROP_SYSTEM_BUS_CONNECTION:
		/* Construct only */
		g_assert (priv->system_bus_connection == NULL);
		priv->system_bus_connection = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsPlugin *plugin = GS_PLUGIN (object);
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	switch ((GsPluginProperty) prop_id) {
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_SCALE:
		g_value_set_uint (value, gs_plugin_get_scale (plugin));
		break;
	case PROP_SESSION_BUS_CONNECTION:
		g_value_set_object (value, priv->session_bus_connection);
		break;
	case PROP_SYSTEM_BUS_CONNECTION:
		g_value_set_object (value, priv->system_bus_connection);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_class_init (GsPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = gs_plugin_constructed;
	object_class->set_property = gs_plugin_set_property;
	object_class->get_property = gs_plugin_get_property;
	object_class->dispose = gs_plugin_dispose;
	object_class->finalize = gs_plugin_finalize;

	/**
	 * GsPlugin:name: (not nullable)
	 *
	 * Name of the plugin.
	 *
	 * This can be used to identify the plugin in log messages, for example.
	 *
	 * This must be set at construction time and will not be %NULL
	 * afterwards. It is automatically set from the `.so` filename by
	 * gs_plugin_create().
	 *
	 * Since: 49
	 */
	obj_props[PROP_NAME] =
		g_param_spec_string ("name", NULL, NULL,
				     NULL,
				     G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPlugin:scale:
	 *
	 * The window scale factor.
	 *
	 * These may change during the plugin’s lifetime.
	 *
	 * Since: 48
	 */
	obj_props[PROP_SCALE] =
		g_param_spec_uint ("scale", NULL, NULL,
				   1, G_MAXUINT, 1,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPlugin:session-bus-connection: (not nullable)
	 *
	 * A connection to the D-Bus session bus.
	 *
	 * This must be set at construction time and will not be %NULL
	 * afterwards.
	 *
	 * Since: 43
	 */
	obj_props[PROP_SESSION_BUS_CONNECTION] =
		g_param_spec_object ("session-bus-connection", NULL, NULL,
				     G_TYPE_DBUS_CONNECTION,
				     G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPlugin:system-bus-connection: (not nullable)
	 *
	 * A connection to the D-Bus system bus.
	 *
	 * This must be set at construction time and will not be %NULL
	 * afterwards.
	 *
	 * Since: 43
	 */
	obj_props[PROP_SYSTEM_BUS_CONNECTION] =
		g_param_spec_object ("system-bus-connection", NULL, NULL,
				     G_TYPE_DBUS_CONNECTION,
				     G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	signals [SIGNAL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginClass, updates_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals [SIGNAL_RELOAD] =
		g_signal_new ("reload",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginClass, reload),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals [SIGNAL_REPORT_EVENT] =
		g_signal_new ("report-event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginClass, report_event),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, GS_TYPE_PLUGIN_EVENT);

	signals [SIGNAL_ALLOW_UPDATES] =
		g_signal_new ("allow-updates",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginClass, allow_updates),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	signals [SIGNAL_BASIC_AUTH_START] =
		g_signal_new ("basic-auth-start",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginClass, basic_auth_start),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_POINTER);

	signals [SIGNAL_REPOSITORY_CHANGED] =
		g_signal_new ("repository-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginClass, repository_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, GS_TYPE_APP);

	signals [SIGNAL_ASK_UNTRUSTED] =
		g_signal_new ("ask-untrusted",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginClass, ask_untrusted),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_BOOLEAN, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
}

static void
gs_plugin_init (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	guint i;

	for (i = 0; i < GS_PLUGIN_RULE_LAST; i++)
		priv->rules[i] = g_ptr_array_new_with_free_func (g_free);

	priv->enabled = TRUE;
	priv->scale = 1;
	priv->cache = g_hash_table_new_full ((GHashFunc) as_utils_data_id_hash,
					     (GEqualFunc) as_utils_data_id_equal,
					     g_free,
					     (GDestroyNotify) g_object_unref);
	priv->vfuncs = g_hash_table_new_full (g_str_hash, g_str_equal,
					      g_free, NULL);
	g_mutex_init (&priv->cache_mutex);
	g_mutex_init (&priv->timer_mutex);
	g_mutex_init (&priv->vfuncs_mutex);
}

typedef struct {
	GWeakRef  plugin_weak;  /* (owned) (element-type GsPlugin) */
	GsApp	 *repository;  /* (owned) */
} GsPluginRepositoryChangedHelper;

static void
gs_plugin_repository_changed_helper_free (GsPluginRepositoryChangedHelper *helper)
{
	g_clear_object (&helper->repository);
	g_weak_ref_clear (&helper->plugin_weak);
	g_slice_free (GsPluginRepositoryChangedHelper, helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginRepositoryChangedHelper, gs_plugin_repository_changed_helper_free)

static gboolean
gs_plugin_repository_changed_cb (gpointer user_data)
{
	GsPluginRepositoryChangedHelper *helper = user_data;
	g_autoptr(GsPlugin) plugin = NULL;

	plugin = g_weak_ref_get (&helper->plugin_weak);
	if (plugin != NULL)
		g_signal_emit (plugin,
			       signals[SIGNAL_REPOSITORY_CHANGED], 0,
			       helper->repository);

	return G_SOURCE_REMOVE;
}

/**
 * gs_plugin_repository_changed:
 * @plugin: a #GsPlugin
 * @repository: a #GsApp representing the repository
 *
 * Emit the "repository-changed" signal in the main thread.
 *
 * Since: 40
 **/
void
gs_plugin_repository_changed (GsPlugin *plugin,
			      GsApp *repository)
{
	g_autoptr(GsPluginRepositoryChangedHelper) helper = NULL;
	g_autoptr(GSource) idle_source = NULL;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP (repository));

	helper = g_slice_new0 (GsPluginRepositoryChangedHelper);
	g_weak_ref_init (&helper->plugin_weak, plugin);
	helper->repository = g_object_ref (repository);

	idle_source = g_idle_source_new ();
	g_source_set_callback (idle_source, gs_plugin_repository_changed_cb, g_steal_pointer (&helper), (GDestroyNotify) gs_plugin_repository_changed_helper_free);
	g_source_attach (idle_source, NULL);
}

/**
 * gs_plugin_update_cache_state_for_repository:
 * @plugin: a #GsPlugin
 * @repository: a #GsApp representing a repository, which changed
 *
 * Update state of the all cached #GsApp instances related
 * to the @repository.
 *
 * Since: 40
 **/
void
gs_plugin_update_cache_state_for_repository (GsPlugin *plugin,
					     GsApp *repository)
{
	GsPluginPrivate *priv;
	GHashTableIter iter;
	g_autoptr(GMutexLocker) locker = NULL;
	g_autoptr(GsPlugin) repo_plugin = NULL;
	gpointer value;
	const gchar *repo_id;
	GsAppState repo_state;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP (repository));

	priv = gs_plugin_get_instance_private (plugin);
	repo_id = gs_app_get_id (repository);
	repo_state = gs_app_get_state (repository);
	repo_plugin = gs_app_dup_management_plugin (repository);

	locker = g_mutex_locker_new (&priv->cache_mutex);

	g_hash_table_iter_init (&iter, priv->cache);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GsApp *app = value;
		GsAppState app_state = gs_app_get_state (app);
		g_autoptr(GsPlugin) app_plugin = gs_app_dup_management_plugin (app);

		if (app_plugin != repo_plugin ||
		    gs_app_get_scope (app) != gs_app_get_scope (repository) ||
		    gs_app_get_bundle_kind (app) != gs_app_get_bundle_kind (repository))
			continue;

		if (((app_state == GS_APP_STATE_AVAILABLE &&
		    repo_state != GS_APP_STATE_INSTALLED) ||
		    (app_state == GS_APP_STATE_UNAVAILABLE &&
		    repo_state == GS_APP_STATE_INSTALLED)) &&
		    g_strcmp0 (gs_app_get_origin (app), repo_id) == 0) {
			/* First reset the state, because move from 'available' to 'unavailable' is not correct */
			gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
			gs_app_set_state (app, repo_state == GS_APP_STATE_INSTALLED ? GS_APP_STATE_AVAILABLE : GS_APP_STATE_UNAVAILABLE);
		}
	}
}

/**
 * gs_plugin_ask_untrusted:
 * @plugin: a #GsPlugin
 * @title: the title for the question
 * @msg: the message for the question
 * @details: (nullable): the detailed error message, or %NULL for none
 * @accept_label: (nullable): a label of the 'accept' button, or %NULL to use 'Accept'
 *
 * Asks the user whether he/she accepts an untrusted package install/download/update,
 * as described by @title and @msg, eventually with the @details.
 *
 * Note: This is a blocking call and can be called only from the main/GUI thread.
 *
 * Returns: whether the user accepted the question
 *
 * Since: 42
 **/
gboolean
gs_plugin_ask_untrusted (GsPlugin *plugin,
			 const gchar *title,
			 const gchar *msg,
			 const gchar *details,
			 const gchar *accept_label)
{
	gboolean accepts = FALSE;
	g_signal_emit (plugin,
		       signals[SIGNAL_ASK_UNTRUSTED], 0,
		       title,
		       msg,
		       details,
		       accept_label,
		       &accepts);
	return accepts;
}

/**
 * gs_plugin_get_session_bus_connection:
 * @self: a #GsPlugin
 *
 * Get the D-Bus session bus connection in use by the plugin.
 *
 * Returns: (transfer none) (not nullable): a D-Bus connection
 * Since: 43
 */
GDBusConnection *
gs_plugin_get_session_bus_connection (GsPlugin *self)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (self);

	g_return_val_if_fail (GS_IS_PLUGIN (self), NULL);

	return priv->session_bus_connection;
}

/**
 * gs_plugin_get_system_bus_connection:
 * @self: a #GsPlugin
 *
 * Get the D-Bus system bus connection in use by the plugin.
 *
 * Returns: (transfer none) (not nullable): a D-Bus connection
 * Since: 43
 */
GDBusConnection *
gs_plugin_get_system_bus_connection (GsPlugin *self)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (self);

	g_return_val_if_fail (GS_IS_PLUGIN (self), NULL);

	return priv->system_bus_connection;
}
