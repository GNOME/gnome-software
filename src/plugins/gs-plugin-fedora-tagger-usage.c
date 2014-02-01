/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <libsoup/soup.h>
#include <string.h>
#include <sqlite3.h>
#include <stdlib.h>

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
	return "fedora-tagger-usage";
}

#define GS_PLUGIN_FEDORA_TAGGER_SERVER		"https://apps.fedoraproject.org/tagger"

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);

	/* this is opt-in, and turned off by default */
	if (!g_settings_get_boolean (plugin->settings, "enable-usage")) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as 'enable-usage' disabled in GSettings",
			 plugin->name);
		return;
	}

	/* check that we are running on Fedora */
	if (!gs_plugin_check_distro_id (plugin, "fedora")) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as we're not Fedora", plugin->name);
		return;
	}
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"packagekit",		/* after the install/remove has succeeded */
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
	gboolean ret = TRUE;

	/* already set up */
	if (plugin->priv->session != NULL)
		goto out;

	/* set up a session */
	plugin->priv->session = soup_session_sync_new_with_options (SOUP_SESSION_USER_AGENT,
								    "gnome-software",
								    SOUP_SESSION_TIMEOUT, 5000,
								    NULL);
	if (plugin->priv->session == NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s: failed to setup networking",
			     plugin->name);
		goto out;
	}
	soup_session_add_feature_by_type (plugin->priv->session,
					  SOUP_TYPE_PROXY_RESOLVER_DEFAULT);
out:
	return ret;
}

/**
 * gs_plugin_app_set_usage_pkg:
 */
static gboolean
gs_plugin_app_set_usage_pkg (GsPlugin *plugin,
			     const gchar *pkgname,
			     gboolean is_install,
			     GError **error)
{
	SoupMessage *msg = NULL;
	gchar *data = NULL;
	gchar *error_msg = NULL;
	gchar *uri = NULL;
	guint status_code;

	/* create the PUT data */
	uri = g_strdup_printf ("%s/api/v1/usage/%s/",
			       GS_PLUGIN_FEDORA_TAGGER_SERVER,
			       pkgname);
	data = g_strdup_printf ("pkgname=%s&usage=%s",
				pkgname,
				is_install ? "true" : "false");
	msg = soup_message_new (SOUP_METHOD_PUT, uri);
	soup_message_set_request (msg, SOUP_FORM_MIME_TYPE_URLENCODED,
				  SOUP_MEMORY_COPY, data, strlen (data));

	/* set sync request */
	status_code = soup_session_send_message (plugin->priv->session, msg);
	if (status_code != SOUP_STATUS_OK) {
		g_debug ("Failed to set usage on fedora-tagger: %s",
			 soup_status_get_phrase (status_code));
		if (msg->response_body->data != NULL) {
			g_debug ("the error given was: %s",
				 msg->response_body->data);
		}
	} else {
		g_debug ("Got response: %s", msg->response_body->data);
	}

	g_free (error_msg);
	g_free (data);
	g_free (uri);
	if (msg != NULL)
		g_object_unref (msg);
	return TRUE;
}

/**
 * gs_plugin_app_set_usage_app:
 */
static gboolean
gs_plugin_app_set_usage_app (GsPlugin *plugin,
			     GsApp *app,
			     gboolean is_install,
			     GError **error)
{
	GPtrArray *sources;
	const gchar *pkgname;
	gboolean ret = TRUE;
	guint i;

	/* get the package name */
	sources = gs_app_get_sources (app);
	if (sources->len == 0)
		goto out;

	/* ensure networking is set up */
	ret = gs_plugin_setup_networking (plugin, error);
	if (!ret)
		goto out;

	/* tell fedora-tagger about this package */
	for (i = 0; i < sources->len; i++) {
		pkgname = g_ptr_array_index (sources, i);
		ret = gs_plugin_app_set_usage_pkg (plugin,
						   pkgname,
						   is_install,
						   error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * gs_plugin_app_install:
 */
gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	return gs_plugin_app_set_usage_app (plugin, app, TRUE, error);
}

/**
 * gs_plugin_app_remove:
 */
gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	return gs_plugin_app_set_usage_app (plugin, app, FALSE, error);
}
