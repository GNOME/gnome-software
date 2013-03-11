/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>

#include "gs-plugin.h"

/**
 * gs_plugin_add_app:
 **/
void
gs_plugin_add_app (GList **list, GsApp *app)
{
	*list = g_list_prepend (*list, app);
}

typedef struct {
	GsPlugin	*plugin;
	GsApp		*app;
	GsPluginStatus	 status;
} GsPluginStatusHelper;

/**
 * gs_plugin_status_update_cb:
 **/
static gboolean
gs_plugin_status_update_cb (gpointer user_data)
{
	GsPluginStatusHelper *helper = (GsPluginStatusHelper *) user_data;

	/* call back into the loader */
	helper->plugin->status_update_fn (helper->plugin,
					  helper->app,
					  helper->status,
					  helper->plugin->status_update_user_data);
	if (helper->app != NULL)
		g_object_unref (helper->app);
	g_slice_free (GsPluginStatusHelper, helper);
	return FALSE;
}

/**
 * gs_plugin_status_update:
 **/
void
gs_plugin_status_update (GsPlugin *plugin, GsApp *app, GsPluginStatus status)
{
	GsPluginStatusHelper *helper;
	helper = g_slice_new0 (GsPluginStatusHelper);
	helper->plugin = plugin;
	helper->status = status;
	if (app != NULL)
		helper->app = g_object_ref (app);
	g_idle_add (gs_plugin_status_update_cb, helper);
}
