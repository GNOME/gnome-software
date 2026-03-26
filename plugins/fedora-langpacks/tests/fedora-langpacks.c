/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "gnome-software-private.h"

#include "gs-test.h"

static void
gs_plugins_fedora_langpacks_func (GsPluginLoader *plugin_loader)
{
	g_autofree gchar *cachefn = NULL;
	g_autoptr(GError) error = NULL;
	GsApp *app = NULL;
	GsAppList *list;
	g_autoptr(GsAppQuery) query = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;

	if (g_file_test ("/run/ostree-booted", G_FILE_TEST_EXISTS)) {
		g_test_skip ("Langpacks are not supported on atomic OSTree systems");
		return;
	}

	os_release = gs_os_release_new (NULL);
	if (g_strcmp0 (gs_os_release_get_id (os_release), "fedora") != 0) {
		g_test_skip ("not on fedora");
		return;
	}

	if (gs_plugin_loader_find_plugin (plugin_loader, "packagekit") == NULL) {
		g_test_skip ("packagekit plugin is required to run fedora-langpacks tests");
		return;
	}

	/* start with a clean slate */
	cachefn = gs_utils_get_cache_filename ("langpacks", "langpacks-pt_BR",
					       GS_UTILS_CACHE_FLAG_WRITEABLE |
					       GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
					       &error);
	g_assert_no_error (error);
	g_unlink (cachefn);

	/* get langpacks result based on locale */
	query = gs_app_query_new ("is-langpack-for-locale", "pt_BR.UTF-8",
				  "refine-require-flags", GS_PLUGIN_REFINE_REQUIRE_FLAGS_ICON,
				  "max-results", 1,
				  NULL);
	plugin_job = gs_plugin_job_list_apps_new (query, GS_PLUGIN_LIST_APPS_FLAGS_NONE);

	gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
	g_assert_no_error (error);
	g_assert_nonnull (list);

	/* check if we have just one app in the list */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);

	/* check app's source and kind */
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_default_source (app), ==, "langpacks-pt_BR");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_COMPONENT_KIND_LOCALIZATION);
}

int
main (int argc, char **argv)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar * const allowlist[] = {
		"fedora-langpacks",
		"packagekit",
		NULL
	};

	/* The tests access the system proxy schemas, so pre-load those before
	 * %G_TEST_OPTION_ISOLATE_DIRS resets the XDG system dirs. */
	g_settings_schema_source_get_default ();

	gs_test_init (&argc, &argv);

	/* we can only load this once per process */
	plugin_loader = gs_plugin_loader_new (NULL, NULL);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR_PACKAGEKIT);
	ret = gs_plugin_loader_setup (plugin_loader,
				      allowlist,
				      NULL,
				      NULL,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* plugin tests go here */
	g_test_add_data_func ("/gnome-software/plugins/fedora-langpacks",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_fedora_langpacks_func);

	return g_test_run ();
}
