/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gnome-software-private.h"

#include "gs-test.h"

static void
gs_plugins_epiphany_func (GsPluginLoader *plugin_loader)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* no epiphany, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "epiphany"))
		return;

	/* a webapp with a local icon */
	app = gs_app_new ("arachne.desktop");
	gs_app_set_kind (app, AS_APP_KIND_WEB_APP);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", app,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
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
		"epiphany",
		"icons",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv ("GS_XMLB_VERBOSE", "1", TRUE);

	fn = gs_test_get_filename (TESTDATADIR, "icons/hicolor/scalable/org.gnome.Software.svg");
	g_assert (fn != NULL);
	xml = g_strdup_printf ("<?xml version=\"1.0\"?>\n"
		"<components version=\"0.9\">\n"
		"  <component type=\"webapp\">\n"
		"    <id>arachne.desktop</id>\n"
		"    <name>test</name>\n"
		"    <pkgname>test</pkgname>\n"
		"    <icon type=\"remote\">file://%s</icon>\n"
		"  </component>\n"
		"  <info>\n"
		"    <scope>user</scope>\n"
		"  </info>\n"
		"</components>\n", fn);
	g_setenv ("GS_SELF_TEST_APPSTREAM_XML", xml, TRUE);

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
	g_test_add_data_func ("/gnome-software/plugins/epiphany",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_epiphany_func);

	return g_test_run ();
}
