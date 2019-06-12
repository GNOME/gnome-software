/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <stdlib.h>
#include <gnome-software.h>

#define DPKG_DEB_BINARY		"/usr/bin/dpkg-deb"

void
gs_plugin_initialize (GsPlugin *plugin)
{
	if (!g_file_test (DPKG_DEB_BINARY, G_FILE_TEST_EXISTS)) {
		g_debug ("disabling '%s' as no %s available",
			 gs_plugin_get_name (plugin), DPKG_DEB_BINARY);
		gs_plugin_set_enabled (plugin, FALSE);
	}
}

gboolean
gs_plugin_file_to_app (GsPlugin *plugin,
		       GsAppList *list,
		       GFile *file,
		       GCancellable *cancellable,
		       GError **error)
{
	guint i;
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *output = NULL;
	g_auto(GStrv) argv = NULL;
	g_auto(GStrv) tokens = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GString) str = NULL;
	const gchar *mimetypes[] = {
		"application/vnd.debian.binary-package",
		NULL };

	/* does this match any of the mimetypes we support */
	content_type = gs_utils_get_content_type (file, cancellable, error);
	if (content_type == NULL)
		return FALSE;
	if (!g_strv_contains (mimetypes, content_type))
		return TRUE;

	/* exec sync */
	argv = g_new0 (gchar *, 5);
	argv[0] = g_strdup (DPKG_DEB_BINARY);
	argv[1] = g_strdup ("--showformat=${Package}\\n"
			    "${Version}\\n"
			    "${Installed-Size}\\n"
			    "${Homepage}\\n"
			    "${Description}");
	argv[2] = g_strdup ("-W");
	argv[3] = g_file_get_path (file);
	if (!g_spawn_sync (NULL, argv, NULL,
			   G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
			   NULL, NULL, &output, NULL, NULL, error)) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	/* parse output */
	tokens = g_strsplit (output, "\n", 0);
	if (g_strv_length (tokens) < 5) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "dpkg-deb output format incorrect:\n\"%s\"\n", output);
		return FALSE;
	}

	/* create app */
	app = gs_plugin_app_new (plugin, NULL);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_add_source (app, tokens[0]);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, tokens[0]);
	gs_app_set_version (app, tokens[1]);
	gs_app_set_size_installed (app, 1024 * g_ascii_strtoull (tokens[2], NULL, 10));
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, tokens[3]);
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, tokens[4]);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_metadata (app, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));

	/* multiline text */
	str = g_string_new ("");
	for (i = 5; tokens[i] != NULL; i++) {
		if (g_strcmp0 (tokens[i], " .") == 0) {
			if (str->len > 0)
				g_string_truncate (str, str->len - 1);
			g_string_append (str, "\n");
			continue;
		}
		g_strstrip (tokens[i]);
		g_string_append_printf (str, "%s ", tokens[i]);
	}
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST, str->str);

	/* success */
	gs_app_list_add (list, app);
	return TRUE;
}
