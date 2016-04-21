/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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

/* Introduction:
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

#include <glib.h>
#include <gio/gdesktopappinfo.h>

#include "gs-plugin-private.h"
#include "gs-os-release.h"

struct GsPluginPrivate {
	AsProfile		*profile;
	GHashTable		*cache;
	GModule			*module;
	GRWLock			 rwlock;
	GsPluginData		*data;			/* for gs-plugin-{name}.c */
	GsPluginFlags		 flags;
	SoupSession		*soup_session;
	const gchar		**conflicts;		/* allow-none */
	const gchar		**order_after;		/* allow-none */
	const gchar		**order_before;		/* allow-none */
	gboolean		 enabled;
	gchar			*locale;		/* allow-none */
	gint			 scale;
	guint			 priority;
	guint			 timer_id;
};

typedef const gchar	**(*GsPluginGetDepsFunc)	(GsPlugin	*plugin);

#define gs_plugin_get_instance_private(p) (p->priv);

/**
 * gs_plugin_status_to_string:
 */
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
 * gs_plugin_new:
 **/
GsPlugin *
gs_plugin_new (void)
{
	GsPlugin *plugin = g_slice_new0 (GsPlugin);
	plugin->priv = g_new0 (GsPluginPrivate, 1);
	plugin->priv->enabled = TRUE;
	plugin->priv->priority = 0.f;
	plugin->priv->scale = 1;
	plugin->priv->profile = as_profile_new ();
	plugin->priv->cache = g_hash_table_new_full (g_str_hash,
						     g_str_equal,
						     g_free,
						     (GDestroyNotify) g_object_unref);
	g_rw_lock_init (&plugin->priv->rwlock);
	return plugin;
}

/**
 * gs_plugin_create:
 **/
GsPlugin *
gs_plugin_create (const gchar *filename, GError **error)
{
	GModule *module;
	GsPluginGetDepsFunc order_after = NULL;
	GsPluginGetDepsFunc order_before = NULL;
	GsPluginGetDepsFunc plugin_conflicts = NULL;
	GsPlugin *plugin = NULL;
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

	/* get plugins this plugin depends on */
	g_module_symbol (module,
			 "gs_plugin_order_after",
			 (gpointer *) &order_after);
	g_module_symbol (module,
			 "gs_plugin_order_before",
			 (gpointer *) &order_before);
	g_module_symbol (module,
			 "gs_plugin_get_conflicts",
			 (gpointer *) &plugin_conflicts);

	/* create new plugin */
	plugin = gs_plugin_new ();
	plugin->priv->module = module;
	plugin->priv->order_after = order_after != NULL ? order_after (plugin) : NULL;
	plugin->priv->order_before = order_before != NULL ? order_before (plugin) : NULL;
	plugin->priv->conflicts = plugin_conflicts != NULL ? plugin_conflicts (plugin) : NULL;
	plugin->name = g_strdup (basename + 13);
	return plugin;
}

/**
 * gs_plugin_free:
 **/
void
gs_plugin_free (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	if (priv->timer_id > 0)
		g_source_remove (priv->timer_id);
	g_free (plugin->name);
	g_free (priv->data);
	g_free (priv->locale);
	g_rw_lock_clear (&priv->rwlock);
	g_object_unref (priv->profile);
	g_object_unref (priv->soup_session);
	g_hash_table_unref (priv->cache);
	g_module_close (priv->module);
	g_free (priv);
	g_slice_free (GsPlugin, plugin);
}

/**
 * gs_plugin_get_data:
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
 *
 * FIXME: unexport soon
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

	g_debug ("plugin no longer recently active: %s", plugin->name);
	priv->flags &= ~GS_PLUGIN_FLAGS_RECENT;
	priv->timer_id = 0;
	return FALSE;
}

/**
 * gs_plugin_action_stop:
 *
 * FIXME: unexport soon
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
 *
 * FIXME: unexport soon
 **/
GModule *
gs_plugin_get_module (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->module;
}

/**
 * gs_plugin_get_enabled:
 **/
gboolean
gs_plugin_get_enabled (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->enabled;
}

/**
 * gs_plugin_set_enabled:
 **/
void
gs_plugin_set_enabled (GsPlugin *plugin, gboolean enabled)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->enabled = enabled;
}

/**
 * gs_plugin_get_scale:
 **/
guint
gs_plugin_get_scale (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->scale;
}

/**
 * gs_plugin_set_scale:
 **/
void
gs_plugin_set_scale (GsPlugin *plugin, guint scale)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->scale = scale;
}

/**
 * gs_plugin_get_priority:
 **/
guint
gs_plugin_get_priority (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->priority;
}

/**
 * gs_plugin_set_priority:
 **/
void
gs_plugin_set_priority (GsPlugin *plugin, guint priority)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	priv->priority = priority;
}

/**
 * gs_plugin_get_locale:
 **/
const gchar *
gs_plugin_get_locale (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->locale;
}

/**
 * gs_plugin_set_locale:
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
 **/
AsProfile *
gs_plugin_get_profile (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->profile;
}

/**
 * gs_plugin_set_profile:
 **/
void
gs_plugin_set_profile (GsPlugin *plugin, AsProfile *profile)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_set_object (&priv->profile, profile);
}

/**
 * gs_plugin_get_soup_session:
 **/
SoupSession *
gs_plugin_get_soup_session (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->soup_session;
}

/**
 * gs_plugin_set_soup_session:
 **/
void
gs_plugin_set_soup_session (GsPlugin *plugin, SoupSession *soup_session)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	g_set_object (&priv->soup_session, soup_session);
}

/**
 * gs_plugin_has_flags:
 **/
gboolean
gs_plugin_has_flags (GsPlugin *plugin, GsPluginFlags flags)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return (priv->flags & flags) > 0;
}

/**
 * gs_plugin_set_running_other:
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
 * gs_plugin_get_order_after:
 **/
const gchar **
gs_plugin_get_order_after (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->order_after;
}

/**
 * gs_plugin_get_order_before:
 **/
const gchar **
gs_plugin_get_order_before (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->order_before;
}

/**
 * gs_plugin_get_conflicts:
 **/
const gchar **
gs_plugin_get_conflicts (GsPlugin *plugin)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	return priv->conflicts;
}

/**
 * gs_plugin_check_distro_id:
 **/
gboolean
gs_plugin_check_distro_id (GsPlugin *plugin, const gchar *distro_id)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *id = NULL;

	/* check that we are running on Fedora */
	id = gs_os_release_get_id (&error);
	if (id == NULL) {
		g_debug ("could not parse os-release: %s", error->message);
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

	/* call back into the loader */
	helper->plugin->status_update_fn (helper->plugin,
					  helper->app,
					  helper->status,
					  helper->plugin->status_update_user_data);
	if (helper->app != NULL)
		g_object_unref (helper->app);
	g_slice_free (GsPluginStatusHelper, helper);
	return FALSE;
}

/**
 * gs_plugin_status_update:
 **/
void
gs_plugin_status_update (GsPlugin *plugin, GsApp *app, GsPluginStatus status)
{
	GsPluginStatusHelper *helper;

	helper = g_slice_new0 (GsPluginStatusHelper);
	helper->plugin = plugin;
	helper->status = status;
	if (app != NULL)
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
	appinfo = G_APP_INFO (g_desktop_app_info_new (desktop_id));
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
	plugin->updates_changed_fn (plugin, plugin->updates_changed_user_data);
	return FALSE;
}

/**
 * gs_plugin_updates_changed:
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
	guint percentage;
	goffset header_size;
	goffset body_length;

	/* cancelled? */
	if (g_cancellable_is_cancelled (helper->cancellable)) {
		g_debug ("cancelling download of %s",
			 gs_app_get_id (helper->app));
		soup_session_cancel_message (helper->plugin->priv->soup_session,
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
 */
GBytes *
gs_plugin_download_data (GsPlugin *plugin,
			 GsApp *app,
			 const gchar *uri,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginDownloadHelper helper;
	guint status_code;
	g_autoptr(SoupMessage) msg = NULL;
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	g_debug ("downloading %s from %s", uri, plugin->name);
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
 */
gboolean
gs_plugin_download_file (GsPlugin *plugin,
			 GsApp *app,
			 const gchar *uri,
			 const gchar *filename,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginDownloadHelper helper;
	guint status_code;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(SoupMessage) msg = NULL;
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);

	g_debug ("downloading %s to %s from %s", uri, filename, plugin->name);
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
 */
GsApp *
gs_plugin_cache_lookup (GsPlugin *plugin, const gchar *key)
{
	GsApp *app;
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	app = g_hash_table_lookup (priv->cache, key);
	if (app == NULL)
		return NULL;
	return g_object_ref (app);
}

/**
 * gs_plugin_cache_add:
 */
void
gs_plugin_cache_add (GsPlugin *plugin, const gchar *key, GsApp *app)
{
	GsPluginPrivate *priv = gs_plugin_get_instance_private (plugin);
	if (g_hash_table_lookup (priv->cache, key) == app)
		return;
	g_hash_table_insert (priv->cache, g_strdup (key), g_object_ref (app));
}

/* vim: set noexpandtab: */
