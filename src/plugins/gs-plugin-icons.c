/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#define _GNU_SOURCE
#include <string.h>

#include <gnome-software.h>

/*
 * SECTION:
 * Loads remote icons and converts them into local cached ones.
 *
 * It is provided so that each plugin handling icons does not
 * have to handle the download and caching functionality.
 */

struct GsPluginData {
	GtkIconTheme		*icon_theme;
	GMutex			 icon_theme_lock;
	GHashTable		*icon_theme_paths;
};

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->icon_theme = gtk_icon_theme_new ();
	priv->icon_theme_paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_mutex_init (&priv->icon_theme_lock);
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->icon_theme);
	g_hash_table_unref (priv->icon_theme_paths);
	g_mutex_clear (&priv->icon_theme_lock);
}

/**
 * gs_plugin_order_after:
 */
const gchar **
gs_plugin_order_after (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",		/* needs remote icons downloaded */
		"epiphany",		/* "" */
		NULL };
	return deps;
}

/**
 * gs_plugin_icons_download:
 */
static gboolean
gs_plugin_icons_download (GsPlugin *plugin, const gchar *uri, const gchar *filename, GError **error)
{
	guint status_code;
	g_autoptr(GdkPixbuf) pixbuf_new = NULL;
	g_autoptr(GdkPixbuf) pixbuf = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* create the GET data */
	msg = soup_message_new (SOUP_METHOD_GET, uri);
	if (msg == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s is not a valid URL", uri);
		return FALSE;
	}

	/* set sync request */
	status_code = soup_session_send_message (gs_plugin_get_soup_session (plugin), msg);
	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to download icon %s: %s",
			     uri, soup_status_get_phrase (status_code));
		return FALSE;
	}

	/* we're assuming this is a 64x64 png file, resize if not */
	stream = g_memory_input_stream_new_from_data (msg->response_body->data,
						      msg->response_body->length,
						      NULL);
	pixbuf = gdk_pixbuf_new_from_stream (stream, NULL, error);
	if (pixbuf == NULL)
		return FALSE;
	if (gdk_pixbuf_get_height (pixbuf) == 64 &&
	    gdk_pixbuf_get_width (pixbuf) == 64) {
		pixbuf_new = g_object_ref (pixbuf);
	} else {
		pixbuf_new = gdk_pixbuf_scale_simple (pixbuf, 64, 64,
						      GDK_INTERP_BILINEAR);
	}

	/* write file */
	return gdk_pixbuf_save (pixbuf_new, filename, "png", error, NULL);
}

/**
 * gs_plugin_icons_load_local:
 */
static gboolean
gs_plugin_icons_load_local (GsPlugin *plugin, GsApp *app, GError **error)
{
	AsIcon *icon;
	g_autoptr(GdkPixbuf) pixbuf = NULL;

	icon = gs_app_get_icon (app);
	if (as_icon_get_filename (icon) == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s icon has no filename",
			     gs_app_get_id (app));
		return FALSE;
	}
	pixbuf = gdk_pixbuf_new_from_file_at_size (as_icon_get_filename (icon),
						   64 * gs_plugin_get_scale (plugin),
						   64 * gs_plugin_get_scale (plugin),
						   error);
	if (pixbuf == NULL)
		return FALSE;
	gs_app_set_pixbuf (app, pixbuf);
	return TRUE;
}

/**
 * gs_plugin_icons_add_theme_path:
 */
static void
gs_plugin_icons_add_theme_path (GsPlugin *plugin, const gchar *path)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (path == NULL)
		return;
	if (!g_hash_table_contains (priv->icon_theme_paths, path)) {
		gtk_icon_theme_prepend_search_path (priv->icon_theme, path);
		g_hash_table_add (priv->icon_theme_paths, g_strdup (path));
	}
}

/**
 * gs_plugin_icons_load_stock:
 */
static gboolean
gs_plugin_icons_load_stock (GsPlugin *plugin, GsApp *app, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsIcon *icon;
	g_autoptr(GdkPixbuf) pixbuf = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->icon_theme_lock);

	icon = gs_app_get_icon (app);
	if (as_icon_get_name (icon) == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s icon has no name",
			     gs_app_get_id (app));
		return FALSE;
	}
	gs_plugin_icons_add_theme_path (plugin, as_icon_get_prefix (icon));
	pixbuf = gtk_icon_theme_load_icon (priv->icon_theme,
					   as_icon_get_name (icon),
					   64 * gs_plugin_get_scale (plugin),
					   GTK_ICON_LOOKUP_USE_BUILTIN |
					   GTK_ICON_LOOKUP_FORCE_SIZE,
					   error);
	if (pixbuf == NULL)
		return FALSE;
	gs_app_set_pixbuf (app, pixbuf);
	return TRUE;
}

/**
 * gs_plugin_refine_app:
 */
gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	AsIcon *ic;
	const gchar *fn;
	gchar *found;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON) == 0)
		return TRUE;

	/* invalid */
	if (gs_app_get_pixbuf (app) != NULL)
		return TRUE;
	ic = gs_app_get_icon (app);
	if (ic == NULL)
		return TRUE;

	/* handle different icon types */
	if (as_icon_get_kind (ic) == AS_ICON_KIND_LOCAL)
		return gs_plugin_icons_load_local (plugin, app, error);
	if (as_icon_get_kind (ic) == AS_ICON_KIND_STOCK)
		return gs_plugin_icons_load_stock (plugin, app, error);

	/* not applicable for remote */
	if (as_icon_get_url (ic) == NULL)
		return TRUE;
	if (as_icon_get_filename (ic) == NULL)
		return TRUE;

	/* a REMOTE that's really LOCAL */
	if (g_str_has_prefix (as_icon_get_url (ic), "file://")) {
		as_icon_set_filename (ic, as_icon_get_url (ic) + 7);
		as_icon_set_kind (ic, AS_ICON_KIND_LOCAL);
		return gs_plugin_icons_load_local (plugin, app, error);
	}

	/* convert filename from jpg to png */
	fn = as_icon_get_filename (ic);
	found = g_strstr_len (fn, -1, ".jpg");
	if (found != NULL)
		memcpy (found, ".png", 4);

	/* create runtime dir and download */
	if (!gs_mkdir_parent (fn, error))
		return FALSE;
	if (!gs_plugin_icons_download (plugin, as_icon_get_url (ic), fn, error))
		return FALSE;
	as_icon_set_kind (ic, AS_ICON_KIND_LOCAL);
	return gs_plugin_icons_load_local (plugin, app, error);
}
