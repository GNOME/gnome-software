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
gs_plugins_fwupd_func (GsPluginLoader *plugin_loader)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* no fwupd, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "fwupd")) {
		g_test_skip ("not enabled");
		return;
	}

	/* load local file */
	fn = gs_test_get_filename (TESTDATADIR, "chiron-0.2.cab");
	g_assert (fn != NULL);
	file = g_file_new_for_path (fn);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_FILE_TO_APP,
					 "file", file,
					 NULL);
	app = gs_plugin_loader_job_process_app (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (app != NULL);
	g_assert_cmpint (gs_app_get_kind (app), ==, AS_APP_KIND_FIRMWARE);
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

int
main (int argc, char **argv)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *whitelist[] = {
		"fwupd",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

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
	g_test_add_data_func ("/gnome-software/plugins/fwupd",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_fwupd_func);

	return g_test_run ();
}

/* vim: set noexpandtab: */
