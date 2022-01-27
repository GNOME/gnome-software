/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
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

#ifdef USE_VALGRIND
#include <valgrind.h>
#endif

#include "gs-app-list-private.h"
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
	GsPluginFlags		 flags;
	SoupSession		*soup_session;
	GPtrArray		*rules[GS_PLUGIN_RULE_LAST];
	GHashTable		*vfuncs;		/* string:pointer */
	GMutex			 vfuncs_mutex;
	gboolean		 enabled;
	guint			 interactive_cnt;
	GMutex			 interactive_mutex;
	gchar			*language;		/* allow-none */
	gchar			*name;
	gchar			*appstream_id;
	guint			 scale;
	guint			 order;
	guint			 priority;
	guint			 timer_id;
	GMutex			 timer_mutex;
	GNetworkMonitor		*network_monitor;
} GsPluginPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsPlugin, gs_plugin, G_TYPE_OBJECT)

G_DEFINE_QUARK (gs-plugin-error-quark, gs_plugin_error)

enum {
	PROP_0,
	PROP_FLAGS,
	PROP_LAST
};

enum {
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_RELOAD,
	SIGNAL_REPORT_EVENT,
	SIGNAL_ALLOW_UPDATES,
	SIGNAL_BASIC_AUTH_START,
	SIGNAL_REPOSITORY_CHANGED,
	SIGNAL_ASK_UNTRUSTED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

typedef const gchar	**(*GsPluginGetDepsFunc)	(GsPlugin	*plugin);

/**
 * gs_plugin_status_to_string:
 * @status: a #GsPluginStatus, e.g. %GS_PLUGIN_STATUS_DOWNLOADING
 *
 * Converts the #GsPluginStatus enum to a string.
 *
 * Returns: the string representation, or "unknown"
 *
 * Since: 3.22
 **/
const gchar *
gs_plugin_status_to_string (GsPluginStatus status)
{
	if (status == GS_PLUGIN_STATUS_WAITING)
		return "waiting";
	if (status == GS_PLUGIN_STATUS_FINISHED)
		return "finished";
	if (status == GS_PLUGIN_STATUS_SETUP)
		return "setup";
	if (status == GS_PLUGIN_STATUS_DOWNLOADING)
		return "downloading";
	if (status == GS_PLUGIN_STATUS_QUERYING)
		return "querying";
	if (status == GS_PLUGIN_STATUS_INSTALLING)
		return "installing";
	if (status == GS_PLUGIN_STATUS_REMOVING)
		return "removing";
	return "unknown";
}

/**
 * gs_plugin_set_name:
 * @plugin: a #GsPlugin
 * @name: a plugin name
 *
 * Sets the name of the plugin.
 *
 * Plugins are not required to set the plugin name as it is automatically set
 * from the `.so` filename.
 *
 * Since: 3.26
 **/
void
gs_plugin_set_name (GsPlugin *plugin, const gchar *name)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	if (priv->name != NULL)
		g_free (priv->name);
	priv->name = g_strdup (name);
}

/**
 * gs_plugin_create:
 * @filename: an absolute filename
 * @error: a #GError, or %NULL
 *
 * Creates a new plugin from an external module.
 *
 * Returns: the #GsPlugin or %NULL
 *
 * Since: 3.22
 **/
GsPlugin *
gs_plugin_create (const gchar *filename, GError **error)
{
	GsPlugin *plugin = NULL;
	GsPluginPrivate *priv;
	g_autofree gchar *basename = NULL;
	GModule *module = NULL;
	GType (*query_type_function) (void) = NULL;
	GType plugin_type;

	/* get the plugin name from the basename */
	basename = g_path_get_basename (filename);
	if (!g_str_has_prefix (basename, "libgs_plugin_")) {
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

	plugin_type = query_type_function ();
	g_assert (g_type_is_a (plugin_type, GS_TYPE_PLUGIN));

	plugin = g_object_new (plugin_type, NULL);
	priv = gs_plugin_get_instance_private (plugin);
	priv->module = g_steal_pointer (&module);

	gs_plugin_set_name (plugin, basename + 13);
	return plugin;
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
	g_free (priv->appstream_id);
	g_free (priv->language);
	if (priv->soup_session != NULL)
		g_object_unref (priv->soup_session);
	if (priv->network_monitor != NULL)
		g_object_unref (priv->network_monitor);
	g_hash_table_unref (priv->cache);
	g_hash_table_unref (priv->vfuncs);
	g_mutex_clear (&priv->cache_mutex);
	g_mutex_clear (&priv->interactive_mutex);
	g_mutex_clear (&priv->timer_mutex);
	g_mutex_clear (&priv->vfuncs_mutex);
#ifndef RUNNING_ON_VALGRIND
	if (priv->module != NULL)
		g_module_close (priv->module);
#endif

	G_OBJECT_CLASS (gs_plugin_parent_class)->finalize (object);
}

/**
 * gs_plugin_get_symbol: (skip)
 * @plugin: a #GsPlugin
 * @function_name: a symbol name
 *
 * Gets the symbol from the module that backs the plugin. If the plugin is not
 * enabled then no symbol is returned.
 *
 * Returns: the pointer to the symbol, or %NULL
 *
 * Since: 3.22
 **/
gpointer
gs_plugin_get_symbol (GsPlugin *plugin, const gchar *function_name)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	gpointer func = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->vfuncs_mutex);

	g_return_val_if_fail (function_name != NULL, NULL);

	/* disabled plugins shouldn't be checked */
	if (!priv->enabled)
		return NULL;

	/* look up the symbol from the cache */
	if (g_hash_table_lookup_extended (priv->vfuncs, function_name, NULL, &func))
		return func;

	/* look up the symbol using the elf headers */
	g_module_symbol (priv->module, function_name, &func);
	g_hash_table_insert (priv->vfuncs, g_strdup (function_name), func);

	return func;
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

void
gs_plugin_interactive_inc (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->interactive_mutex);
	priv->interactive_cnt++;
	gs_plugin_add_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);
}

void
gs_plugin_interactive_dec (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->interactive_mutex);
	if (priv->interactive_cnt > 0)
		priv->interactive_cnt--;
	if (priv->interactive_cnt == 0)
		gs_plugin_remove_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE);
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
 * gs_plugin_get_appstream_id:
 * @plugin: a #GsPlugin
 *
 * Gets the plugin AppStream ID.
 *
 * Returns: a string, e.g. `org.gnome.Software.Plugin.Epiphany`
 *
 * Since: 3.24
 **/
const gchar *
gs_plugin_get_appstream_id (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->appstream_id;
}

/**
 * gs_plugin_set_appstream_id:
 * @plugin: a #GsPlugin
 * @appstream_id: an appstream ID, e.g. `org.gnome.Software.Plugin.Epiphany`
 *
 * Sets the plugin AppStream ID.
 *
 * Since: 3.24
 **/
void
gs_plugin_set_appstream_id (GsPlugin *plugin, const gchar *appstream_id)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_free (priv->appstream_id);
	priv->appstream_id = g_strdup (appstream_id);
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
	priv->scale = scale;
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
 * gs_plugin_get_soup_session:
 * @plugin: a #GsPlugin
 *
 * Gets the soup session that this plugin can use when downloading.
 *
 * Returns: the #SoupSession
 *
 * Since: 3.22
 **/
SoupSession *
gs_plugin_get_soup_session (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->soup_session;
}

/**
 * gs_plugin_set_soup_session:
 * @plugin: a #GsPlugin
 * @soup_session: a #SoupSession
 *
 * Sets the soup session that this plugin will use when downloading.
 *
 * Since: 3.22
 **/
void
gs_plugin_set_soup_session (GsPlugin *plugin, SoupSession *soup_session)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_set_object (&priv->soup_session, soup_session);
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
 * gs_plugin_has_flags:
 * @plugin: a #GsPlugin
 * @flags: a #GsPluginFlags, e.g. %GS_PLUGIN_FLAGS_INTERACTIVE
 *
 * Finds out if a plugin has a specific flag set.
 *
 * Returns: TRUE if the flag is set
 *
 * Since: 3.22
 **/
gboolean
gs_plugin_has_flags (GsPlugin *plugin, GsPluginFlags flags)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return (priv->flags & flags) > 0;
}

/**
 * gs_plugin_add_flags:
 * @plugin: a #GsPlugin
 * @flags: a #GsPluginFlags, e.g. %GS_PLUGIN_FLAGS_INTERACTIVE
 *
 * Adds specific flags to the plugin.
 *
 * Since: 3.22
 **/
void
gs_plugin_add_flags (GsPlugin *plugin, GsPluginFlags flags)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->flags |= flags;
}

/**
 * gs_plugin_remove_flags:
 * @plugin: a #GsPlugin
 * @flags: a #GsPluginFlags, e.g. %GS_PLUGIN_FLAGS_INTERACTIVE
 *
 * Removes specific flags from the plugin.
 *
 * Since: 3.22
 **/
void
gs_plugin_remove_flags (GsPlugin *plugin, GsPluginFlags flags)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->flags &= ~flags;
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
	GsApp		*app;
	GsPluginStatus	 status;
	guint		 percentage;
} GsPluginStatusHelper;

static gboolean
gs_plugin_status_update_cb (gpointer user_data)
{
	GsPluginStatusHelper *helper = (GsPluginStatusHelper *) user_data;
	g_signal_emit (helper->plugin,
		       signals[SIGNAL_STATUS_CHANGED], 0,
		       helper->app,
		       helper->status);
	if (helper->app != NULL)
		g_object_unref (helper->app);
	g_slice_free (GsPluginStatusHelper, helper);
	return FALSE;
}

/**
 * gs_plugin_status_update:
 * @plugin: a #GsPlugin
 * @app: a #GsApp, or %NULL
 * @status: a #GsPluginStatus, e.g. %GS_PLUGIN_STATUS_DOWNLOADING
 *
 * Update the state of the plugin so any UI can be updated.
 *
 * Since: 3.22
 **/
void
gs_plugin_status_update (GsPlugin *plugin, GsApp *app, GsPluginStatus status)
{
	GsPluginStatusHelper *helper;
	g_autoptr(GSource) idle_source = NULL;

	helper = g_slice_new0 (GsPluginStatusHelper);
	helper->plugin = plugin;
	helper->status = status;
	if (app != NULL)
		helper->app = g_object_ref (app);
	idle_source = g_idle_source_new ();
	g_source_set_callback (idle_source, gs_plugin_status_update_cb, helper, NULL);
	g_source_attach (idle_source, NULL);
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

static gboolean
gs_plugin_app_launch_cb (gpointer user_data)
{
	GAppInfo *appinfo = (GAppInfo *) user_data;
	GdkDisplay *display;
	g_autoptr(GAppLaunchContext) context = NULL;
	g_autoptr(GError) error = NULL;

	display = gdk_display_get_default ();
	context = G_APP_LAUNCH_CONTEXT (gdk_display_get_app_launch_context (display));
	if (!g_app_info_launch (appinfo, NULL, context, &error))
		g_warning ("Failed to launch: %s", error->message);

	return FALSE;
}

/**
 * gs_plugin_app_launch:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @error: a #GError, or %NULL
 *
 * Launches the application using #GAppInfo.
 *
 * Returns: %TRUE for success
 *
 * Since: 3.22
 **/
gboolean
gs_plugin_app_launch (GsPlugin *plugin, GsApp *app, GError **error)
{
	const gchar *desktop_id;
	g_autoptr(GAppInfo) appinfo = NULL;

	desktop_id = gs_app_get_launchable (app, AS_LAUNCHABLE_KIND_DESKTOP_ID);
	if (desktop_id == NULL)
		desktop_id = gs_app_get_id (app);
	if (desktop_id == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no such desktop file: %s",
			     desktop_id);
		return FALSE;
	}
	appinfo = G_APP_INFO (gs_utils_get_desktop_app_info (desktop_id));
	if (appinfo == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no such desktop file: %s",
			     desktop_id);
		return FALSE;
	}
	g_idle_add_full (G_PRIORITY_DEFAULT,
			 gs_plugin_app_launch_cb,
			 g_object_ref (appinfo),
			 (GDestroyNotify) g_object_unref);
	return TRUE;
}

static gboolean
gs_plugin_updates_changed_cb (gpointer user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);
	g_signal_emit (plugin, signals[SIGNAL_UPDATES_CHANGED], 0);
	return FALSE;
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
	g_idle_add (gs_plugin_updates_changed_cb, plugin);
}

static gboolean
gs_plugin_reload_cb (gpointer user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);
	g_signal_emit (plugin, signals[SIGNAL_RELOAD], 0);
	return FALSE;
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
	g_debug ("emitting ::reload in idle");
	g_idle_add (gs_plugin_reload_cb, plugin);
}

#if SOUP_CHECK_VERSION(3, 0, 0)
static GBytes * /* (transfer full) */
gs_plugin_download_with_progress (GsPlugin *plugin,
				  GsApp *app,
				  SoupMessage *msg,
				  GInputStream *stream,
				  GCancellable *cancellable,
				  GError **error)
{
	g_autoptr(GByteArray) byte_array = NULL;
	gsize nread, total_read, expected_length;
	guint8 buffer[16384];
	gboolean success = FALSE;

	if (stream == NULL || !SOUP_STATUS_IS_SUCCESSFUL (soup_message_get_status (msg)) ||
	    g_cancellable_is_cancelled (cancellable))
		return NULL;

	byte_array = g_byte_array_new ();

	total_read = 0;
	expected_length = soup_message_headers_get_content_length (soup_message_get_response_headers (msg));

	while (g_input_stream_read_all (stream, buffer, sizeof (buffer), &nread, cancellable, error)) {
		if (!nread) {
			success = TRUE;
			break;
		}
		g_byte_array_append (byte_array, buffer, nread);
		total_read += nread;
		if (app != NULL && expected_length > 0) {
			/* calculate percentage */
			guint percentage = (guint) ((100 * total_read) / expected_length);
			g_debug ("%s progress: %u%%", gs_app_get_id (app), percentage);
			gs_app_set_progress (app, percentage);
			gs_plugin_status_update (plugin, app, GS_PLUGIN_STATUS_DOWNLOADING);
		}
		if (nread < sizeof (buffer)) {
			success = TRUE;
			break;
		}
	}

	if (success) {
		GBytes *bytes = g_byte_array_free_to_bytes (byte_array);
		byte_array = NULL;
		return bytes;
	}

	return NULL;
}
#else
typedef struct {
	GsPlugin	*plugin;
	GsApp		*app;
	GCancellable	*cancellable;
} GsPluginDownloadHelper;

static void
gs_plugin_download_chunk_cb (SoupMessage *msg, SoupBuffer *chunk,
			     GsPluginDownloadHelper *helper)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (helper->plugin);
	guint percentage;
	goffset header_size;
	goffset body_length;

	/* cancelled? */
	if (g_cancellable_is_cancelled (helper->cancellable)) {
		g_debug ("cancelling download of %s",
			 gs_app_get_id (helper->app));
		soup_session_cancel_message (priv->soup_session,
					     msg,
					     SOUP_STATUS_CANCELLED);
		return;
	}

	/* if it's returning "Found" or an error, ignore the percentage */
	if (msg->status_code != SOUP_STATUS_OK) {
		g_debug ("ignoring status code %u (%s)",
			 msg->status_code, msg->reason_phrase);
		return;
	}

	/* get data */
	body_length = msg->response_body->length;
	header_size = soup_message_headers_get_content_length (msg->response_headers);

	/* size is not known */
	if (header_size < body_length)
		return;

	/* calculate percentage */
	percentage = (guint) ((100 * body_length) / header_size);
	g_debug ("%s progress: %u%%", gs_app_get_id (helper->app), percentage);
	gs_app_set_progress (helper->app, percentage);
	gs_plugin_status_update (helper->plugin,
				 helper->app,
				 GS_PLUGIN_STATUS_DOWNLOADING);
}
#endif

/**
 * gs_plugin_download_data:
 * @plugin: a #GsPlugin
 * @app: a #GsApp, or %NULL
 * @uri: a remote URI
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Downloads data.
 *
 * Returns: the downloaded data, or %NULL
 *
 * Since: 3.22
 **/
GBytes *
gs_plugin_download_data (GsPlugin *plugin,
			 GsApp *app,
			 const gchar *uri,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
#if SOUP_CHECK_VERSION(3, 0, 0)
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error_local = NULL;
	GBytes *bytes;
#else
	GsPluginDownloadHelper helper;
	guint status_code;
#endif
	g_autoptr(SoupMessage) msg = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* local */
	if (g_str_has_prefix (uri, "file://")) {
		gsize length = 0;
		g_autofree gchar *contents = NULL;
#if !SOUP_CHECK_VERSION(3, 0, 0)
		g_autoptr(GError) error_local = NULL;
#endif
		g_debug ("copying %s from plugin %s", uri, priv->name);
		if (!g_file_get_contents (uri + 7, &contents, &length, &error_local)) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
				     "failed to copy %s: %s",
				     uri, error_local->message);
			return NULL;
		}
		return g_bytes_new (contents, length);
	}

	/* remote */
	g_debug ("downloading %s from plugin %s", uri, priv->name);
	msg = soup_message_new (SOUP_METHOD_GET, uri);
	if (msg == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
			     "failed to parse URI %s", uri);
		return NULL;
	}
#if SOUP_CHECK_VERSION(3, 0, 0)
	stream = soup_session_send (priv->soup_session, msg, cancellable, &error_local);
	bytes = gs_plugin_download_with_progress (plugin, app, msg, stream, cancellable, &error_local);
	if (bytes == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_propagate_error (error, error_local);
		} else {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
				     "failed to download %s: %s",
				     uri, error_local ? error_local->message : "Unknown error");
		}
	}
	return bytes;
#else
	if (app != NULL) {
		helper.plugin = plugin;
		helper.app = app;
		helper.cancellable = cancellable;
		g_signal_connect (msg, "got-chunk",
				  G_CALLBACK (gs_plugin_download_chunk_cb),
				  &helper);
	}
	status_code = soup_session_send_message (priv->soup_session, msg);
	if (status_code != SOUP_STATUS_OK) {
		g_autoptr(GString) str = g_string_new (NULL);
		g_string_append (str, soup_status_get_phrase (status_code));
		if (msg->response_body->data != NULL) {
			g_string_append (str, ": ");
			g_string_append (str, msg->response_body->data);
		}
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
			     "failed to download %s: %s",
			     uri, str->str);
		return NULL;
	}
	return g_bytes_new (msg->response_body->data,
			    (gsize) msg->response_body->length);
#endif
}

/**
 * gs_plugin_download_file:
 * @plugin: a #GsPlugin
 * @app: a #GsApp, or %NULL
 * @uri: a remote URI
 * @filename: a local filename
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Downloads data and saves it to a file.
 *
 * Returns: %TRUE for success
 *
 * Since: 3.22
 **/
gboolean
gs_plugin_download_file (GsPlugin *plugin,
			 GsApp *app,
			 const gchar *uri,
			 const gchar *filename,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
#if SOUP_CHECK_VERSION(3, 0, 0)
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GBytes) bytes = NULL;
#else
	GsPluginDownloadHelper helper;
#endif
	const gchar *new_etag;
	guint status_code;
	gconstpointer downloaded_data = NULL;
	gsize downloaded_data_length = 0;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* local */
	if (g_str_has_prefix (uri, "file://")) {
		gsize length = 0;
		g_autofree gchar *contents = NULL;
		g_debug ("copying %s from plugin %s", uri, priv->name);
		if (!g_file_get_contents (uri + 7, &contents, &length, &error_local)) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
				     "failed to copy %s: %s",
				     uri, error_local->message);
			return FALSE;
		}
		if (!g_file_set_contents (filename, contents, length, &error_local)) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_WRITE_FAILED,
				     "Failed to save file: %s",
				     error_local->message);
			return FALSE;
		}
		return TRUE;
	}

	/* remote */
	g_debug ("downloading %s to %s from plugin %s", uri, filename, priv->name);
	msg = soup_message_new (SOUP_METHOD_GET, uri);
	if (msg == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
			     "failed to parse URI %s", uri);
		return FALSE;
	}
	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		g_autofree gchar *last_etag = gs_utils_get_file_etag (filename, cancellable);
		if (last_etag != NULL && *last_etag != '\0') {
#if SOUP_CHECK_VERSION(3, 0, 0)
			soup_message_headers_append (soup_message_get_request_headers (msg), "If-None-Match", last_etag);
#else
			soup_message_headers_append (msg->request_headers, "If-None-Match", last_etag);
#endif
		}
	}
#if SOUP_CHECK_VERSION(3, 0, 0)
	stream = soup_session_send (priv->soup_session, msg, cancellable, &error_local);
	bytes = gs_plugin_download_with_progress (plugin, app, msg, stream, cancellable, &error_local);
	if (bytes != NULL)
		downloaded_data = g_bytes_get_data (bytes, &downloaded_data_length);
	status_code = soup_message_get_status (msg);
#else
	if (app != NULL) {
		helper.plugin = plugin;
		helper.app = app;
		helper.cancellable = cancellable;
		g_signal_connect (msg, "got-chunk",
				  G_CALLBACK (gs_plugin_download_chunk_cb),
				  &helper);
	}
	status_code = soup_session_send_message (priv->soup_session, msg);
	downloaded_data = msg->response_body ? msg->response_body->data : NULL;
	downloaded_data_length = msg->response_body ? msg->response_body->length : 0;
#endif
	if (status_code == SOUP_STATUS_NOT_MODIFIED)
		return TRUE;
	if (status_code != SOUP_STATUS_OK) {
		g_autoptr(GString) str = g_string_new (NULL);
		g_string_append (str, soup_status_get_phrase (status_code));
#if SOUP_CHECK_VERSION(3, 0, 0)
		if (error_local != NULL) {
			g_string_append (str, ": ");
			g_string_append (str, error_local->message);
		}
#endif
		if (downloaded_data != NULL && downloaded_data_length > 0) {
			g_string_append (str, ": ");
			g_string_append_len (str, downloaded_data, downloaded_data_length);
		}
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
			     "failed to download %s: %s",
			     uri, str->str);
		return FALSE;
	}
#if SOUP_CHECK_VERSION(3, 0, 0)
	g_clear_error (&error_local);
#endif
	if (!gs_mkdir_parent (filename, error))
		return FALSE;
	if (!g_file_set_contents (filename, downloaded_data, downloaded_data_length, &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_WRITE_FAILED,
			     "Failed to save file: %s",
			     error_local->message);
		return FALSE;
	}
#if SOUP_CHECK_VERSION(3, 0, 0)
	new_etag = soup_message_headers_get_one (soup_message_get_response_headers (msg), "ETag");
#else
	new_etag = soup_message_headers_get_one (msg->response_headers, "ETag");
#endif
	if (new_etag != NULL && *new_etag == '\0')
		new_etag = NULL;
	gs_utils_set_file_etag (filename, new_etag, cancellable);
	return TRUE;
}

static gchar *
gs_plugin_download_rewrite_resource_uri (GsPlugin *plugin,
					 GsApp *app,
					 const gchar *uri,
					 GCancellable *cancellable,
					 GError **error)
{
	g_autofree gchar *cachefn = NULL;

	/* local files */
	if (g_str_has_prefix (uri, "file://"))
		uri += 7;
	if (g_str_has_prefix (uri, "/")) {
		if (!g_file_test (uri, G_FILE_TEST_EXISTS)) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "Failed to find file: %s", uri);
			return NULL;
		}
		return g_strdup (uri);
	}

	/* get cache location */
	cachefn = gs_utils_get_cache_filename ("cssresource", uri,
					       GS_UTILS_CACHE_FLAG_WRITEABLE |
					       GS_UTILS_CACHE_FLAG_USE_HASH |
					       GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
					       error);
	if (cachefn == NULL)
		return NULL;

	/* already exists */
	if (g_file_test (cachefn, G_FILE_TEST_EXISTS))
		return g_steal_pointer (&cachefn);

	/* download */
	if (!gs_plugin_download_file (plugin, app, uri, cachefn,
				      cancellable, error)) {
		return NULL;
	}
	return g_steal_pointer (&cachefn);
}

/**
 * gs_plugin_download_rewrite_resource:
 * @plugin: a #GsPlugin
 * @app: a #GsApp, or %NULL
 * @resource: the CSS resource
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Downloads remote assets and rewrites a CSS resource to use cached local URIs.
 *
 * Returns: %TRUE for success
 *
 * Since: 3.26
 **/
gchar *
gs_plugin_download_rewrite_resource (GsPlugin *plugin,
				     GsApp *app,
				     const gchar *resource,
				     GCancellable *cancellable,
				     GError **error)
{
	guint start = 0;
	g_autoptr(GString) resource_str = g_string_new (resource);
	g_autoptr(GString) str = g_string_new (NULL);

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), NULL);
	g_return_val_if_fail (resource != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* replace datadir */
	as_gstring_replace (resource_str, "@datadir@", DATADIR);
	resource = resource_str->str;

	/* look in string for any url() links */
	for (guint i = 0; resource[i] != '\0'; i++) {
		if (i > 4 && strncmp (resource + i - 4, "url(", 4) == 0) {
			start = i;
			continue;
		}
		if (start == 0) {
			g_string_append_c (str, resource[i]);
			continue;
		}
		if (resource[i] == ')') {
			guint len;
			g_autofree gchar *cachefn = NULL;
			g_autofree gchar *uri = NULL;

			/* remove optional single quotes */
			if (resource[start] == '\'' || resource[start] == '"')
				start++;
			len = i - start;
			if (i > 0 && (resource[i - 1] == '\'' || resource[i - 1] == '"'))
				len--;
			uri = g_strndup (resource + start, len);

			/* download them to per-user cache */
			cachefn = gs_plugin_download_rewrite_resource_uri (plugin,
									   app,
									   uri,
									   cancellable,
									   error);
			if (cachefn == NULL)
				return NULL;
			g_string_append_printf (str, "'file://%s'", cachefn);
			g_string_append_c (str, resource[i]);
			start = 0;
		}
	}
	return g_strdup (str->str);
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
 * gs_plugin_action_to_function_name: (skip)
 * @action: a #GsPluginAction, e.g. %GS_PLUGIN_ERROR_NO_NETWORK
 *
 * Converts the enumerated action to the vfunc name.
 *
 * Returns: a string, or %NULL for invalid
 **/
const gchar *
gs_plugin_action_to_function_name (GsPluginAction action)
{
	if (action == GS_PLUGIN_ACTION_REFRESH)
		return "gs_plugin_refresh";
	if (action == GS_PLUGIN_ACTION_INSTALL)
		return "gs_plugin_app_install";
	if (action == GS_PLUGIN_ACTION_REMOVE)
		return "gs_plugin_app_remove";
	if (action == GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD)
		return "gs_plugin_app_upgrade_download";
	if (action == GS_PLUGIN_ACTION_UPGRADE_TRIGGER)
		return "gs_plugin_app_upgrade_trigger";
	if (action == GS_PLUGIN_ACTION_LAUNCH)
		return "gs_plugin_launch";
	if (action == GS_PLUGIN_ACTION_UPDATE_CANCEL)
		return "gs_plugin_update_cancel";
	if (action == GS_PLUGIN_ACTION_UPDATE)
		return "gs_plugin_update";
	if (action == GS_PLUGIN_ACTION_DOWNLOAD)
		return "gs_plugin_download";
	if (action == GS_PLUGIN_ACTION_FILE_TO_APP)
		return "gs_plugin_file_to_app";
	if (action == GS_PLUGIN_ACTION_URL_TO_APP)
		return "gs_plugin_url_to_app";
	if (action == GS_PLUGIN_ACTION_GET_DISTRO_UPDATES)
		return "gs_plugin_add_distro_upgrades";
	if (action == GS_PLUGIN_ACTION_GET_SOURCES)
		return "gs_plugin_add_sources";
	if (action == GS_PLUGIN_ACTION_GET_FEATURED)
		return "gs_plugin_add_featured";
	if (action == GS_PLUGIN_ACTION_GET_UPDATES_HISTORICAL)
		return "gs_plugin_add_updates_historical";
	if (action == GS_PLUGIN_ACTION_GET_UPDATES)
		return "gs_plugin_add_updates";
	if (action == GS_PLUGIN_ACTION_GET_POPULAR)
		return "gs_plugin_add_popular";
	if (action == GS_PLUGIN_ACTION_GET_RECENT)
		return "gs_plugin_add_recent";
	if (action == GS_PLUGIN_ACTION_SEARCH)
		return "gs_plugin_add_search";
	if (action == GS_PLUGIN_ACTION_SEARCH_FILES)
		return "gs_plugin_add_search_files";
	if (action == GS_PLUGIN_ACTION_SEARCH_PROVIDES)
		return "gs_plugin_add_search_what_provides";
	if (action == GS_PLUGIN_ACTION_GET_CATEGORY_APPS)
		return "gs_plugin_add_category_apps";
	if (action == GS_PLUGIN_ACTION_GET_CATEGORIES)
		return "gs_plugin_add_categories";
	if (action == GS_PLUGIN_ACTION_GET_ALTERNATES)
		return "gs_plugin_add_alternates";
	if (action == GS_PLUGIN_ACTION_GET_LANGPACKS)
		return "gs_plugin_add_langpacks";
	if (action == GS_PLUGIN_ACTION_INSTALL_REPO)
		return "gs_plugin_install_repo";
	if (action == GS_PLUGIN_ACTION_REMOVE_REPO)
		return "gs_plugin_remove_repo";
	if (action == GS_PLUGIN_ACTION_ENABLE_REPO)
		return "gs_plugin_enable_repo";
	if (action == GS_PLUGIN_ACTION_DISABLE_REPO)
		return "gs_plugin_disable_repo";
	return NULL;
}

/**
 * gs_plugin_action_to_string:
 * @action: a #GsPluginAction, e.g. %GS_PLUGIN_ERROR_NO_NETWORK
 *
 * Converts the enumerated action to a string.
 *
 * Returns: a string, or %NULL for invalid
 **/
const gchar *
gs_plugin_action_to_string (GsPluginAction action)
{
	if (action == GS_PLUGIN_ACTION_UNKNOWN)
		return "unknown";
	if (action == GS_PLUGIN_ACTION_INSTALL)
		return "install";
	if (action == GS_PLUGIN_ACTION_DOWNLOAD)
		return "download";
	if (action == GS_PLUGIN_ACTION_REMOVE)
		return "remove";
	if (action == GS_PLUGIN_ACTION_UPDATE)
		return "update";
	if (action == GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD)
		return "upgrade-download";
	if (action == GS_PLUGIN_ACTION_UPGRADE_TRIGGER)
		return "upgrade-trigger";
	if (action == GS_PLUGIN_ACTION_LAUNCH)
		return "launch";
	if (action == GS_PLUGIN_ACTION_UPDATE_CANCEL)
		return "update-cancel";
	if (action == GS_PLUGIN_ACTION_GET_UPDATES)
		return "get-updates";
	if (action == GS_PLUGIN_ACTION_GET_DISTRO_UPDATES)
		return "get-distro-updates";
	if (action == GS_PLUGIN_ACTION_GET_SOURCES)
		return "get-sources";
	if (action == GS_PLUGIN_ACTION_GET_POPULAR)
		return "get-popular";
	if (action == GS_PLUGIN_ACTION_GET_FEATURED)
		return "get-featured";
	if (action == GS_PLUGIN_ACTION_SEARCH)
		return "search";
	if (action == GS_PLUGIN_ACTION_SEARCH_FILES)
		return "search-files";
	if (action == GS_PLUGIN_ACTION_SEARCH_PROVIDES)
		return "search-provides";
	if (action == GS_PLUGIN_ACTION_GET_CATEGORIES)
		return "get-categories";
	if (action == GS_PLUGIN_ACTION_GET_CATEGORY_APPS)
		return "get-category-apps";
	if (action == GS_PLUGIN_ACTION_REFRESH)
		return "refresh";
	if (action == GS_PLUGIN_ACTION_FILE_TO_APP)
		return "file-to-app";
	if (action == GS_PLUGIN_ACTION_URL_TO_APP)
		return "url-to-app";
	if (action == GS_PLUGIN_ACTION_GET_RECENT)
		return "get-recent";
	if (action == GS_PLUGIN_ACTION_GET_UPDATES_HISTORICAL)
		return "get-updates-historical";
	if (action == GS_PLUGIN_ACTION_GET_ALTERNATES)
		return "get-alternates";
	if (action == GS_PLUGIN_ACTION_GET_LANGPACKS)
		return "get-langpacks";
	if (action == GS_PLUGIN_ACTION_INSTALL_REPO)
		return "repo-install";
	if (action == GS_PLUGIN_ACTION_REMOVE_REPO)
		return "repo-remove";
	if (action == GS_PLUGIN_ACTION_ENABLE_REPO)
		return "repo-enable";
	if (action == GS_PLUGIN_ACTION_DISABLE_REPO)
		return "repo-disable";
	return NULL;
}

/**
 * gs_plugin_action_from_string:
 * @action: a #GsPluginAction, e.g. "install"
 *
 * Converts the string to an enumerated action.
 *
 * Returns: a GsPluginAction, e.g. %GS_PLUGIN_ACTION_INSTALL
 *
 * Since: 3.26
 **/
GsPluginAction
gs_plugin_action_from_string (const gchar *action)
{
	if (g_strcmp0 (action, "install") == 0)
		return GS_PLUGIN_ACTION_INSTALL;
	if (g_strcmp0 (action, "download") == 0)
		return GS_PLUGIN_ACTION_DOWNLOAD;
	if (g_strcmp0 (action, "remove") == 0)
		return GS_PLUGIN_ACTION_REMOVE;
	if (g_strcmp0 (action, "update") == 0)
		return GS_PLUGIN_ACTION_UPDATE;
	if (g_strcmp0 (action, "upgrade-download") == 0)
		return GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD;
	if (g_strcmp0 (action, "upgrade-trigger") == 0)
		return GS_PLUGIN_ACTION_UPGRADE_TRIGGER;
	if (g_strcmp0 (action, "launch") == 0)
		return GS_PLUGIN_ACTION_LAUNCH;
	if (g_strcmp0 (action, "update-cancel") == 0)
		return GS_PLUGIN_ACTION_UPDATE_CANCEL;
	if (g_strcmp0 (action, "get-updates") == 0)
		return GS_PLUGIN_ACTION_GET_UPDATES;
	if (g_strcmp0 (action, "get-distro-updates") == 0)
		return GS_PLUGIN_ACTION_GET_DISTRO_UPDATES;
	if (g_strcmp0 (action, "get-sources") == 0)
		return GS_PLUGIN_ACTION_GET_SOURCES;
	if (g_strcmp0 (action, "get-popular") == 0)
		return GS_PLUGIN_ACTION_GET_POPULAR;
	if (g_strcmp0 (action, "get-featured") == 0)
		return GS_PLUGIN_ACTION_GET_FEATURED;
	if (g_strcmp0 (action, "search") == 0)
		return GS_PLUGIN_ACTION_SEARCH;
	if (g_strcmp0 (action, "search-files") == 0)
		return GS_PLUGIN_ACTION_SEARCH_FILES;
	if (g_strcmp0 (action, "search-provides") == 0)
		return GS_PLUGIN_ACTION_SEARCH_PROVIDES;
	if (g_strcmp0 (action, "get-categories") == 0)
		return GS_PLUGIN_ACTION_GET_CATEGORIES;
	if (g_strcmp0 (action, "get-category-apps") == 0)
		return GS_PLUGIN_ACTION_GET_CATEGORY_APPS;
	if (g_strcmp0 (action, "refresh") == 0)
		return GS_PLUGIN_ACTION_REFRESH;
	if (g_strcmp0 (action, "file-to-app") == 0)
		return GS_PLUGIN_ACTION_FILE_TO_APP;
	if (g_strcmp0 (action, "url-to-app") == 0)
		return GS_PLUGIN_ACTION_URL_TO_APP;
	if (g_strcmp0 (action, "get-recent") == 0)
		return GS_PLUGIN_ACTION_GET_RECENT;
	if (g_strcmp0 (action, "get-updates-historical") == 0)
		return GS_PLUGIN_ACTION_GET_UPDATES_HISTORICAL;
	if (g_strcmp0 (action, "get-alternates") == 0)
		return GS_PLUGIN_ACTION_GET_ALTERNATES;
	if (g_strcmp0 (action, "get-langpacks") == 0)
		return GS_PLUGIN_ACTION_GET_LANGPACKS;
	if (g_strcmp0 (action, "repo-install") == 0)
		return GS_PLUGIN_ACTION_INSTALL_REPO;
	if (g_strcmp0 (action, "repo-remove") == 0)
		return GS_PLUGIN_ACTION_REMOVE_REPO;
	if (g_strcmp0 (action, "repo-enable") == 0)
		return GS_PLUGIN_ACTION_ENABLE_REPO;
	if (g_strcmp0 (action, "repo-disable") == 0)
		return GS_PLUGIN_ACTION_DISABLE_REPO;
	return GS_PLUGIN_ACTION_UNKNOWN;
}

/**
 * gs_plugin_refine_flags_to_string:
 * @refine_flags: some #GsPluginRefineFlags, e.g. %GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE
 *
 * Converts the flags to a string.
 *
 * Returns: a string
 **/
gchar *
gs_plugin_refine_flags_to_string (GsPluginRefineFlags refine_flags)
{
	g_autoptr(GPtrArray) cstrs = g_ptr_array_new ();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID)
		g_ptr_array_add (cstrs, "require-id");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE)
		g_ptr_array_add (cstrs, "require-license");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL)
		g_ptr_array_add (cstrs, "require-url");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION)
		g_ptr_array_add (cstrs, "require-description");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE)
		g_ptr_array_add (cstrs, "require-size");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING)
		g_ptr_array_add (cstrs, "require-rating");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION)
		g_ptr_array_add (cstrs, "require-version");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY)
		g_ptr_array_add (cstrs, "require-history");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION)
		g_ptr_array_add (cstrs, "require-setup-action");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS)
		g_ptr_array_add (cstrs, "require-update-details");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN)
		g_ptr_array_add (cstrs, "require-origin");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED)
		g_ptr_array_add (cstrs, "require-related");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS)
		g_ptr_array_add (cstrs, "require-addons");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES)
		g_ptr_array_add (cstrs, "require-allow-packages");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_SEVERITY)
		g_ptr_array_add (cstrs, "require-update-severity");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED)
		g_ptr_array_add (cstrs, "require-upgrade-removed");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE)
		g_ptr_array_add (cstrs, "require-provenance");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS)
		g_ptr_array_add (cstrs, "require-reviews");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS)
		g_ptr_array_add (cstrs, "require-review-ratings");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON)
		g_ptr_array_add (cstrs, "require-icon");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS)
		g_ptr_array_add (cstrs, "require-permissions");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME)
		g_ptr_array_add (cstrs, "require-origin-hostname");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_UI)
		g_ptr_array_add (cstrs, "require-origin-ui");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME)
		g_ptr_array_add (cstrs, "require-runtime");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS)
		g_ptr_array_add (cstrs, "require-screenshots");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_CATEGORIES)
		g_ptr_array_add (cstrs, "require-categories");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROJECT_GROUP)
		g_ptr_array_add (cstrs, "require-project-group");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DEVELOPER_NAME)
		g_ptr_array_add (cstrs, "require-developer-name");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS)
		g_ptr_array_add (cstrs, "require-kudos");
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_CONTENT_RATING)
		g_ptr_array_add (cstrs, "content-rating");
#pragma GCC diagnostic pop
	if (cstrs->len == 0)
		return g_strdup ("none");
	g_ptr_array_add (cstrs, NULL);
	return g_strjoinv (",", (gchar**) cstrs->pdata);
}

static void
gs_plugin_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsPlugin *plugin = GS_PLUGIN (object);
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	switch (prop_id) {
	case PROP_FLAGS:
		priv->flags = g_value_get_flags (value);
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
	switch (prop_id) {
	case PROP_FLAGS:
		g_value_set_flags (value, priv->flags);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_class_init (GsPluginClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gs_plugin_set_property;
	object_class->get_property = gs_plugin_get_property;
	object_class->finalize = gs_plugin_finalize;

	pspec = g_param_spec_flags ("flags", NULL, NULL,
				    GS_TYPE_PLUGIN_FLAGS, GS_PLUGIN_FLAGS_NONE,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_FLAGS, pspec);

	signals [SIGNAL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginClass, updates_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals [SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2, GS_TYPE_APP, G_TYPE_UINT);

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
	g_mutex_init (&priv->interactive_mutex);
	g_mutex_init (&priv->timer_mutex);
	g_mutex_init (&priv->vfuncs_mutex);
}

/**
 * gs_plugin_new:
 *
 * Creates a new plugin.
 *
 * Returns: a #GsPlugin
 *
 * Since: 3.22
 **/
GsPlugin *
gs_plugin_new (void)
{
	GsPlugin *plugin;
	plugin = g_object_new (GS_TYPE_PLUGIN, NULL);
	return plugin;
}

typedef struct {
	GsPlugin *plugin;
	GsApp	 *repository;
} GsPluginRepositoryChangedHelper;

static gboolean
gs_plugin_repository_changed_cb (gpointer user_data)
{
	GsPluginRepositoryChangedHelper *helper = user_data;
	g_signal_emit (helper->plugin,
		       signals[SIGNAL_REPOSITORY_CHANGED], 0,
		       helper->repository);
	g_clear_object (&helper->repository);
	g_clear_object (&helper->plugin);
	g_slice_free (GsPluginRepositoryChangedHelper, helper);
	return FALSE;
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
	GsPluginRepositoryChangedHelper *helper;
	g_autoptr(GSource) idle_source = NULL;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP (repository));

	helper = g_slice_new0 (GsPluginRepositoryChangedHelper);
	helper->plugin = g_object_ref (plugin);
	helper->repository = g_object_ref (repository);

	idle_source = g_idle_source_new ();
	g_source_set_callback (idle_source, gs_plugin_repository_changed_cb, helper, NULL);
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
	gpointer value;
	const gchar *repo_id;
	GsAppState repo_state;

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP (repository));

	priv = gs_plugin_get_instance_private (plugin);
	repo_id = gs_app_get_id (repository);
	repo_state = gs_app_get_state (repository);

	locker = g_mutex_locker_new (&priv->cache_mutex);

	g_hash_table_iter_init (&iter, priv->cache);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GsApp *app = value;
		GsAppState app_state = gs_app_get_state (app);

		if (((app_state == GS_APP_STATE_AVAILABLE &&
		    repo_state != GS_APP_STATE_INSTALLED) ||
		    (app_state == GS_APP_STATE_UNAVAILABLE &&
		    repo_state == GS_APP_STATE_INSTALLED)) &&
		    g_strcmp0 (gs_app_get_origin (app), repo_id) == 0) {
			gs_app_set_state (app, repo_state == GS_APP_STATE_INSTALLED ? GS_APP_STATE_AVAILABLE : GS_APP_STATE_UNAVAILABLE);
		}
	}
}

/**
 * gs_plugin_get_action_supported:
 * @plugin: a #GsPlugin
 * @action: a #GsPluginAction
 *
 * Checks whether the @plugin supports @action, meaning whether
 * the @plugin can execute the @action.
 *
 * Returns: Whether the @plugin supports the @action
 *
 * Since: 41
 **/
gboolean
gs_plugin_get_action_supported (GsPlugin *plugin,
				GsPluginAction action)
{
	const gchar *function_name;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), FALSE);

	function_name = gs_plugin_action_to_function_name (action);
	g_return_val_if_fail (function_name != NULL, FALSE);

	return gs_plugin_get_symbol (plugin, function_name) != NULL;
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
