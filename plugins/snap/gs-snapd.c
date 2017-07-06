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

#include <stdlib.h>
#include <string.h>
#include <gs-plugin.h>
#include <libsoup/soup.h>
#include <gio/gunixsocketaddress.h>

#include <gnome-software.h>

#include "gs-snapd.h"

// snapd API documentation is at https://github.com/snapcore/snapd/blob/master/docs/rest.md

#define SNAPD_SOCKET "/run/snapd.socket"

gboolean
gs_snapd_exists (void)
{
	return g_file_test (SNAPD_SOCKET, G_FILE_TEST_EXISTS);
}

static GSocket *
open_snapd_socket (GCancellable *cancellable, GError **error)
{
	GSocket *socket;
	g_autoptr(GSocketAddress) address = NULL;
	g_autoptr(GError) error_local = NULL;

	socket = g_socket_new (G_SOCKET_FAMILY_UNIX,
			       G_SOCKET_TYPE_STREAM,
			       G_SOCKET_PROTOCOL_DEFAULT,
			       &error_local);
	if (!socket) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "Unable to open snapd socket: %s",
			     error_local->message);
		return NULL;
	}
	address = g_unix_socket_address_new (SNAPD_SOCKET);
	if (!g_socket_connect (socket, address, cancellable, &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "Unable to connect snapd socket: %s",
			     error_local->message);
		g_object_unref (socket);
		return NULL;
	}

	return socket;
}

static gboolean
read_from_snapd (GSocket *socket,
		 GByteArray *buffer,
		 gsize *read_offset,
		 gsize size,
		 GCancellable *cancellable,
		 GError **error)
{
	gssize n_read;

	if (*read_offset + size > buffer->len)
		g_byte_array_set_size (buffer, *read_offset + size + 1);
	n_read = g_socket_receive (socket,
				   buffer->data + *read_offset,
				   size,
				   cancellable,
				   error);
	if (n_read < 0) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}
	*read_offset += (gsize) n_read;
	buffer->data[*read_offset] = '\0';

	return TRUE;
}

static gboolean
send_request (const gchar  *method,
	      const gchar  *path,
	      const gchar  *content,
	      const gchar  *macaroon,
	      gchar       **discharges,
	      guint        *status_code,
	      gchar       **reason_phrase,
	      gchar       **response_type,
	      gchar       **response,
	      gsize        *response_length,
	      GCancellable *cancellable,
	      GError      **error)
{
	g_autoptr (GSocket) socket = NULL;
	g_autoptr (GString) request = NULL;
	gssize n_written;
	g_autoptr (GByteArray) buffer = NULL;
	gsize data_length = 0, header_length, body_offset = 0;
	g_autoptr (SoupMessageHeaders) headers = NULL;
	gsize chunk_length = 0, n_required, chunk_offset;
	guint code;

	// NOTE: Would love to use libsoup but it doesn't support unix sockets
	// https://bugzilla.gnome.org/show_bug.cgi?id=727563

	socket = open_snapd_socket (cancellable, error);
	if (socket == NULL)
		return FALSE;

	request = g_string_new ("");
	g_string_append_printf (request, "%s %s HTTP/1.1\r\n", method, path);
	g_string_append (request, "Host:\r\n");
	if (macaroon != NULL) {
		gint i;

		g_string_append_printf (request, "Authorization: Macaroon root=\"%s\"", macaroon);
		for (i = 0; discharges[i] != NULL; i++)
			g_string_append_printf (request, ",discharge=\"%s\"", discharges[i]);
		g_string_append (request, "\r\n");
	}
	if (content)
		g_string_append_printf (request, "Content-Length: %zu\r\n", strlen (content));
	g_string_append (request, "\r\n");
	if (content)
		g_string_append (request, content);

	g_debug ("begin snapd request: %s", request->str);

	/* send HTTP request */
	n_written = g_socket_send (socket, request->str, request->len, cancellable, error);
	if (n_written < 0) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	/* read HTTP headers */
	buffer = g_byte_array_new ();
	while (TRUE) {
		const gchar *divider;

		gsize n_read = data_length;
		if (!read_from_snapd (socket,
				      buffer,
				      &data_length,
				      1024,
				      cancellable,
				      error))
			return FALSE;

		if (n_read == data_length) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_INVALID_FORMAT,
					     "Unable to find header separator in snapd response");
			return FALSE;
		}

		divider = strstr (buffer->data, "\r\n\r\n");
		if (divider != NULL) {
			body_offset = ((guint8*) divider - buffer->data) + 4;
			break;
		}
	}

	/* parse headers */
	headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
	if (!soup_headers_parse_response (buffer->data, (gint) body_offset, headers,
					  NULL, &code, reason_phrase)) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "snapd response HTTP headers not parseable");
		return FALSE;
	}

	if (status_code != NULL)
		*status_code = code;

	/* read content */
	switch (soup_message_headers_get_encoding (headers)) {
	case SOUP_ENCODING_EOF:
		chunk_offset = body_offset;
		while (TRUE) {
			gsize n_read = data_length;
			if (!read_from_snapd (socket,
					      buffer,
					      &data_length,
					      1024,
					      cancellable,
					      error))
				return FALSE;
			if (n_read == data_length)
				break;
			chunk_length += data_length - n_read;
		}
		break;
	case SOUP_ENCODING_CHUNKED:
		// FIXME: support multiple chunks
		while (TRUE) {
			const gchar *divider;
			gsize n_read;

			divider = strstr (buffer->data + body_offset, "\r\n");
			if (divider) {
				chunk_length = strtoul (buffer->data + body_offset, NULL, 16);
				chunk_offset = ((guint8*) divider - buffer->data) + 2;
				break;
			}

			n_read = data_length;
			if (!read_from_snapd (socket,
					      buffer,
					      &data_length,
					      1024,
					      cancellable,
					      error))
				return FALSE;

			if (n_read == data_length) {
				g_set_error_literal (error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_INVALID_FORMAT,
						     "Unable to find chunk header in "
						     "snapd response");
				return FALSE;
			}
		}

		/* check if enough space to read chunk */
		n_required = chunk_offset + chunk_length;
		while (data_length < n_required)
			if (!read_from_snapd (socket,
					      buffer,
					      &data_length,
					      n_required - data_length,
					      cancellable,
					      error))
				return FALSE;
		break;
	case SOUP_ENCODING_CONTENT_LENGTH:
		chunk_offset = body_offset;
		chunk_length = soup_message_headers_get_content_length (headers);
		n_required = chunk_offset + chunk_length;
		while (data_length < n_required) {
			if (!read_from_snapd (socket,
					      buffer,
					      &data_length,
					      n_required - data_length,
					      cancellable,
					      error))
				return FALSE;
		}
		break;
	default:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "Unable to determine content "
				     "length of snapd response");
		return FALSE;
	}

	if (response_type)
		*response_type = g_strdup (soup_message_headers_get_content_type (headers, NULL));
	if (response != NULL) {
		*response = g_malloc (chunk_length + 1);
		memcpy (*response, buffer->data + chunk_offset, chunk_length + 1);
		g_debug ("snapd status %u: %s", code, *response);
	}
	if (response_length)
		*response_length = chunk_length;

	return TRUE;
}

static JsonParser *
parse_result (const gchar *response, const gchar *response_type, GError **error)
{
	g_autoptr(JsonParser) parser = NULL;
	g_autoptr(GError) error_local = NULL;

	if (response_type == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "snapd returned no content type");
		return NULL;
	}
	if (g_strcmp0 (response_type, "application/json") != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "snapd returned unexpected content type %s", response_type);
		return NULL;
	}

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, response, -1, &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "Unable to parse snapd response: %s",
			     error_local->message);
		return NULL;
	}
	if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser))) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "snapd response does is not a valid JSON object");
		return NULL;
	}

	return g_object_ref (parser);
}

JsonObject *
gs_snapd_get_system_info (GCancellable *cancellable, GError **error)
{
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *response = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root, *result;

	if (!send_request ("GET", "/v2/system-info", NULL,
			   NULL, NULL,
			   &status_code, &reason_phrase,
			   &response_type, &response, NULL,
			   cancellable, error))
		return NULL;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return NULL;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return NULL;
	root = json_node_get_object (json_parser_get_root (parser));
	result = json_object_get_object_member (root, "result");
	if (result == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "snapd returned no system information");
		return NULL;
	}

	return json_object_ref (result);
}

JsonObject *
gs_snapd_list_one (const gchar *macaroon, gchar **discharges,
		   const gchar *name,
		   GCancellable *cancellable, GError **error)
{
	g_autofree gchar *path = NULL;
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *response = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root, *result;

	path = g_strdup_printf ("/v2/snaps/%s", name);
	if (!send_request ("GET", path, NULL,
			   macaroon, discharges,
			   &status_code, &reason_phrase,
			   &response_type, &response, NULL,
			   cancellable, error))
		return NULL;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return NULL;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return NULL;
	root = json_node_get_object (json_parser_get_root (parser));
	result = json_object_get_object_member (root, "result");
	if (result == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "snapd returned no results for %s", name);
		return NULL;
	}

	return json_object_ref (result);
}

JsonArray *
gs_snapd_list (const gchar *macaroon, gchar **discharges,
	       GCancellable *cancellable, GError **error)
{
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *response = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root;
	JsonArray *result;

	if (!send_request ("GET", "/v2/snaps", NULL,
			   macaroon, discharges,
			   &status_code, &reason_phrase,
			   &response_type, &response, NULL,
			   cancellable, error))
		return NULL;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return NULL;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return NULL;
	root = json_node_get_object (json_parser_get_root (parser));
	result = json_object_get_array_member (root, "result");
	if (result == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned no result");
		return NULL;
	}

	return json_array_ref (result);
}

JsonArray *
gs_snapd_find (const gchar *macaroon, gchar **discharges,
	       const gchar *section, gboolean match_name, gchar *query,
	       GCancellable *cancellable, GError **error)
{
	g_autoptr(GString) path = NULL;
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *response = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root;
	JsonArray *result;

	path = g_string_new ("/v2/find?");
	if (section != NULL) {
		g_string_append_printf (path, "section=%s", section);
	}
	if (query != NULL) {
		g_autofree gchar *escaped = NULL;

		escaped = soup_uri_encode (query, NULL);
		if (section != NULL)
			g_string_append (path, "&");
		if (match_name)
			g_string_append (path, "name=");
		else
			g_string_append (path, "q=");
		g_string_append (path, escaped);
	}
	if (!send_request ("GET", path->str, NULL,
			   macaroon, discharges,
			   &status_code, &reason_phrase,
			   &response_type, &response, NULL,
			   cancellable, error))
		return NULL;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return NULL;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return NULL;
	root = json_node_get_object (json_parser_get_root (parser));
	result = json_object_get_array_member (root, "result");
	if (result == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned no result");
		return NULL;
	}

	return json_array_ref (result);
}

JsonObject *
gs_snapd_get_interfaces (const gchar *macaroon, gchar **discharges, GCancellable *cancellable, GError **error)
{
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *response = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root;
	JsonObject *result;

	if (!send_request ("GET", "/v2/interfaces", NULL,
			   macaroon, discharges,
			   &status_code, &reason_phrase,
			   &response_type, &response, NULL,
			   cancellable, error))
		return NULL;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return NULL;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return NULL;
	root = json_node_get_object (json_parser_get_root (parser));
	result = json_object_get_object_member (root, "result");
	if (result == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned no result");
		return NULL;
	}

	return json_object_ref (result);
}

static JsonObject *
get_changes (const gchar *macaroon, gchar **discharges,
	     const gchar *change_id,
	     GCancellable *cancellable, GError **error)
{
	g_autofree gchar *path = NULL;
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *response = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root, *result;

	path = g_strdup_printf ("/v2/changes/%s", change_id);
	if (!send_request ("GET", path, NULL,
			   macaroon, discharges,
			   &status_code, &reason_phrase,
			   &response_type, &response, NULL,
			   cancellable, error))
		return NULL;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return NULL;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return NULL;
	root = json_node_get_object (json_parser_get_root (parser));
	result = json_object_get_object_member (root, "result");
	if (result == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned no result");
		return NULL;
	}

	return json_object_ref (result);
}

static gboolean
send_package_action (const gchar *macaroon,
		     gchar **discharges,
		     const gchar *name,
		     const gchar *action,
		     GsSnapdProgressCallback callback,
		     gpointer user_data,
		     GCancellable *cancellable,
		     GError **error)
{
	g_autofree gchar *content = NULL, *path = NULL;
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *response = NULL;
	g_autofree gchar *status = NULL;
	g_autoptr(JsonParser) parser = NULL;
	JsonObject *root, *result;
	const gchar *type;

	content = g_strdup_printf ("{\"action\": \"%s\"}", action);
	path = g_strdup_printf ("/v2/snaps/%s", name);
	if (!send_request ("POST", path, content,
			   macaroon, discharges,
			   &status_code, &reason_phrase,
			   &response_type, &response, NULL,
			   cancellable, error))
		return FALSE;

	if (status_code == SOUP_STATUS_UNAUTHORIZED) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_AUTH_REQUIRED,
				     "Requires authentication with @snapd");
		return FALSE;
	}

	if (status_code != SOUP_STATUS_ACCEPTED) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return FALSE;
	}

	parser = parse_result (response, response_type, error);
	if (parser == NULL)
		return FALSE;

	root = json_node_get_object (json_parser_get_root (parser));
	type = json_object_get_string_member (root, "type");

	if (g_strcmp0 (type, "async") == 0) {
		const gchar *change_id;

		change_id = json_object_get_string_member (root, "change");

		while (TRUE) {
			/* Wait for a little bit before polling */
			g_usleep (100 * 1000);

			result = get_changes (macaroon, discharges, change_id, cancellable, error);
			if (result == NULL)
				return FALSE;

			status = g_strdup (json_object_get_string_member (result, "status"));

			if (g_strcmp0 (status, "Done") == 0)
				break;

			callback (result, user_data);
		}
	}

	if (g_strcmp0 (status, "Done") != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "snapd operation finished with status %s", status);
		return FALSE;
	}

	return TRUE;
}

gboolean
gs_snapd_install (const gchar *macaroon, gchar **discharges,
		  const gchar *name,
		  GsSnapdProgressCallback callback, gpointer user_data,
		  GCancellable *cancellable,
		  GError **error)
{
	return send_package_action (macaroon, discharges, name, "install", callback, user_data, cancellable, error);
}

gboolean
gs_snapd_remove (const gchar *macaroon, gchar **discharges,
		 const gchar *name,
		 GsSnapdProgressCallback callback, gpointer user_data,
		 GCancellable *cancellable, GError **error)
{
	return send_package_action (macaroon, discharges, name, "remove", callback, user_data, cancellable, error);
}

gchar *
gs_snapd_get_resource (const gchar *macaroon, gchar **discharges,
		       const gchar *path,
		       gsize *data_length,
		       GCancellable *cancellable, GError **error)
{
	guint status_code;
	g_autofree gchar *reason_phrase = NULL;
	g_autofree gchar *response_type = NULL;
	g_autofree gchar *data = NULL;

	if (!send_request ("GET", path, NULL,
			   macaroon, discharges,
			   &status_code, &reason_phrase,
			   NULL, &data, data_length,
			   cancellable, error))
		return NULL;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "snapd returned status code %u: %s",
			     status_code, reason_phrase);
		return NULL;
	}

	return g_steal_pointer (&data);
}

static gboolean
parse_date (const gchar *date_string, gint *year, gint *month, gint *day)
{
	/* Example: 2016-05-17 */
	if (strchr (date_string, '-') != NULL) {
		g_auto(GStrv) tokens = NULL;

		tokens = g_strsplit (date_string, "-", -1);
		if (g_strv_length (tokens) != 3)
			return FALSE;

		*year = atoi (tokens[0]);
		*month = atoi (tokens[1]);
		*day = atoi (tokens[2]);

		return TRUE;
	}
	/* Example: 20160517 */
	else if (strlen (date_string) == 8) {
		// FIXME: Implement
		return FALSE;
	}
	else
		return FALSE;
}

static gboolean
parse_time (const gchar *time_string, gint *hour, gint *minute, gdouble *seconds)
{
	/* Example: 09:36:53.682 or 09:36:53 or 09:36 */
	if (strchr (time_string, ':') != NULL) {
		g_auto(GStrv) tokens = NULL;

		tokens = g_strsplit (time_string, ":", 3);
		*hour = atoi (tokens[0]);
		if (tokens[1] == NULL)
			return FALSE;
		*minute = atoi (tokens[1]);
		if (tokens[2] != NULL)
			*seconds = g_ascii_strtod (tokens[2], NULL);
		else
			*seconds = 0.0;

		return TRUE;
	}
	/* Example: 093653.682 or 093653 or 0936 */
	else {
		// FIXME: Implement
		return FALSE;
	}
}

static gboolean
is_timezone_prefix (gchar c)
{
	return c == '+' || c == '-' || c == 'Z';
}

GDateTime *
gs_snapd_parse_date (const gchar *value)
{
	g_auto(GStrv) tokens = NULL;
	g_autoptr(GTimeZone) timezone = NULL;
	gint year = 0, month = 0, day = 0, hour = 0, minute = 0;
	gdouble seconds = 0.0;

	if (value == NULL)
		return NULL;

	/* Example: 2016-05-17T09:36:53+12:00 */
	tokens = g_strsplit (value, "T", 2);
	if (!parse_date (tokens[0], &year, &month, &day))
		return NULL;
	if (tokens[1] != NULL) {
		gchar *timezone_start;

		/* Timezone is either Z (UTC) +hh:mm or -hh:mm */
		timezone_start = tokens[1];
		while (*timezone_start != '\0' && !is_timezone_prefix (*timezone_start))
			timezone_start++;
		if (*timezone_start != '\0')
			timezone = g_time_zone_new (timezone_start);

		/* Strip off timezone */
		*timezone_start = '\0';

		if (!parse_time (tokens[1], &hour, &minute, &seconds))
			return NULL;
	}

	if (timezone == NULL)
		timezone = g_time_zone_new_local ();

	return g_date_time_new (timezone, year, month, day, hour, minute, seconds);
}
