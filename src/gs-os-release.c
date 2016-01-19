/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include "gs-os-release.h"

#include <string.h>

G_DEFINE_QUARK (gs-os-release-error-quark, gs_os_release_error)

/* strip any quotes surrounding the string */
static gchar *
dequote (gchar *s)
{
	size_t len;

	g_assert (s != NULL);

	len = strlen (s);
	if (len >= 2 &&
	    *s == *(s + len - 1) &&
	    (*s == '"' || *s == '\'')) {
		s[len - 1] = '\0';
		s++;
	}

	return s;
}

static gchar *
get_item (gchar *line, const gchar *key)
{
	g_autofree gchar *label = NULL;

	label = g_strconcat (key, "=", NULL);
	if (g_str_has_prefix (line, label)) {
		return g_strcompress (dequote (line + strlen (label)));
	}

	return NULL;
}

static gchar *
gs_os_release_parse_variable (const gchar *variable, GError **error)
{
	const gchar *filename;
	g_autofree gchar *buffer = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (variable != NULL, NULL);

	filename = "/etc/os-release";
	if (!g_file_test (filename, G_FILE_TEST_EXISTS))
		filename = "/usr/lib/os-release";

	if (!g_file_get_contents (filename, &buffer, NULL, error))
		return NULL;

	if (buffer != NULL) {
		gint i;
		g_auto(GStrv) lines = NULL;

		lines = g_strsplit (buffer, "\n", -1);
		for (i = 0; lines[i] != NULL; i++) {
			gchar *line = g_strstrip (lines[i]);
			gchar *ret;

			if ((ret = get_item (line, variable)) != NULL)
				return ret;
		}
	}

	g_set_error (error,
	             GS_OS_RELEASE_ERROR,
	             GS_OS_RELEASE_ERROR_FAILED,
	             "could not find variable '%s' in %s", variable, filename);
	return NULL;
}

gchar *
gs_os_release_get_name (GError **error)
{
	return gs_os_release_parse_variable ("NAME", error);
}

gchar *
gs_os_release_get_version (GError **error)
{
	return gs_os_release_parse_variable ("VERSION", error);
}

gchar *
gs_os_release_get_id (GError **error)
{
	return gs_os_release_parse_variable ("ID", error);
}

gchar *
gs_os_release_get_version_id (GError **error)
{
	return gs_os_release_parse_variable ("VERSION_ID", error);
}

gchar *
gs_os_release_get_pretty_name (GError **error)
{
	return gs_os_release_parse_variable ("PRETTY_NAME", error);
}

/* vim: set noexpandtab: */
