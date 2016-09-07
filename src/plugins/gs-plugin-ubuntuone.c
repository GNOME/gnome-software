/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd
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

#include <string.h>
#include <json-glib/json-glib.h>
#include <gnome-software.h>

// Documented in http://canonical-identity-provider.readthedocs.io
#define UBUNTU_LOGIN_HOST "https://login.ubuntu.com"

struct GsPluginData {
	GsAuth *auth;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* check that we are running on Ubuntu */
	if (!gs_plugin_check_distro_id (plugin, "ubuntu")) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as we're not Ubuntu", gs_plugin_get_name (plugin));
		return;
	}

	priv->auth = gs_auth_new (gs_plugin_get_name (plugin));
	gs_auth_set_provider_name (priv->auth, "Ubuntu One");
	gs_auth_set_provider_schema (priv->auth, "com.ubuntu.UbuntuOne.GnomeSoftware");
        //gs_auth_set_provider_logo (priv->auth, "...");
	gs_plugin_add_auth (plugin, priv->auth);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* load from disk */
	gs_auth_add_metadata (priv->auth, "consumer-key", NULL);
	gs_auth_add_metadata (priv->auth, "consumer-secret", NULL);
	gs_auth_add_metadata (priv->auth, "token-key", NULL);
	gs_auth_add_metadata (priv->auth, "token-secret", NULL);
	if (!gs_auth_store_load (priv->auth,
				 GS_AUTH_STORE_FLAG_USERNAME |
				 GS_AUTH_STORE_FLAG_METADATA,
				 cancellable, error))
		return FALSE;

	/* success */
	return TRUE;
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_clear_object (&priv->auth);
}

gboolean
gs_plugin_auth_login (GsPlugin *plugin, GsAuth *auth,
		      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonNode) json_root = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autofree gchar *data = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(SoupMessage) msg = NULL;
	guint status_code;
	g_autoptr(JsonParser) parser = NULL;
	JsonNode *response_root;
	const gchar *tmp;

	if (auth != priv->auth)
		return TRUE;

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "token_name");
	json_builder_add_string_value (builder, "GNOME Software");
	json_builder_set_member_name (builder, "email");
	json_builder_add_string_value (builder, gs_auth_get_username (auth));
	json_builder_set_member_name (builder, "password");
	json_builder_add_string_value (builder, gs_auth_get_password (auth));
	if (gs_auth_get_pin (auth)) {
		json_builder_set_member_name (builder, "otp");
		json_builder_add_string_value (builder, gs_auth_get_pin (auth));
	}
	json_builder_end_object (builder);

	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "Failed to generate JSON request");
		return FALSE;
	}

	uri = g_strdup_printf ("%s/api/v2/tokens/oauth", UBUNTU_LOGIN_HOST);
	msg = soup_message_new (SOUP_METHOD_POST, uri);
	soup_message_set_request (msg, "application/json", SOUP_MEMORY_COPY, data, strlen (data));
	status_code = soup_session_send_message (gs_plugin_get_soup_session (plugin), msg);

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, msg->response_body->data, -1, error))
		return FALSE;
	response_root = json_parser_get_root (parser);

	if (status_code != SOUP_STATUS_OK) {
		const gchar *message, *code;

		message = json_object_get_string_member (json_node_get_object (response_root), "message");
		code = json_object_get_string_member (json_node_get_object (response_root), "code");

		if (g_strcmp0 (code, "INVALID_CREDENTIALS") == 0 ||
		    g_strcmp0 (code, "EMAIL_INVALIDATED") == 0 ||
		    g_strcmp0 (code, "TWOFACTOR_FAILURE") == 0) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_AUTH_INVALID,
					     message);
		} else if (g_strcmp0 (code, "ACCOUNT_SUSPENDED") == 0) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_ACCOUNT_SUSPENDED,
					     message);
		} else if (g_strcmp0 (code, "ACCOUNT_DEACTIVATED") == 0) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_ACCOUNT_DEACTIVATED,
					     message);
		} else if (g_strcmp0 (code, "TWOFACTOR_REQUIRED") == 0) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_PIN_REQUIRED,
					     message);
		} else {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     message);
		}
		return FALSE;
	}

	/* consumer-key */
	tmp = json_object_get_string_member (json_node_get_object (response_root), "consumer_key");
	if (tmp == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "Response from %s missing required field consumer_key",
			     UBUNTU_LOGIN_HOST);
		return FALSE;
	}
	gs_auth_add_metadata (auth, "consumer-key", tmp);

	/* consumer-secret */
	tmp = json_object_get_string_member (json_node_get_object (response_root), "consumer_secret");
	if (tmp == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "Response from %s missing required field consumer_secret",
			     UBUNTU_LOGIN_HOST);
		return FALSE;
	}
	gs_auth_add_metadata (auth, "consumer-secret", tmp);

	/* token-key */
	tmp = json_object_get_string_member (json_node_get_object (response_root), "token_key");
	if (tmp == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "Response from %s missing required field token_key",
			     UBUNTU_LOGIN_HOST);
		return FALSE;
	}
	gs_auth_add_metadata (auth, "token-key", tmp);

	/* token-secret */
	tmp = json_object_get_string_member (json_node_get_object (response_root), "token_secret");
	if (tmp == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "Response from %s missing required field token_secret",
			     UBUNTU_LOGIN_HOST);
		return FALSE;
	}
	gs_auth_add_metadata (auth, "token-secret", tmp);

	/* store */
	if (!gs_auth_store_save (auth,
				 GS_AUTH_STORE_FLAG_USERNAME |
				 GS_AUTH_STORE_FLAG_METADATA,
				 cancellable, error))
		return FALSE;

	gs_auth_add_flags (priv->auth, GS_AUTH_FLAG_VALID);

	return TRUE;
}

gboolean
gs_plugin_auth_lost_password (GsPlugin *plugin, GsAuth *auth,
			      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (auth != priv->auth)
		return TRUE;

	/* return with data */
	g_set_error (error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_AUTH_INVALID,
		     "do online using @%s/+forgot_password", UBUNTU_LOGIN_HOST);
	return FALSE;
}

gboolean
gs_plugin_auth_register (GsPlugin *plugin, GsAuth *auth,
			 GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (auth != priv->auth)
		return TRUE;

	/* return with data */
	g_set_error (error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_AUTH_INVALID,
		     "do online using @%s/+login", UBUNTU_LOGIN_HOST);
	return FALSE;
}
