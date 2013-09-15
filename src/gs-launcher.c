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

static void
gs_launcher_class_init (GsLauncherClass *class)
{
}

GsLauncher *
gs_launcher_new (void)
{
	return g_object_new (gs_launcher_get_type (),
			     "application-id", "org.gnome.Software",
			     "flags", G_APPLICATION_IS_LAUNCHER,
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
