/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gstdio.h>

#include "gnome-software-private.h"

#include "gs-test.h"

static guint _status_changed_cnt = 0;

typedef struct {
	GError *error;
	GMainLoop *loop;
} GsDummyTestHelper;

static GsDummyTestHelper *
gs_dummy_test_helper_new (void)
{
        return g_new0 (GsDummyTestHelper, 1);
}

static void
gs_dummy_test_helper_free (GsDummyTestHelper *helper)
{
	if (helper->error != NULL)
		g_error_free (helper->error);
	if (helper->loop != NULL)
		g_main_loop_unref (helper->loop);
	g_free (helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsDummyTestHelper, gs_dummy_test_helper_free)

static void
gs_plugin_loader_status_changed_cb (GsPluginLoader *plugin_loader,
				    GsApp *app,
				    GsPluginStatus status,
				    gpointer user_data)
{
	_status_changed_cnt++;
}

static void
gs_plugins_dummy_install_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GError) error = NULL;

	/* install */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_management_plugin (app, "dummy");
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);

	/* remove */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
}

static void
gs_plugins_dummy_error_func (GsPluginLoader *plugin_loader)
{
	const GError *app_error;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) events = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginEvent) event = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* drop all caches */
	gs_utils_rmtree (g_getenv ("GS_SELF_TEST_CACHEDIR"), NULL);
	gs_plugin_loader_setup_again (plugin_loader);

	/* update, which should cause an error to be emitted */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_management_plugin (app, "dummy");
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPDATE,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	/* get last active event */
	event = gs_plugin_loader_get_event_default (plugin_loader);
	g_assert (event != NULL);
	g_assert (gs_plugin_event_get_app (event) == app);

	/* check all the events */
	events = gs_plugin_loader_get_events (plugin_loader);
	g_assert_cmpint (events->len, ==, 1);
	event = g_ptr_array_index (events, 0);
	g_assert (gs_plugin_event_get_app (event) == app);
	app_error = gs_plugin_event_get_error (event);
	g_assert (app_error != NULL);
	g_assert_error (app_error,
			GS_PLUGIN_ERROR,
			GS_PLUGIN_ERROR_DOWNLOAD_FAILED);
}

static void
gs_plugins_dummy_refine_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get the extra bits */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_management_plugin (app, "dummy");
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpstr (gs_app_get_license (app), ==, "GPL-2.0+");
	g_assert_cmpstr (gs_app_get_description (app), !=, NULL);
	g_assert_cmpstr (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE), ==, "http://www.test.org/");
}

static void
gs_plugins_dummy_metadata_quirks (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get the extra bits */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_management_plugin (app, "dummy");
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpstr (gs_app_get_description (app), !=, NULL);

	/* check the not-launchable quirk */

	g_assert (!gs_app_has_quirk(app, GS_APP_QUIRK_NOT_LAUNCHABLE));

	gs_app_set_metadata (app, "GnomeSoftware::quirks::not-launchable", "true");

	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	g_assert (gs_app_has_quirk(app, GS_APP_QUIRK_NOT_LAUNCHABLE));

	gs_app_set_metadata (app, "GnomeSoftware::quirks::not-launchable", NULL);
	gs_app_set_metadata (app, "GnomeSoftware::quirks::not-launchable", "false");

	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);

	g_assert (!gs_app_has_quirk(app, GS_APP_QUIRK_NOT_LAUNCHABLE));
}

static void
gs_plugins_dummy_key_colors_func (GsPluginLoader *plugin_loader)
{
	GPtrArray *array;
	gboolean ret;
	guint i;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GError) error = NULL;

	/* get the extra bits */
	app = gs_app_new ("zeus.desktop");
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_KEY_COLORS,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
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
gs_plugins_dummy_updates_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get the updates list */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_UPDATES,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there are three entries */
	g_assert_cmpint (gs_app_list_length (list), ==, 3);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "chiron.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE_LIVE);
	g_assert_cmpstr (gs_app_get_update_details (app), ==, "Do not crash when using libvirt.");
	g_assert_cmpint (gs_app_get_update_urgency (app), ==, AS_URGENCY_KIND_HIGH);

	/* get the virtual non-apps OS update */
	app = gs_app_list_index (list, 2);
	g_assert_cmpstr (gs_app_get_id (app), ==, "org.gnome.Software.OsUpdate");
	g_assert_cmpstr (gs_app_get_name (app), ==, "OS Updates");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Includes performance, stability and security improvements.");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_OS_UPDATE);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE);
	g_assert_cmpint (gs_app_list_length (gs_app_get_related (app)), ==, 2);

	/* get the virtual non-apps OS update */
	app = gs_app_list_index (list, 1);
	g_assert_cmpstr (gs_app_get_id (app), ==, "proxy.desktop");
	g_assert (gs_app_has_quirk (app, GS_APP_QUIRK_IS_PROXY));
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE_LIVE);
	g_assert_cmpint (gs_app_list_length (gs_app_get_related (app)), ==, 2);
}

static void
gs_plugins_dummy_distro_upgrades_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get the updates list */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_DISTRO_UPDATES, NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
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
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE);

	/* trigger the update */
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPGRADE_TRIGGER,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UPDATABLE);
}

static void
gs_plugins_dummy_installed_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	GsApp *addon;
	GsAppList *addons;
	g_autofree gchar *menu_path = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get installed packages */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_INSTALLED,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_CATEGORIES |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
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
	g_assert (gs_app_has_quirk (app, GS_APP_QUIRK_PROVENANCE));
	g_assert_cmpstr (gs_app_get_license (app), ==, "GPL-2.0+");
	g_assert (gs_app_get_license_is_free (app));

	/* check kudos */
	g_assert_true (gs_app_has_kudo (app, GS_APP_KUDO_MY_LANGUAGE));

	/* check categories */
	g_assert (gs_app_has_category (app, "Player"));
	g_assert (gs_app_has_category (app, "AudioVideo"));
	g_assert (!gs_app_has_category (app, "ImageProcessing"));
	g_assert (gs_app_get_menu_path (app) != NULL);
	menu_path = g_strjoinv ("->", gs_app_get_menu_path (app));
	g_assert_cmpstr (menu_path, ==, "Audio & Video->Music Players");

	/* check addon */
	addons = gs_app_get_addons (app);
	g_assert_cmpint (gs_app_list_length (addons), ==, 1);
	addon = gs_app_list_index (addons, 0);
	g_assert_cmpstr (gs_app_get_id (addon), ==, "zeus-spell.addon");
	g_assert_cmpint (gs_app_get_kind (addon), ==, AS_APP_KIND_ADDON);
	g_assert_cmpint (gs_app_get_state (addon), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpstr (gs_app_get_name (addon), ==, "Spell Check");
	g_assert_cmpstr (gs_app_get_source_default (addon), ==, "zeus-spell");
	g_assert_cmpstr (gs_app_get_license (addon), ==,
			 "LicenseRef-free=https://www.debian.org/");
	g_assert (gs_app_get_pixbuf (addon) == NULL);
}

static void
gs_plugins_dummy_search_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get search result based on addon keyword */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "zeus",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the parent app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "zeus.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
}

static void
gs_plugins_dummy_search_alternate_func (GsPluginLoader *plugin_loader)
{
	GsApp *app_tmp;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get search result based on addon keyword */
	app = gs_app_new ("zeus.desktop");
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_ALTERNATES,
					 "app", app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is the original app, and the alternate */
	g_assert_cmpint (gs_app_list_length (list), ==, 2);
	app_tmp = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app_tmp), ==, "chiron.desktop");
	g_assert_cmpint (gs_app_get_kind (app_tmp), ==, AS_APP_KIND_DESKTOP);
	app_tmp = gs_app_list_index (list, 1);
	g_assert_cmpstr (gs_app_get_id (app_tmp), ==, "zeus.desktop");
	g_assert_cmpint (gs_app_get_kind (app_tmp), ==, AS_APP_KIND_DESKTOP);
}

static void
gs_plugins_dummy_hang_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* drop all caches */
	gs_utils_rmtree (g_getenv ("GS_SELF_TEST_CACHEDIR"), NULL);
	gs_plugin_loader_setup_again (plugin_loader);

	/* get search result based on addon keyword */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "hang",
					 "timeout", 1, /* seconds */
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, cancellable, &error);
	gs_test_flush_main_context ();
	g_assert_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_TIMED_OUT);
	g_assert (list == NULL);
}

static void
gs_plugins_dummy_search_invalid_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get search result based on addon keyword */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "X",
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NOT_SUPPORTED);
	g_assert (list == NULL);
}

static void
gs_plugins_dummy_url_to_app_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_URL_TO_APP,
					 "search", "dummy://chiron.desktop",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	app = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpstr (gs_app_get_id (app), ==, "chiron.desktop");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DESKTOP);
}

static void
gs_plugins_dummy_plugin_cache_func (GsPluginLoader *plugin_loader)
{
	GsApp *app1;
	GsApp *app2;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list1 = NULL;
	g_autoptr(GsAppList) list2 = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* ensure we get the same results back from calling the methods twice */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_DISTRO_UPDATES, NULL);
	list1 = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list1 != NULL);
	g_assert_cmpint (gs_app_list_length (list1), ==, 1);
	app1 = gs_app_list_index (list1, 0);

	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_DISTRO_UPDATES, NULL);
	list2 = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list2 != NULL);
	g_assert_cmpint (gs_app_list_length (list2), ==, 1);
	app2 = gs_app_list_index (list2, 0);

	/* make sure there is one GObject */
	g_assert_cmpstr (gs_app_get_id (app1), ==, gs_app_get_id (app2));
	g_assert (app1 == app2);
}

static void
gs_plugins_dummy_wildcard_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list1 = NULL;
	g_autoptr(GsAppList) list2 = NULL;
	const gchar *popular_override = "chiron.desktop,zeus.desktop";
	g_auto(GStrv) apps = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* use the plugin's add_popular function */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_POPULAR,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list1 = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list1 != NULL);
	g_assert_cmpint (gs_app_list_length (list1), ==, 1);

	/* override the popular list (do not use the add_popular function) */
	g_setenv ("GNOME_SOFTWARE_POPULAR", popular_override, TRUE);
	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_POPULAR,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list2 = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list2 != NULL);

	apps = g_strsplit (popular_override, ",", 0);
	g_assert_cmpint (gs_app_list_length (list2), ==, g_strv_length (apps));

	for (guint i = 0; i < gs_app_list_length (list2); ++i) {
		GsApp *app = gs_app_list_index (list2, i);
		g_assert (g_strv_contains ((const gchar * const *) apps, gs_app_get_id (app)));
	}
}

static void
plugin_job_action_cb (GObject *source,
		      GAsyncResult *res,
		      gpointer user_data)
{
      GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
      GsDummyTestHelper *helper = (GsDummyTestHelper *) user_data;

      gs_plugin_loader_job_action_finish (plugin_loader, res, &helper->error);
      if (helper->loop != NULL)
              g_main_loop_quit (helper->loop);
}

static void
gs_plugins_dummy_limit_parallel_ops_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GsAppList) list = NULL;
        GsApp *app1 = NULL;
	g_autoptr(GsApp) app2 = NULL;
	g_autoptr(GsApp) app3 = NULL;
	g_autoptr(GsPluginJob) plugin_job1 = NULL;
	g_autoptr(GsPluginJob) plugin_job2 = NULL;
	g_autoptr(GsPluginJob) plugin_job3 = NULL;
	g_autoptr(GMainContext) context = NULL;
	g_autoptr(GsDummyTestHelper) helper1 = gs_dummy_test_helper_new ();
	g_autoptr(GsDummyTestHelper) helper2 = gs_dummy_test_helper_new ();
	g_autoptr(GsDummyTestHelper) helper3 = gs_dummy_test_helper_new ();

	/* drop all caches */
	gs_utils_rmtree (g_getenv ("GS_SELF_TEST_CACHEDIR"), NULL);
	gs_plugin_loader_setup_again (plugin_loader);

	/* get the updates list */
	plugin_job1 = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_DISTRO_UPDATES, NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job1, NULL, &helper3->error);
	gs_test_flush_main_context ();
	g_assert_no_error (helper3->error);
	g_assert (list != NULL);
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app1 = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app1), ==, "org.fedoraproject.release-rawhide.upgrade");
	g_assert_cmpint (gs_app_get_kind (app1), ==, AS_APP_KIND_OS_UPGRADE);
	g_assert_cmpint (gs_app_get_state (app1), ==, AS_APP_STATE_AVAILABLE);

	/* allow only one operation at a time */
	gs_plugin_loader_set_max_parallel_ops (plugin_loader, 1);

	app2 = gs_app_new ("chiron.desktop");
	gs_app_set_management_plugin (app2, "dummy");
	gs_app_set_state (app2, AS_APP_STATE_AVAILABLE);

	/* use "proxy" prefix so the update function succeeds... */
	app3 = gs_app_new ("proxy-zeus.desktop");
	gs_app_set_management_plugin (app3, "dummy");
	gs_app_set_state (app3, AS_APP_STATE_UPDATABLE_LIVE);

	context = g_main_context_new ();
	helper3->loop = g_main_loop_new (context, FALSE);
	g_main_context_push_thread_default (context);

	/* call a few operations at the "same time" */

	/* download an upgrade */
	g_object_unref (plugin_job1);
	plugin_job1 = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD,
					  "app", app1,
					  NULL);
	gs_plugin_loader_job_process_async (plugin_loader,
					    plugin_job1,
					    NULL,
					    plugin_job_action_cb,
					    helper1);

	/* install an app */
	plugin_job2 = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					  "app", app2,
					  NULL);
	gs_plugin_loader_job_process_async (plugin_loader,
					    plugin_job2,
					    NULL,
					    plugin_job_action_cb,
					    helper2);

	/* update an app */
	plugin_job3 = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPDATE,
					  "app", app3,
					  NULL);
	gs_plugin_loader_job_process_async (plugin_loader,
					    plugin_job3,
					    NULL,
					    plugin_job_action_cb,
					    helper3);

	/* since we have only 1 parallel installation op possible,
	 * verify the last operations are pending */
	g_assert_cmpint (gs_app_get_state (app2), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpint (gs_app_get_pending_action (app2), ==, GS_PLUGIN_ACTION_INSTALL);
	g_assert_cmpint (gs_app_get_state (app3), ==, AS_APP_STATE_UPDATABLE_LIVE);
	g_assert_cmpint (gs_app_get_pending_action (app3), ==, GS_PLUGIN_ACTION_UPDATE);

	/* wait for the 2nd installation to finish, it means the 1st should have been
	 * finished too */
	g_main_loop_run (helper3->loop);
	g_main_context_pop_thread_default (context);

	gs_test_flush_main_context ();
	g_assert_no_error (helper1->error);
	g_assert_no_error (helper2->error);
	g_assert_no_error (helper3->error);

	g_assert_cmpint (gs_app_get_state (app1), ==, AS_APP_STATE_UPDATABLE);
	g_assert_cmpint (gs_app_get_state (app2), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpint (gs_app_get_state (app3), ==, AS_APP_STATE_INSTALLED);

	/* set the default max parallel ops */
	gs_plugin_loader_set_max_parallel_ops (plugin_loader, 0);
}

int
main (int argc, char **argv)
{
	g_autofree gchar *tmp_root = NULL;
	gboolean ret;
	int retval;
	g_autofree gchar *xml = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *allowlist[] = {
		"appstream",
		"dummy",
		"generic-updates",
		"hardcoded-blocklist",
		"desktop-categories",
		"desktop-menu-path",
		"icons",
		"key-colors",
		"provenance",
		"provenance-license",
		NULL
	};

	/* While we use %G_TEST_OPTION_ISOLATE_DIRS to create temporary directories
	 * for each of the tests, we want to use the system MIME registry, assuming
	 * that it exists and correctly has shared-mime-info installed. */
#if GLIB_CHECK_VERSION(2, 60, 0)
	g_content_type_set_mime_dirs (NULL);
#endif

	/* Similarly, add the system-wide icon theme path before it’s
	 * overwritten by %G_TEST_OPTION_ISOLATE_DIRS. */
	gs_test_expose_icon_theme_paths ();

	g_test_init (&argc, &argv,
#if GLIB_CHECK_VERSION(2, 60, 0)
		     G_TEST_OPTION_ISOLATE_DIRS,
#endif
		     NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv ("GS_XMLB_VERBOSE", "1", TRUE);

	/* set all the things required as a dummy test harness */
	g_setenv ("GS_SELF_TEST_LOCALE", "en_GB", TRUE);
	g_setenv ("GS_SELF_TEST_DUMMY_ENABLE", "1", TRUE);
	g_setenv ("GS_SELF_TEST_PROVENANCE_SOURCES", "london*,boston", TRUE);
	g_setenv ("GS_SELF_TEST_PROVENANCE_LICENSE_SOURCES", "london*,boston", TRUE);
	g_setenv ("GS_SELF_TEST_PROVENANCE_LICENSE_URL", "https://www.debian.org/", TRUE);
	g_setenv ("GNOME_SOFTWARE_POPULAR", "", TRUE);

	/* Use a common cache directory for all tests, since the appstream
	 * plugin uses it and cannot be reinitialised for each test. */
	tmp_root = g_dir_make_tmp ("gnome-software-dummy-test-XXXXXX", NULL);
	g_assert (tmp_root != NULL);
	g_setenv ("GS_SELF_TEST_CACHEDIR", tmp_root, TRUE);

	xml = g_strdup ("<?xml version=\"1.0\"?>\n"
		"<components version=\"0.9\">\n"
		"  <component type=\"desktop\">\n"
		"    <id>chiron.desktop</id>\n"
		"    <name>Chiron</name>\n"
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
		"    <name>Fedora Rawhide</name>\n"
		"    <summary>Release specific tagline</summary>\n"
		"    <pkgname>fedora-release</pkgname>\n"
		"  </component>\n"
		"  <info>\n"
		"    <scope>user</scope>\n"
		"  </info>\n"
		"</components>\n");
	g_setenv ("GS_SELF_TEST_APPSTREAM_XML", xml, TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* we can only load this once per process */
	plugin_loader = gs_plugin_loader_new ();
	g_signal_connect (plugin_loader, "status-changed",
			  G_CALLBACK (gs_plugin_loader_status_changed_cb), NULL);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR_CORE);
	ret = gs_plugin_loader_setup (plugin_loader,
				      (gchar**) allowlist,
				      NULL,
				      NULL,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (!gs_plugin_loader_get_enabled (plugin_loader, "notgoingtoexist"));
	g_assert (gs_plugin_loader_get_enabled (plugin_loader, "appstream"));
	g_assert (gs_plugin_loader_get_enabled (plugin_loader, "dummy"));

	/* plugin tests go here */
	g_test_add_data_func ("/gnome-software/plugins/dummy/wildcard",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_wildcard_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/plugin-cache",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_plugin_cache_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/key-colors",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_key_colors_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/search",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_search_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/search-alternate",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_search_alternate_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/hang",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_hang_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/search{invalid}",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_search_invalid_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/url-to-app",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_url_to_app_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/install",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_install_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/error",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_error_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/installed",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_installed_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/refine",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_refine_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/updates",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_updates_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/distro-upgrades",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_distro_upgrades_func);
	g_test_add_data_func ("/gnome-software/plugins/dummy/metadata-quirks",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_metadata_quirks);
	g_test_add_data_func ("/gnome-software/plugins/dummy/limit-parallel-ops",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_dummy_limit_parallel_ops_func);
	retval = g_test_run ();

	/* Clean up. */
	gs_utils_rmtree (tmp_root, NULL);

	return retval;
}
