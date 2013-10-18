/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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
#include <gio/gio.h>
#include <locale.h>


typedef struct {
	GApplication parent;
} GsLauncher;

typedef struct {
	GApplicationClass parent_class;
} GsLauncherClass;

GType gs_launcher_get_type (void);

G_DEFINE_TYPE (GsLauncher, gs_launcher, G_TYPE_APPLICATION)

static void
gs_launcher_init (GsLauncher *launcher)
{
}

static gboolean
gs_launcher_local_command_line (GApplication *app, gchar ***args, gint *status)
{
	GOptionContext *context;
	gchar *mode = NULL;
	gchar *search = NULL;
	gchar *id = NULL;
	gboolean activate_ui = TRUE;
	gboolean version = FALSE;
	gboolean profile = FALSE;
	gint debug_level = -1;
	gint argc;
	const GOptionEntry options[] = {
		{ "mode", '\0', 0, G_OPTION_ARG_STRING, &mode,
		  /* TRANSLATORS: this is a command line option */
		  _("Start up mode: either ‘updates’, ‘updated’, ‘installed’ or ‘overview’"), _("MODE") },
		{ "search", '\0', 0, G_OPTION_ARG_STRING, &search,
		  _("Search for applications"), _("SEARCH") },
		{ "details", '\0', 0, G_OPTION_ARG_STRING, &id,
		  _("Show application details"), _("ID") },
		{ "set-debug-level", '\0', 0, G_OPTION_ARG_INT, &debug_level,
		  _("Set the specified debugging level"), _("ID") },
		{ "profile", 0, 0, G_OPTION_ARG_NONE, &profile,
		  _("Show profiling information for the service"), NULL },
		{ "version", 0, 0, G_OPTION_ARG_NONE, &version, NULL, NULL },
		{ NULL}
	};
	GError *error = NULL;

	context = g_option_context_new ("");
	g_option_context_add_main_entries (context, options, NULL);

	argc = g_strv_length (*args);
	if (!g_option_context_parse (context, &argc, args, &error)) {
		g_printerr ("%s\n", error->message);
		g_error_free (error);
		*status = 1;
		goto out;
	}

	if (version) {
		g_print ("gnome-software " VERSION "\n");
		*status = 0;
		goto out;
	}

	if (!g_application_register (app, NULL, &error)) {
		g_printerr ("%s\n", error->message);
		g_error_free (error);
		*status = 1;
		goto out;
	}

	if (profile) {
		activate_ui = FALSE;
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"profile",
						NULL);
	}
	if (debug_level >= 0) {
		activate_ui = FALSE;
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"set-debug-level",
						g_variant_new_int32 (debug_level));
	}

	if (mode != NULL) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"set-mode",
						g_variant_new_string (mode));
	} else if (search != NULL) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"search",
						g_variant_new_string (search));
	} else if (id != NULL) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"details",
						g_variant_new ("(ss)", id, ""));
	} else if (activate_ui) {
		g_application_activate (app);
	}

	*status = 0;

out:
	g_option_context_free (context);

	return TRUE;
}

static void
gs_launcher_class_init (GsLauncherClass *class)
{
	GApplicationClass *application_class = G_APPLICATION_CLASS (class);

	application_class->local_command_line = gs_launcher_local_command_line;
}

static GsLauncher *
gs_launcher_new (void)
{
	return g_object_new (gs_launcher_get_type (),
			     "application-id", "org.gnome.Software",
			     "flags", G_APPLICATION_IS_LAUNCHER |
				      G_APPLICATION_SEND_ENVIRONMENT,
			     NULL);
}

int
main (int argc, char **argv)
{
	int status = 0;
	GsLauncher *launcher;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	launcher = gs_launcher_new ();
	status = g_application_run (G_APPLICATION (launcher), argc, argv);
	g_object_unref (launcher);

	return status;
}

/* vim: set noexpandtab: */
