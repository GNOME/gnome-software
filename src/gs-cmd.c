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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>

#include "gs-profile.h"
#include "gs-plugin-loader.h"
#include "gs-plugin-loader-sync.h"

static void
show_results (GList *list)
{
	GList *l;
	GsApp *app;
	gchar *tmp;

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		tmp = gs_app_to_string (app);
		g_print ("%s\n", tmp);
		g_free (tmp);
	}
}

int
main (int argc, char **argv)
{
	GError *error = NULL;
	GList *list = NULL;
	GsPluginLoader *plugin_loader;
	GsProfile *profile;
	gboolean ret;
	int status = 0;

	setlocale (LC_ALL, "");
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	profile = gs_profile_new ();
	gs_profile_start (profile, "GsCmd");

	/* load plugins */
	plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_location (plugin_loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (plugin_loader, &error);
	if (!ret) {
		g_warning ("Failed to setup plugins: %s", error->message);
		g_error_free (error);
		goto out;
	}
//	gs_plugin_loader_set_enabled (plugin_loader, "xxx", TRUE);
	gs_plugin_loader_dump_state (plugin_loader);

	/* do action */
	if (argc == 2 && g_strcmp0 (argv[1], "installed") == 0) {
		list = gs_plugin_loader_get_installed (plugin_loader,
						       GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						       NULL,
						       &error);
	} else if (argc == 3 && g_strcmp0 (argv[1], "search") == 0) {
		list = gs_plugin_loader_search (plugin_loader,
						argv[2],
						GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						NULL,
						&error);
	} else {
		g_warning ("Did not recognise option, use 'installed', or 'search'");
	}

	show_results (list);
	gs_profile_stop (profile, "GsCmd");
	gs_profile_dump (profile);
out:
	gs_plugin_list_free (list);
	g_object_unref (plugin_loader);
	g_object_unref (profile);
	return status;
}

/* vim: set noexpandtab: */
