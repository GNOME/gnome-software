/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gstdio.h>
#include <xmlb.h>

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
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_INSTALLED,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_CATEGORIES,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* no shell-extensions installed, abort */
	if (gs_app_list_length (list) < 1) {
		g_test_skip ("no shell extensions installed");
		return;
	}

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
	const gchar *cachedir = "/var/tmp/gs-self-test";
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* no shell-extensions, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "shell-extensions")) {
		g_test_skip ("not enabled");
		return;
	}

	/* ensure files are removed */
	g_setenv ("GS_SELF_TEST_CACHEDIR", cachedir, TRUE);
	gs_utils_rmtree (cachedir, NULL);

	/* refresh the metadata */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) G_MAXUINT,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* ensure file was populated */
	silo = xb_silo_new ();
	fn = gs_utils_get_cache_filename ("shell-extensions",
					  "extensions-web.xmlb",
					  GS_UTILS_CACHE_FLAG_WRITEABLE,
					  &error);
	g_assert_no_error (error);
	g_assert_nonnull (fn);
	file = g_file_new_for_path (fn);
	ret = xb_silo_load_from_file (silo, file,
				      XB_SILO_LOAD_FLAG_NONE,
				      NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	components = xb_silo_query (silo, "components/component", 0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (components);
	g_assert_cmpint (components->len, >, 20);
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
