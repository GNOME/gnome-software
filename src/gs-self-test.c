/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-plugin-loader.h"
#include "gs-plugin-loader-sync.h"

static void
gs_app_func (void)
{
	GsApp *app;

	app = gs_app_new ("gnome-software");
	g_assert (GS_IS_APP (app));

	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-software");

	g_object_unref (app);
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
gs_plugin_loader_func (void)
{
	gboolean ret;
	GError *error = NULL;
	GList *list;
	GList *l;
	GsApp *app;
	GsPluginLoader *loader;

	loader = gs_plugin_loader_new ();
	g_assert (GS_IS_PLUGIN_LOADER (loader));
	g_signal_connect (loader, "status-changed",
			  G_CALLBACK (gs_plugin_loader_status_changed_cb), NULL);

	/* load the plugins */
	gs_plugin_loader_set_location (loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (loader, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* enable some that will give us predictable results */
	ret = gs_plugin_loader_set_enabled (loader, "dummy", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-kind", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-popular", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-ratings", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "datadir-filename", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "datadir-apps", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "notgoingtoexist", TRUE);
	g_assert (!ret);

	list = gs_plugin_loader_get_popular (loader, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (_status_changed_cnt, ==, 1);
	g_assert_cmpint (g_list_length (list), ==, 6);
	app = g_list_nth_data (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-boxes");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Boxes");

	app = g_list_nth_data (list, 1);
	g_assert_cmpstr (gs_app_get_id (app), ==, "gedit");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Edit text files");

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* get updates */
	_status_changed_cnt = 0;
	list = gs_plugin_loader_get_updates (loader, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (_status_changed_cnt, ==, 2);
	g_assert_cmpint (g_list_length (list), ==, 2);
	app = g_list_nth_data (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "os-update:gnome-boxes-libs;0.0.1;i386;updates-testing,libvirt-glib-devel;0.0.1;noarch;fedora");
	g_assert_cmpstr (gs_app_get_name (app), ==, "OS Update");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "This updates the system:\nDo not segfault when using newer versons of libvirt.\nFix several memory leaks.");
	g_assert_cmpint (gs_app_get_kind (app), ==, GS_APP_KIND_OS_UPDATE);

	app = g_list_nth_data (list, 1);
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-boxes");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Boxes");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Do not segfault when using newer versons of libvirt.");
	g_assert_cmpint (gs_app_get_kind (app), ==, GS_APP_KIND_NORMAL);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* test packagekit */
	gs_plugin_loader_set_enabled (loader, "dummy", FALSE);
	ret = gs_plugin_loader_set_enabled (loader, "packagekit", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "desktopdb", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "datadir-apps", TRUE);
	g_assert (ret);

	list = gs_plugin_loader_get_installed (loader, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (g_list_length (list), >, 50);

	/* find a specific app */
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (g_strcmp0 (gs_app_get_id (app), "gnome-screenshot") == 0)
			break;
	}
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-screenshot");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Screenshot");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Save images of your screen or individual windows");
	g_assert_cmpint (gs_app_get_state (app), ==, GS_APP_STATE_INSTALLED);
	g_assert_cmpint (gs_app_get_kind (app), ==, GS_APP_KIND_SYSTEM);
	g_assert (gs_app_get_pixbuf (app) != NULL);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* do this again, which should be much faster */
	list = gs_plugin_loader_get_installed (loader, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (g_list_length (list), >, 50);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	g_object_unref (loader);
}

int
main (int argc, char **argv)
{
	g_type_init ();
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/gnome-software/app", gs_app_func);
	g_test_add_func ("/gnome-software/plugin-loader", gs_plugin_loader_func);

	return g_test_run ();
}

