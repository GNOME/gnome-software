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
#include <stdlib.h>

#include "gs-app.h"
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

static gboolean
gs_plugin_list_filter_cb (GsApp *app, gpointer user_data)
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
	GList *list = NULL;
	GList *list_dup;
	GList *list_remove = NULL;
	GsApp *app;

	/* add a couple of duplicate IDs */
	app = gs_app_new ("a");
	gs_plugin_add_app (&list, app);
	g_object_unref (app);

	/* test refcounting */
	g_assert_cmpstr (gs_app_get_id (GS_APP (list->data)), ==, "a");
	list_dup = gs_plugin_list_copy (list);
	gs_plugin_list_free (list);
	g_assert_cmpint (g_list_length (list_dup), ==, 1);
	g_assert_cmpstr (gs_app_get_id (GS_APP (list_dup->data)), ==, "a");
	gs_plugin_list_free (list_dup);

	/* test removing obects */
	app = gs_app_new ("a");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("c");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	g_assert_cmpint (g_list_length (list_remove), ==, 3);
	gs_plugin_list_filter (&list_remove, gs_plugin_list_filter_cb, NULL);
	g_assert_cmpint (g_list_length (list_remove), ==, 1);
	g_assert_cmpstr (gs_app_get_id (GS_APP (list_remove->data)), ==, "b");

	/* test removing duplicates */
	app = gs_app_new ("b");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	gs_plugin_list_filter_duplicates (&list_remove);
	g_assert_cmpint (g_list_length (list_remove), ==, 1);
	g_assert_cmpstr (gs_app_get_id (GS_APP (list_remove->data)), ==, "b");
	gs_plugin_list_free (list_remove);
}

static void
gs_app_subsume_func (void)
{
	g_autoptr(GsApp) new = NULL;
	g_autoptr(GsApp) old = NULL;

	new = gs_app_new ("xxx.desktop");
	old = gs_app_new ("yyy.desktop");
	gs_app_set_metadata (old, "foo", "bar");
	gs_app_subsume (new, old);
	g_assert_cmpstr (gs_app_get_metadata_item (new, "foo"), ==, "bar");
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
			       "failed to call gs_plugin_app_update on dummy*");

	/* update, which should cause an error to be emitted */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_management_plugin (app, "dummy");
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	ret = gs_plugin_loader_app_action (plugin_loader, app,
					   GS_PLUGIN_LOADER_ACTION_UPDATE,
					   NULL,
					   &error);
//	g_assert_no_error (error);
//	g_assert (ret);

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

	g_assert_cmpstr (gs_app_get_license (app), ==,
			 "<a href=\"http://spdx.org/licenses/GPL-2.0+\">GPL-2.0+</a>");
	g_assert_cmpstr (gs_app_get_description (app), !=, NULL);
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://www.test.org/");
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
	g_assert_cmpint (g_list_length (list), ==, 2);
	app = g_list_nth_data (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "chiron.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE_LIVE);
	g_assert_cmpstr (gs_app_get_update_details (app), ==, "Do not crash when using libvirt.");
	g_assert_cmpint (gs_app_get_update_urgency (app), ==, AS_URGENCY_KIND_HIGH);

	/* get the virtual non-apps OS update */
	app = g_list_nth_data (list, 1);
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
	g_assert_cmpint (g_list_length (list), ==, 1);
	app = GS_APP (list->data);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.fedoraproject.release-24.upgrade");
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
					       GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
					       GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH |
					       GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE,
					       NULL,
					       &error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry */
	g_assert_cmpint (g_list_length (list), ==, 1);
	app = GS_APP (list->data);
	g_assert_cmpstr (gs_app_get_id (app), ==, "zeus.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpstr (gs_app_get_name (app), ==, "Zeus");
	g_assert_cmpstr (gs_app_get_source_default (app), ==, "zeus");
	g_assert (gs_app_get_pixbuf (app) != NULL);

	/* check various bitfields */
	g_assert (gs_app_has_quirk (app, AS_APP_QUIRK_PROVENANCE));
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
	g_assert_cmpint (g_list_length (list), ==, 1);
	app = GS_APP (list->data);
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
					   GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert (gs_app_get_pixbuf (app) != NULL);
}

int
main (int argc, char **argv)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *xml = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *whitelist[] = {
		"appstream",
		"dummy",
		"epiphany",
		"hardcoded-blacklist",
		"icons",
		"menu-spec-refine",
		"provenance",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* set all the things required as a dummy test harness */
	g_setenv ("GS_SELF_TEST_LOCALE", "en_GB", TRUE);
	g_setenv ("GS_SELF_TEST_DUMMY_ENABLE", "1", TRUE);
	g_setenv ("GS_SELF_TEST_PROVENANCE_SOURCES", "london*,boston", TRUE);

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
		"    <id>org.fedoraproject.release-24.upgrade</id>\n"
		"    <summary>Release specific tagline</summary>\n"
		"  </component>\n"
		"  <component type=\"webapp\">\n"
		"    <id>arachne.desktop</id>\n"
		"    <name>test</name>\n"
		"    <icon type=\"remote\">file://%s</icon>\n"
		"  </component>\n"
		"</components>\n", fn);
	g_setenv ("GS_SELF_TEST_APPSTREAM_XML", xml, TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* generic tests go here */
	g_test_add_func ("/gnome-software/app", gs_app_func);
	g_test_add_func ("/gnome-software/app{subsume}", gs_app_subsume_func);
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
