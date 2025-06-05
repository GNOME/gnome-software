/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2024 GNOME Foundation, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>

#include <stdlib.h>
#include <gnome-software.h>

#include "gs-plugin-dpkg.h"

/**
 * SECTION:
 * Plugin to support loading `.deb` package files.
 *
 * It requires the `dpkg-deb` program to be installed.
 *
 * This plugin runs entirely in the main thread, deferring most of its work to
 * a `dpkg-deb` subprocess, which it communicates with asynchronously. No
 * locking is required.
 */

struct _GsPluginDpkg
{
	GsPlugin	parent;
};

G_DEFINE_TYPE (GsPluginDpkg, gs_plugin_dpkg, GS_TYPE_PLUGIN)

#define DPKG_DEB_BINARY		"/usr/bin/dpkg-deb"

static void
gs_plugin_dpkg_init (GsPluginDpkg *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	if (!g_file_test (DPKG_DEB_BINARY, G_FILE_TEST_EXISTS)) {
		g_debug ("disabling itself as no %s available", DPKG_DEB_BINARY);
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	/* need package name */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "appstream");
}

static void get_content_type_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);
static void subprocess_communicate_cb (GObject      *source_object,
                                       GAsyncResult *result,
                                       gpointer      user_data);

static void
gs_plugin_dpkg_file_to_app_async (GsPlugin *plugin,
				  GFile *file,
				  GsPluginFileToAppFlags flags,
				  GsPluginEventCallback event_callback,
				  void *event_user_data,
				  GCancellable *cancellable,
				  GAsyncReadyCallback callback,
				  gpointer user_data)
{
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_file_to_app_data_new_task (plugin, file, flags, event_callback, event_user_data, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dpkg_file_to_app_async);

	/* does this match any of the mimetypes we support */
	gs_utils_get_content_type_async (file, cancellable, get_content_type_cb, g_steal_pointer (&task));
}

static void
get_content_type_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
	GFile *file = G_FILE (source_object);
	g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
	GCancellable *cancellable = g_task_get_cancellable (task);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autofree gchar *content_type = NULL;
	const gchar *mimetypes[] = {
		"application/vnd.debian.binary-package",
		NULL };

	content_type = gs_utils_get_content_type_finish (file, result, &local_error);
	if (content_type == NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	} else if (!g_strv_contains (mimetypes, content_type)) {
		g_task_return_pointer (task, gs_app_list_new (), g_object_unref);
		return;
	}

	/* exec sync */
	subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
				       G_SUBPROCESS_FLAGS_STDERR_SILENCE,
				       &local_error,
				       DPKG_DEB_BINARY,
				       "--showformat=${Package}\\n"
				       "${Version}\\n"
				       "${License}\\n"
				       "${Installed-Size}\\n"
				       "${Homepage}\\n"
				       "${Description}",
				       "-W",
				       g_file_peek_path (file),
				       NULL);

	if (subprocess == NULL) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_subprocess_communicate_async (subprocess, NULL, cancellable,
					subprocess_communicate_cb, g_steal_pointer (&task));
}

static void
subprocess_communicate_cb (GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
	GSubprocess *subprocess = G_SUBPROCESS (source_object);
	g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
	GsPluginFileToAppData *data = g_task_get_task_data (task);
	GsPluginDpkg *self = g_task_get_source_object (task);
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GBytes) stdout_buf = NULL;
	const char *output;
	g_auto(GStrv) tokens = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GString) str = NULL;

	if (!g_subprocess_communicate_finish (subprocess, result, &stdout_buf, NULL, &local_error)) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	/* parse output; assume it doesn’t contain any nul bytes */
	output = g_bytes_get_data (stdout_buf, NULL);
	tokens = g_strsplit (output, "\n", 0);
	if (g_strv_length (tokens) < 6) {
		g_task_return_new_error (task,
					 GS_PLUGIN_ERROR,
					 GS_PLUGIN_ERROR_NOT_SUPPORTED,
					 "dpkg-deb output format incorrect:\n“%s”", output);
		return;
	}

	/* create app */
	app = gs_app_new (NULL);
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_add_source (app, tokens[0]);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, tokens[0]);
	gs_app_set_version (app, tokens[1]);
	gs_app_set_license (app, GS_APP_QUALITY_LOWEST, tokens[2]);
	gs_app_set_size_installed (app, GS_SIZE_TYPE_VALID, 1024 * g_ascii_strtoull (tokens[3], NULL, 10));
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, tokens[4]);
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, tokens[5]);
	gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_local_file (app, data->file);
	gs_app_set_metadata (app, "GnomeSoftware::Creator",
			     gs_plugin_get_name (GS_PLUGIN (self)));

	/* multiline text */
	str = g_string_new ("");
	for (guint i = 6; tokens[i] != NULL; i++) {
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

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_dpkg_file_to_app_finish (GsPlugin *plugin,
				   GAsyncResult *result,
				   GError **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void
gs_plugin_dpkg_class_init (GsPluginDpkgClass *klass)
{
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	plugin_class->file_to_app_async = gs_plugin_dpkg_file_to_app_async;
	plugin_class->file_to_app_finish = gs_plugin_dpkg_file_to_app_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_DPKG;
}
