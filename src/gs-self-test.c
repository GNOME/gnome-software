/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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

#include <glib-object.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <fnmatch.h>

#include "gs-app-private.h"
#include "gs-app-list-private.h"
#include "gs-os-release.h"
#include "gs-plugin-private.h"
#include "gs-plugin-loader.h"
#include "gs-plugin-loader-sync.h"
#include "gs-utils.h"

static gchar *
gs_test_get_filename (const gchar *filename)
{
	gchar *tmp;
	char full_tmp[PATH_MAX];
	g_autofree gchar *path = NULL;
	path = g_build_filename (TESTDATADIR, filename, NULL);
	tmp = realpath (path, full_tmp);
	if (tmp == NULL)
		return NULL;
	return g_strdup (full_tmp);
}

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

	fn = gs_test_get_filename ("tests/os-release");
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
gs_plugin_error_func (void)
{
	guint i;
	for (i = 0; i < GS_PLUGIN_ERROR_LAST; i++)
		g_assert (gs_plugin_error_to_string (i) != NULL);
}

static void
gs_plugin_global_cache_func (void)
{
	const gchar *unique_id;
	g_autoptr(GsPlugin) plugin1 = NULL;
	g_autoptr(GsPlugin) plugin2 = NULL;
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GsApp) app = gs_app_new ("gimp.desktop");
	g_autoptr(GsApp) app1 = NULL;
	g_autoptr(GsApp) app2 = NULL;

	plugin1 = gs_plugin_new ();
	gs_plugin_set_global_cache (plugin1, list);

	plugin2 = gs_plugin_new ();
	gs_plugin_set_global_cache (plugin2, list);

	/* both plugins not opted into the global cache */
	unique_id = gs_app_get_unique_id (app);
	gs_plugin_cache_add (plugin1, unique_id, app);
	g_assert (gs_plugin_cache_lookup (plugin2, unique_id) == NULL);
	app1 = gs_plugin_cache_lookup (plugin1, unique_id);
	g_assert (app1 != NULL);

	/* one plugin opted in */
	gs_plugin_add_flags (plugin1, GS_PLUGIN_FLAGS_GLOBAL_CACHE);
	gs_plugin_cache_add (plugin1, unique_id, app);
	g_assert (gs_plugin_cache_lookup (plugin2, unique_id) == NULL);

	/* both plugins opted in */
	gs_plugin_add_flags (plugin2, GS_PLUGIN_FLAGS_GLOBAL_CACHE);
	gs_plugin_cache_add (plugin1, unique_id, app);
	app2 = gs_plugin_cache_lookup (plugin2, unique_id);
	g_assert (app2 != NULL);
}

static void
gs_plugin_func (void)
{
	GsAppList *list;
	GsAppList *list_dup;
	GsAppList *list_remove;
	GsApp *app;

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

	/* test removing obects */
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
	gs_app_list_filter_duplicates (list, GS_APP_LIST_FILTER_FLAG_PRIORITY);
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	g_assert_cmpstr (gs_app_get_unique_id (gs_app_list_index (list, 0)), ==, "user/bar/*/*/e/*");
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
}

static void
gs_app_unique_id_func (void)
{
	g_autoptr(GsApp) app = NULL;
	const gchar *unique_id;

	unique_id = "system/flatpak/gnome/desktop/org.gnome.Software.desktop/master";
	app = gs_app_new_from_unique_id (unique_id);
	g_assert (GS_IS_APP (app));
	g_assert_cmpint (gs_app_get_scope (app), ==, AS_APP_SCOPE_SYSTEM);
	g_assert_cmpint (gs_app_get_bundle_kind (app), ==, AS_BUNDLE_KIND_FLATPAK);
	g_assert_cmpstr (gs_app_get_origin (app), ==, "gnome");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.gnome.Software.desktop");
	g_assert_cmpstr (gs_app_get_branch (app), ==, "master");
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
}

static guint _status_changed_cnt = 0;

static void
gs_plugin_loader_status_changed_cb (GsPluginLoader *plugin_loader,
				    GsApp *app,
				    GsPluginStatus status,
				    gpointer user_data)
{
	_status_changed_cnt++;
}

static void
gs_plugin_loader_install_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;

	/* install */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_management_plugin (app, "dummy");
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	ret = gs_plugin_loader_app_action (plugin_loader, app,
					   GS_PLUGIN_LOADER_ACTION_INSTALL,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);

	/* remove -- we're really testing for return code UNKNOWN,
	 * but dummy::refine() sets it */
	ret = gs_plugin_loader_app_action (plugin_loader, app,
					   GS_PLUGIN_LOADER_ACTION_REMOVE,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
}

static void
gs_plugin_loader_error_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;
	GError *last_error;

	/* suppress this */
	g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			       "failed to call gs_plugin_update_app on dummy*");

	/* update, which should cause an error to be emitted */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_management_plugin (app, "dummy");
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	ret = gs_plugin_loader_app_action (plugin_loader, app,
					   GS_PLUGIN_LOADER_ACTION_UPDATE,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* ensure we failed the plugin action */
	g_test_assert_expected_messages ();

	/* retrieve the error from the application */
	last_error = gs_app_get_last_error (app);
	g_assert_error (last_error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NO_NETWORK);
}

static void
gs_plugin_loader_refine_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;

	/* get the extra bits */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_management_plugin (app, "dummy");
	ret = gs_plugin_loader_app_refine (plugin_loader, app,
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpstr (gs_app_get_license (app), ==, "GPL-2.0+");
	g_assert_cmpstr (gs_app_get_description (app), !=, NULL);
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://www.test.org/");
}

static void
gs_plugin_loader_key_colors_func (GsPluginLoader *plugin_loader)
{
	GPtrArray *array;
	gboolean ret;
	guint i;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;

	/* get the extra bits */
	app = gs_app_new ("zeus.desktop");
	ret = gs_plugin_loader_app_refine (plugin_loader, app,
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_KEY_COLORS,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	array = gs_app_get_key_colors (app);
	g_assert_cmpint (array->len, >=, 3);

	/* check values are in range */
	for (i = 0; i < array->len; i++) {
		GdkRGBA *kc = g_ptr_array_index (array, i);
		g_assert_cmpfloat (kc->red, >=, 0.f);
		g_assert_cmpfloat (kc->red, <=, 1.f);
		g_assert_cmpfloat (kc->green, >=, 0.f);
		g_assert_cmpfloat (kc->green, <=, 1.f);
		g_assert_cmpfloat (kc->blue, >=, 0.f);
		g_assert_cmpfloat (kc->blue, <=, 1.f);
		g_assert_cmpfloat (kc->alpha, >=, 0.f);
		g_assert_cmpfloat (kc->alpha, <=, 1.f);
	}
}

static void
gs_plugin_loader_updates_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get the updates list */
	list = gs_plugin_loader_get_updates (plugin_loader,
					     GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					     NULL,
					     &error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there are two entries */
	g_assert_cmpint (gs_app_list_length (list), ==, 2);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "chiron.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE_LIVE);
	g_assert_cmpstr (gs_app_get_update_details (app), ==, "Do not crash when using libvirt.");
	g_assert_cmpint (gs_app_get_update_urgency (app), ==, AS_URGENCY_KIND_HIGH);

	/* get the virtual non-apps OS update */
	app = gs_app_list_index (list, 1);
	g_assert_cmpstr (gs_app_get_id (app), ==, "os-update.virtual");
	g_assert_cmpstr (gs_app_get_name (app), ==, "OS Updates");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Includes performance, stability and security improvements.");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_OS_UPDATE);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE);
	g_assert_cmpint (gs_app_get_related(app)->len, ==, 2);
}

static void
gs_plugin_loader_distro_upgrades_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get the updates list */
	list = gs_plugin_loader_get_distro_upgrades (plugin_loader,
						     GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						     NULL,
						     &error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.fedoraproject.release-rawhide.upgrade");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_OS_UPGRADE);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);

	/* this should be set with a higher priority by AppStream */
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Release specific tagline");

	/* download the update */
	ret = gs_plugin_loader_app_action (plugin_loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_UPGRADE_DOWNLOAD,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE);

	/* trigger the update */
	ret = gs_plugin_loader_app_action (plugin_loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_UPGRADE_TRIGGER,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE);
}

static void
gs_plugin_loader_installed_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	GsApp *addon;
	GPtrArray *addons;
	guint64 kudos;
	g_autofree gchar *menu_path = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get installed packages */
	list = gs_plugin_loader_get_installed (plugin_loader,
					       GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN |
					       GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS |
					       GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
					       GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH |
					       GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
					       GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE,
					       NULL,
					       &error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "zeus.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_name (app), ==, "Zeus");
	g_assert_cmpstr (gs_app_get_source_default (app), ==, "zeus");
	g_assert (gs_app_get_pixbuf (app) != NULL);

	/* check various bitfields */
	g_assert (gs_app_has_quirk (app, AS_APP_QUIRK_PROVENANCE));
	g_assert_cmpstr (gs_app_get_license (app), ==, "GPL-2.0+");
	g_assert (gs_app_get_license_is_free (app));

	/* check kudos */
	kudos = gs_app_get_kudos (app);
	g_assert (kudos & GS_APP_KUDO_MY_LANGUAGE);

	/* check categories */
	g_assert (gs_app_has_category (app, "Player"));
	g_assert (gs_app_has_category (app, "AudioVideo"));
	g_assert (!gs_app_has_category (app, "ImageProcessing"));
	g_assert (gs_app_get_menu_path (app) != NULL);
	menu_path = g_strjoinv ("->", gs_app_get_menu_path (app));
	g_assert_cmpstr (menu_path, ==, "Audio & Video->Music Players");

	/* check addon */
	addons = gs_app_get_addons (app);
	g_assert_cmpint (addons->len, ==, 1);
	addon = g_ptr_array_index (addons, 0);
	g_assert_cmpstr (gs_app_get_id (addon), ==, "zeus-spell.addon");
	g_assert_cmpint (gs_app_get_kind (addon), ==, AS_APP_KIND_ADDON);
	g_assert_cmpint (gs_app_get_state (addon), ==, AS_APP_STATE_UNKNOWN);
	g_assert_cmpstr (gs_app_get_name (addon), ==, "Spell Check");
	g_assert_cmpstr (gs_app_get_source_default (addon), ==, "zeus-spell");
	g_assert_cmpstr (gs_app_get_license (addon), ==,
			 "LicenseRef-free=https://www.debian.org/");
	g_assert (gs_app_get_pixbuf (addon) == NULL);
}

static void
gs_plugin_loader_search_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	g_autofree gchar *menu_path = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get search result based on addon keyword */
	list = gs_plugin_loader_search (plugin_loader,
					"spell",
					GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					NULL,
					&error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the parent app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "zeus.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
}

static void
gs_plugin_loader_modalias_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	g_autofree gchar *menu_path = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get search result based on addon keyword */
	list = gs_plugin_loader_search (plugin_loader,
					"colorhug2",
					GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					NULL,
					&error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the parent app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "com.hughski.ColorHug2.driver");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DRIVER);
	g_assert (gs_app_has_category (app, "Addons"));
	g_assert (gs_app_has_category (app, "Drivers"));
}

static void
gs_plugin_loader_webapps_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = NULL;

	/* no epiphany, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "epiphany"))
		return;

	/* a webapp with a local icon */
	app = gs_app_new ("arachne.desktop");
	gs_app_set_kind (app, AS_APP_KIND_WEB_APP);
	ret = gs_plugin_loader_app_refine (plugin_loader, app,
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert (gs_app_get_pixbuf (app) != NULL);
}

static void
gs_plugin_loader_dpkg_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;

	/* no dpkg, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "dpkg"))
		return;

	/* load local file */
	fn = gs_test_get_filename ("tests/chiron-1.1-1.deb");
	g_assert (fn != NULL);
	file = g_file_new_for_path (fn);
	app = gs_plugin_loader_file_to_app (plugin_loader,
					    file,
					    GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					    NULL,
					    &error);
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpstr (gs_app_get_id (app), ==, "chiron.desktop");
	g_assert_cmpstr (gs_app_get_source_default (app), ==, "chiron");
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://127.0.0.1/");
	g_assert_cmpstr (gs_app_get_name (app), ==, "chiron");
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.1-1");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Single line synopsis");
	g_assert_cmpstr (gs_app_get_description (app), ==,
			 "This is the first paragraph in the example "
			 "package control file.\nThis is the second paragraph.");
	g_assert (gs_app_get_local_file (app) != NULL);
}

static void
gs_plugin_loader_packagekit_local_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;

	/* no dpkg, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "packagekit-local"))
		return;

	/* load local file */
	fn = gs_test_get_filename ("tests/chiron-1.1-1.fc24.x86_64.rpm");
	g_assert (fn != NULL);
	file = g_file_new_for_path (fn);
	app = gs_plugin_loader_file_to_app (plugin_loader,
					    file,
					    GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					    NULL,
					    &error);
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpstr (gs_app_get_id (app), ==, "chiron.desktop");
	g_assert_cmpstr (gs_app_get_source_default (app), ==, "chiron");
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://127.0.0.1/");
	g_assert_cmpstr (gs_app_get_name (app), ==, "chiron");
	g_assert_cmpstr (gs_app_get_version (app), ==, "1.1-1.fc24");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Single line synopsis");
	g_assert_cmpstr (gs_app_get_description (app), ==,
			 "This is the first paragraph in the example "
			 "package spec file.  This is the second paragraph.");
}

static void
gs_plugin_loader_fwupd_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;

	/* no dpkg, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "fwupd"))
		return;

	/* load local file */
	fn = gs_test_get_filename ("tests/chiron-0.2.cab");
	g_assert (fn != NULL);
	file = g_file_new_for_path (fn);
	app = gs_plugin_loader_file_to_app (plugin_loader,
					    file,
					    GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					    NULL,
					    &error);
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_FIRMWARE);
	g_assert (gs_app_get_pixbuf (app) != NULL);
	g_assert (gs_app_get_license (app) != NULL);
	g_assert (gs_app_has_category (app, "System"));
	g_assert_cmpstr (gs_app_get_id (app), ==, "com.test.chiron.firmware");
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://127.0.0.1/");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Chiron");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Single line synopsis");
	g_assert_cmpstr (gs_app_get_version (app), ==, "0.2");
	g_assert_cmpint ((gint64) gs_app_get_size_download (app), ==, 32784);
	g_assert_cmpstr (gs_app_get_description (app), ==,
			 "This is the first paragraph in the example "
			 "cab file.\n\nThis is the second paragraph.");
	g_assert_cmpstr (gs_app_get_update_details (app), ==,
			 "Latest firmware release.");

	/* seems wrong, but this is only set if the update is available */
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UNKNOWN);
}

static void
gs_plugin_loader_repos_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;

	/* get the extra bits */
	app = gs_app_new ("testrepos.desktop");
	gs_app_set_origin (app, "utopia");
	ret = gs_plugin_loader_app_refine (plugin_loader, app,
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpstr (gs_app_get_origin_hostname (app), ==, "people.freedesktop.org");
}

static void
gs_plugin_loader_flatpak_repo_func (GsPluginLoader *plugin_loader)
{
	const gchar *group_name = "remote \"example\"";
	const gchar *root = NULL;
	gboolean ret;
	g_autofree gchar *config_fn = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *remote_url = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GKeyFile) kf = NULL;
	g_autoptr(GsApp) app2 = NULL;
	g_autoptr(GsApp) app = NULL;

	/* no flatpak, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "flatpak-user"))
		return;

	/* load local file */
	fn = gs_test_get_filename ("tests/example.flatpakrepo");
	g_assert (fn != NULL);
	file = g_file_new_for_path (fn);
	app = gs_plugin_loader_file_to_app (plugin_loader,
					    file,
					    GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					    NULL,
					    &error);
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_SOURCE);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpstr (gs_app_get_id (app), ==, "example");
	g_assert_cmpstr (gs_app_get_management_plugin (app), ==, "flatpak-user");
	g_assert_cmpstr (gs_app_get_origin_hostname(app), ==, "foo.bar");
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://foo.bar");
	g_assert_cmpstr (gs_app_get_name (app), ==, "foo-bar");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Longer one line comment");
	g_assert_cmpstr (gs_app_get_description (app), ==,
			 "Longer multiline comment that does into detail.");
	g_assert (gs_app_get_local_file (app) != NULL);
	g_assert (gs_app_get_pixbuf (app) != NULL);

	/* now install the remote */
	ret = gs_plugin_loader_app_action (plugin_loader, app,
					   GS_PLUGIN_LOADER_ACTION_INSTALL,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);

	/* check config file was updated */
	root = g_getenv ("GS_SELF_TEST_FLATPACK_DATADIR");
	config_fn = g_build_filename (root, "flatpak", "repo", "config", NULL);
	kf = g_key_file_new ();
	ret = g_key_file_load_from_file (kf, config_fn, 0, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert (g_key_file_has_group (kf, "core"));
	g_assert (g_key_file_has_group (kf, group_name));
	g_assert (g_key_file_get_boolean (kf, group_name, "gpg-verify", NULL));

	/* check the URL was unmangled */
	remote_url = g_key_file_get_string (kf, group_name, "url", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (remote_url, ==, "http://foo.bar");

	/* try again, check state is correct */
	app2 = gs_plugin_loader_file_to_app (plugin_loader,
					     file,
					     GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					     NULL,
					     &error);
	g_assert_no_error (error);
	g_assert (app2 != NULL);
	g_assert_cmpint (gs_app_get_state (app2), ==, AS_APP_STATE_INSTALLED);

	/* remove it */
	ret = gs_plugin_loader_app_action (plugin_loader, app,
					   GS_PLUGIN_LOADER_ACTION_REMOVE,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
}

static void
gs_plugin_loader_flatpak_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
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
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsAppList) sources = NULL;

	/* no flatpak, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "flatpak-user"))
		return;

	/* no files to use */
	repodir_fn = gs_test_get_filename ("tests/flatpak/repo");
	if (!g_file_test (repodir_fn, G_FILE_TEST_EXISTS)) {
		g_test_skip ("no flatpak test repo");
		return;
	}

	/* check changed file exists */
	root = g_getenv ("GS_SELF_TEST_FLATPACK_DATADIR");
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
	app_source = gs_app_new ("test");
	testdir = gs_test_get_filename ("tests/flatpak");
	if (testdir == NULL)
		return;
	testdir_repourl = g_strdup_printf ("file://%s/repo", testdir);
	gs_app_set_kind (app_source, AS_APP_KIND_SOURCE);
	gs_app_set_management_plugin (app_source, "flatpak-user");
	gs_app_set_state (app_source, AS_APP_STATE_AVAILABLE);
	gs_app_set_url (app_source, AS_URL_KIND_HOMEPAGE, testdir_repourl);
	ret = gs_plugin_loader_app_action (plugin_loader, app_source,
					   GS_PLUGIN_LOADER_ACTION_INSTALL,
					   NULL,
					   &error);
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
	sources = gs_plugin_loader_get_sources (plugin_loader,
						GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						NULL,
						&error);
	g_assert_no_error (error);
	g_assert (sources != NULL);
	g_assert_cmpint (gs_app_list_length (sources), ==, 1);
	app = gs_app_list_index (sources, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "test");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_SOURCE);

	/* refresh the appstream metadata */
	ret = gs_plugin_loader_refresh (plugin_loader,
					G_MAXUINT,
					GS_PLUGIN_REFRESH_FLAGS_METADATA,
					NULL,
					&error);
	g_assert_no_error (error);
	g_assert (ret);

	/* find available application */
	list = gs_plugin_loader_search (plugin_loader,
					"Bingo",
					GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME |
					GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS |
					GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					NULL,
					&error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the flatpak app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.test.Chiron.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpint ((gint64) gs_app_get_kudos (app), ==,
			 GS_APP_KUDO_HAS_KEYWORDS |
			 GS_APP_KUDO_SANDBOXED_SECURE |
			 GS_APP_KUDO_SANDBOXED);
	g_assert_cmpstr (gs_app_get_origin_hostname (app), ==, "");

	/* install, also installing runtime */
	ret = gs_plugin_loader_app_action (plugin_loader, app,
					   GS_PLUGIN_LOADER_ACTION_INSTALL,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);

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
	ret = gs_plugin_loader_app_action (plugin_loader, app,
					   GS_PLUGIN_LOADER_ACTION_REMOVE,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert (!g_file_test (metadata_fn, G_FILE_TEST_IS_REGULAR));
	g_assert (!g_file_test (desktop_fn, G_FILE_TEST_IS_REGULAR));
}

static void
gs_plugin_loader_plugin_cache_func (GsPluginLoader *plugin_loader)
{
	GsApp *app1;
	GsApp *app2;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list1 = NULL;
	g_autoptr(GsAppList) list2 = NULL;

	/* ensure we get the same results back from calling the methods twice */
	list1 = gs_plugin_loader_get_distro_upgrades (plugin_loader,
						      GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						      NULL,
						      &error);
	g_assert_no_error (error);
	g_assert (list1 != NULL);
	g_assert_cmpint (gs_app_list_length (list1), ==, 1);
	app1 = gs_app_list_index (list1, 0);

	list2 = gs_plugin_loader_get_distro_upgrades (plugin_loader,
						      GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						      NULL,
						      &error);
	g_assert_no_error (error);
	g_assert (list2 != NULL);
	g_assert_cmpint (gs_app_list_length (list2), ==, 1);
	app2 = gs_app_list_index (list2, 0);

	/* make sure there is one GObject */
	g_assert_cmpstr (gs_app_get_id (app1), ==, gs_app_get_id (app2));
	g_assert (app1 == app2);
}

static void
gs_plugin_loader_authentication_func (GsPluginLoader *plugin_loader)
{
	GsAuth *auth;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(AsReview) review = NULL;
	g_autoptr(AsReview) review2 = NULL;

	/* check initial state */
	auth = gs_plugin_loader_get_auth_by_id (plugin_loader, "dummy");
	g_assert (GS_IS_AUTH (auth));
	g_assert_cmpint (gs_auth_get_flags (auth), ==, 0);

	/* do an action that returns a URL */
	ret = gs_plugin_loader_auth_action (plugin_loader, auth,
					    GS_AUTH_ACTION_REGISTER,
					    NULL, &error);
	g_assert_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_AUTH_INVALID);
	g_assert (!ret);
	g_clear_error (&error);
	g_assert (!gs_auth_has_flag (auth, GS_AUTH_FLAG_VALID));

	/* do an action that requires a login */
	app = gs_app_new (NULL);
	review = as_review_new ();
	ret = gs_plugin_loader_review_action (plugin_loader, app, review,
					      GS_PLUGIN_REVIEW_ACTION_REMOVE,
					      NULL, &error);
	g_assert_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_AUTH_REQUIRED);
	g_assert (!ret);
	g_clear_error (&error);

	/* pretend to auth with no credentials */
	ret = gs_plugin_loader_auth_action (plugin_loader, auth,
					    GS_AUTH_ACTION_LOGIN,
					    NULL, &error);
	g_assert_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_AUTH_INVALID);
	g_assert (!ret);
	g_clear_error (&error);
	g_assert (!gs_auth_has_flag (auth, GS_AUTH_FLAG_VALID));

	/* auth again with correct credentials */
	gs_auth_set_username (auth, "dummy");
	gs_auth_set_password (auth, "dummy");
	ret = gs_plugin_loader_auth_action (plugin_loader, auth,
					    GS_AUTH_ACTION_LOGIN,
					    NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (gs_auth_has_flag (auth, GS_AUTH_FLAG_VALID));

	/* do the action that requires a login */
	review2 = as_review_new ();
	ret = gs_plugin_loader_review_action (plugin_loader, app, review2,
					      GS_PLUGIN_REVIEW_ACTION_REMOVE,
					      NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
gs_auth_secret_func (void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAuth) auth1 = NULL;
	g_autoptr(GsAuth) auth2 = NULL;

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
gs_plugin_loader_wildcard_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	list = gs_plugin_loader_get_popular (plugin_loader,
					     GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					     NULL,
					     &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
}

int
main (int argc, char **argv)
{
	const gchar *tmp_root = "/var/tmp/self-test";
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *xml = NULL;
	g_autofree gchar *reposdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *whitelist[] = {
		"appstream",
		"dpkg",
		"dummy",
		"epiphany",
		"flatpak-user",
		"fwupd",
		"hardcoded-blacklist",
		"desktop-categories",
		"desktop-menu-path",
		"icons",
		"key-colors",
		"modalias",
		"provenance",
		"provenance-license",
		"packagekit-local",
		"repos",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* set all the things required as a dummy test harness */
	g_setenv ("GS_SELF_TEST_LOCALE", "en_GB", TRUE);
	g_setenv ("GS_SELF_TEST_DUMMY_ENABLE", "1", TRUE);
	g_setenv ("GS_SELF_TEST_PROVENANCE_SOURCES", "london*,boston", TRUE);
	g_setenv ("GS_SELF_TEST_PROVENANCE_LICENSE_SOURCES", "london*,boston", TRUE);
	g_setenv ("GS_SELF_TEST_PROVENANCE_LICENSE_URL", "https://www.debian.org/", TRUE);
	g_setenv ("GS_SELF_TEST_FLATPACK_DATADIR", tmp_root, TRUE);

	/* ensure test root does not exist */
	if (g_file_test (tmp_root, G_FILE_TEST_EXISTS)) {
		ret = gs_utils_rmtree (tmp_root, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert (!g_file_test (tmp_root, G_FILE_TEST_EXISTS));
	}

	/* dummy data */
	reposdir = gs_test_get_filename ("tests/yum.repos.d");
	g_assert (reposdir != NULL);
	g_setenv ("GS_SELF_TEST_REPOS_DIR", reposdir, TRUE);

	fn = gs_test_get_filename ("icons/hicolor/48x48/org.gnome.Software.png");
	g_assert (fn != NULL);
	xml = g_strdup_printf ("<?xml version=\"1.0\"?>\n"
		"<components version=\"0.9\">\n"
		"  <component type=\"driver\">\n"
		"    <id>com.hughski.ColorHug2.driver</id>\n"
		"    <name>ColorHug2</name>\n"
		"    <summary>ColorHug2 Colorimeter Driver</summary>\n"
		"    <provides>\n"
		"      <modalias>pci:*</modalias>\n"
		"    </provides>\n"
		"  </component>\n"
		"  <component type=\"desktop\">\n"
		"    <id>chiron.desktop</id>\n"
		"    <pkgname>chiron</pkgname>\n"
		"  </component>\n"
		"  <component type=\"desktop\">\n"
		"    <id>zeus.desktop</id>\n"
		"    <name>Zeus</name>\n"
		"    <summary>A teaching application</summary>\n"
		"    <pkgname>zeus</pkgname>\n"
		"    <icon type=\"stock\">drive-harddisk</icon>\n"
		"    <categories>\n"
		"      <category>AudioVideo</category>\n"
		"      <category>Player</category>\n"
		"    </categories>\n"
		"    <languages>\n"
		"      <lang percentage=\"100\">en_GB</lang>\n"
		"    </languages>\n"
		"  </component>\n"
		"  <component type=\"desktop\">\n"
		"    <id>mate-spell.desktop</id>\n"
		"    <name>Spell</name>\n"
		"    <summary>A spelling application for MATE</summary>\n"
		"    <pkgname>mate-spell</pkgname>\n"
		"    <icon type=\"stock\">drive-harddisk</icon>\n"
		"    <project_group>MATE</project_group>\n"
		"  </component>\n"
		"  <component type=\"addon\">\n"
		"    <id>zeus-spell.addon</id>\n"
		"    <extends>zeus.desktop</extends>\n"
		"    <name>Spell Check</name>\n"
		"    <summary>Check the spelling when teaching</summary>\n"
		"    <pkgname>zeus-spell</pkgname>\n"
		"  </component>\n"
		"  <component type=\"desktop\">\n"
		"    <id>Uninstall Zeus.desktop</id>\n"
		"    <name>Uninstall Zeus</name>\n"
		"    <summary>Uninstall the teaching application</summary>\n"
		"    <icon type=\"stock\">drive-harddisk</icon>\n"
		"  </component>\n"
		"  <component type=\"os-upgrade\">\n"
		"    <id>org.fedoraproject.release-rawhide.upgrade</id>\n"
		"    <summary>Release specific tagline</summary>\n"
		"  </component>\n"
		"  <component type=\"webapp\">\n"
		"    <id>arachne.desktop</id>\n"
		"    <name>test</name>\n"
		"    <icon type=\"remote\">file://%s</icon>\n"
		"  </component>\n"
		"  <component type=\"desktop\">\n"
		"    <id>org.test.Chiron.desktop</id>\n"
		"    <name>Chiron</name>\n"
		"    <summary>Single line synopsis</summary>\n"
		"    <description><p>Long description.</p></description>\n"
		"    <icon height=\"128\" width=\"128\" type=\"cached\">128x128/org.test.Chiron.png</icon>\n"
		"    <icon height=\"64\" width=\"64\" type=\"cached\">64x64/org.test.Chiron.png</icon>\n"
		"    <keywords>\n"
		"      <keyword>Bingo</keyword>\n"
		"    </keywords>\n"
		"    <metadata>\n"
		"      <value key=\"GnomeSoftware::Plugin\">flatpak-user</value>\n"
		"    </metadata>\n"
		"    <project_license>GPL-2.0+</project_license>\n"
		"    <url type=\"homepage\">http://127.0.0.1/</url>\n"
		"    <bundle type=\"flatpak\" runtime=\"org.test.Runtime/x86_64/master\">app/org.test.Chiron/x86_64/master</bundle>\n"
		"  </component>\n"
		"</components>\n", fn);
	g_setenv ("GS_SELF_TEST_APPSTREAM_XML", xml, TRUE);
	g_setenv ("GS_SELF_TEST_APPSTREAM_ICON_ROOT",
		  "/var/tmp/self-test/flatpak/appstream/test/x86_64/active/", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* generic tests go here */
	g_test_add_func ("/gnome-software/utils{wilson}", gs_utils_wilson_func);
	g_test_add_func ("/gnome-software/os-release", gs_os_release_func);
	g_test_add_func ("/gnome-software/app", gs_app_func);
	g_test_add_func ("/gnome-software/app{unique-id}", gs_app_unique_id_func);
	g_test_add_func ("/gnome-software/plugin", gs_plugin_func);
	g_test_add_func ("/gnome-software/plugin{error}", gs_plugin_error_func);
	g_test_add_func ("/gnome-software/plugin{global-cache}", gs_plugin_global_cache_func);
	g_test_add_func ("/gnome-software/auth{secret}", gs_auth_secret_func);

	/* we can only load this once per process */
	plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_network_status (plugin_loader, TRUE);
	g_signal_connect (plugin_loader, "status-changed",
			  G_CALLBACK (gs_plugin_loader_status_changed_cb), NULL);
	gs_plugin_loader_set_location (plugin_loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (plugin_loader, (gchar**) whitelist, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (!gs_plugin_loader_get_enabled (plugin_loader, "notgoingtoexist"));
	g_assert (!gs_plugin_loader_get_enabled (plugin_loader, "packagekit"));
	g_assert (gs_plugin_loader_get_enabled (plugin_loader, "appstream"));
	g_assert (gs_plugin_loader_get_enabled (plugin_loader, "dummy"));

	/* plugin tests go here */
	g_test_add_data_func ("/gnome-software/plugin-loader{wildcard}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_wildcard_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{authentication}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_authentication_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{plugin-cache}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_plugin_cache_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{repos}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_repos_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{flatpak}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_flatpak_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{fwupd}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_fwupd_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{key-colors}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_key_colors_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{packagekit-local}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_packagekit_local_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{dpkg}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_dpkg_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{webapps}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_webapps_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{modalias}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_modalias_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{search}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_search_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{install}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_install_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{error}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_error_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{installed}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_installed_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{refine}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_refine_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{updates}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_updates_func);
	g_test_add_data_func ("/gnome-software/plugin-loader{distro-upgrades}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_distro_upgrades_func);

	/* done last as it would otherwise try to do downloading in other
	 * gs_plugin_file_to_app()-using tests */
	g_test_add_data_func ("/gnome-software/plugin-loader{flatpak:repo}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugin_loader_flatpak_repo_func);
	return g_test_run ();
}

/* vim: set noexpandtab: */
