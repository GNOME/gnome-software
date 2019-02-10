/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Joaquim Rocha <jrocha@endlessm.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gstdio.h>

#include "gnome-software-private.h"

#include "gs-appstream.h"
#include "gs-test.h"

static void
gs_plugins_core_search_repo_name_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	g_autofree gchar *menu_path = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app_tmp = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* drop all caches */
	g_unlink ("/var/tmp/self-test/appstream/components.xmlb");
	gs_plugin_loader_setup_again (plugin_loader);

	/* force this app to be installed */
	app_tmp = gs_plugin_loader_app_create (plugin_loader, "*/*/yellow/desktop/arachne.desktop/*");
	gs_app_set_state (app_tmp, AS_APP_STATE_INSTALLED);

	/* get search result based on addon keyword */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "yellow",
					 NULL);
	gs_plugin_job_set_refine_flags (plugin_job, GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the parent app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "arachne.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
}

static void
gs_plugins_core_os_release_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsApp) app3 = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GError) error = NULL;

	/* drop all caches */
	g_unlink ("/var/tmp/self-test/appstream/components.xmlb");
	gs_plugin_loader_setup_again (plugin_loader);

	/* refine system application */
	app = gs_plugin_loader_get_system_app (plugin_loader);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure there is valid content */
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.fedoraproject.Fedora-25");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_OS_UPGRADE);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_name (app), ==, "Fedora");
	g_assert_cmpstr (gs_app_get_version (app), ==, "25");
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==,
			 "https://fedoraproject.org/");
	g_assert_cmpstr (gs_app_get_metadata_item (app, "GnomeSoftware::CpeName"), ==,
			 "cpe:/o:fedoraproject:fedora:25");

	/* this comes from appstream */
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Fedora Workstation");

	/* check we can get this by the old name too */
	app3 = gs_plugin_loader_get_system_app (plugin_loader);
	g_assert (app3 != NULL);
	g_assert (app3 == app);
}

static void
gs_plugins_core_generic_updates_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	GsApp *os_update;
	GsAppList *related;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GsPluginJob) plugin_job2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app1 = NULL;
	g_autoptr(GsApp) app2 = NULL;
	g_autoptr(GsApp) app_wildcard = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsAppList) list_wildcard = NULL;

	/* drop all caches */
	g_unlink ("/var/tmp/self-test/appstream/components.xmlb");
	gs_plugin_loader_setup_again (plugin_loader);

	/* create a list with generic apps */
	list = gs_app_list_new ();
	app1 = gs_app_new ("package1");
	app2 = gs_app_new ("package2");
	gs_app_set_kind (app1, AS_APP_KIND_GENERIC);
	gs_app_set_kind (app2, AS_APP_KIND_GENERIC);
	gs_app_set_bundle_kind (app1, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_bundle_kind (app2, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_scope (app1, AS_APP_SCOPE_SYSTEM);
	gs_app_set_scope (app2, AS_APP_SCOPE_SYSTEM);
	gs_app_set_state (app1, AS_APP_STATE_UPDATABLE);
	gs_app_set_state (app2, AS_APP_STATE_UPDATABLE);
	gs_app_add_source (app1, "package1");
	gs_app_add_source (app2, "package2");
	gs_app_list_add (list, app1);
	gs_app_list_add (list, app2);

	/* refine to make the generic-updates plugin merge them into a single OsUpdate item */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
	                                 "list", list,
	                                 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS,
	                                 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure there is one entry, the os update */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	os_update = gs_app_list_index (list, 0);

	/* make sure the os update is valid */
	g_assert_cmpstr (gs_app_get_id (os_update), ==, "org.gnome.Software.OsUpdate");
	g_assert_cmpint (gs_app_get_kind (os_update), ==, AS_APP_KIND_OS_UPDATE);
	g_assert (gs_app_has_quirk (os_update, GS_APP_QUIRK_IS_PROXY));

	/* must have two related apps, the ones we added earlier */
	related = gs_app_get_related (os_update);
	g_assert_cmpint (gs_app_list_length (related), ==, 2);

	/* another test to make sure that we don't get an OsUpdate item created for wildcard apps */
	list_wildcard = gs_app_list_new ();
	app_wildcard = gs_app_new ("nosuchapp.desktop");
	gs_app_add_quirk (app_wildcard, GS_APP_QUIRK_IS_WILDCARD);
	gs_app_set_kind (app_wildcard, AS_APP_KIND_GENERIC);
	gs_app_list_add (list_wildcard, app_wildcard);
	plugin_job2 = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
	                                  "list", list_wildcard,
	                                  "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS,
	                                  NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job2, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	/* no OsUpdate item created */
	for (guint i = 0; i < gs_app_list_length (list_wildcard); i++) {
		GsApp *app_tmp = gs_app_list_index (list_wildcard, i);
		g_assert_cmpint (gs_app_get_kind (app_tmp), !=, AS_APP_KIND_OS_UPDATE);
		g_assert (!gs_app_has_quirk (app_tmp, GS_APP_QUIRK_IS_PROXY));
	}
}

int
main (int argc, char **argv)
{
	const gchar *tmp_root = "/var/tmp/self-test";
	gboolean ret;
	g_autofree gchar *os_release_filename = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *xml;
	const gchar *whitelist[] = {
		"appstream",
		"generic-updates",
		"icons",
		"os-release",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv ("GS_SELF_TEST_CACHEDIR", tmp_root, TRUE);

	os_release_filename = gs_test_get_filename (TESTDATADIR, "os-release");
	g_assert (os_release_filename != NULL);
	g_setenv ("GS_SELF_TEST_OS_RELEASE_FILENAME", os_release_filename, TRUE);

	/* ensure test root does not exist */
	if (g_file_test (tmp_root, G_FILE_TEST_EXISTS)) {
		ret = gs_utils_rmtree (tmp_root, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert (!g_file_test (tmp_root, G_FILE_TEST_EXISTS));
	}

	//g_setenv ("GS_SELF_TEST_APPSTREAM_XML", xml, TRUE);
	g_setenv ("GS_SELF_TEST_APPSTREAM_ICON_ROOT",
		  "/var/tmp/self-test/flatpak/appstream/test/x86_64/active/", TRUE);

	/* fake some data */
	xml = "<?xml version=\"1.0\"?>\n"
		"<components origin=\"yellow\" version=\"0.9\">\n"
		"  <component type=\"desktop\">\n"
		"    <id>arachne.desktop</id>\n"
		"    <name>test</name>\n"
		"    <summary>Test</summary>\n"
		"    <icon type=\"stock\">system-file-manager</icon>\n"
		"    <pkgname>arachne</pkgname>\n"
		"  </component>\n"
		"  <component type=\"os-upgrade\">\n"
		"    <id>org.fedoraproject.Fedora-25</id>\n"
		"    <summary>Fedora Workstation</summary>\n"
		"    <pkgname>fedora-release</pkgname>\n"
		"  </component>\n"
		"</components>\n";
	g_setenv ("GS_SELF_TEST_APPSTREAM_XML", xml, TRUE);

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
	g_test_add_data_func ("/gnome-software/plugins/core/search-repo-name",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_core_search_repo_name_func);
	g_test_add_data_func ("/gnome-software/plugins/core/os-release",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_core_os_release_func);
	g_test_add_data_func ("/gnome-software/plugins/core/generic-updates",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_core_generic_updates_func);
	return g_test_run ();
}
