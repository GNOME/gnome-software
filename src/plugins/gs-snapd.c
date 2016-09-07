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
			     GS_PLUGIN_ERROR_FAILED,
			     "Unable to open snapd socket: %s",
			     error_local->message);
		return NULL;
	}
	address = g_unix_socket_address_new (SNAPD_SOCKET);
	if (!g_socket_connect (socket, address, cancellable, &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Unable to connect snapd socket: %s",
			     error_local->message);
		g_object_unref (socket);
		return NULL;
	}

	return socket;
}

static gboolean
read_from_snapd (GSocket *socket,
		 gchar *buffer, gsize buffer_length,
		 gsize *read_offset,
		 GCancellable *cancellable,
		 GError **error)
{
	gssize n_read;
	n_read = g_socket_receive (socket,
				   buffer + *read_offset,
				   buffer_length - *read_offset,
				   cancellable,
				   error);
	if (n_read < 0)
		return FALSE;
	*read_offset += (gsize) n_read;
	buffer[*read_offset] = '\0';

	return TRUE;
}

gboolean
gs_snapd_request (const gchar  *method,
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
	gsize max_data_length = 65535, data_length = 0, header_length;
	gchar data[max_data_length + 1], *body = NULL;
	g_autoptr (SoupMessageHeaders) headers = NULL;
	gsize chunk_length = 0, n_required;
	gchar *chunk_start = NULL;
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
	if (n_written < 0)
		return FALSE;

	/* read HTTP headers */
	while (data_length < max_data_length && !body) {
		if (!read_from_snapd (socket,
				      data,
				      max_data_length,
				      &data_length,
				      cancellable,
				      error))
			return FALSE;
		body = strstr (data, "\r\n\r\n");
	}
	if (!body) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "Unable to find header separator in snapd response");
		return FALSE;
	}

	/* body starts after header divider */
	body += 4;
	header_length = (gsize) (body - data);

	/* parse headers */
	headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
	if (!soup_headers_parse_response (data, (gint) header_length, headers,
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
		while (TRUE) {
			gsize n_read = data_length;
			if (n_read == max_data_length) {
				g_set_error_literal (error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_INVALID_FORMAT,
						     "Out of space reading snapd response");
				return FALSE;
			}
			if (!read_from_snapd (socket, data,
					      max_data_length - data_length,
					      &data_length,
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
		while (data_length < max_data_length) {
			chunk_start = strstr (body, "\r\n");
			if (chunk_start)
				break;
			if (!read_from_snapd (socket,
					      data,
					      max_data_length,
					      &data_length,
					      cancellable,
					      error))
				return FALSE;
		}
		if (!chunk_start) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_INVALID_FORMAT,
					     "Unable to find chunk header in "
					     "snapd response");
			return FALSE;
		}
		chunk_length = strtoul (body, NULL, 16);
		chunk_start += 2;

		/* check if enough space to read chunk */
		n_required = (chunk_start - data) + chunk_length;
		if (n_required > max_data_length) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "Not enough space for snapd response, "
				     "require %" G_GSIZE_FORMAT " octets, "
				     "have %" G_GSIZE_FORMAT,
				     n_required, max_data_length);
			return FALSE;
		}

		while (data_length < n_required)
			if (!read_from_snapd (socket, data,
					      n_required - data_length,
					      &data_length,
					      cancellable,
					      error))
				return FALSE;
		break;
	case SOUP_ENCODING_CONTENT_LENGTH:
		chunk_length = soup_message_headers_get_content_length (headers);
		chunk_start = body;
		n_required = header_length + chunk_length;

		/* check if enough space available */
		if (n_required > max_data_length) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "Not enough space for snapd response, "
				     "require %" G_GSIZE_FORMAT " octets, "
				     "have %" G_GSIZE_FORMAT,
				     n_required, max_data_length);
			return FALSE;
		}

		while (data_length < n_required) {
			if (!read_from_snapd (socket, data,
					      n_required - data_length,
					      &data_length,
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
	if (response != NULL && chunk_start != NULL) {
		*response = g_malloc (chunk_length + 2);
		memcpy (*response, chunk_start, chunk_length + 1);
		(*response)[chunk_length + 1] = '\0';
		g_debug ("snapd status %u: %s", code, *response);
	}
	if (response_length)
		*response_length = chunk_length;

	return TRUE;
}

gboolean
gs_snapd_parse_result (const gchar	*response_type,
		       const gchar	*response,
		       JsonObject	**result,
		       GError		**error)
{
	g_autoptr(JsonParser) parser = NULL;
	g_autoptr(GError) error_local = NULL;
	JsonObject *root;

	if (response_type == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "snapd returned no content type");
		return FALSE;
	}
	if (g_strcmp0 (response_type, "application/json") != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "snapd returned unexpected content type %s", response_type);
		return FALSE;
	}

	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, response, -1, &error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "Unable to parse snapd response: %s",
			     error_local->message);
		return FALSE;
	}

	if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser))) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "snapd response does is not a valid JSON object");
		return FALSE;
	}
	root = json_node_get_object (json_parser_get_root (parser));
	if (!json_object_has_member (root, "result")) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "snapd response does not contain a \"result\" field");
		return FALSE;
	}
	if (result != NULL)
		*result = json_object_ref (json_object_get_object_member (root, "result"));

	return TRUE;
}

gboolean
gs_snapd_parse_error (const gchar	*response_type,
		      const gchar	*response,
		      gchar		**message,
		      gchar		**kind,
		      GError		**error)
{
	g_autoptr(JsonObject) result = NULL;

	if (!gs_snapd_parse_result (response_type, response, &result, error))
		return FALSE;

	if (message != NULL)
		*message = g_strdup (json_object_get_string_member (result, "message"));
	if (kind != NULL)
		*kind = json_object_has_member (result, "kind") ? g_strdup (json_object_get_string_member (result, "kind")) : NULL;

	return TRUE;
}
