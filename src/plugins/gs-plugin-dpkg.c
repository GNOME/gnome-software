/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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

#include <gs-plugin.h>
#include <gs-utils.h>

#define DPKG_DEB_BINARY		"/usr/bin/dpkg-deb"

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	if (!g_file_test (DPKG_DEB_BINARY, G_FILE_TEST_EXISTS)) {
		g_debug ("disabling '%s' as no %s available",
			 gs_plugin_get_name (plugin), DPKG_DEB_BINARY);
		gs_plugin_set_enabled (plugin, FALSE);
	}
}

/**
 * gs_plugin_filename_to_app:
 */
gboolean
gs_plugin_filename_to_app (GsPlugin *plugin,
			   GList **list,
			   const gchar *filename,
			   GCancellable *cancellable,
			   GError **error)
{
	GsApp *app;
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *description = NULL;
	g_autofree gchar *output = NULL;
	g_auto(GStrv) argv = NULL;
	g_auto(GStrv) tokens = NULL;
	const gchar *mimetypes[] = {
		"application/vnd.debian.binary-package",
		NULL };

	/* does this match any of the mimetypes we support */
	content_type = gs_utils_get_content_type (filename, cancellable, error);
	if (content_type == NULL)
		return FALSE;
	if (!g_strv_contains (mimetypes, content_type))
		return TRUE;

	/* exec sync */
	argv = g_new0 (gchar *, 4);
	argv[0] = g_strdup (DPKG_DEB_BINARY);
	argv[1] = g_strdup ("--showformat=${Package}\\n"
			    "${Version}\\n"
			    "${Installed-Size}\\n"
			    "${Homepage}\\n"
			    "${Description}");
	argv[2] = g_strdup ("-W");
	argv[3] = g_strdup (filename);
	if (!g_spawn_sync (NULL, argv, NULL,
			   G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
			   NULL, NULL, &output, NULL, NULL, error))
		return FALSE;

	/* parse output */
	tokens = g_strsplit (output, "\n", 0);
	if (g_strv_length (tokens) < 5) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "dpkg-deb output format incorrect:\n\"%s\"\n", output);
		return FALSE;
	}

	/* create app */
	app = gs_app_new (NULL);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_add_source (app, tokens[0]);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, tokens[0]);
	gs_app_set_version (app, tokens[1]);
	gs_app_set_size_installed (app, 1024 * atoi (tokens[2]));
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, tokens[3]);
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, tokens[4]);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);

	/* multiline text */
	description = g_strjoinv (NULL, tokens + 5);
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST, description + 1);

	/* success */
	gs_app_list_add (list, app);
	return TRUE;
}
