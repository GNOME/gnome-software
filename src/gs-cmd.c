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

/**
 * gs_cmd_show_results:
 **/
static void
gs_cmd_show_results (GList *list)
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

/**
 * gs_cmd_refine_flag_from_string:
 **/
static GsPluginRefineFlags
gs_cmd_refine_flag_from_string (const gchar *flag, GError **error)
{
	if (g_strcmp0 (flag, "licence") == 0)
		return GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENCE;
	if (g_strcmp0 (flag, "url") == 0)
		return GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL;
	if (g_strcmp0 (flag, "description") == 0)
		return GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION;
	if (g_strcmp0 (flag, "size") == 0)
		return GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE;
	if (g_strcmp0 (flag, "rating") == 0)
		return GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING;
	if (g_strcmp0 (flag, "version") == 0)
		return GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION;
	if (g_strcmp0 (flag, "history") == 0)
		return GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY;
	if (g_strcmp0 (flag, "setup-action") == 0)
		return GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION;
	g_set_error (error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_NOT_SUPPORTED,
		     "GsPluginRefineFlag '%s' not recognised", flag);
	return 0;
}

/**
 * gs_cmd_parse_refine_flags:
 **/
static guint64
gs_cmd_parse_refine_flags (const gchar *extra, GError **error)
{
	GsPluginRefineFlags tmp;
	gchar **split = NULL;
	guint i;
	guint64 refine_flags = GS_PLUGIN_REFINE_FLAGS_DEFAULT;

	if (extra == NULL)
		goto out;

	split = g_strsplit (extra, ",", -1);
	for (i = 0; split[i] != NULL; i++) {
		tmp = gs_cmd_refine_flag_from_string (split[i], error);
		if (tmp == 0) {
			refine_flags = G_MAXUINT64;
			goto out;
		}
		refine_flags |= tmp;
	}
out:
	g_strfreev (split);
	return refine_flags;
}

int
main (int argc, char **argv)
{
	GError *error = NULL;
	GList *list = NULL;
	GOptionContext *context;
	GsApp *app = NULL;
	GsPluginLoader *plugin_loader = NULL;
	GsProfile *profile = NULL;
	gboolean ret;
	gboolean show_results = FALSE;
	guint64 refine_flags = GS_PLUGIN_REFINE_FLAGS_DEFAULT;
	gchar *refine_flags_str = NULL;
	int status = 0;
	const GOptionEntry options[] = {
		{ "show-results", '\0', 0, G_OPTION_ARG_NONE, &show_results,
		  "Show the results for the action", NULL },
		{ "refine-flags", '\0', 0, G_OPTION_ARG_STRING, &refine_flags_str,
		  "Set any refine flags required for the action", NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, "GNOME Software Test Program");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_print ("Failed to parse options: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* parse any refine flags */
	refine_flags = gs_cmd_parse_refine_flags (refine_flags_str, &error);
	if (refine_flags == G_MAXUINT64) {
		g_print ("Flag unknown: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	profile = gs_profile_new ();
	gs_profile_start (profile, "GsCmd");

	/* load plugins */
	plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_location (plugin_loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (plugin_loader, &error);
	if (!ret) {
		g_print ("Failed to setup plugins: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
	gs_plugin_loader_dump_state (plugin_loader);

	/* do action */
	if (argc == 2 && g_strcmp0 (argv[1], "installed") == 0) {
		list = gs_plugin_loader_get_installed (plugin_loader,
						       refine_flags,
						       NULL,
						       &error);
	} else if (argc == 3 && g_strcmp0 (argv[1], "search") == 0) {
		list = gs_plugin_loader_search (plugin_loader,
						argv[2],
						refine_flags,
						NULL,
						&error);
	} else if (argc == 3 && g_strcmp0 (argv[1], "refine") == 0) {
		app = gs_app_new (argv[2]);
		ret = gs_plugin_loader_app_refine (plugin_loader,
						   app,
						   refine_flags,
						   NULL,
						   &error);
	} else if (argc == 2 && g_strcmp0 (argv[1], "updates") == 0) {
		list = gs_plugin_loader_get_updates (plugin_loader,
						     refine_flags,
						     NULL,
						     &error);
	} else if (argc == 2 && g_strcmp0 (argv[1], "popular") == 0) {
		list = gs_plugin_loader_get_popular (plugin_loader,
						     refine_flags,
						     NULL,
						     &error);
	} else {
		g_warning ("Did not recognise option, use 'installed', "
			   "'updates', 'popular', or 'search'");
	}
	if (error != NULL) {
		g_warning ("Failed: %s", error->message);
		g_error_free (error);
		goto out;
	}

	if (show_results)
		gs_cmd_show_results (list);
	gs_profile_stop (profile, "GsCmd");
	gs_profile_dump (profile);
out:
	g_option_context_free (context);
	g_free (refine_flags_str);
	gs_plugin_list_free (list);
	if (app != NULL)
		g_object_unref (app);
	if (plugin_loader != NULL)
		g_object_unref (plugin_loader);
	if (profile != NULL)
		g_object_unref (profile);
	return status;
}

/* vim: set noexpandtab: */
