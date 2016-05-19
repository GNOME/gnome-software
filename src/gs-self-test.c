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
#include "gs-plugin.h"
#include "gs-plugin-loader.h"
#include "gs-plugin-loader-sync.h"
#include "gs-utils.h"

/**
 * gs_test_get_filename:
 **/
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

/**
 * gs_test_rmtree:
 **/
static gboolean
gs_test_rmtree (const gchar *directory, GError **error)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;

	/* try to open */
	dir = g_dir_open (directory, 0, error);
	if (dir == NULL)
		return FALSE;

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		g_autofree gchar *src = NULL;
		src = g_build_filename (directory, filename, NULL);
		if (g_file_test (src, G_FILE_TEST_IS_DIR) &&
		    !g_file_test (src, G_FILE_TEST_IS_SYMLINK)) {
			if (!gs_test_rmtree (src, error))
				return FALSE;
		} else {
			g_debug ("deleting %s", src);
			if (g_unlink (src) != 0) {
				g_set_error (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "Failed to delete: %s", src);
				return FALSE;
			}
		}
	}
	g_debug ("removing empty %s", directory);
	if (g_rmdir (directory) != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to remove: %s", directory);
		return FALSE;
	}
	return TRUE;
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

	/* test removing duplicates */
	app = gs_app_new ("b");
	gs_app_list_add (list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_app_list_add (list_remove, app);
	g_object_unref (app);
	gs_app_list_filter_duplicates (list_remove);
	g_assert_cmpint (gs_app_list_length (list_remove), ==, 1);
	g_assert_cmpstr (gs_app_get_id (gs_app_list_index (list_remove, 0)), ==, "b");
	g_object_unref (list_remove);
}

static void
gs_app_func (void)
{
	g_autoptr(GsApp) app = NULL;

	app = gs_app_new ("gnome-software");
	g_assert (GS_IS_APP (app));

	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-software");

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
	gboolean ret;
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
	g_assert_cmpint (gs_app_get_key_colors(app)->len, >=, 3);
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
	g_assert (gs_app_has_category (app, "Audio"));
	g_assert (gs_app_has_category (app, "Player"));
	g_assert (gs_app_has_category (app, "AudioVideo"));
	g_assert (!gs_app_has_category (app, "ImageProcessing"));
	g_assert (gs_app_get_menu_path (app) != NULL);
	menu_path = g_strjoinv ("->", gs_app_get_menu_path (app));
	g_assert_cmpstr (menu_path, ==, "Audio->Players");

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
					GS_PLUGIN_REFINE_FLAGS_DEFAULT,
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
gs_plugin_loader_webapps_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = NULL;

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
	g_assert_cmpstr (gs_app_get_id (app), ==, NULL);
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
	g_assert_cmpstr (gs_app_get_id (app), ==, NULL);
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
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://127.0.0.1/");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Chiron");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Single line synopsis");
	g_assert_cmpstr (gs_app_get_version (app), ==, "0.2");
	g_assert_cmpint (gs_app_get_size_download (app), ==, 32784);
	g_assert_cmpstr (gs_app_get_description (app), ==,
			 "This is the first paragraph in the example "
			 "cab file.\n\nThis is the second paragraph.");
	g_assert_cmpstr (gs_app_get_update_details (app), ==,
			 "Latest firmware release.");

	/* seems wrong, but this is only set if the update is available */
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UNKNOWN);
	g_assert_cmpstr (gs_app_get_id (app), ==, NULL);
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
	if (!gs_plugin_loader_get_enabled (plugin_loader, "flatpak"))
		return;

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
	gs_app_set_management_plugin (app_source, "flatpak");
	gs_app_set_state (app_source, AS_APP_STATE_AVAILABLE);
	gs_app_set_url (app_source, AS_URL_KIND_HOMEPAGE, testdir_repourl);
	ret = gs_plugin_loader_app_action (plugin_loader, app_source,
					   GS_PLUGIN_LOADER_ACTION_ADD_SOURCE,
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
					GS_PLUGIN_REFINE_FLAGS_DEFAULT,
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

int
main (int argc, char **argv)
{
	const gchar *tmp_root = "/var/tmp/self-test";
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *xml = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *whitelist[] = {
		"appstream",
		"dpkg",
		"dummy",
		"epiphany",
		"flatpak",
		"fwupd",
		"hardcoded-blacklist",
		"icons",
		"menu-spec-refine",
		"key-colors",
		"provenance",
		"provenance-license",
		"packagekit-local",
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
		ret = gs_test_rmtree (tmp_root, &error);
		g_assert_no_error (error);
		g_assert (ret);
		g_assert (!g_file_test (tmp_root, G_FILE_TEST_EXISTS));
	}

	fn = gs_test_get_filename ("icons/hicolor/48x48/org.gnome.Software.png");
	g_assert (fn != NULL);
	xml = g_strdup_printf ("<?xml version=\"1.0\"?>\n"
		"<components version=\"0.9\">\n"
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
	g_test_add_func ("/gnome-software/app", gs_app_func);
	g_test_add_func ("/gnome-software/plugin", gs_plugin_func);

	/* we can only load this once per process */
	plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_network_status (plugin_loader, TRUE);
	g_signal_connect (plugin_loader, "status-changed",
			  G_CALLBACK (gs_plugin_loader_status_changed_cb), NULL);
	gs_plugin_loader_set_location (plugin_loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (plugin_loader, (gchar**) whitelist, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (!gs_plugin_loader_get_enabled (plugin_loader, "notgoingtoexist"));
	g_assert (!gs_plugin_loader_get_enabled (plugin_loader, "packagekit"));
	g_assert (gs_plugin_loader_get_enabled (plugin_loader, "appstream"));
	g_assert (gs_plugin_loader_get_enabled (plugin_loader, "dummy"));

	/* plugin tests go here */
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
	return g_test_run ();
}

/* vim: set noexpandtab: */
