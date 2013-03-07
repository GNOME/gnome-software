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

static void
gs_app_func (void)
{
	GsApp *app;

	app = gs_app_new ("gnome-software");
	g_assert (GS_IS_APP (app));

	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-software");

	g_object_unref (app);
}

static void
gs_plugin_loader_func (void)
{
	gboolean ret;
	GError *error = NULL;
	GsPluginLoader *loader;

	loader = gs_plugin_loader_new ();
	g_assert (GS_IS_PLUGIN_LOADER (loader));

	/* load the plugins */
	gs_plugin_loader_set_location (loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (loader, &error);
	g_assert_no_error (error);
	g_assert (ret);

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

