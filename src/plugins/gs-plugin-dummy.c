/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <gs-plugin.h>

struct GsPluginPrivate {
	guint			 dummy;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "dummy";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->dummy = 999;
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 1.0f;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	plugin->priv->dummy = 0;
}

/**
 * gs_plugin_add_search:
 */
gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      const gchar *value,
		      GList **list,
		      GCancellable *cancellable,
		      GError **error)
{
	return TRUE;
}

/**
 * gs_plugin_add_updates:
 */
gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsApp *app;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* spin */
	g_usleep (2 * G_USEC_PER_SEC);

	/* add a normal application */
	app = gs_app_new ("gnome-boxes");
	gs_app_set_name (app, "Boxes");
	gs_app_set_summary (app, "Do not segfault when using newer versons of libvirt.");
	gs_app_set_kind (app, GS_APP_KIND_NORMAL);
	gs_plugin_add_app (list, app);

	/* add an OS update */
	app = gs_app_new ("libvirt-glib-devel;0.0.1;noarch;fedora");
	gs_app_set_name (app, "libvirt-glib-devel");
	gs_app_set_summary (app, "Fix several memory leaks.");
	gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
	gs_plugin_add_app (list, app);

	/* add a second OS update */
	app = gs_app_new ("gnome-boxes-libs;0.0.1;i386;updates-testing");
	gs_app_set_name (app, "gnome-boxes-libs");
	gs_app_set_summary (app, "Do not segfault when using newer versons of libvirt.");
	gs_app_set_kind (app, GS_APP_KIND_PACKAGE);
	gs_plugin_add_app (list, app);

	return TRUE;
}

/**
 * gs_plugin_add_installed:
 */
gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GList **list,
			 GCancellable *cancellable,
			 GError **error)
{
	GsApp *app;

	app = gs_app_new ("gnome-power-manager");
	gs_app_set_name (app, "Power Manager");
	gs_app_set_summary (app, "Power Management Program");
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	gs_app_set_kind (app, GS_APP_KIND_NORMAL);
	gs_plugin_add_app (list, app);

	return TRUE;
}

/**
 * gs_plugin_add_popular:
 */
gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsApp *app;

	app = gs_app_new ("gnome-power-manager");
	gs_app_set_name (app, "Power Manager");
	gs_app_set_summary (app, "Power Management Program");
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	gs_app_set_kind (app, GS_APP_KIND_NORMAL);
	gs_plugin_add_app (list, app);

	return TRUE;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GCancellable *cancellable,
		  GError **error)
{
	GsApp *app;
	GList *l;

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_name (app) == NULL) {
			if (g_strcmp0 (gs_app_get_id (app), "gnome-boxes") == 0) {
				gs_app_set_name (app, "Boxes");
				gs_app_set_summary (app, "A simple GNOME 3 application to access remote or virtual systems");
			}
		}
	}
	return TRUE;
}

/**
 * gs_plugin_add_category_apps:
 */
gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GList **list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsApp *app;
	app = gs_app_new ("gnome-boxes");
	gs_app_set_name (app, "Boxes");
	gs_app_set_summary (app, "View and use virtual machines");
	gs_app_set_url (app, "http://www.box.org");
	gs_app_set_kind (app, GS_APP_KIND_NORMAL);
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	gs_app_set_pixbuf (app, gdk_pixbuf_new_from_file ("/usr/share/icons/hicolor/48x48/apps/gnome-boxes.png", NULL));
	gs_plugin_add_app (list, app);
	return TRUE;
}
