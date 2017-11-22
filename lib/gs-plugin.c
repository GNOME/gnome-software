/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include "gs-os-release.h"
#include "gs-plugin-private.h"
#include "gs-plugin.h"
#include "gs-utils.h"

typedef struct
{
	GPtrArray		*auth_array;
	GHashTable		*cache;
	GMutex			 cache_mutex;
	GModule			*module;
	GRWLock			 rwlock;
	GsPluginData		*data;			/* for gs-plugin-{name}.c */
	GsPluginFlags		 flags;
	SoupSession		*soup_session;
	GPtrArray		*rules[GS_PLUGIN_RULE_LAST];
	GHashTable		*vfuncs;		/* string:pointer */
	GMutex			 vfuncs_mutex;
	gboolean		 enabled;
	guint			 interactive_cnt;
	GMutex			 interactive_mutex;
	gchar			*locale;		/* allow-none */
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

G_DEFINE_TYPE_WITH_PRIVATE (GsPlugin, gs_plugin, G_TYPE_OBJECT)

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
	plugin = gs_plugin_new ();
	priv = gs_plugin_get_instance_private (plugin);
	priv->module = g_module_open (filename, 0);
	if (priv->module == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to open plugin %s: %s",
			     filename, g_module_error ());
		return NULL;
	}
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
	g_free (priv->data);
	g_free (priv->locale);
	g_free (priv->language);
	g_rw_lock_clear (&priv->rwlock);
	if (priv->auth_array != NULL)
		g_ptr_array_unref (priv->auth_array);
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
}

/**
 * gs_plugin_get_data:
 * @plugin: a #GsPlugin
 *
 * Gets the private data for the plugin if gs_plugin_alloc_data() has
 * been called.
 *
 * Returns: the #GsPluginData, or %NULL
 *
 * Since: 3.22
 **/
GsPluginData *
gs_plugin_get_data (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_assert (priv->data != NULL);
	return priv->data;
}

/**
 * gs_plugin_alloc_data:
 * @plugin: a #GsPlugin
 * @sz: the size of data to allocate, e.g. `sizeof(FooPluginPrivate)`
 *
 * Allocates a private data area for the plugin which can be retrieved
 * using gs_plugin_get_data().
 * This is normally called in gs_plugin_initialize() and the data should
 * not be manually freed.
 *
 * Returns: the #GsPluginData, cleared to NUL bytes
 *
 * Since: 3.22
 **/
GsPluginData *
gs_plugin_alloc_data (GsPlugin *plugin, gsize sz)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_assert (priv->data == NULL);
	priv->data = g_malloc0 (sz);
	return priv->data;
}

/**
 * gs_plugin_clear_data:
 * @plugin: a #GsPlugin
 *
 * Clears and resets the private data. Only run this from the self tests.
 **/
void
gs_plugin_clear_data (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	if (priv->data == NULL)
		return;
	g_clear_pointer (&priv->data, g_free);
}

/**
 * gs_plugin_action_start:
 * @plugin: a #GsPlugin
 * @exclusive: if the plugin action should be performed exclusively
 *
 * Starts a plugin action.
 *
 * Since: 3.22
 **/
void
gs_plugin_action_start (GsPlugin *plugin, gboolean exclusive)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	/* lock plugin */
	if (exclusive) {
		g_rw_lock_writer_lock (&priv->rwlock);
		gs_plugin_add_flags (plugin, GS_PLUGIN_FLAGS_EXCLUSIVE);
	} else {
		g_rw_lock_reader_lock (&priv->rwlock);
	}

	/* set plugin as SELF */
	gs_plugin_add_flags (plugin, GS_PLUGIN_FLAGS_RUNNING_SELF);
}

static gboolean
gs_plugin_action_delay_cb (gpointer user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->timer_mutex);

	g_debug ("plugin no longer recently active: %s", priv->name);
	gs_plugin_remove_flags (plugin, GS_PLUGIN_FLAGS_RECENT);
	priv->timer_id = 0;
	return FALSE;
}

/**
 * gs_plugin_action_stop:
 * @plugin: a #GsPlugin
 *
 * Stops an plugin action.
 *
 * Since: 3.22
 **/
void
gs_plugin_action_stop (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->timer_mutex);

	/* clear plugin as SELF */
	gs_plugin_remove_flags (plugin, GS_PLUGIN_FLAGS_RUNNING_SELF);

	/* unlock plugin */
	if (priv->flags & GS_PLUGIN_FLAGS_EXCLUSIVE) {
		g_rw_lock_writer_unlock (&priv->rwlock);
		gs_plugin_remove_flags (plugin, GS_PLUGIN_FLAGS_EXCLUSIVE);
	} else {
		g_rw_lock_reader_unlock (&priv->rwlock);
	}

	/* unset this flag after 5 seconds */
	gs_plugin_add_flags (plugin, GS_PLUGIN_FLAGS_RECENT);
	if (priv->timer_id > 0)
		g_source_remove (priv->timer_id);
	priv->timer_id = g_timeout_add (5000,
					gs_plugin_action_delay_cb,
					plugin);
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
 * This is normally only called from gs_plugin_initialize().
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
 * gs_plugin_get_locale:
 * @plugin: a #GsPlugin
 *
 * Gets the user locale.
 *
 * Returns: the locale string, e.g. "en_GB"
 *
 * Since: 3.22
 **/
const gchar *
gs_plugin_get_locale (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->locale;
}

/**
 * gs_plugin_get_language:
 * @plugin: a #GsPlugin
 *
 * Gets the user language from the locale.
 *
 * Returns: the language string, e.g. "fr"
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
 * gs_plugin_set_locale:
 * @plugin: a #GsPlugin
 * @locale: a locale string, e.g. "en_GB"
 *
 * Sets the plugin locale.
 *
 * Since: 3.22
 **/
void
gs_plugin_set_locale (GsPlugin *plugin, const gchar *locale)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_free (priv->locale);
	priv->locale = g_strdup (locale);
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
 * gs_plugin_set_auth_array:
 * @plugin: a #GsPlugin
 * @auth_array: (element-type GsAuth): an array
 *
 * Sets the authentication objects that can be added by the plugin.
 *
 * Since: 3.22
 **/
void
gs_plugin_set_auth_array (GsPlugin *plugin, GPtrArray *auth_array)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->auth_array = g_ptr_array_ref (auth_array);
}

/**
 * gs_plugin_add_auth:
 * @plugin: a #GsPlugin
 * @auth: a #GsAuth
 *
 * Adds an authentication object that can be used for all the plugins.
 *
 * Since: 3.22
 **/
void
gs_plugin_add_auth (GsPlugin *plugin, GsAuth *auth)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_ptr_array_add (priv->auth_array, g_object_ref (auth));
}

/**
 * gs_plugin_get_auth_by_id:
 * @plugin: a #GsPlugin
 * @provider_id: an ID, e.g. "dummy-sso"
 *
 * Gets a specific authentication object.
 *
 * Returns: the #GsAuth, or %NULL if not found
 *
 * Since: 3.22
 **/
GsAuth *
gs_plugin_get_auth_by_id (GsPlugin *plugin, const gchar *provider_id)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	guint i;

	/* match on ID */
	for (i = 0; i < priv->auth_array->len; i++) {
		GsAuth *auth = g_ptr_array_index (priv->auth_array, i);
		if (g_strcmp0 (gs_auth_get_provider_id (auth), provider_id) == 0)
			return auth;
	}
	return NULL;
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
 * @flags: a #GsPluginFlags, e.g. %GS_PLUGIN_FLAGS_RUNNING_SELF
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
 * @flags: a #GsPluginFlags, e.g. %GS_PLUGIN_FLAGS_RUNNING_SELF
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
 * @flags: a #GsPluginFlags, e.g. %GS_PLUGIN_FLAGS_RUNNING_SELF
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
 * gs_plugin_set_running_other:
 * @plugin: a #GsPlugin
 * @running_other: %TRUE if another plugin is running
 *
 * Inform the plugin that another plugin is running in the loader.
 *
 * Since: 3.22
 **/
void
gs_plugin_set_running_other (GsPlugin *plugin, gboolean running_other)
{
	if (running_other)
		gs_plugin_add_flags (plugin, GS_PLUGIN_FLAGS_RUNNING_OTHER);
	else
		gs_plugin_remove_flags (plugin, GS_PLUGIN_FLAGS_RUNNING_OTHER);
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
	GsPluginDownloadHelper helper;
	guint status_code;
	g_autoptr(SoupMessage) msg = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), NULL);
	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* local */
	if (g_str_has_prefix (uri, "file://")) {
		gsize length = 0;
		g_autofree gchar *contents = NULL;
		g_autoptr(GError) error_local = NULL;
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
	GsPluginDownloadHelper helper;
	guint status_code;
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
		return FALSE;
	}
	if (!gs_mkdir_parent (filename, error))
		return FALSE;
	if (!g_file_set_contents (filename,
				  msg->response_body->data,
				  msg->response_body->length,
				  &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_WRITE_FAILED,
			     "Failed to save file: %s",
			     error_local->message);
		return FALSE;
	}
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
					       GS_UTILS_CACHE_FLAG_USE_HASH,
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
	as_utils_string_replace (resource_str, "@datadir@", DATADIR);
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
			g_string_append_printf (str, "'%s'", cachefn);
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
	if (error == GS_PLUGIN_ERROR_PIN_REQUIRED)
		return "pin-required";
	if (error == GS_PLUGIN_ERROR_ACCOUNT_SUSPENDED)
		return "account-suspended";
	if (error == GS_PLUGIN_ERROR_ACCOUNT_DEACTIVATED)
		return "account-deactivated";
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
	if (error == GS_PLUGIN_ERROR_TIMED_OUT)
		return "timed-out";
	if (error == GS_PLUGIN_ERROR_PURCHASE_NOT_SETUP)
		return "purchase-not-setup";
	if (error == GS_PLUGIN_ERROR_PURCHASE_DECLINED)
		return "purchase-declined";
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
	if (action == GS_PLUGIN_ACTION_REVIEW_SUBMIT)
		return "gs_plugin_review_submit";
	if (action == GS_PLUGIN_ACTION_REVIEW_UPVOTE)
		return "gs_plugin_review_upvote";
	if (action == GS_PLUGIN_ACTION_REVIEW_DOWNVOTE)
		return "gs_plugin_review_downvote";
	if (action == GS_PLUGIN_ACTION_REVIEW_REPORT)
		return "gs_plugin_review_report";
	if (action == GS_PLUGIN_ACTION_REVIEW_REMOVE)
		return "gs_plugin_review_remove";
	if (action == GS_PLUGIN_ACTION_REVIEW_DISMISS)
		return "gs_plugin_review_dismiss";
	if (action == GS_PLUGIN_ACTION_INSTALL)
		return "gs_plugin_app_install";
	if (action == GS_PLUGIN_ACTION_REMOVE)
		return "gs_plugin_app_remove";
	if (action == GS_PLUGIN_ACTION_SET_RATING)
		return "gs_plugin_app_set_rating";
	if (action == GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD)
		return "gs_plugin_app_upgrade_download";
	if (action == GS_PLUGIN_ACTION_UPGRADE_TRIGGER)
		return "gs_plugin_app_upgrade_trigger";
	if (action == GS_PLUGIN_ACTION_LAUNCH)
		return "gs_plugin_launch";
	if (action == GS_PLUGIN_ACTION_UPDATE_CANCEL)
		return "gs_plugin_update_cancel";
	if (action == GS_PLUGIN_ACTION_ADD_SHORTCUT)
		return "gs_plugin_add_shortcut";
	if (action == GS_PLUGIN_ACTION_REMOVE_SHORTCUT)
		return "gs_plugin_remove_shortcut";
	if (action == GS_PLUGIN_ACTION_REFINE)
		return "gs_plugin_refine";
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
	if (action == GS_PLUGIN_ACTION_GET_UNVOTED_REVIEWS)
		return "gs_plugin_add_unvoted_reviews";
	if (action == GS_PLUGIN_ACTION_GET_INSTALLED)
		return "gs_plugin_add_installed";
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
	if (action == GS_PLUGIN_ACTION_AUTH_LOGIN)
		return "gs_plugin_auth_login";
	if (action == GS_PLUGIN_ACTION_AUTH_LOGOUT)
		return "gs_plugin_auth_logout";
	if (action == GS_PLUGIN_ACTION_AUTH_REGISTER)
		return "gs_plugin_auth_register";
	if (action == GS_PLUGIN_ACTION_AUTH_LOST_PASSWORD)
		return "gs_plugin_auth_lost_password";
	if (action == GS_PLUGIN_ACTION_GET_CATEGORY_APPS)
		return "gs_plugin_add_category_apps";
	if (action == GS_PLUGIN_ACTION_GET_CATEGORIES)
		return "gs_plugin_add_categories";
	if (action == GS_PLUGIN_ACTION_SETUP)
		return "gs_plugin_setup";
	if (action == GS_PLUGIN_ACTION_INITIALIZE)
		return "gs_plugin_initialize";
	if (action == GS_PLUGIN_ACTION_DESTROY)
		return "gs_plugin_destroy";
	if (action == GS_PLUGIN_ACTION_PURCHASE)
		return "gs_plugin_app_purchase";
	if (action == GS_PLUGIN_ACTION_SWITCH_CHANNEL)
		return "gs_plugin_app_switch_channel";
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
	if (action == GS_PLUGIN_ACTION_SETUP)
		return "setup";
	if (action == GS_PLUGIN_ACTION_INSTALL)
		return "install";
	if (action == GS_PLUGIN_ACTION_DOWNLOAD)
		return "download";
	if (action == GS_PLUGIN_ACTION_REMOVE)
		return "remove";
	if (action == GS_PLUGIN_ACTION_UPDATE)
		return "update";
	if (action == GS_PLUGIN_ACTION_SET_RATING)
		return "set-rating";
	if (action == GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD)
		return "upgrade-download";
	if (action == GS_PLUGIN_ACTION_UPGRADE_TRIGGER)
		return "upgrade-trigger";
	if (action == GS_PLUGIN_ACTION_LAUNCH)
		return "launch";
	if (action == GS_PLUGIN_ACTION_UPDATE_CANCEL)
		return "update-cancel";
	if (action == GS_PLUGIN_ACTION_ADD_SHORTCUT)
		return "add-shortcut";
	if (action == GS_PLUGIN_ACTION_REMOVE_SHORTCUT)
		return "remove-shortcut";
	if (action == GS_PLUGIN_ACTION_REVIEW_SUBMIT)
		return "review-submit";
	if (action == GS_PLUGIN_ACTION_REVIEW_UPVOTE)
		return "review-upvote";
	if (action == GS_PLUGIN_ACTION_REVIEW_DOWNVOTE)
		return "review-downvote";
	if (action == GS_PLUGIN_ACTION_REVIEW_REPORT)
		return "review-report";
	if (action == GS_PLUGIN_ACTION_REVIEW_REMOVE)
		return "review-remove";
	if (action == GS_PLUGIN_ACTION_REVIEW_DISMISS)
		return "review-dismiss";
	if (action == GS_PLUGIN_ACTION_GET_UPDATES)
		return "get-updates";
	if (action == GS_PLUGIN_ACTION_GET_DISTRO_UPDATES)
		return "get-distro-updates";
	if (action == GS_PLUGIN_ACTION_GET_UNVOTED_REVIEWS)
		return "get-unvoted-reviews";
	if (action == GS_PLUGIN_ACTION_GET_SOURCES)
		return "get-sources";
	if (action == GS_PLUGIN_ACTION_GET_INSTALLED)
		return "get-installed";
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
	if (action == GS_PLUGIN_ACTION_REFINE)
		return "refine";
	if (action == GS_PLUGIN_ACTION_REFRESH)
		return "refresh";
	if (action == GS_PLUGIN_ACTION_FILE_TO_APP)
		return "file-to-app";
	if (action == GS_PLUGIN_ACTION_URL_TO_APP)
		return "url-to-app";
	if (action == GS_PLUGIN_ACTION_AUTH_LOGIN)
		return "auth-login";
	if (action == GS_PLUGIN_ACTION_AUTH_LOGOUT)
		return "auth-logout";
	if (action == GS_PLUGIN_ACTION_AUTH_REGISTER)
		return "auth-register";
	if (action == GS_PLUGIN_ACTION_AUTH_LOST_PASSWORD)
		return "auth-lost-password";
	if (action == GS_PLUGIN_ACTION_GET_RECENT)
		return "get-recent";
	if (action == GS_PLUGIN_ACTION_GET_UPDATES_HISTORICAL)
		return "get-updates-historical";
	if (action == GS_PLUGIN_ACTION_INITIALIZE)
		return "initialize";
	if (action == GS_PLUGIN_ACTION_DESTROY)
		return "destroy";
	if (action == GS_PLUGIN_ACTION_PURCHASE)
		return "purchase";
	if (action == GS_PLUGIN_ACTION_SWITCH_CHANNEL)
		return "switch-channel";
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
	if (g_strcmp0 (action, "setup") == 0)
		return GS_PLUGIN_ACTION_SETUP;
	if (g_strcmp0 (action, "install") == 0)
		return GS_PLUGIN_ACTION_INSTALL;
	if (g_strcmp0 (action, "download") == 0)
		return GS_PLUGIN_ACTION_DOWNLOAD;
	if (g_strcmp0 (action, "remove") == 0)
		return GS_PLUGIN_ACTION_REMOVE;
	if (g_strcmp0 (action, "update") == 0)
		return GS_PLUGIN_ACTION_UPDATE;
	if (g_strcmp0 (action, "set-rating") == 0)
		return GS_PLUGIN_ACTION_SET_RATING;
	if (g_strcmp0 (action, "upgrade-download") == 0)
		return GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD;
	if (g_strcmp0 (action, "upgrade-trigger") == 0)
		return GS_PLUGIN_ACTION_UPGRADE_TRIGGER;
	if (g_strcmp0 (action, "launch") == 0)
		return GS_PLUGIN_ACTION_LAUNCH;
	if (g_strcmp0 (action, "update-cancel") == 0)
		return GS_PLUGIN_ACTION_UPDATE_CANCEL;
	if (g_strcmp0 (action, "add-shortcut") == 0)
		return GS_PLUGIN_ACTION_ADD_SHORTCUT;
	if (g_strcmp0 (action, "remove-shortcut") == 0)
		return GS_PLUGIN_ACTION_REMOVE_SHORTCUT;
	if (g_strcmp0 (action, "review-submit") == 0)
		return GS_PLUGIN_ACTION_REVIEW_SUBMIT;
	if (g_strcmp0 (action, "review-upvote") == 0)
		return GS_PLUGIN_ACTION_REVIEW_UPVOTE;
	if (g_strcmp0 (action, "review-downvote") == 0)
		return GS_PLUGIN_ACTION_REVIEW_DOWNVOTE;
	if (g_strcmp0 (action, "review-report") == 0)
		return GS_PLUGIN_ACTION_REVIEW_REPORT;
	if (g_strcmp0 (action, "review-remove") == 0)
		return GS_PLUGIN_ACTION_REVIEW_REMOVE;
	if (g_strcmp0 (action, "review-dismiss") == 0)
		return GS_PLUGIN_ACTION_REVIEW_DISMISS;
	if (g_strcmp0 (action, "get-updates") == 0)
		return GS_PLUGIN_ACTION_GET_UPDATES;
	if (g_strcmp0 (action, "get-distro-updates") == 0)
		return GS_PLUGIN_ACTION_GET_DISTRO_UPDATES;
	if (g_strcmp0 (action, "get-unvoted-reviews") == 0)
		return GS_PLUGIN_ACTION_GET_UNVOTED_REVIEWS;
	if (g_strcmp0 (action, "get-sources") == 0)
		return GS_PLUGIN_ACTION_GET_SOURCES;
	if (g_strcmp0 (action, "get-installed") == 0)
		return GS_PLUGIN_ACTION_GET_INSTALLED;
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
	if (g_strcmp0 (action, "refine") == 0)
		return GS_PLUGIN_ACTION_REFINE;
	if (g_strcmp0 (action, "refresh") == 0)
		return GS_PLUGIN_ACTION_REFRESH;
	if (g_strcmp0 (action, "file-to-app") == 0)
		return GS_PLUGIN_ACTION_FILE_TO_APP;
	if (g_strcmp0 (action, "url-to-app") == 0)
		return GS_PLUGIN_ACTION_URL_TO_APP;
	if (g_strcmp0 (action, "auth-login") == 0)
		return GS_PLUGIN_ACTION_AUTH_LOGIN;
	if (g_strcmp0 (action, "auth-logout") == 0)
		return GS_PLUGIN_ACTION_AUTH_LOGOUT;
	if (g_strcmp0 (action, "auth-register") == 0)
		return GS_PLUGIN_ACTION_AUTH_REGISTER;
	if (g_strcmp0 (action, "auth-lost-password") == 0)
		return GS_PLUGIN_ACTION_AUTH_LOST_PASSWORD;
	if (g_strcmp0 (action, "get-recent") == 0)
		return GS_PLUGIN_ACTION_GET_RECENT;
	if (g_strcmp0 (action, "get-updates-historical") == 0)
		return GS_PLUGIN_ACTION_GET_UPDATES_HISTORICAL;
	if (g_strcmp0 (action, "initialize") == 0)
		return GS_PLUGIN_ACTION_INITIALIZE;
	if (g_strcmp0 (action, "destroy") == 0)
		return GS_PLUGIN_ACTION_DESTROY;
	if (g_strcmp0 (action, "purchase") == 0)
		return GS_PLUGIN_ACTION_PURCHASE;
	if (g_strcmp0 (action, "switch-channel") == 0)
		return GS_PLUGIN_ACTION_SWITCH_CHANNEL;
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
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_USE_HISTORY)
		g_ptr_array_add (cstrs, "use-history");
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
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH)
		g_ptr_array_add (cstrs, "require-menu-path");
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
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KEY_COLORS)
		g_ptr_array_add (cstrs, "require-key-colors");
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
		priv->flags = g_value_get_uint64 (value);
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
		g_value_set_uint64 (value, priv->flags);
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

	pspec = g_param_spec_uint64 ("flags", NULL, NULL,
				     0, G_MAXUINT64, 0, G_PARAM_READWRITE);
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
	priv->cache = g_hash_table_new_full ((GHashFunc) as_utils_unique_id_hash,
					     (GEqualFunc) as_utils_unique_id_equal,
					     g_free,
					     (GDestroyNotify) g_object_unref);
	priv->vfuncs = g_hash_table_new_full (g_str_hash, g_str_equal,
					      g_free, NULL);
	g_mutex_init (&priv->cache_mutex);
	g_mutex_init (&priv->interactive_mutex);
	g_mutex_init (&priv->timer_mutex);
	g_mutex_init (&priv->vfuncs_mutex);
	g_rw_lock_init (&priv->rwlock);
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

/* vim: set noexpandtab: */
