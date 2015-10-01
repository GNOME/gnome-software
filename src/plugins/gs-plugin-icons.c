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

#include <glib/gi18n.h>
#include <libsoup/soup.h>

#include <gs-plugin.h>
#include <gs-utils.h>

struct GsPluginPrivate {
	SoupSession		*session;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "icons";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",		/* needs remote icons downloaded */
		"epiphany",		/* "" */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	if (plugin->priv->session != NULL)
		g_object_unref (plugin->priv->session);
}

/**
 * gs_plugin_setup_networking:
 */
static gboolean
gs_plugin_setup_networking (GsPlugin *plugin, GError **error)
{
	/* already set up */
	if (plugin->priv->session != NULL)
		return TRUE;

	/* set up a session */
	plugin->priv->session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT,
	                                                       "gnome-software",
	                                                       NULL);
	if (plugin->priv->session == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s: failed to setup networking",
			     plugin->name);
		return FALSE;
	}
	return TRUE;
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

	/* ensure networking is set up */
	if (!gs_plugin_setup_networking (plugin, error))
		return FALSE;

	/* set sync request */
	status_code = soup_session_send_message (plugin->priv->session, msg);
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
 * gs_plugin_refine:
 */
static gboolean
gs_plugin_refine_app (GsPlugin *plugin, GsApp *app, GError **error)
{
	AsIcon *ic;
	const gchar *fn;
	gchar *found;

	/* not applicable */
	ic = gs_app_get_icon (app);
	if (as_icon_get_url (ic) == NULL)
		return TRUE;
	if (as_icon_get_filename (ic) == NULL)
		return TRUE;

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
	return gs_app_load_icon (app, plugin->scale, error);
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList **list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	GError *error_local = NULL;
	GList *l;
	GsApp *app;

	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_pixbuf (app) != NULL)
			continue;
		if (gs_app_get_icon (app) == NULL)
			continue;
		if (!gs_plugin_refine_app (plugin, app, &error_local)) {
			g_warning ("ignoring: %s", error_local->message);
			g_clear_error (&error_local);
		}
	}
	return TRUE;
}
