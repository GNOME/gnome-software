/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Canonical Ltd
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

#include <snapd-glib/snapd-glib.h>

#include "config.h"

#include "gnome-software-private.h"

#include "gs-test.h"

SnapdClient *
snapd_client_new (void)
{
	/* use a dummy socket */
	g_autoptr(GSocket) socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, NULL);
	return snapd_client_new_from_socket (socket);
}

gboolean
snapd_client_connect_sync (SnapdClient *client, GCancellable *cancellable, GError **error)
{
	/* skip connection */
	return TRUE;
}

SnapdAuthData *
snapd_client_login_sync (SnapdClient *client,
			 const gchar *username, const gchar *password, const gchar *otp,
			 GCancellable *cancellable, GError **error)
{
	return snapd_auth_data_new ("macaroon", NULL);
}

static SnapdSnap *
make_snap (const gchar *name, SnapdSnapStatus status)
{
	g_autoptr(GDateTime) install_date = NULL;
	g_autoptr(GPtrArray) apps = NULL;
	g_autoptr(GPtrArray) screenshots = NULL;
	SnapdScreenshot *screenshot;

	install_date = g_date_time_new_utc (2017, 1, 2, 11, 23, 58);

	apps = g_ptr_array_new_with_free_func (g_object_unref);

	screenshots = g_ptr_array_new_with_free_func (g_object_unref);
	screenshot = g_object_new (SNAPD_TYPE_SCREENSHOT,
				   "url", "http://example.com/screenshot1.jpg",
				   "width", 640,
				   "height", 480,
				   NULL);
	g_ptr_array_add (screenshots, screenshot);
	screenshot = g_object_new (SNAPD_TYPE_SCREENSHOT,
				   "url", "http://example.com/screenshot2.jpg",
				   "width", 1024,
				   "height", 768,
				   NULL);
	g_ptr_array_add (screenshots, screenshot);

	return g_object_new (SNAPD_TYPE_SNAP,
			     "apps", apps,
			     "description", "DESCRIPTION",
			     "download-size", 500,
			     "icon", "/icon",
			     "id", name,
			     "install-date", install_date,
			     "installed-size", 1000,
			     "name", name,
			     "screenshots", screenshots,
			     "status", status,
			     "summary", "SUMMARY",
			     "version", "VERSION",
			     NULL);
}

GPtrArray *
snapd_client_list_sync (SnapdClient *client,
			GCancellable *cancellable, GError **error)
{
	GPtrArray *snaps;

	snaps = g_ptr_array_new_with_free_func (g_object_unref);
	g_ptr_array_add (snaps, make_snap ("snap", SNAPD_SNAP_STATUS_INSTALLED));

	return snaps;
}

SnapdSnap *
snapd_client_list_one_sync (SnapdClient *client,
			    const gchar *name,
			    GCancellable *cancellable, GError **error)
{
	return make_snap (name, SNAPD_SNAP_STATUS_INSTALLED);
}

SnapdIcon *
snapd_client_get_icon_sync (SnapdClient *client,
			    const gchar *name,
			    GCancellable *cancellable, GError **error)
{
	g_autoptr(GBytes) data = NULL;
	/* apparently this is the smallest valid PNG file (1x1) */
	const gchar png_data[67] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
				     0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
				     0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
				     0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
				     0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
				     0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
				     0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
				     0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
				     0x42, 0x60, 0x82 };

	data = g_bytes_new (png_data, 67);
	return g_object_new (SNAPD_TYPE_ICON,
			     "mime-type", "image/png",
			     "data", data,
			     NULL);
}

gboolean
snapd_client_get_interfaces_sync (SnapdClient *client,
				  GPtrArray **plugs, GPtrArray **slots,
				  GCancellable *cancellable, GError **error)
{
	if (plugs)
		*plugs = g_ptr_array_new_with_free_func (g_object_unref);
	if (slots)
		*slots = g_ptr_array_new_with_free_func (g_object_unref);
	return TRUE;
}

GPtrArray *
snapd_client_find_sync (SnapdClient *client,
			SnapdFindFlags flags, const gchar *query,
			gchar **suggested_currency,
			GCancellable *cancellable, GError **error)
{
	GPtrArray *snaps;

	snaps = g_ptr_array_new_with_free_func (g_object_unref);
	g_ptr_array_add (snaps, make_snap ("snap", SNAPD_SNAP_STATUS_AVAILABLE));

	return snaps;
}

gboolean
snapd_client_install_sync (SnapdClient *client,
			   const gchar *name, const gchar *channel,
			   SnapdProgressCallback progress_callback, gpointer progress_callback_data,
			   GCancellable *cancellable, GError **error)
{
	g_assert_cmpstr (name, ==, "snap");
	g_assert (channel == NULL);
	return TRUE;
}

gboolean
snapd_client_remove_sync (SnapdClient *client,
			  const gchar *name,
			  SnapdProgressCallback progress_callback, gpointer progress_callback_data,
			  GCancellable *cancellable, GError **error)
{
	g_assert_cmpstr (name, ==, "snap");
	return TRUE;
}

static void
gs_plugins_snap_test_func (GsPluginLoader *plugin_loader)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GsAppList) apps = NULL;
	gboolean ret;
	GsApp *app;
	GPtrArray *screenshots, *images;
	AsScreenshot *screenshot;
	AsImage *image;
	GdkPixbuf *pixbuf;
	g_autoptr(GError) error = NULL;

	/* no snap, abort */
	if (!gs_plugin_loader_get_enabled (plugin_loader, "snap")) {
		g_test_skip ("not enabled");
		return;
	}

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", "snap",
					 NULL);
	apps = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (apps != NULL);
	g_assert_cmpint (gs_app_list_length (apps), ==, 1);
	app = gs_app_list_index (apps, 0);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_AVAILABLE);
	g_assert_cmpstr (gs_app_get_name (app), ==, "snap");
	g_assert_cmpstr (gs_app_get_version (app), ==, "VERSION");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "SUMMARY");
	g_assert_cmpstr (gs_app_get_description (app), ==, "DESCRIPTION");
	screenshots = gs_app_get_screenshots (app);
	g_assert_cmpint (screenshots->len, ==, 2);
	screenshot = g_ptr_array_index (screenshots, 0);
	images = as_screenshot_get_images (screenshot);
	g_assert_cmpint (images->len, ==, 1);
	image = g_ptr_array_index (images, 0);
	g_assert_cmpstr (as_image_get_url (image), ==, "http://example.com/screenshot1.jpg");
	g_assert_cmpint (as_image_get_width (image), ==, 640);
	g_assert_cmpint (as_image_get_height (image), ==, 480);
	screenshot = g_ptr_array_index (screenshots, 1);
	images = as_screenshot_get_images (screenshot);
	g_assert_cmpint (images->len, ==, 1);
	image = g_ptr_array_index (images, 0);
	g_assert_cmpstr (as_image_get_url (image), ==, "http://example.com/screenshot2.jpg");
	g_assert_cmpint (as_image_get_width (image), ==, 1024);
	g_assert_cmpint (as_image_get_height (image), ==, 768);
	pixbuf = gs_app_get_pixbuf (app);
	g_assert_cmpint (gdk_pixbuf_get_width (pixbuf), ==, 1);
	g_assert_cmpint (gdk_pixbuf_get_height (pixbuf), ==, 1);
	g_assert_cmpint (gs_app_get_size_installed (app), ==, 1000);
	g_assert_cmpint (gs_app_get_size_download (app), ==, 500);
	g_assert_cmpint (gs_app_get_install_date (app), ==, g_date_time_to_unix (g_date_time_new_utc (2017, 1, 2, 11, 23, 58)));

	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "app", app,
					 NULL);
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	gs_test_flush_main_context ();
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);

	g_object_unref (plugin_job);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "app", app,
					 NULL);
	gs_test_flush_main_context ();
	ret = gs_plugin_loader_job_action (plugin_loader, plugin_job, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
}

int
main (int argc, char **argv)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginLoader) plugin_loader = NULL;
	const gchar *whitelist[] = {
		"snap",
		NULL
	};

	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* we can only load this once per process */
	plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR);
	gs_plugin_loader_add_location (plugin_loader, LOCALPLUGINDIR_CORE);
	ret = gs_plugin_loader_setup (plugin_loader,
				      (gchar**) whitelist,
				      NULL,
				      GS_PLUGIN_FAILURE_FLAGS_NONE,
				      NULL,
				      &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* plugin tests go here */
	g_test_add_data_func ("/gnome-software/plugins/snap/test",
			      plugin_loader,
			      (GTestDataFunc) gs_plugins_snap_test_func);
	return g_test_run ();
}

/* vim: set noexpandtab: */
