/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
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

static void
gs_plugins_modalias_func (GsPluginLoader *plugin_loader)
{
	GsApp *app;
	g_autofree gchar *menu_path = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get search result based on addon keyword */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "colorhug2",
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* make sure there is one entry, the parent app */
	g_assert_cmpint (gs_app_list_length (list), ==, 1);
	app = gs_app_list_index (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "com.hughski.ColorHug2.driver");
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_DRIVER);
	g_assert (gs_app_has_category (app, "Addon"));
	g_assert (gs_app_has_category (app, "Driver"));
}

int
main (int argc, char **argv)
{
	gboolean ret;
	g_autofree gchar *xml = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *whitelist[] = {
		"appstream",
		"dummy",
		"modalias",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv ("GS_SELF_TEST_DUMMY_ENABLE", "1", TRUE);

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
		"</components>\n");
	g_setenv ("GS_SELF_TEST_APPSTREAM_XML", xml, TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* we can only load this once per process */
	plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR_CORE);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR_DUMMY);
	ret = gs_plugin_loader_setup (plugin_loader,
				      (gchar**) whitelist,
				      NULL,
				      NULL,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* plugin tests go here */
	g_test_add_data_func ("/gnome-software/plugins/modalias",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_modalias_func);

	return g_test_run ();
}

/* vim: set noexpandtab: */
