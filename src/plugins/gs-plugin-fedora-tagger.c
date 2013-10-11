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
	return "fedora-tagger";
}

#define GS_PLUGIN_FEDORA_TAGGER_OS_RELEASE_FN	"/etc/os-release"
#define GS_PLUGIN_FEDORA_TAGGER_SERVER		"https://apps.fedoraproject.org/tagger"

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	GError *error = NULL;
	gboolean ret;
	gchar *data = NULL;

	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);

	/* check that we are running on Fedora */
	ret = g_file_get_contents (GS_PLUGIN_FEDORA_TAGGER_OS_RELEASE_FN,
				   &data, NULL, &error);
	if (!ret) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_warning ("disabling '%s' as %s could not be read: %s",
			   plugin->name,
			   GS_PLUGIN_FEDORA_TAGGER_OS_RELEASE_FN,
			   error->message);
		g_error_free (error);
		goto out;
	}
	if (g_strstr_len (data, -1, "ID=fedora\n") == NULL) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as %s suggests we're not Fedora",
			 plugin->name, GS_PLUGIN_FEDORA_TAGGER_OS_RELEASE_FN);
		goto out;
	}

	/* setup networking */
	plugin->priv->session = soup_session_sync_new_with_options (SOUP_SESSION_USER_AGENT,
								    "gnome-software",
								    SOUP_SESSION_TIMEOUT, 5000,
								    NULL);
	if (plugin->priv->session != NULL) {
		soup_session_add_feature_by_type (plugin->priv->session,
						  SOUP_TYPE_PROXY_RESOLVER_DEFAULT);
	}
out:
	g_free (data);
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 1.0f;
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
 * gs_plugin_parse_json:
 *
 * This is a quick and dirty JSON parser that extracts one line from the
 * JSON formatted data. Sorry JsonGlib, you look awesome, but you're just too
 * heavy for an error message.
 */
static gchar *
gs_plugin_parse_json (const gchar *data, gsize data_len, const gchar *key)
{
	GString *string;
	gchar *key_full;
	gchar *value = NULL;
	gchar **split;
	guint i;
	gchar *tmp;
	guint len;

	/* format the key to match what JSON returns */
	key_full = g_strdup_printf ("\"%s\":", key);

	/* replace escaping with something sane */
	string = g_string_new_len (data, data_len);
	gs_string_replace (string, "\\\"", "'");

	/* find the line that corresponds to our key */
	split = g_strsplit (string->str, "\n", -1);
	for (i = 0; split[i] != NULL; i++) {
		tmp = g_strchug (split[i]);
		if (g_str_has_prefix (tmp, key_full)) {
			tmp += strlen (key_full);

			/* remove leading chars */
			tmp = g_strstrip (tmp);
			if (tmp[0] == '\"')
				tmp += 1;

			/* remove trailing chars */
			len = strlen (tmp);
			if (tmp[len-1] == ',')
				len -= 1;
			if (tmp[len-1] == '\"')
				len -= 1;
			value = g_strndup (tmp, len);
		}
	}
	g_strfreev (split);
	g_string_free (string, TRUE);
	return value;
}

/**
 * gs_plugin_app_set_rating:
 */
gboolean
gs_plugin_app_set_rating (GsPlugin *plugin,
			  GsApp *app,
			  GCancellable *cancellable,
			  GError **error)
{
	SoupMessage *msg = NULL;
	const gchar *pkgname;
	gchar *data = NULL;
	gchar *error_msg = NULL;
	gchar *uri = NULL;
	guint status_code;

	/* get the package name */
	pkgname = gs_app_get_source (app);
	if (pkgname == NULL) {
		g_warning ("no pkgname for %s", gs_app_get_id (app));
		goto out;
	}

	/* create the PUT data */
	uri = g_strdup_printf ("%s/api/v1/rating/%s/",
			       GS_PLUGIN_FEDORA_TAGGER_SERVER,
			       pkgname);
	data = g_strdup_printf ("pkgname=%s&rating=%i",
				pkgname,
				gs_app_get_rating (app));
	msg = soup_message_new (SOUP_METHOD_PUT, uri);
	soup_message_set_request (msg, SOUP_FORM_MIME_TYPE_URLENCODED,
				  SOUP_MEMORY_COPY, data, strlen (data));

	/* set sync request */
	status_code = soup_session_send_message (plugin->priv->session, msg);
	if (status_code != SOUP_STATUS_OK) {
		g_debug ("Failed to set rating on fedora-tagger: %s",
			 soup_status_get_phrase (status_code));
		if (msg->response_body->data != NULL) {
			error_msg = gs_plugin_parse_json (msg->response_body->data,
							  msg->response_body->length,
							  "error");
			g_debug ("the error given was: %s", error_msg);
		}
	} else {
		g_debug ("Got response: %s", msg->response_body->data);
	}
out:
	g_free (error_msg);
	g_free (data);
	g_free (uri);
	if (msg != NULL)
		g_object_unref (msg);
	return TRUE;
}
