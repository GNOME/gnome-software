/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <glib/gstdio.h>

#include "gnome-software-private.h"

#include "gs-test.h"

static void
gs_plugins_shell_extensions_installed_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* no shell-extensions, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "shell-extensions")) {
		g_test_skip ("not enabled");
		return;
	}

	/* get installed packages */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_INSTALLED, NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (gs_app_list_length (list), >, 1);

	/* test properties */
	app = gs_app_list_lookup (list, "*/*/*/*/background-logo_fedorahosted.org/*");
	if (app == NULL) {
		g_test_skip ("not found");
		return;
	}

	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpint (gs_app_get_scope (app), ==, AS_APP_SCOPE_USER);
	g_assert_cmpstr (gs_app_get_name (app), ==, "Background Logo");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "GNOME Shell Extension");
	g_assert_cmpstr (gs_app_get_description (app), ==,
			 "Overlay a tasteful logo on the background to "
			 "enhance the user experience");
	g_assert_cmpstr (gs_app_get_license (app), ==, "GPL-2.0+");
	g_assert_cmpstr (gs_app_get_management_plugin (app), ==, "shell-extensions");
	g_assert (gs_app_has_category (app, "Addon"));
	g_assert (gs_app_has_category (app, "ShellExtension"));
	g_assert_cmpstr (gs_app_get_metadata_item (app, "shell-extensions::has-prefs"), ==, "");
	g_assert_cmpstr (gs_app_get_metadata_item (app, "shell-extensions::uuid"), ==,
			 "background-logo@fedorahosted.org");
}

static void
gs_plugins_shell_extensions_remote_func (GsPluginLoader *plugin_loader)
{
	const gchar *xml_fn = "/var/tmp/self-test/extensions-web.xml";
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(AsStore) store = NULL;

	/* no shell-extensions, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "shell-extensions")) {
		g_test_skip ("not enabled");
		return;
	}

	/* ensure files are removed */
	g_unlink (xml_fn);

	/* refresh the metadata */
	g_setenv ("GS_SELF_TEST_SHELL_EXTENSIONS_XML_FN", xml_fn, TRUE);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) G_MAXUINT,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* ensure file was populated */
	store = as_store_new ();
	file = g_file_new_for_path (xml_fn);
	ret = as_store_from_file (store, file, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (as_store_get_size (store), >, 20);
}

int
main (int argc, char **argv)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *whitelist[] = {
		"shell-extensions",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* we can only load this once per process */
	plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR);
	ret = gs_plugin_loader_setup (plugin_loader,
				      (gchar**) whitelist,
				      NULL,
				      NULL,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* plugin tests go here */
	g_test_add_data_func ("/gnome-software/plugins/shell-extensions/installed",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_shell_extensions_installed_func);
	g_test_add_data_func ("/gnome-software/plugins/shell-extensions/remote",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_shell_extensions_remote_func);

	return g_test_run ();
}

/* vim: set noexpandtab: */
