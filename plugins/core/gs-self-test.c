/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Joaquim Rocha <jrocha@endlessm.com>
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

#include "gnome-software-private.h"

#include "gs-appstream.h"
#include "gs-test.h"

static void
gs_plugins_core_app_creation_func (GsPluginLoader *plugin_loader)
{
	AsApp *as_app = NULL;
	GsPlugin *plugin;
	gboolean ret;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsApp) app2 = NULL;
	g_autoptr(GsApp) cached_app = NULL;
	g_autoptr(GsApp) cached_app2 = NULL;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *test_icon_root = g_getenv ("GS_SELF_TEST_APPSTREAM_ICON_ROOT");
	g_autofree gchar *xml = g_strdup ("<?xml version=\"1.0\"?>\n"
					  "<components version=\"0.9\">\n"
					  "  <component type=\"desktop\">\n"
					  "    <id>demeter.desktop</id>\n"
					  "    <name>Demeter</name>\n"
					  "    <summary>An agriculture application</summary>\n"
					  "  </component>\n"
					  "</components>\n");

	/* drop all caches */
	gs_plugin_loader_setup_again (plugin_loader);

	app = gs_plugin_loader_app_create (plugin_loader,
					   "*/*/*/desktop/demeter.desktop/*");
	gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);

	cached_app = gs_plugin_loader_app_create (plugin_loader,
						  "*/*/*/desktop/demeter.desktop/*");

	g_assert (app == cached_app);

	/* Make sure the app still has the match-any-prefix quirk*/
	g_assert(gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX));

	/* Ensure gs_appstream creates a new app when a matching one is cached but
	 * has the match-any-prefix quirk */
	store = as_store_new ();
	ret = as_store_from_xml (store, xml, test_icon_root, &error);
	g_assert_no_error (error);
	g_assert (ret);

	as_app = as_store_get_app_by_id (store, "demeter.desktop");
	g_assert (as_app != NULL);

	plugin = gs_plugin_loader_find_plugin (plugin_loader, "appstream");
	g_assert (plugin != NULL);

	app2 = gs_appstream_create_app (plugin, as_app, NULL);
	g_assert (app2 != NULL);
	g_assert (cached_app != app2);
	g_assert (!gs_app_has_quirk (app2, AS_APP_QUIRK_MATCH_ANY_PREFIX));

	cached_app2 = gs_plugin_loader_app_create (plugin_loader,
						   "*/*/*/desktop/demeter.desktop/*");
	g_assert (cached_app2 == app2);
}

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
	g_autoptr(GsApp) app2 = NULL;
	g_autoptr(GsApp) app3 = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GError) error = NULL;

	/* drop all caches */
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

	/* get using the new name */
	app2 = gs_plugin_loader_app_create (plugin_loader,
					    "*/*/*/*/org.fedoraproject.Fedora-25/*");
	g_assert (app2 != NULL);
	g_assert (app2 == app);

	/* check we can get this by the old name too */
	app3 = gs_plugin_loader_get_system_app (plugin_loader);
	g_assert (app3 != NULL);
	g_assert (app3 == app);
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
		"icons",
		"os-release",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv ("GS_SELF_TEST_CORE_DATADIR", tmp_root, TRUE);

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
		"  </component>\n"
		"  <component type=\"os-upgrade\">\n"
		"    <id>org.fedoraproject.Fedora-25</id>\n"
		"    <summary>Fedora Workstation</summary>\n"
		"  </component>\n"
		"</components>\n";
	g_setenv ("GS_SELF_TEST_APPSTREAM_XML", xml, TRUE);
	g_setenv ("GS_SELF_TEST_ALL_ORIGIN_KEYWORDS", "1", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* we can only load this once per process */
	plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR);
	ret = gs_plugin_loader_setup (plugin_loader,
				      (gchar**) whitelist,
				      NULL,
				      GS_PLUGIN_FAILURE_FLAGS_NONE,
				      NULL,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* plugin tests go here */
	g_test_add_data_func ("/gnome-software/plugins/core/search-repo-name",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_core_search_repo_name_func);
	g_test_add_data_func ("/gnome-software/plugins/core/app-creation",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_core_app_creation_func);
	g_test_add_data_func ("/gnome-software/plugins/core/os-release",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_core_os_release_func);
	return g_test_run ();
}

/* vim: set noexpandtab: */
