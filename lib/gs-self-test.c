/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
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

#include "gs-test.h"

static gboolean
gs_app_list_filter_cb (GsApp *app, gpointer user_data)
{
	if (g_strcmp0 (gs_app_get_id (app), "a") == 0)
		return FALSE;
	if (g_strcmp0 (gs_app_get_id (app), "c") == 0)
		return FALSE;
	return TRUE;
}

static void
gs_utils_url_func (void)
{
	g_autofree gchar *path1 = NULL;
	g_autofree gchar *path2 = NULL;
	g_autofree gchar *path3 = NULL;
	g_autofree gchar *scheme1 = NULL;
	g_autofree gchar *scheme2 = NULL;
	g_autofree gchar *value1 = NULL;
	g_autofree gchar *value2 = NULL;
	g_autofree gchar *value3 = NULL;
	g_autofree gchar *value4 = NULL;
	g_autofree gchar *value5 = NULL;

	scheme1 = gs_utils_get_url_scheme ("appstream://gimp.desktop");
	g_assert_cmpstr (scheme1, ==, "appstream");
	scheme2 = gs_utils_get_url_scheme ("appstream:gimp.desktop");
	g_assert_cmpstr (scheme2, ==, "appstream");

	path1 = gs_utils_get_url_path ("appstream://gimp.desktop");
	g_assert_cmpstr (path1, ==, "gimp.desktop");
	path2 = gs_utils_get_url_path ("appstream:gimp.desktop");
	g_assert_cmpstr (path2, ==, "gimp.desktop");
	path3 = gs_utils_get_url_path ("apt:/gimp");
	g_assert_cmpstr (path3, ==, "gimp");

	value1 = gs_utils_get_url_query_param ("snap://moon-buggy", "channel");
	g_assert_null (value1);
	value2 = gs_utils_get_url_query_param ("snap://moon-buggy?", "channel");
	g_assert_null (value2);
	value3 = gs_utils_get_url_query_param ("snap://moon-buggy?channel=beta", "channel");
	g_assert_cmpstr (value3, ==, "beta");
	value4 = gs_utils_get_url_query_param ("snap://moon-buggy?channel=beta&foo=bar", "channel");
	g_assert_cmpstr (value4, ==, "beta");
	value5 = gs_utils_get_url_query_param ("snap://moon-buggy?foo=bar&channel=beta", "channel");
	g_assert_cmpstr (value5, ==, "beta");
}

static void
gs_utils_wilson_func (void)
{
	g_assert_cmpint ((gint64) gs_utils_get_wilson_rating (0, 0, 0, 0, 0), ==, -1);
	g_assert_cmpint ((gint64) gs_utils_get_wilson_rating (0, 0, 0, 0, 400), ==, 100);
	g_assert_cmpint ((gint64) gs_utils_get_wilson_rating (10, 0, 0, 0, 400), ==, 98);
	g_assert_cmpint ((gint64) gs_utils_get_wilson_rating (0, 0, 0, 0, 1), ==, 76);
	g_assert_cmpint ((gint64) gs_utils_get_wilson_rating (5, 4, 20, 100, 400), ==, 93);
}

static void
gs_os_release_func (void)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;

	fn = gs_test_get_filename (TESTDATADIR, "tests/os-release");
	g_assert (fn != NULL);
	g_setenv ("GS_SELF_TEST_OS_RELEASE_FILENAME", fn, TRUE);

	os_release = gs_os_release_new (&error);
	g_assert_no_error (error);
	g_assert (os_release != NULL);
	g_assert_cmpstr (gs_os_release_get_id (os_release), ==, "fedora");
	g_assert_cmpstr (gs_os_release_get_name (os_release), ==, "Fedora");
	g_assert_cmpstr (gs_os_release_get_version (os_release), ==, "25 (Workstation Edition)");
	g_assert_cmpstr (gs_os_release_get_version_id (os_release), ==, "25");
	g_assert_cmpstr (gs_os_release_get_pretty_name (os_release), ==, "Fedora 25 (Workstation Edition)");
}

static void
gs_utils_append_kv_func (void)
{
	g_autoptr(GString) str = g_string_new (NULL);

	/* normal */
	gs_utils_append_key_value (str, 5, "key", "val");
	g_assert_cmpstr (str->str, ==, "key:  val\n");

	/* oversize */
	g_string_truncate (str, 0);
	gs_utils_append_key_value (str, 5, "longkey", "val");
	g_assert_cmpstr (str->str, ==, "longkey: val\n");

	/* null key */
	g_string_truncate (str, 0);
	gs_utils_append_key_value (str, 5, NULL, "val");
	g_assert_cmpstr (str->str, ==, "      val\n");

	/* zero align key */
	g_string_truncate (str, 0);
	gs_utils_append_key_value (str, 0, "key", "val");
	g_assert_cmpstr (str->str, ==, "key: val\n");
}

static void
gs_utils_cache_func (void)
{
	g_autofree gchar *fn1 = NULL;
	g_autofree gchar *fn2 = NULL;
	g_autoptr(GError) error = NULL;

	fn1 = gs_utils_get_cache_filename ("test",
					   "http://www.foo.bar/baz",
					   GS_UTILS_CACHE_FLAG_WRITEABLE,
					   &error);
	g_assert_no_error (error);
	g_assert_cmpstr (fn1, !=, NULL);
	g_assert (g_str_has_prefix (fn1, g_get_user_cache_dir ()));
	g_assert (g_str_has_suffix (fn1, "test/baz"));

	fn2 = gs_utils_get_cache_filename ("test",
					   "http://www.foo.bar/baz",
					   GS_UTILS_CACHE_FLAG_WRITEABLE |
					   GS_UTILS_CACHE_FLAG_USE_HASH,
					   &error);
	g_assert_no_error (error);
	g_assert_cmpstr (fn2, !=, NULL);
	g_assert (g_str_has_prefix (fn2, g_get_user_cache_dir ()));
	g_assert (g_str_has_suffix (fn2, "test/295099f59d12b3eb0b955325fcb699cd23792a89-baz"));
}

static void
gs_utils_error_func (void)
{
	g_autofree gchar *app_id = NULL;
	g_autofree gchar *origin_id = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = gs_app_new ("gimp.desktop");
	g_autoptr(GsApp) origin = gs_app_new ("gimp-repo");

	for (guint i = 0; i < GS_PLUGIN_ERROR_LAST; i++)
		g_assert (gs_plugin_error_to_string (i) != NULL);

	/* noop */
	gs_utils_error_add_app_id (&error, app);
	gs_utils_error_add_origin_id (&error, origin);

	g_set_error (&error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
		     "failed");
	g_assert_cmpstr (error->message, ==, "failed");
	gs_utils_error_add_app_id (&error, app);
	gs_utils_error_add_origin_id (&error, origin);
	g_assert_cmpstr (error->message, ==, "[*/*/*/*/gimp-repo/*] {*/*/*/*/gimp.desktop/*} failed");

	/* find and strip any unique IDs from the error message */
	for (guint i = 0; i < 2; i++) {
		if (app_id == NULL)
			app_id = gs_utils_error_strip_app_id (error);
		if (origin_id == NULL)
			origin_id = gs_utils_error_strip_origin_id (error);
	}

	g_assert_cmpstr (app_id, ==, "*/*/*/*/gimp.desktop/*");
	g_assert_cmpstr (origin_id, ==, "*/*/*/*/gimp-repo/*");
	g_assert_cmpstr (error->message, ==, "failed");
}

static void
gs_utils_parse_evr_func (void)
{
	gboolean ret;

	{
		g_autofree gchar *epoch = NULL;
		g_autofree gchar *version = NULL;
		g_autofree gchar *release = NULL;

		ret = gs_utils_parse_evr ("3.26.0-1.fc27", &epoch, &version, &release);
		g_assert (ret);
		g_assert_cmpstr (epoch, ==, "0");
		g_assert_cmpstr (version, ==, "3.26.0");
		g_assert_cmpstr (release, ==, "1.fc27");
	}

	{
		g_autofree gchar *epoch = NULL;
		g_autofree gchar *version = NULL;
		g_autofree gchar *release = NULL;

		ret = gs_utils_parse_evr ("1:3.26.0-1.fc27", &epoch, &version, &release);
		g_assert (ret);
		g_assert_cmpstr (epoch, ==, "1");
		g_assert_cmpstr (version, ==, "3.26.0");
		g_assert_cmpstr (release, ==, "1.fc27");
	}

	{
		g_autofree gchar *epoch = NULL;
		g_autofree gchar *version = NULL;
		g_autofree gchar *release = NULL;

		ret = gs_utils_parse_evr ("234", &epoch, &version, &release);
		g_assert (ret);
		g_assert_cmpstr (epoch, ==, "0");
		g_assert_cmpstr (version, ==, "234");
		g_assert_cmpstr (release, ==, "0");
	}

	{
		g_autofree gchar *epoch = NULL;
		g_autofree gchar *version = NULL;
		g_autofree gchar *release = NULL;

		ret = gs_utils_parse_evr ("3:1.6~git20131207+dfsg-2ubuntu1~14.04.3", &epoch, &version, &release);
		g_assert (ret);
		g_assert_cmpstr (epoch, ==, "3");
		g_assert_cmpstr (version, ==, "1.6~git20131207+dfsg");
		g_assert_cmpstr (release, ==, "2ubuntu1~14.04.3");
	}

	{
		g_autofree gchar *epoch = NULL;
		g_autofree gchar *version = NULL;
		g_autofree gchar *release = NULL;

		ret = gs_utils_parse_evr ("1-2-3-4-5-6", &epoch, &version, &release);
		g_assert (!ret);
	}

	{
		g_autofree gchar *epoch = NULL;
		g_autofree gchar *version = NULL;
		g_autofree gchar *release = NULL;

		ret = gs_utils_parse_evr ("", &epoch, &version, &release);
		g_assert (!ret);
	}
}

static void
gs_plugin_download_rewrite_func (void)
{
	g_autofree gchar *css = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPlugin) plugin = NULL;
	const gchar *resource = "background:\n"
				" url('file://" DATADIR "/gnome-software/featured-maps.png')\n"
				" url('file://" DATADIR "/gnome-software/featured-maps-bg.png')\n"
				" bottom center / contain no-repeat;\n";

	/* only when installed */
	if (!g_file_test (DATADIR "/gnome-software/featured-maps.png", G_FILE_TEST_EXISTS)) {
		g_test_skip ("not installed");
		return;
	}

	/* test rewrite */
	plugin = gs_plugin_new ();
	gs_plugin_set_name (plugin, "self-test");
	css = gs_plugin_download_rewrite_resource (plugin,
						   NULL, /* app */
						   resource,
						   NULL,
						   &error);
	g_assert_no_error (error);
	g_assert (css != NULL);
}

static void
gs_plugin_func (void)
{
	GsAppList *list;
	GsAppList *list_dup;
	GsAppList *list_remove;
	GsApp *app;

	/* check enums converted */
	for (guint i = 0; i < GS_PLUGIN_ACTION_LAST; i++) {
		const gchar *tmp = gs_plugin_action_to_string (i);
		if (tmp == NULL)
			g_critical ("failed to convert %u", i);
		g_assert_cmpint (gs_plugin_action_from_string (tmp), ==, i);
	}
	for (guint i = 1; i < GS_PLUGIN_ACTION_LAST; i++) {
		const gchar *tmp = gs_plugin_action_to_function_name (i);
		if (tmp == NULL)
			g_critical ("failed to convert %u", i);
	}

	/* add a couple of duplicate IDs */
	app = gs_app_new ("a");
	list = gs_app_list_new ();
	gs_app_list_add (list, app);
	g_object_unref (app);

	/* test refcounting */
	g_assert_cmpstr (gs_app_get_id (gs_app_list_index (list, 0)), ==, "a");
	list_dup = gs_app_list_copy (list);
	g_object_unref (list);
	g_assert_cmpint (gs_app_list_length (list_dup), ==, 1);
	g_assert_cmpstr (gs_app_get_id (gs_app_list_index (list_dup, 0)), ==, "a");
	g_object_unref (list_dup);

	/* test removing objects */
	app = gs_app_new ("a");
	list_remove = gs_app_list_new ();
	gs_app_list_add (list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_app_list_add (list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("c");
	gs_app_list_add (list_remove, app);
	g_object_unref (app);
	g_assert_cmpint (gs_app_list_length (list_remove), ==, 3);
	gs_app_list_filter (list_remove, gs_app_list_filter_cb, NULL);
	g_assert_cmpint (gs_app_list_length (list_remove), ==, 1);
	g_assert_cmpstr (gs_app_get_id (gs_app_list_index (list_remove, 0)), ==, "b");

	/* test removing duplicates at runtime */
	app = gs_app_new ("b");
	gs_app_list_add (list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_app_list_add (list_remove, app);
	g_object_unref (app);
	g_assert_cmpint (gs_app_list_length (list_remove), ==, 1);
	g_assert_cmpstr (gs_app_get_id (gs_app_list_index (list_remove, 0)), ==, "b");
	g_object_unref (list_remove);

	/* test removing duplicates when lazy-loading */
	list_remove = gs_app_list_new ();
	app = gs_app_new (NULL);
	gs_app_list_add (list_remove, app);
	gs_app_set_id (app, "e");
	g_object_unref (app);
	app = gs_app_new (NULL);
	gs_app_list_add (list_remove, app);
	gs_app_set_id (app, "e");
	g_object_unref (app);
	g_assert_cmpint (gs_app_list_length (list_remove), ==, 2);
	gs_app_list_filter_duplicates (list_remove, GS_APP_LIST_FILTER_FLAG_NONE);
	g_assert_cmpint (gs_app_list_length (list_remove), ==, 1);
	g_object_unref (list_remove);

	/* test removing duplicates when some apps have no app ID */
	list_remove = gs_app_list_new ();
	app = gs_app_new (NULL);
	gs_app_list_add (list_remove, app);
	g_object_unref (app);
	app = gs_app_new (NULL);
	gs_app_list_add (list_remove, app);
	g_object_unref (app);
	app = gs_app_new (NULL);
	gs_app_list_add (list_remove, app);
	gs_app_set_id (app, "e");
	g_object_unref (app);
	g_assert_cmpint (gs_app_list_length (list_remove), ==, 3);
	gs_app_list_filter_duplicates (list_remove, GS_APP_LIST_FILTER_FLAG_NONE);
	g_assert_cmpint (gs_app_list_length (list_remove), ==, 3);
	g_object_unref (list_remove);

	/* remove lazy-loaded app */
	list_remove = gs_app_list_new ();
	app = gs_app_new (NULL);
	gs_app_list_add (list_remove, app);
	gs_app_list_remove (list_remove, app);
	g_assert_cmpint (gs_app_list_length (list_remove), ==, 0);
	g_object_unref (app);
	g_object_unref (list_remove);

	/* respect priority when deduplicating */
	list = gs_app_list_new ();
	app = gs_app_new ("e");
	gs_app_set_unique_id (app, "user/foo/*/*/e/*");
	gs_app_list_add (list, app);
	gs_app_set_priority (app, 0);
	g_object_unref (app);
	app = gs_app_new ("e");
	gs_app_set_unique_id (app, "user/bar/*/*/e/*");
	gs_app_list_add (list, app);
	gs_app_set_priority (app, 99);
	g_object_unref (app);
	app = gs_app_new ("e");
	gs_app_set_unique_id (app, "user/baz/*/*/e/*");
	gs_app_list_add (list, app);
	gs_app_set_priority (app, 50);
	g_object_unref (app);
	g_assert_cmpint (gs_app_list_length (list), ==, 3);
	gs_app_list_filter_duplicates (list, GS_APP_LIST_FILTER_FLAG_KEY_ID);
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	g_assert_cmpstr (gs_app_get_unique_id (gs_app_list_index (list, 0)), ==, "user/bar/*/*/e/*");
	g_object_unref (list);

	/* respect priority (using name and version) when deduplicating */
	list = gs_app_list_new ();
	app = gs_app_new ("e");
	gs_app_add_source (app, "foo");
	gs_app_set_version (app, "1.2.3");
	gs_app_set_unique_id (app, "user/foo/repo/*/*/*");
	gs_app_list_add (list, app);
	gs_app_set_priority (app, 0);
	g_object_unref (app);
	app = gs_app_new ("e");
	gs_app_add_source (app, "foo");
	gs_app_set_version (app, "1.2.3");
	gs_app_set_unique_id (app, "user/foo/repo-security/*/*/*");
	gs_app_list_add (list, app);
	gs_app_set_priority (app, 99);
	g_object_unref (app);
	app = gs_app_new ("e");
	gs_app_add_source (app, "foo");
	gs_app_set_version (app, "1.2.3");
	gs_app_set_unique_id (app, "user/foo/repo-universe/*/*/*");
	gs_app_list_add (list, app);
	gs_app_set_priority (app, 50);
	g_object_unref (app);
	g_assert_cmpint (gs_app_list_length (list), ==, 3);
	gs_app_list_filter_duplicates (list, GS_APP_LIST_FILTER_FLAG_KEY_ID |
					     GS_APP_LIST_FILTER_FLAG_KEY_SOURCE |
					     GS_APP_LIST_FILTER_FLAG_KEY_VERSION);
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	g_assert_cmpstr (gs_app_get_unique_id (gs_app_list_index (list, 0)), ==, "user/foo/repo-security/*/*/*");
	g_object_unref (list);

	/* use globs when adding */
	list = gs_app_list_new ();
	app = gs_app_new ("b");
	gs_app_set_unique_id (app, "a/b/c/d/e/f");
	gs_app_list_add (list, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_app_set_unique_id (app, "a/b/c/*/e/f");
	gs_app_list_add (list, app);
	g_object_unref (app);
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	g_assert_cmpstr (gs_app_get_id (gs_app_list_index (list, 0)), ==, "b");
	g_object_unref (list);

	/* lookup with a wildcard */
	list = gs_app_list_new ();
	app = gs_app_new ("b");
	gs_app_set_unique_id (app, "a/b/c/d/e/f");
	gs_app_list_add (list, app);
	g_object_unref (app);
	g_assert (gs_app_list_lookup (list, "a/b/c/d/e/f") != NULL);
	g_assert (gs_app_list_lookup (list, "a/b/c/d/e/*") != NULL);
	g_assert (gs_app_list_lookup (list, "*/b/c/d/e/f") != NULL);
	g_assert (gs_app_list_lookup (list, "x/x/x/x/x/x") == NULL);
	g_object_unref (list);

	/* allow duplicating a wildcard */
	list = gs_app_list_new ();
	app = gs_app_new ("gimp.desktop");
	gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
	gs_app_list_add (list, app);
	g_object_unref (app);
	app = gs_app_new ("gimp.desktop");
	gs_app_set_unique_id (app, "system/flatpak/*/*/gimp.desktop/stable");
	gs_app_list_add (list, app);
	g_object_unref (app);
	g_assert_cmpint (gs_app_list_length (list), ==, 2);
	g_object_unref (list);

	/* add a list to a list */
	list = gs_app_list_new ();
	list_dup = gs_app_list_new ();
	app = gs_app_new ("a");
	gs_app_list_add (list, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_app_list_add (list_dup, app);
	g_object_unref (app);
	gs_app_list_add_list (list, list_dup);
	g_assert_cmpint (gs_app_list_length (list), ==, 2);
	g_assert_cmpint (gs_app_list_length (list_dup), ==, 1);
	g_object_unref (list);
	g_object_unref (list_dup);

	/* remove apps from the list */
	list = gs_app_list_new ();
	app = gs_app_new ("a");
	gs_app_list_add (list, app);
	g_object_unref (app);
	app = gs_app_new ("a");
	gs_app_list_remove (list, app);
	g_object_unref (app);
	g_assert_cmpint (gs_app_list_length (list), ==, 0);
	g_object_unref (list);

	/* truncate list */
	list = gs_app_list_new ();
	app = gs_app_new ("a");
	gs_app_list_add (list, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_app_list_add (list, app);
	g_object_unref (app);
	app = gs_app_new ("c");
	gs_app_list_add (list, app);
	g_object_unref (app);
	g_assert (!gs_app_list_has_flag (list, GS_APP_LIST_FLAG_IS_TRUNCATED));
	g_assert_cmpint (gs_app_list_get_size_peak (list), ==, 3);
	gs_app_list_truncate (list, 3);
	g_assert_cmpint (gs_app_list_length (list), ==, 3);
	g_assert (gs_app_list_has_flag (list, GS_APP_LIST_FLAG_IS_TRUNCATED));
	g_assert_cmpint (gs_app_list_get_size_peak (list), ==, 3);
	gs_app_list_truncate (list, 2);
	g_assert_cmpint (gs_app_list_length (list), ==, 2);
	gs_app_list_truncate (list, 1);
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	gs_app_list_truncate (list, 0);
	g_assert_cmpint (gs_app_list_length (list), ==, 0);
	g_assert_cmpint (gs_app_list_get_size_peak (list), ==, 3);
	g_object_unref (list);
}

static gpointer
gs_app_thread_cb (gpointer data)
{
	GsApp *app = GS_APP (data);
	for (guint i = 0; i < 10000; i++) {
		g_assert_cmpstr (gs_app_get_unique_id (app), !=, NULL);
		gs_app_set_branch (app, "master");
		g_assert_cmpstr (gs_app_get_unique_id (app), !=, NULL);
		gs_app_set_branch (app, "stable");
	}
	return NULL;
}

static void
gs_app_thread_func (void)
{
	GThread *thread1;
	GThread *thread2;
	g_autoptr(GsApp) app = gs_app_new ("gimp.desktop");

	/* try really hard to cause a threading problem */
	g_setenv ("G_MESSAGES_DEBUG", "", TRUE);
	thread1 = g_thread_new ("thread1", gs_app_thread_cb, app);
	thread2 = g_thread_new ("thread2", gs_app_thread_cb, app);
	g_thread_join (thread1); /* consumes the reference  */
	g_thread_join (thread2);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
}

static void
gs_app_unique_id_func (void)
{
	g_autoptr(GsApp) app = gs_app_new (NULL);
	const gchar *unique_id;

	unique_id = "system/flatpak/gnome/desktop/org.gnome.Software.desktop/master";
	gs_app_set_from_unique_id (app, unique_id);
	g_assert (GS_IS_APP (app));
	g_assert_cmpint (gs_app_get_scope (app), ==, AS_APP_SCOPE_SYSTEM);
	g_assert_cmpint (gs_app_get_bundle_kind (app), ==, AS_BUNDLE_KIND_FLATPAK);
	g_assert_cmpstr (gs_app_get_origin (app), ==, "gnome");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.gnome.Software.desktop");
	g_assert_cmpstr (gs_app_get_branch (app), ==, "master");
}

static void
gs_app_addons_func (void)
{
	g_autoptr(GsApp) app = gs_app_new ("test.desktop");
	GsApp *addon;

	/* create, add then drop ref, so @app has the only refcount of addon */
	addon = gs_app_new ("test.desktop");
	gs_app_add_addon (app, addon);
	g_object_unref (addon);

	gs_app_remove_addon (app, addon);
}

static void
gs_app_func (void)
{
	g_autoptr(GsApp) app = NULL;

	app = gs_app_new ("gnome-software.desktop");
	g_assert (GS_IS_APP (app));
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-software.desktop");

	/* check we clean up the version, but not at the expense of having
	 * the same string as the update version */
	gs_app_set_version (app, "2.8.6-3.fc20");
	gs_app_set_update_version (app, "2.8.6-4.fc20");
	g_assert_cmpstr (gs_app_get_version (app), ==, "2.8.6-3.fc20");
	g_assert_cmpstr (gs_app_get_update_version (app), ==, "2.8.6-4.fc20");
	g_assert_cmpstr (gs_app_get_version_ui (app), ==, "2.8.6-3");
	g_assert_cmpstr (gs_app_get_update_version_ui (app), ==, "2.8.6-4");

	/* check the quality stuff works */
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "dave");
	g_assert_cmpstr (gs_app_get_name (app), ==, "dave");
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, "brian");
	g_assert_cmpstr (gs_app_get_name (app), ==, "dave");
	gs_app_set_name (app, GS_APP_QUALITY_HIGHEST, "hugh");
	g_assert_cmpstr (gs_app_get_name (app), ==, "hugh");

	/* check non-transient state saving */
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_REMOVING);
	gs_app_set_state_recover (app); // simulate an error
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);

	/* try again */
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_REMOVING);
	gs_app_set_state_recover (app); // simulate an error
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);

	/* correctly parse URL */
	gs_app_set_origin_hostname (app, "https://mirrors.fedoraproject.org/metalink");
	g_assert_cmpstr (gs_app_get_origin_hostname (app), ==, "fedoraproject.org");
	gs_app_set_origin_hostname (app, "file:///home/hughsie");
	g_assert_cmpstr (gs_app_get_origin_hostname (app), ==, "localhost");

	/* check setting the progress */
	gs_app_set_progress (app, 42);
	g_assert_cmpuint (gs_app_get_progress (app), ==, 42);
	gs_app_set_progress (app, 142);
	g_assert_cmpuint (gs_app_get_progress (app), ==, 100);

	/* check pending action */
	g_assert_cmpuint (gs_app_get_pending_action (app), ==, GS_PLUGIN_ACTION_UNKNOWN);
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
	gs_app_set_pending_action (app, GS_PLUGIN_ACTION_UPDATE);
	g_assert_cmpuint (gs_app_get_pending_action (app), ==, GS_PLUGIN_ACTION_UPDATE);
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	g_assert_cmpuint (gs_app_get_pending_action (app), ==, GS_PLUGIN_ACTION_UNKNOWN);
	gs_app_set_state_recover (app);
}

static void
gs_auth_secret_func (void)
{
	gboolean ret;
	g_autoptr(GDBusConnection) conn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAuth) auth1 = NULL;
	g_autoptr(GsAuth) auth2 = NULL;

	/* we not have an active session bus */
	conn = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (conn == NULL) {
		g_prefix_error (&error, "no session bus available: ");
		g_test_skip (error->message);
		return;
	}

	/* save secrets to disk */
	auth1 = gs_auth_new ("self-test");
	gs_auth_set_provider_schema (auth1, "org.gnome.Software.Dummy");
	gs_auth_set_username (auth1, "hughsie");
	gs_auth_set_password (auth1, "foobarbaz");
	gs_auth_add_metadata (auth1, "day", "monday");
	ret = gs_auth_store_save (auth1,
				  GS_AUTH_STORE_FLAG_USERNAME |
				  GS_AUTH_STORE_FLAG_PASSWORD |
				  GS_AUTH_STORE_FLAG_METADATA,
				  NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* load secrets from disk */
	auth2 = gs_auth_new ("self-test");
	gs_auth_add_metadata (auth2, "day", NULL);
	gs_auth_add_metadata (auth2, "notgoingtoexist", NULL);
	gs_auth_set_provider_schema (auth2, "org.gnome.Software.Dummy");
	ret = gs_auth_store_load (auth2,
				  GS_AUTH_STORE_FLAG_USERNAME |
				  GS_AUTH_STORE_FLAG_PASSWORD |
				  GS_AUTH_STORE_FLAG_METADATA,
				  NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpstr (gs_auth_get_username (auth2), ==, "hughsie");
	g_assert_cmpstr (gs_auth_get_password (auth2), ==, "foobarbaz");
	g_assert_cmpstr (gs_auth_get_metadata_item (auth2, "day"), ==, "monday");
}

static void
gs_app_list_func (void)
{
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GsApp) app1 = gs_app_new ("app1");
	g_autoptr(GsApp) app2 = gs_app_new ("app2");

	/* turn on */
	gs_app_list_add_flag (list, GS_APP_LIST_FLAG_WATCH_APPS);

	g_assert_cmpint (gs_app_list_get_progress (list), ==, 0);
	g_assert_cmpint (gs_app_list_get_state (list), ==, AS_APP_STATE_UNKNOWN);
	gs_app_list_add (list, app1);
	gs_app_set_progress (app1, 75);
	gs_app_set_state (app1, AS_APP_STATE_AVAILABLE);
	gs_app_set_state (app1, AS_APP_STATE_INSTALLING);
	gs_test_flush_main_context ();
	g_assert_cmpint (gs_app_list_get_progress (list), ==, 75);
	g_assert_cmpint (gs_app_list_get_state (list), ==, AS_APP_STATE_INSTALLING);

	gs_app_list_add (list, app2);
	gs_app_set_progress (app2, 25);
	gs_test_flush_main_context ();
	g_assert_cmpint (gs_app_list_get_progress (list), ==, 50);
	g_assert_cmpint (gs_app_list_get_state (list), ==, AS_APP_STATE_INSTALLING);

	gs_app_list_remove (list, app1);
	g_assert_cmpint (gs_app_list_get_progress (list), ==, 25);
	g_assert_cmpint (gs_app_list_get_state (list), ==, AS_APP_STATE_UNKNOWN);
}

static void
gs_app_list_related_func (void)
{
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GsApp) app = gs_app_new ("app");
	g_autoptr(GsApp) related = gs_app_new ("related");

	/* turn on */
	gs_app_list_add_flag (list,
			      GS_APP_LIST_FLAG_WATCH_APPS |
			      GS_APP_LIST_FLAG_WATCH_APPS_RELATED);
	gs_app_add_related (app, related);
	gs_app_list_add (list, app);

	gs_app_set_progress (app, 75);
	gs_app_set_progress (related, 25);
	gs_test_flush_main_context ();
	g_assert_cmpint (gs_app_list_get_progress (list), ==, 50);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/gnome-software/lib/utils{url}", gs_utils_url_func);
	g_test_add_func ("/gnome-software/lib/utils{wilson}", gs_utils_wilson_func);
	g_test_add_func ("/gnome-software/lib/utils{error}", gs_utils_error_func);
	g_test_add_func ("/gnome-software/lib/utils{cache}", gs_utils_cache_func);
	g_test_add_func ("/gnome-software/lib/utils{append-kv}", gs_utils_append_kv_func);
	g_test_add_func ("/gnome-software/lib/utils{parse-evr}", gs_utils_parse_evr_func);
	g_test_add_func ("/gnome-software/lib/os-release", gs_os_release_func);
	g_test_add_func ("/gnome-software/lib/app", gs_app_func);
	g_test_add_func ("/gnome-software/lib/app{addons}", gs_app_addons_func);
	g_test_add_func ("/gnome-software/lib/app{unique-id}", gs_app_unique_id_func);
	g_test_add_func ("/gnome-software/lib/app{thread}", gs_app_thread_func);
	g_test_add_func ("/gnome-software/lib/app{list}", gs_app_list_func);
	g_test_add_func ("/gnome-software/lib/app{list-related}", gs_app_list_related_func);
	g_test_add_func ("/gnome-software/lib/plugin", gs_plugin_func);
	g_test_add_func ("/gnome-software/lib/plugin{download-rewrite}", gs_plugin_download_rewrite_func);
	g_test_add_func ("/gnome-software/lib/auth{secret}", gs_auth_secret_func);

	return g_test_run ();
}

/* vim: set noexpandtab: */
