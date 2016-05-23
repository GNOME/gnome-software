/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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

#include "gs-os-release.h"
#include "gs-plugin-private.h"
#include "gs-plugin.h"
#include "gs-utils.h"

typedef struct
{
	AsProfile		*profile;
	GHashTable		*cache;
	GMutex			 cache_mutex;
	GModule			*module;
	GRWLock			 rwlock;
	GsPluginData		*data;			/* for gs-plugin-{name}.c */
	GsPluginFlags		 flags;
	SoupSession		*soup_session;
	GPtrArray		*rules[GS_PLUGIN_RULE_LAST];
	gboolean		 enabled;
	gchar			*locale;		/* allow-none */
	gchar			*name;
	gint			 scale;
	guint			 priority;
	guint			 timer_id;
} GsPluginPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsPlugin, gs_plugin, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_FLAGS,
	PROP_LAST
};

enum {
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_STATUS_CHANGED,
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
 * gs_plugin_create:
 * @filename: an absolute filename
 * @error: a #GError, or %NULL
 *
 * Creates a new plugin from an external module.
 *
 * Returns: the #GsPlugin or %NULL
 **/
GsPlugin *
gs_plugin_create (const gchar *filename, GError **error)
{
	GModule *module;
	GsPlugin *plugin = NULL;
	GsPluginPrivate *priv;
	g_autofree gchar *basename = NULL;

	module = g_module_open (filename, 0);
	if (module == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to open plugin %s: %s",
			     filename, g_module_error ());
		return NULL;
	}

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
	priv->module = module;
	priv->name = g_strdup (basename + 13);
	return plugin;
}

/**
 * gs_plugin_finalize:
 **/
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
	g_free (priv->data);
	g_free (priv->locale);
	g_rw_lock_clear (&priv->rwlock);
	g_object_unref (priv->profile);
	g_object_unref (priv->soup_session);
	g_hash_table_unref (priv->cache);
	g_mutex_clear (&priv->cache_mutex);
	g_module_close (priv->module);
}

/**
 * gs_plugin_get_data:
 * @plugin: a #GsPlugin
 *
 * Gets the private data for the plugin if gs_plugin_alloc_data() has
 * been called.
 *
 * Returns: the #GsPluginData, or %NULL
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
 * Returns: the #GsPluginData, cleared to NUL butes
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
 * gs_plugin_action_start:
 * @plugin: a #GsPlugin
 * @exclusive: if the plugin action should be performed exclusively
 *
 * Starts a plugin action.
 **/
void
gs_plugin_action_start (GsPlugin *plugin, gboolean exclusive)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	/* lock plugin */
	if (exclusive) {
		g_rw_lock_writer_lock (&priv->rwlock);
		priv->flags |= GS_PLUGIN_FLAGS_EXCLUSIVE;
	} else {
		g_rw_lock_reader_lock (&priv->rwlock);
	}

	/* set plugin as SELF */
	priv->flags |= GS_PLUGIN_FLAGS_RUNNING_SELF;
}

/**
 * gs_plugin_action_delay_cb:
 **/
static gboolean
gs_plugin_action_delay_cb (gpointer user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	g_debug ("plugin no longer recently active: %s", priv->name);
	priv->flags &= ~GS_PLUGIN_FLAGS_RECENT;
	priv->timer_id = 0;
	return FALSE;
}

/**
 * gs_plugin_action_stop:
 * @plugin: a #GsPlugin
 *
 * Stops an plugin action.
 **/
void
gs_plugin_action_stop (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	/* clear plugin as SELF */
	priv->flags &= ~GS_PLUGIN_FLAGS_RUNNING_SELF;

	/* unlock plugin */
	if (priv->flags & GS_PLUGIN_FLAGS_EXCLUSIVE) {
		g_rw_lock_writer_unlock (&priv->rwlock);
		priv->flags &= ~GS_PLUGIN_FLAGS_EXCLUSIVE;
	} else {
		g_rw_lock_reader_unlock (&priv->rwlock);
	}

	/* unset this flag after 5 seconds */
	priv->flags |= GS_PLUGIN_FLAGS_RECENT;
	if (priv->timer_id > 0)
		g_source_remove (priv->timer_id);
	priv->timer_id = g_timeout_add (5000,
					gs_plugin_action_delay_cb,
					plugin);
}

/**
 * gs_plugin_get_module:
 * @plugin: a #GsPlugin
 *
 * Gets the external module that backs the plugin.
 *
 * Returns: the #GModule, or %NULL
 **/
GModule *
gs_plugin_get_module (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->module;
}

/**
 * gs_plugin_get_enabled:
 * @plugin: a #GsPlugin
 *
 * Gets if the plugin is enabled.
 *
 * Returns: %TRUE if enabled
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
 **/
void
gs_plugin_set_scale (GsPlugin *plugin, guint scale)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->scale = scale;
}

/**
 * gs_plugin_get_priority:
 * @plugin: a #GsPlugin
 *
 * Gets the plugin priority, where higher numbers are run after lower
 * numbers.
 *
 * Returns: the integer value
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
 * Sets the plugin priority, where higher numbers are run after lower
 * numbers.
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
 **/
const gchar *
gs_plugin_get_locale (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->locale;
}

/**
 * gs_plugin_set_locale:
 * @plugin: a #GsPlugin
 * @locale: a locale string, e.g. "en_GB"
 *
 * Sets the plugin locale.
 **/
void
gs_plugin_set_locale (GsPlugin *plugin, const gchar *locale)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_free (priv->locale);
	priv->locale = g_strdup (locale);
}

/**
 * gs_plugin_get_profile:
 * @plugin: a #GsPlugin
 *
 * Gets the profile object to be used for the plugin.
 * This can be used to make plugin actions appear in the global profile
 * output.
 *
 * Returns: the #AsProfile
 **/
AsProfile *
gs_plugin_get_profile (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->profile;
}

/**
 * gs_plugin_set_profile:
 * @plugin: a #GsPlugin
 * @profile: a #AsProfile
 *
 * Sets the profile object to be used for the plugin.
 **/
void
gs_plugin_set_profile (GsPlugin *plugin, AsProfile *profile)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_set_object (&priv->profile, profile);
}

/**
 * gs_plugin_get_soup_session:
 * @plugin: a #GsPlugin
 *
 * Gets the soup session that plugins can use when downloading.
 *
 * Returns: the #SoupSession
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
 * Sets the soup session that plugins will use when downloading.
 **/
void
gs_plugin_set_soup_session (GsPlugin *plugin, SoupSession *soup_session)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_set_object (&priv->soup_session, soup_session);
}

/**
 * gs_plugin_has_flags:
 * @plugin: a #GsPlugin
 * @flags: a #GsPluginFlags, e.g. %GS_PLUGIN_FLAGS_RUNNING_SELF
 *
 * Finds out if a plugin has a specific flag set.
 *
 * Returns: TRUE if the flag is set
 **/
gboolean
gs_plugin_has_flags (GsPlugin *plugin, GsPluginFlags flags)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return (priv->flags & flags) > 0;
}

/**
 * gs_plugin_set_running_other:
 * @plugin: a #GsPlugin
 * @running_other: %TRUE if another plugin is running
 *
 * Inform the plugin that another plugin is running in the loader.
 **/
void
gs_plugin_set_running_other (GsPlugin *plugin, gboolean running_other)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	if (running_other)
		priv->flags |= GS_PLUGIN_FLAGS_RUNNING_OTHER;
	else
		priv->flags &= ~GS_PLUGIN_FLAGS_RUNNING_OTHER;
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

/**
 * gs_plugin_status_update_cb:
 **/
static gboolean
gs_plugin_status_update_cb (gpointer user_data)
{
	GsPluginStatusHelper *helper = (GsPluginStatusHelper *) user_data;
	g_signal_emit (helper->plugin,
		       signals[SIGNAL_STATUS_CHANGED], 0,
		       helper->app,
		       helper->status);
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
 **/
void
gs_plugin_status_update (GsPlugin *plugin, GsApp *app, GsPluginStatus status)
{
	GsPluginStatusHelper *helper;
	helper = g_slice_new0 (GsPluginStatusHelper);
	helper->plugin = plugin;
	helper->status = status;
	if (app == NULL)
		helper->app = gs_app_new (NULL);
	else
		helper->app = g_object_ref (app);
	g_idle_add (gs_plugin_status_update_cb, helper);
}

/**
 * gs_plugin_app_launch_cb:
 **/
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
 * Launches the application using GAppInfo.
 *
 * Returns: %TRUE for success
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

/**
 * gs_plugin_updates_changed_cb:
 **/
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
 **/
void
gs_plugin_updates_changed (GsPlugin *plugin)
{
	g_idle_add (gs_plugin_updates_changed_cb, plugin);
}

typedef struct {
	GsPlugin	*plugin;
	GsApp		*app;
	GCancellable	*cancellable;
} GsPluginDownloadHelper;

/**
 * gs_plugin_download_chunk_cb:
 **/
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
		g_debug ("ignoring status code %i (%s)",
			 msg->status_code, msg->reason_phrase);
		return;
	}

	/* get data */
	body_length = msg->response_body->length;
	header_size = soup_message_headers_get_content_length (msg->response_headers);

	/* size is not known */
	if (header_size < body_length)
		return;

	/* calulate percentage */
	percentage = (100 * body_length) / header_size;
	g_debug ("%s progress: %i%%", gs_app_get_id (helper->app), percentage);
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

	g_debug ("downloading %s from %s", uri, priv->name);
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
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to get shell extensions: %s",
			     msg->response_body->data);
		return NULL;
	}
	return g_bytes_new (msg->response_body->data,
			    msg->response_body->length);
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

	g_debug ("downloading %s to %s from %s", uri, filename, priv->name);
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
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to get shell extensions: %s",
			     msg->response_body->data);
		return FALSE;
	}
	if (!g_file_set_contents (filename,
				  msg->response_body->data,
				  msg->response_body->length,
				  &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to save firmware: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

/**
 * gs_plugin_cache_lookup:
 * @plugin: a #GsPlugin
 * @key: a string
 *
 * Looks up an application object from the per-plugin cache
 *
 * Returns: (transfer full) (nullable): the #GsApp, or %NULL
 **/
GsApp *
gs_plugin_cache_lookup (GsPlugin *plugin, const gchar *key)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	GsApp *app;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->cache_mutex);

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	app = g_hash_table_lookup (priv->cache, key);
	if (app == NULL)
		return NULL;
	return g_object_ref (app);
}

/**
 * gs_plugin_cache_add:
 * @plugin: a #GsPlugin
 * @key: a string
 * @app: a #GsApp
 *
 * Adds an application to the per-plugin cache. This is optional,
 * and the plugin can use the cache however it likes.
 **/
void
gs_plugin_cache_add (GsPlugin *plugin, const gchar *key, GsApp *app)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->cache_mutex);

	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (key != NULL);
	g_return_if_fail (GS_IS_APP (app));

	if (g_hash_table_lookup (priv->cache, key) == app)
		return;
	g_hash_table_insert (priv->cache, g_strdup (key), g_object_ref (app));
}

/**
 * gs_plugin_cache_invalidate:
 * @plugin: a #GsPlugin
 *
 * Invalidate the per-plugin cache by marking all entries as invalid.
 * This is optional, and the plugin can evict the cache whenever it likes.
 **/
void
gs_plugin_cache_invalidate (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->cache_mutex);

	g_return_if_fail (GS_IS_PLUGIN (plugin));

	g_hash_table_remove_all (priv->cache);
}

/**
 * gs_plugin_set_property:
 */
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

/**
 * gs_plugin_get_property:
 */
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

/**
 * gs_plugin_class_init:
 */
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
}

/**
 * gs_plugin_init:
 */
static void
gs_plugin_init (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	guint i;

	for (i = 0; i < GS_PLUGIN_RULE_LAST; i++)
		priv->rules[i] = g_ptr_array_new_with_free_func (g_free);

	priv->enabled = TRUE;
	priv->priority = 0.f;
	priv->scale = 1;
	priv->profile = as_profile_new ();
	priv->cache = g_hash_table_new_full (g_str_hash,
					     g_str_equal,
					     g_free,
					     (GDestroyNotify) g_object_unref);
	g_mutex_init (&priv->cache_mutex);
	g_rw_lock_init (&priv->rwlock);
}

/**
 * gs_plugin_new:
 *
 * Creates a new plugin.
 *
 * Returns: a #GsPlugin
 **/
GsPlugin *
gs_plugin_new (void)
{
	GsPlugin *plugin;
	plugin = g_object_new (GS_TYPE_PLUGIN, NULL);
	return plugin;
}

/* vim: set noexpandtab: */
