/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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


typedef struct {
	GtkBuilder	*builder;
	GtkApplication	*application;
} GsMainPrivate;

/**
 * gs_main_activate_cb:
 **/
static void
gs_main_activate_cb (GApplication *application, GsMainPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	gtk_window_present (window);
}

/**
 * gs_main_startup_cb:
 **/
static void
gs_main_startup_cb (GApplication *application, GsMainPrivate *priv)
{
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_resource (priv->builder,
						"/org/gnome/software/gnome-software.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GS_DATA G_DIR_SEPARATOR_S "icons");

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_software"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);

	/* setup callbacks */

	/* show main UI */
	gtk_widget_show (main_window);
out:
	return;
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	GsMainPrivate *priv;
	GOptionContext *context;
	int status = 0;
	gboolean ret;
	GError *error = NULL;

	const GOptionEntry options[] = {
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	context = g_option_context_new ("gnome-color-manager profile main");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("failed to parse options: %s", error->message);
		g_error_free (error);
	}
	g_option_context_free (context);

	priv = g_new0 (GsMainPrivate, 1);

	/* ensure single instance */
	priv->application = gtk_application_new ("org.gnome.Software", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (gs_main_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (gs_main_activate_cb), priv);

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->application);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	g_free (priv);
	return status;
}
