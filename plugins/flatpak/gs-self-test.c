/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
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

#include "gs-flatpak-app.h"

#include "gs-test.h"

static gboolean
gs_flatpak_test_write_repo_file (const gchar *fn, const gchar *testdir, GError **error)
{
	g_autofree gchar *testdir_repourl = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	/* create file */
	testdir_repourl = g_strdup_printf ("file://%s/repo", testdir);
	g_string_append (str, "[Flatpak Repo]\n");
	g_string_append (str, "Title=foo-bar\n");
	g_string_append (str, "Comment=Longer one line comment\n");
	g_string_append (str, "Description=Longer multiline comment that "
			      "does into detail.\n");
	g_string_append (str, "DefaultBranch=master\n");
	g_string_append_printf (str, "Url=%s\n", testdir_repourl);
	g_string_append (str, "Homepage=http://foo.bar\n");
	return g_file_set_contents (fn, str->str, -1, error);
}

static gboolean
gs_flatpak_test_write_ref_file (const gchar *filename, const gchar *url, const gchar *runtimerepo, GError **error)
{
	g_autoptr(GString) str = g_string_new (NULL);
	g_string_append (str, "[Flatpak Ref]\n");
	g_string_append (str, "Title=Chiron\n");
	g_string_append (str, "Name=org.test.Chiron\n");
	g_string_append (str, "Branch=master\n");
	g_string_append_printf (str, "Url=%s\n", url);
	g_string_append (str, "IsRuntime=False\n");
	g_string_append (str, "Comment=Single line synopsis\n");
	g_string_append (str, "Description=A Testing Application\n");
	g_string_append (str, "Icon=https://getfedora.org/static/images/fedora-logotext.png\n");
	if (runtimerepo != NULL)
		g_string_append_printf (str, "RuntimeRepo=%s\n", runtimerepo);
	return g_file_set_contents (filename, str->str, -1, error);
}

/* create duplicate file as if downloaded in firefox */
static void
gs_plugins_flatpak_repo_non_ascii_func (GsPluginLoader *plugin_loader)
{
	const gchar *fn = "/var/tmp/self-test/example (1)….flatpakrepo";
	gboolean ret;
	g_autofree gchar *testdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get a resolvable  */
	testdir = gs_test_get_filename (TESTDATADIR, "app-with-runtime");
	if (testdir == NULL)
		return;

	ret = gs_flatpak_test_write_repo_file (fn, testdir, &error);
	g_assert_no_error (error);
	g_assert (ret);
	file = g_file_new_for_path (fn);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 NULL);
	app = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpstr (gs_app_get_unique_id (app), ==, "user/*/*/source/example__1____/master");
}

static void
gs_plugins_flatpak_repo_func (GsPluginLoader *plugin_loader)
{
	const gchar *group_name = "remote \"example\"";
	const gchar *root = NULL;
	const gchar *fn = "/var/tmp/self-test/example.flatpakrepo";
	gboolean ret;
	g_autofree gchar *config_fn = NULL;
	g_autofree gchar *remote_url = NULL;
	g_autofree gchar *testdir = NULL;
	g_autofree gchar *testdir_repourl = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GKeyFile) kf = NULL;
	g_autoptr(GsApp) app2 = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* no flatpak, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "flatpak"))
		return;

	/* get a resolvable  */
	testdir = gs_test_get_filename (TESTDATADIR, "app-with-runtime");
	if (testdir == NULL)
		return;
	testdir_repourl = g_strdup_printf ("file://%s/repo", testdir);

	/* create file */
	ret = gs_flatpak_test_write_repo_file (fn, testdir, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* load local file */
	file = g_file_new_for_path (fn);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 NULL);
	app = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_SOURCE);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE_LOCAL);
	g_assert_cmpstr (gs_app_get_id (app), ==, "example");
	g_assert_cmpstr (gs_app_get_management_plugin (app), ==, "flatpak");
	g_assert_cmpstr (gs_app_get_origin_hostname (app), ==, "localhost");
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://foo.bar");
	g_assert_cmpstr (gs_app_get_name (app), ==, "foo-bar");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Longer one line comment");
	g_assert_cmpstr (gs_app_get_description (app), ==,
			 "Longer multiline comment that does into detail.");
	g_assert (gs_app_get_local_file (app) != NULL);
	g_assert (gs_app_get_pixbuf (app) != NULL);

	/* now install the remote */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);

	/* check config file was updated */
	root = g_getenv ("GS_SELF_TEST_FLATPAK_DATADIR");
	config_fn = g_build_filename (root, "flatpak", "repo", "config", NULL);
	kf = g_key_file_new ();
	ret = g_key_file_load_from_file (kf, config_fn, 0, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert (g_key_file_has_group (kf, "core"));
	g_assert (g_key_file_has_group (kf, group_name));
	g_assert (!g_key_file_get_boolean (kf, group_name, "gpg-verify", NULL));

	/* check the URL was unmangled */
	remote_url = g_key_file_get_string (kf, group_name, "url", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (remote_url, ==, testdir_repourl);

	/* try again, check state is correct */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 NULL);
	app2 = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (app2 != NULL);
	g_assert_cmpint (gs_app_get_state (app2), ==, AS_APP_STATE_INSTALLED);

	/* remove it */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpint (gs_app_get_progress (app), ==, 0);
}

static void
gs_plugins_flatpak_app_with_runtime_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	GsApp *runtime;
	const gchar *root;
	gboolean ret;
	gint kf_remote_repo_version;
	g_autofree gchar *changed_fn = NULL;
	g_autofree gchar *config_fn = NULL;
	g_autofree gchar *desktop_fn = NULL;
	g_autofree gchar *kf_remote_url = NULL;
	g_autofree gchar *metadata_fn = NULL;
	g_autofree gchar *repodir_fn = NULL;
	g_autofree gchar *runtime_fn = NULL;
	g_autofree gchar *testdir = NULL;
	g_autofree gchar *testdir_repourl = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GKeyFile) kf1 = g_key_file_new ();
	g_autoptr(GKeyFile) kf2 = g_key_file_new ();
	g_autoptr(GsApp) app_source = NULL;
	g_autoptr(GsAppList) list_all = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsAppList) sources = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* drop all caches */
	gs_plugin_loader_setup_again (plugin_loader);

	/* no flatpak, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "flatpak"))
		return;

	/* no files to use */
	repodir_fn = gs_test_get_filename (TESTDATADIR, "app-with-runtime/repo");
	if (repodir_fn == NULL ||
	    !g_file_test (repodir_fn, G_FILE_TEST_EXISTS)) {
		g_test_skip ("no flatpak test repo");
		return;
	}

	/* check changed file exists */
	root = g_getenv ("GS_SELF_TEST_FLATPAK_DATADIR");
	changed_fn = g_build_filename (root, "flatpak", ".changed", NULL);
	g_assert (g_file_test (changed_fn, G_FILE_TEST_IS_REGULAR));

	/* check repo is set up */
	config_fn = g_build_filename (root, "flatpak", "repo", "config", NULL);
	ret = g_key_file_load_from_file (kf1, config_fn, G_KEY_FILE_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	kf_remote_repo_version = g_key_file_get_integer (kf1, "core", "repo_version", &error);
	g_assert_no_error (error);
	g_assert_cmpint (kf_remote_repo_version, ==, 1);

	/* add a remote */
	app_source = gs_flatpak_app_new ("test");
	testdir = gs_test_get_filename (TESTDATADIR, "app-with-runtime");
	if (testdir == NULL)
		return;
	testdir_repourl = g_strdup_printf ("file://%s/repo", testdir);
	gs_app_set_kind (app_source, AS_APP_KIND_SOURCE);
	gs_app_set_management_plugin (app_source, "flatpak");
	gs_app_set_state (app_source, AS_APP_STATE_AVAILABLE);
	gs_flatpak_app_set_repo_url (app_source, testdir_repourl);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_INSTALLED);

	/* check remote was set up */
	ret = g_key_file_load_from_file (kf2, config_fn, G_KEY_FILE_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	kf_remote_url = g_key_file_get_string (kf2, "remote \"test\"", "url", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (kf_remote_url, !=, NULL);

	/* check the source now exists */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_SOURCES, NULL);
	sources = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (sources != NULL);
	g_assert_cmpint (gs_app_list_length (sources), ==, 1);
	app = gs_app_list_index (sources, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "test");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_SOURCE);

	/* refresh the appstream metadata */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) G_MAXUINT,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* all the apps should have the flatpak keyword */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "flatpak",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list_all = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (list_all != NULL);
	g_assert_cmpint (gs_app_list_length (list_all), ==, 2);

	/* find available application */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "Bingo",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the flatpak app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.test.Chiron.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpint ((gint64) gs_app_get_kudos (app), ==,
			 GS_APP_KUDO_MY_LANGUAGE |
			 GS_APP_KUDO_HAS_KEYWORDS |
			 GS_APP_KUDO_HI_DPI_ICON |
			 GS_APP_KUDO_SANDBOXED_SECURE |
			 GS_APP_KUDO_SANDBOXED);
	g_assert_cmpstr (gs_app_get_origin_hostname (app), ==, "localhost");
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.3");
	g_assert_cmpstr (gs_app_get_update_version (app), ==, NULL);
	g_assert_cmpstr (gs_app_get_update_details (app), ==, NULL);
	g_assert_cmpint (gs_app_get_update_urgency (app), ==, AS_URGENCY_KIND_UNKNOWN);

	/* check runtime */
	runtime = gs_app_get_runtime (app);
	g_assert (runtime != NULL);
	g_assert_cmpstr (gs_app_get_unique_id (runtime), ==, "user/flatpak/test/runtime/org.test.Runtime/master");
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_AVAILABLE);

	/* install, also installing runtime */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.3");
	g_assert_cmpint (gs_app_get_progress (app), ==, 0);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_INSTALLED);

	/* check the application exists in the right places */
	metadata_fn = g_build_filename (root,
					"flatpak",
					"app",
					"org.test.Chiron",
					"current",
					"active",
					"metadata",
					NULL);
	g_assert (g_file_test (metadata_fn, G_FILE_TEST_IS_REGULAR));
	desktop_fn = g_build_filename (root,
					"flatpak",
					"app",
					"org.test.Chiron",
					"current",
					"active",
					"export",
					"share",
					"applications",
					"org.test.Chiron.desktop",
					NULL);
	g_assert (g_file_test (desktop_fn, G_FILE_TEST_IS_REGULAR));

	/* check the runtime was installed as well */
	runtime_fn = g_build_filename (root,
					"flatpak",
					"runtime",
					"org.test.Runtime",
					"x86_64",
					"master",
					"active",
					"files",
					"share",
					"libtest",
					"README",
					NULL);
	g_assert (g_file_test (runtime_fn, G_FILE_TEST_IS_REGULAR));

	/* remove the application */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_INSTALLED);
	g_assert (!g_file_test (metadata_fn, G_FILE_TEST_IS_REGULAR));
	g_assert (!g_file_test (desktop_fn, G_FILE_TEST_IS_REGULAR));

	/* install again, to check whether the progress gets initialized */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);

	/* progress should be set to zero right before installing */
	g_assert_cmpint (gs_app_get_progress (app), ==, 0);

	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.3");
	g_assert_cmpint (gs_app_get_progress (app), ==, 0);

	/* remove the application */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_INSTALLED);
	g_assert (!g_file_test (metadata_fn, G_FILE_TEST_IS_REGULAR));
	g_assert (!g_file_test (desktop_fn, G_FILE_TEST_IS_REGULAR));

	/* remove the remote (fail, as the runtime is still installed) */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED);
	g_assert (!ret);
	g_clear_error (&error);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_INSTALLED);

	/* remove the runtime */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", runtime,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_AVAILABLE);

	/* remove the remote */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_AVAILABLE);
}

static void
gs_plugins_flatpak_app_missing_runtime_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	gboolean ret;
	g_autofree gchar *repodir_fn = NULL;
	g_autofree gchar *testdir = NULL;
	g_autofree gchar *testdir_repourl = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app_source = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* drop all caches */
	gs_plugin_loader_setup_again (plugin_loader);

	/* no flatpak, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "flatpak"))
		return;

	/* no files to use */
	repodir_fn = gs_test_get_filename (TESTDATADIR, "app-missing-runtime/repo");
	if (repodir_fn == NULL ||
	    !g_file_test (repodir_fn, G_FILE_TEST_EXISTS)) {
		g_test_skip ("no flatpak test repo");
		return;
	}

	/* add a remote */
	app_source = gs_flatpak_app_new ("test");
	testdir = gs_test_get_filename (TESTDATADIR, "app-missing-runtime");
	if (testdir == NULL)
		return;
	testdir_repourl = g_strdup_printf ("file://%s/repo", testdir);
	gs_app_set_kind (app_source, AS_APP_KIND_SOURCE);
	gs_app_set_management_plugin (app_source, "flatpak");
	gs_app_set_state (app_source, AS_APP_STATE_AVAILABLE);
	gs_flatpak_app_set_repo_url (app_source, testdir_repourl);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_INSTALLED);

	/* refresh the appstream metadata */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) G_MAXUINT,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* find available application */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "Bingo",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the flatpak app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.test.Chiron.desktop");
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);

	/* install, also installing runtime */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NOT_SUPPORTED);
	g_assert (!ret);
	g_clear_error (&error);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpint (gs_app_get_progress (app), ==, 0);

	/* remove the remote */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_AVAILABLE);
}

static void
update_app_progress_notify_cb (GsApp *app, GParamSpec *pspec, gpointer user_data)
{
	g_debug ("progress now %u%%", gs_app_get_progress (app));
	if (user_data != NULL) {
		guint *tmp = (guint *) user_data;
		(*tmp)++;
	}
}

static void
update_app_state_notify_cb (GsApp *app, GParamSpec *pspec, gpointer user_data)
{
	AsAppState state = gs_app_get_state (app);
	g_debug ("state now %s", as_app_state_to_string (state));
	if (state == AS_APP_STATE_INSTALLING) {
		gboolean *tmp = (gboolean *) user_data;
		*tmp = TRUE;
	}
}

static gboolean
update_app_action_delay_cb (gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *) user_data;
	g_main_loop_quit (loop);
	return FALSE;
}

static void
update_app_action_finish_sync (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	gboolean ret;
	g_autoptr(GError) error = NULL;
	ret = gs_plugin_loader_job_action_finish (plugin_loader, res, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_timeout_add_seconds (5, update_app_action_delay_cb, user_data);
}

static void
gs_plugins_flatpak_runtime_repo_func (GsPluginLoader *plugin_loader)
{
	GsApp *app_source;
	GsApp *runtime;
	const gchar *fn_ref = "/var/tmp/self-test/test.flatpakref";
	const gchar *fn_repo = "/var/tmp/self-test/test.flatpakrepo";
	gboolean ret;
	g_autofree gchar *fn_repourl = NULL;
	g_autofree gchar *testdir2 = NULL;
	g_autofree gchar *testdir2_repourl = NULL;
	g_autofree gchar *testdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsAppList) sources2 = NULL;
	g_autoptr(GsAppList) sources = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* drop all caches */
	gs_plugin_loader_setup_again (plugin_loader);

	/* write a flatpakrepo file */
	testdir = gs_test_get_filename (TESTDATADIR, "only-runtime");
	if (testdir == NULL)
		return;
	ret = gs_flatpak_test_write_repo_file (fn_repo, testdir, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* write a flatpakref file */
	fn_repourl = g_strdup_printf ("file://%s", fn_repo);
	testdir2 = gs_test_get_filename (TESTDATADIR, "app-missing-runtime");
	if (testdir2 == NULL)
		return;
	testdir2_repourl = g_strdup_printf ("file://%s/repo", testdir2);
	ret = gs_flatpak_test_write_ref_file (fn_ref, testdir2_repourl, fn_repourl, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* convert it to a GsApp */
	file = g_file_new_for_path (fn_ref);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME,
					 NULL);
	app = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE_LOCAL);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.test.Chiron.desktop");
	g_assert (as_utils_unique_id_equal (gs_app_get_unique_id (app),
			"user/flatpak/org.test.Chiron-origin/desktop/org.test.Chiron.desktop/master"));
	g_assert (gs_app_get_local_file (app) != NULL);

	/* get runtime */
	runtime = gs_app_get_runtime (app);
	g_assert_cmpstr (gs_app_get_unique_id (runtime), ==, "user/flatpak/*/runtime/org.test.Runtime/master");
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_AVAILABLE_LOCAL);

	/* check the number of sources */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_SOURCES, NULL);
	sources = gs_plugin_loader_job_process (plugin_loader, plugin_job,
						NULL, &error);
	g_assert_no_error (error);
	g_assert (sources != NULL);
	g_assert_cmpint (gs_app_list_length (sources), ==, 0);

	/* install, which will install the runtime from the new remote */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (plugin_loader, plugin_job,
					    NULL,
					    update_app_action_finish_sync,
					    loop);
	g_main_loop_run (loop);
	gs_test_flush_main_context ();
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_INSTALLED);

	/* check the number of sources */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_SOURCES, NULL);
	sources2 = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (sources2 != NULL);
	g_assert_cmpint (gs_app_list_length (sources2), ==, 1);

	/* remove the app */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UNKNOWN);

	/* remove the runtime */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", runtime,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_AVAILABLE);

	/* remove the remote */
	app_source = gs_app_list_index (sources2, 0);
	g_assert (app_source != NULL);
	g_assert_cmpstr (gs_app_get_unique_id (app_source), ==, "user/flatpak/*/source/test/*");
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_AVAILABLE);
}

/* same as gs_plugins_flatpak_runtime_repo_func, but this time manually
 * installing the flatpakrepo BEFORE the flatpakref is installed */
static void
gs_plugins_flatpak_runtime_repo_redundant_func (GsPluginLoader *plugin_loader)
{
	GsApp *app_source;
	GsApp *runtime;
	const gchar *fn_ref = "/var/tmp/self-test/test.flatpakref";
	const gchar *fn_repo = "/var/tmp/self-test/test.flatpakrepo";
	gboolean ret;
	g_autofree gchar *fn_repourl = NULL;
	g_autofree gchar *testdir2 = NULL;
	g_autofree gchar *testdir2_repourl = NULL;
	g_autofree gchar *testdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFile) file_repo = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsApp) app_src = NULL;
	g_autoptr(GsAppList) sources2 = NULL;
	g_autoptr(GsAppList) sources = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* drop all caches */
	gs_plugin_loader_setup_again (plugin_loader);

	/* write a flatpakrepo file */
	testdir = gs_test_get_filename (TESTDATADIR, "only-runtime");
	if (testdir == NULL)
		return;
	ret = gs_flatpak_test_write_repo_file (fn_repo, testdir, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* convert it to a GsApp */
	file_repo = g_file_new_for_path (fn_repo);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file_repo,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME,
					 NULL);
	app_src = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (app_src != NULL);
	g_assert_cmpint (gs_app_get_kind (app_src), ==, AS_APP_KIND_SOURCE);
	g_assert_cmpint (gs_app_get_state (app_src), ==, AS_APP_STATE_AVAILABLE_LOCAL);
	g_assert_cmpstr (gs_app_get_id (app_src), ==, "test");
	g_assert_cmpstr (gs_app_get_unique_id (app_src), ==, "user/*/*/source/test/master");
	g_assert (gs_app_get_local_file (app_src) != NULL);

	/* install the source manually */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app_src,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_src), ==, AS_APP_STATE_INSTALLED);

	/* write a flatpakref file */
	fn_repourl = g_strdup_printf ("file://%s", fn_repo);
	testdir2 = gs_test_get_filename (TESTDATADIR, "app-missing-runtime");
	if (testdir2 == NULL)
		return;
	testdir2_repourl = g_strdup_printf ("file://%s/repo", testdir2);
	ret = gs_flatpak_test_write_ref_file (fn_ref, testdir2_repourl, fn_repourl, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* convert it to a GsApp */
	file = g_file_new_for_path (fn_ref);
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME,
					 NULL);
	app = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE_LOCAL);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.test.Chiron.desktop");
	g_assert (as_utils_unique_id_equal (gs_app_get_unique_id (app),
			"user/flatpak/org.test.Chiron-origin/desktop/org.test.Chiron.desktop/master"));
	g_assert (gs_app_get_local_file (app) != NULL);

	/* get runtime */
	runtime = gs_app_get_runtime (app);
	g_assert_cmpstr (gs_app_get_unique_id (runtime), ==, "user/flatpak/test/runtime/org.test.Runtime/master");
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_AVAILABLE);

	/* check the number of sources */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_SOURCES, NULL);
	sources = gs_plugin_loader_job_process (plugin_loader, plugin_job,
						NULL, &error);
	g_assert_no_error (error);
	g_assert (sources != NULL);
	g_assert_cmpint (gs_app_list_length (sources), ==, 1); /* repo */

	/* install, which will NOT install the runtime from the RuntimeRemote,
	 * but from the existing test repo */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_INSTALLED);

	/* check the number of sources */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_SOURCES, NULL);
	sources2 = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (sources2 != NULL);

	/* remove the app */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UNKNOWN);

	/* remove the runtime */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", runtime,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_AVAILABLE);

	/* remove the remote */
	app_source = gs_app_list_index (sources2, 0);
	g_assert (app_source != NULL);
	g_assert_cmpstr (gs_app_get_unique_id (app_source), ==, "user/flatpak/*/source/test/*");
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_AVAILABLE);
}

static void
gs_plugins_flatpak_broken_remote_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	const gchar *fn = "/tmp/test.flatpakref";
	g_autofree gchar *testdir2 = NULL;
	g_autofree gchar *testdir2_repourl = NULL;
	g_autofree gchar *testdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsApp) app_source = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* drop all caches */
	gs_plugin_loader_setup_again (plugin_loader);

	/* no flatpak, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "flatpak"))
		return;

	/* add a remote with only the runtime in */
	app_source = gs_flatpak_app_new ("test");
	testdir = gs_test_get_filename (TESTDATADIR, "only-runtime");
	if (testdir == NULL)
		return;
	gs_app_set_kind (app_source, AS_APP_KIND_SOURCE);
	gs_app_set_management_plugin (app_source, "flatpak");
	gs_app_set_state (app_source, AS_APP_STATE_AVAILABLE);
	gs_flatpak_app_set_repo_url (app_source, "file:///wont/work");
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_INSTALLED);

	/* write a flatpakref file */
	testdir2 = gs_test_get_filename (TESTDATADIR, "app-with-runtime");
	if (testdir2 == NULL)
		return;
	testdir2_repourl = g_strdup_printf ("file://%s/repo", testdir2);
	ret = gs_flatpak_test_write_ref_file (fn, testdir2_repourl, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* convert it to a GsApp */
	file = g_file_new_for_path (fn);
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION,
					 NULL);
	app = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE_LOCAL);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.test.Chiron.desktop");
	g_assert (as_utils_unique_id_equal (gs_app_get_unique_id (app),
			"user/flatpak/org.test.Chiron-origin/desktop/org.test.Chiron.desktop/master"));
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://127.0.0.1/");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Chiron");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Single line synopsis");
	g_assert_cmpstr (gs_app_get_description (app), ==, "Long description.");
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.3");
	g_assert (gs_app_get_local_file (app) != NULL);

	/* remove source */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
gs_plugins_flatpak_ref_func (GsPluginLoader *plugin_loader)
{
	GsApp *app_tmp;
	GsApp *runtime;
	gboolean ret;
	const gchar *fn = "/tmp/test.flatpakref";
	g_autofree gchar *testdir2 = NULL;
	g_autofree gchar *testdir2_repourl = NULL;
	g_autofree gchar *testdir = NULL;
	g_autofree gchar *testdir_repourl = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsApp) app2 = NULL;
	g_autoptr(GsApp) app_source = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsAppList) search1 = NULL;
	g_autoptr(GsAppList) search2 = NULL;
	g_autoptr(GsAppList) sources = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	/* drop all caches */
	gs_plugin_loader_setup_again (plugin_loader);

	/* no flatpak, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "flatpak"))
		return;

	/* add a remote with only the runtime in */
	app_source = gs_flatpak_app_new ("test");
	testdir = gs_test_get_filename (TESTDATADIR, "only-runtime");
	if (testdir == NULL)
		return;
	testdir_repourl = g_strdup_printf ("file://%s/repo", testdir);
	gs_app_set_kind (app_source, AS_APP_KIND_SOURCE);
	gs_app_set_management_plugin (app_source, "flatpak");
	gs_app_set_state (app_source, AS_APP_STATE_AVAILABLE);
	gs_flatpak_app_set_repo_url (app_source, testdir_repourl);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_INSTALLED);

	/* refresh the appstream metadata */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) 0,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* find available application */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "runtime",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the flatpak runtime */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	runtime = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (runtime), ==, "org.test.Runtime");
	g_assert_cmpstr (gs_app_get_unique_id (runtime), ==, "user/flatpak/test/runtime/org.test.Runtime/master");
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_AVAILABLE);

	/* install the runtime ahead of time */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", runtime,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_INSTALLED);

	/* write a flatpakref file */
	testdir2 = gs_test_get_filename (TESTDATADIR, "app-with-runtime");
	if (testdir2 == NULL)
		return;
	testdir2_repourl = g_strdup_printf ("file://%s/repo", testdir2);
	ret = gs_flatpak_test_write_ref_file (fn, testdir2_repourl, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* convert it to a GsApp */
	file = g_file_new_for_path (fn);
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME,
					 NULL);
	app = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE_LOCAL);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.test.Chiron.desktop");
	g_assert (as_utils_unique_id_equal (gs_app_get_unique_id (app),
			"user/flatpak/org.test.Chiron-origin/desktop/org.test.Chiron.desktop/master"));
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://127.0.0.1/");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Chiron");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Single line synopsis");
	g_assert_cmpstr (gs_app_get_description (app), ==, "Long description.");
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.3");
	g_assert (gs_app_get_local_file (app) != NULL);

	/* get runtime */
	runtime = gs_app_get_runtime (app);
	g_assert_cmpstr (gs_app_get_unique_id (runtime), ==, "user/flatpak/test/runtime/org.test.Runtime/master");
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_INSTALLED);

	/* install */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.3");
	g_assert_cmpstr (gs_app_get_update_version (app), ==, NULL);
	g_assert_cmpstr (gs_app_get_update_details (app), ==, NULL);

	/* search for the application */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "chiron",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	search1 = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (search1 != NULL);
	g_assert_cmpint (gs_app_list_length (search1), ==, 1);
	app_tmp = gs_app_list_index (search1, 0);
	g_assert_cmpstr (gs_app_get_id (app_tmp), ==, "org.test.Chiron.desktop");

	/* convert it to a GsApp again, and get the installed thing */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME,
					 NULL);
	app2 = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (app2 != NULL);
	g_assert_cmpint (gs_app_get_state (app2), ==, AS_APP_STATE_INSTALLED);
	g_assert (as_utils_unique_id_equal (gs_app_get_unique_id (app2),
		  "user/flatpak/org.test.Chiron-origin/desktop/org.test.Chiron.desktop/master"));

	/* remove app */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app2,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* remove runtime */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", runtime,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* remove source */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* there should be no sources now */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_SOURCES, NULL);
	sources = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (sources != NULL);
	g_assert_cmpint (gs_app_list_length (sources), ==, 0);

	/* there should be no matches now */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "chiron",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	search2 = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (search2 != NULL);
	g_assert_cmpint (gs_app_list_length (search2), ==, 0);
}

static void
gs_plugins_flatpak_count_signal_cb (GsPluginLoader *plugin_loader, guint *cnt)
{
	if (cnt != NULL)
		(*cnt)++;
}

static void
gs_plugins_flatpak_app_update_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	GsApp *app_tmp;
	GsApp *runtime;
	GsApp *old_runtime;
	gboolean got_progress_installing = FALSE;
	gboolean ret;
	guint notify_progress_id;
	guint notify_state_id;
	guint pending_app_changed_cnt = 0;
	guint pending_apps_changed_id;
	guint progress_cnt = 0;
	guint updates_changed_cnt = 0;
	guint updates_changed_id;
	g_autofree gchar *repodir1_fn = NULL;
	g_autofree gchar *repodir2_fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app_source = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsAppList) list_updates = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);

	/* drop all caches */
	gs_plugin_loader_setup_again (plugin_loader);

	/* no flatpak, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "flatpak"))
		return;

	/* no files to use */
	repodir1_fn = gs_test_get_filename (TESTDATADIR, "app-with-runtime/repo");
	if (repodir1_fn == NULL ||
	    !g_file_test (repodir1_fn, G_FILE_TEST_EXISTS)) {
		g_test_skip ("no flatpak test repo");
		return;
	}
	repodir2_fn = gs_test_get_filename (TESTDATADIR, "app-update/repo");
	if (repodir2_fn == NULL ||
	    !g_file_test (repodir2_fn, G_FILE_TEST_EXISTS)) {
		g_test_skip ("no flatpak test repo");
		return;
	}

	/* add indirection so we can switch this after install */
	unlink ("/var/tmp/self-test/repo");
	g_assert (symlink (repodir1_fn, "/var/tmp/self-test/repo") == 0);

	/* add a remote */
	app_source = gs_flatpak_app_new ("test");
	gs_app_set_kind (app_source, AS_APP_KIND_SOURCE);
	gs_app_set_management_plugin (app_source, "flatpak");
	gs_app_set_state (app_source, AS_APP_STATE_AVAILABLE);
	gs_flatpak_app_set_repo_url (app_source, "file:///var/tmp/self-test/repo");
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_INSTALLED);

	/* refresh the appstream metadata */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) G_MAXUINT,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	/* find available application */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "Bingo",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the flatpak app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.test.Chiron.desktop");
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);

	/* install, also installing runtime */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.3");
	g_assert_cmpstr (gs_app_get_update_version (app), ==, NULL);
	g_assert_cmpstr (gs_app_get_update_details (app), ==, NULL);
	g_assert_cmpint (gs_app_get_progress (app), ==, 0);

	/* switch to the new repo */
	g_assert (unlink ("/var/tmp/self-test/repo") == 0);
	g_assert (symlink (repodir2_fn, "/var/tmp/self-test/repo") == 0);

	/* refresh the appstream metadata */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) 0, /* force now */
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get the updates list */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_UPDATES,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS,
					 NULL);
	list_updates = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list_updates != NULL);

	/* make sure there are two entries */
	g_assert_cmpint (gs_app_list_length (list_updates), ==, 1);
	for (guint i = 0; i < gs_app_list_length (list_updates); i++) {
		app_tmp = gs_app_list_index (list_updates, i);
		g_debug ("got update %s", gs_app_get_unique_id (app_tmp));
	}

	/* check they are the same GObject */
	app_tmp = gs_app_list_lookup (list_updates, "*/flatpak/test/*/org.test.Chiron.desktop/*");
	g_assert (app_tmp == app);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE_LIVE);
	g_assert_cmpstr (gs_app_get_update_details (app), ==, "Version 1.2.4:\nThis is best.\n\nVersion 1.2.3:\nThis is better.");
	g_assert_cmpstr (gs_app_get_update_version (app), ==, "1.2.4");

	/* care about signals */
	pending_apps_changed_id =
		g_signal_connect (plugin_loader, "pending-apps-changed",
				  G_CALLBACK (gs_plugins_flatpak_count_signal_cb),
				  &pending_app_changed_cnt);
	updates_changed_id =
		g_signal_connect (plugin_loader, "updates-changed",
				  G_CALLBACK (gs_plugins_flatpak_count_signal_cb),
				  &updates_changed_cnt);
	notify_state_id =
		g_signal_connect (app, "notify::state",
				  G_CALLBACK (update_app_state_notify_cb),
				  &got_progress_installing);
	notify_progress_id =
		g_signal_connect (app, "notify::progress",
				  G_CALLBACK (update_app_progress_notify_cb),
				  &progress_cnt);

	/* check that the runtime is not the update's one */
	old_runtime = gs_app_get_runtime (app);
	g_assert (old_runtime != NULL);
	g_assert_cmpstr (gs_app_get_branch (old_runtime), !=, "new_master");

	/* use a mainloop so we get the events in the default context */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPDATE,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (plugin_loader, plugin_job,
					    NULL,
					    update_app_action_finish_sync,
					    loop);
	g_main_loop_run (loop);
	gs_test_flush_main_context ();
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.4");
	g_assert_cmpstr (gs_app_get_update_version (app), ==, NULL);
	g_assert_cmpstr (gs_app_get_update_details (app), ==, NULL);
	g_assert_cmpint (gs_app_get_progress (app), ==, 0);
	g_assert (got_progress_installing);
	//g_assert_cmpint (progress_cnt, >, 20); //FIXME: bug in OSTree
	g_assert_cmpint (pending_app_changed_cnt, ==, 0);
	g_assert_cmpint (updates_changed_cnt, ==, 1);

	/* check that the app's runtime has changed */
	runtime = gs_app_get_runtime (app);
	g_assert (runtime != NULL);
	g_assert_cmpstr (gs_app_get_unique_id (runtime), ==, "user/flatpak/test/runtime/org.test.Runtime/new_master");
	g_assert (old_runtime != runtime);
	g_assert_cmpstr (gs_app_get_branch (runtime), ==, "new_master");
	g_assert (gs_app_get_state (runtime) == AS_APP_STATE_INSTALLED);

	/* no longer care */
	g_signal_handler_disconnect (plugin_loader, pending_apps_changed_id);
	g_signal_handler_disconnect (plugin_loader, updates_changed_id);
	g_signal_handler_disconnect (app, notify_state_id);
	g_signal_handler_disconnect (app, notify_progress_id);

	/* remove the app */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* remove the old_runtime */
	g_assert_cmpstr (gs_app_get_unique_id (old_runtime), ==, "user/flatpak/test/runtime/org.test.Runtime/master");
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", old_runtime,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	/* remove the runtime */
	g_assert_cmpstr (gs_app_get_unique_id (runtime), ==, "user/flatpak/test/runtime/org.test.Runtime/new_master");
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", runtime,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	/* remove the remote */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_AVAILABLE);
}

static void
gs_plugins_flatpak_runtime_extension_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	GsApp *runtime;
	GsApp *app_tmp;
	gboolean got_progress_installing = FALSE;
	gboolean ret;
	guint notify_progress_id;
	guint notify_state_id;
	guint pending_app_changed_cnt = 0;
	guint pending_apps_changed_id;
	guint progress_cnt = 0;
	guint updates_changed_cnt = 0;
	guint updates_changed_id;
	g_autofree gchar *repodir1_fn = NULL;
	g_autofree gchar *repodir2_fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app_source = NULL;
	g_autoptr(GsApp) extension = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsAppList) list_updates = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);

	/* drop all caches */
	gs_plugin_loader_setup_again (plugin_loader);

	/* no flatpak, abort */
	g_assert_true (gs_plugin_loader_get_enabled (plugin_loader, "flatpak"));

	/* no files to use */
	repodir1_fn = gs_test_get_filename (TESTDATADIR, "app-extension/repo");
	if (repodir1_fn == NULL ||
	    !g_file_test (repodir1_fn, G_FILE_TEST_EXISTS)) {
		g_test_skip ("no flatpak test repo");
		return;
	}
	repodir2_fn = gs_test_get_filename (TESTDATADIR, "app-extension-update/repo");
	if (repodir2_fn == NULL ||
	    !g_file_test (repodir2_fn, G_FILE_TEST_EXISTS)) {
		g_test_skip ("no flatpak test repo");
		return;
	}

	/* add indirection so we can switch this after install */
	g_assert_cmpint (symlink (repodir1_fn, "/var/tmp/self-test/repo"), ==, 0);

	/* add a remote */
	app_source = gs_flatpak_app_new ("test");
	gs_app_set_kind (app_source, AS_APP_KIND_SOURCE);
	gs_app_set_management_plugin (app_source, "flatpak");
	gs_app_set_state (app_source, AS_APP_STATE_AVAILABLE);
	gs_flatpak_app_set_repo_url (app_source, "file:///var/tmp/self-test/repo");
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_INSTALLED);

	/* refresh the appstream metadata */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) G_MAXUINT,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert_true (ret);

	/* find available application */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "Bingo",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert_nonnull (list);

	/* make sure there is one entry, the flatpak app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.test.Chiron.desktop");
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);

	/* install, also installing runtime and suggested extensions */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.3");

	/* check if the extension was installed */
	extension = gs_plugin_loader_app_create (plugin_loader,
			"user/flatpak/*/runtime/org.test.Chiron.Extension/master");
	g_assert_nonnull (extension);
	g_assert_cmpint (gs_app_get_state (extension), ==, AS_APP_STATE_INSTALLED);

	/* switch to the new repo (to get the update) */
	g_assert_cmpint (unlink ("/var/tmp/self-test/repo"), ==, 0);
	g_assert_cmpint (symlink (repodir2_fn, "/var/tmp/self-test/repo"), ==, 0);

	/* refresh the appstream metadata */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) 0, /* force now */
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* get the updates list */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_UPDATES,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS,
					 NULL);
	list_updates = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert_nonnull (list_updates);

	g_assert_cmpint (gs_app_list_length (list_updates), ==, 1);
	for (guint i = 0; i < gs_app_list_length (list_updates); i++) {
		app_tmp = gs_app_list_index (list_updates, i);
		g_debug ("got update %s", gs_app_get_unique_id (app_tmp));
	}

	/* check that the extension has no update */
	app_tmp = gs_app_list_lookup (list_updates, "*/flatpak/test/*/org.test.Chiron.Extension/*");
	g_assert_null (app_tmp);

	/* check that the app has an update (it's affected by the extension's update) */
	app_tmp = gs_app_list_lookup (list_updates, "*/flatpak/test/*/org.test.Chiron.desktop/*");
	g_assert (app_tmp == app);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE_LIVE);

	/* care about signals */
	pending_apps_changed_id =
		g_signal_connect (plugin_loader, "pending-apps-changed",
				  G_CALLBACK (gs_plugins_flatpak_count_signal_cb),
				  &pending_app_changed_cnt);
	updates_changed_id =
		g_signal_connect (plugin_loader, "updates-changed",
				  G_CALLBACK (gs_plugins_flatpak_count_signal_cb),
				  &updates_changed_cnt);
	notify_state_id =
		g_signal_connect (app, "notify::state",
				  G_CALLBACK (update_app_state_notify_cb),
				  &got_progress_installing);
	notify_progress_id =
		g_signal_connect (app, "notify::progress",
				  G_CALLBACK (update_app_progress_notify_cb),
				  &progress_cnt);

	/* use a mainloop so we get the events in the default context */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPDATE,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (plugin_loader, plugin_job,
					    NULL,
					    update_app_action_finish_sync,
					    loop);
	g_main_loop_run (loop);
	gs_test_flush_main_context ();
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.2.3");
	g_assert_true (got_progress_installing);
	g_assert_cmpint (pending_app_changed_cnt, ==, 0);

	/* check the extension's state after the update */
	g_assert_cmpint (gs_app_get_state (extension), ==, AS_APP_STATE_INSTALLED);

	/* no longer care */
	g_signal_handler_disconnect (plugin_loader, pending_apps_changed_id);
	g_signal_handler_disconnect (plugin_loader, updates_changed_id);
	g_signal_handler_disconnect (app, notify_state_id);
	g_signal_handler_disconnect (app, notify_progress_id);

	/* getting the runtime for later removal */
	runtime = gs_app_get_runtime (app);

	/* remove the app */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* remove the runtime */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", runtime,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (runtime), ==, AS_APP_STATE_AVAILABLE);

	/* remove the remote */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app_source,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app_source), ==, AS_APP_STATE_AVAILABLE);

	/* verify that the extension has been removed by the app's removal */
	g_assert_false (gs_app_is_installed (extension));
}

int
main (int argc, char **argv)
{
	const gchar *tmp_root = "/var/tmp/self-test";
	gboolean ret;
	g_autofree gchar *xml = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *whitelist[] = {
		"appstream",
		"flatpak",
		"icons",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv ("GS_SELF_TEST_FLATPAK_DATADIR", tmp_root, TRUE);
	g_setenv ("GS_SELF_TEST_PLUGIN_ERROR_FAIL_HARD", "1", TRUE);

	/* allow dist'ing with no gnome-software installed */
	if (g_getenv ("GS_SELF_TEST_SKIP_ALL") != NULL)
		return 0;

	/* ensure test root does not exist */
	if (g_file_test (tmp_root, G_FILE_TEST_EXISTS)) {
		ret = gs_utils_rmtree (tmp_root, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert (!g_file_test (tmp_root, G_FILE_TEST_EXISTS));
	}

	xml = g_strdup ("<?xml version=\"1.0\"?>\n"
		"<components version=\"0.9\">\n"
		"  <component type=\"desktop\">\n"
		"    <id>zeus.desktop</id>\n"
		"    <name>Zeus</name>\n"
		"    <summary>A teaching application</summary>\n"
		"  </component>\n"
		"</components>\n");
	g_setenv ("GS_SELF_TEST_APPSTREAM_XML", xml, TRUE);
	g_setenv ("GS_SELF_TEST_APPSTREAM_ICON_ROOT",
		  "/var/tmp/self-test/flatpak/appstream/test/x86_64/active/", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* we can only load this once per process */
	plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR_CORE);
	ret = gs_plugin_loader_setup (plugin_loader,
				      (gchar**) whitelist,
				      NULL,
				      NULL,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* plugin tests go here */
	g_test_add_data_func ("/gnome-software/plugins/flatpak/app-with-runtime",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_app_with_runtime_func);
	g_test_add_data_func ("/gnome-software/plugins/flatpak/app-missing-runtime",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_app_missing_runtime_func);
	g_test_add_data_func ("/gnome-software/plugins/flatpak/ref",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_ref_func);
	g_test_add_data_func ("/gnome-software/plugins/flatpak/broken-remote",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_broken_remote_func);
	g_test_add_data_func ("/gnome-software/plugins/flatpak/runtime-repo",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_runtime_repo_func);
	g_test_add_data_func ("/gnome-software/plugins/flatpak/runtime-repo-redundant",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_runtime_repo_redundant_func);
	g_test_add_data_func ("/gnome-software/plugins/flatpak/app-runtime-extension",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_runtime_extension_func);
	g_test_add_data_func ("/gnome-software/plugins/flatpak/app-update-runtime",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_app_update_func);
	g_test_add_data_func ("/gnome-software/plugins/flatpak/repo",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_repo_func);
	g_test_add_data_func ("/gnome-software/plugins/flatpak/repo{non-ascii}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_flatpak_repo_non_ascii_func);
	return g_test_run ();
}

/* vim: set noexpandtab: */
