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
#include <packagekit-glib2/packagekit.h>

typedef enum {
	GS_MAIN_MODE_NEW,
	GS_MAIN_MODE_INSTALLED,
	GS_MAIN_MODE_UPDATES
} GsMainMode;

typedef struct {
	GtkBuilder		*builder;
	GtkApplication		*application;
	GsMainMode		 mode;
	PkTask			*task;
	GCancellable		*cancellable;
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
 * gpk_application_progress_cb:
 **/
static void
gpk_application_progress_cb (PkProgress *progress, PkProgressType type, GsMainPrivate *priv)
{
	PkStatusEnum status;
	gint percentage;
	gboolean allow_cancel;

	g_object_get (progress,
		      "status", &status,
		      "percentage", &percentage,
		      "allow-cancel", &allow_cancel,
		      NULL);
	g_debug ("%s : %i (allow-cancel:%i",
		 pk_status_enum_to_string (status),
		 percentage,
		 allow_cancel);
}

/**
 * gpk_application_search_cb:
 **/
static void
gpk_application_search_cb (PkClient *client, GAsyncResult *res, GsMainPrivate *priv)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GPtrArray *array = NULL;
	PkPackage *item;
	guint i;
	GtkWidget *widget;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to search: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to search: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	array = pk_results_get_package_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_debug ("add %s", pk_package_get_id (item));
	}

	/* focus back to the text extry */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	gtk_widget_grab_focus (widget);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gs_main_get_installed_packages:
 **/
static void
gs_main_get_installed_packages (GsMainPrivate *priv)
{
	PkBitfield filter;
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
					 PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 -1);
	pk_client_get_packages_async (PK_CLIENT(priv->task),
				      filter,
				      priv->cancellable,
				      (PkProgressCallback) gpk_application_progress_cb, priv,
				      (GAsyncReadyCallback) gpk_application_search_cb, priv);
}

/**
 * gs_main_get_updates:
 **/
static void
gs_main_get_updates (GsMainPrivate *priv)
{
	PkBitfield filter;
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 -1);
	pk_client_get_updates_async (PK_CLIENT(priv->task),
				     filter,
				     priv->cancellable,
				     (PkProgressCallback) gpk_application_progress_cb, priv,
				     (GAsyncReadyCallback) gpk_application_search_cb, priv);
}

/**
 * gs_main_get_featured:
 **/
static void
gs_main_get_featured (GsMainPrivate *priv)
{
	PkBitfield filter;
	const gchar *packages[] = { "firefox", "gimp", "xchat", NULL };
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 PK_FILTER_ENUM_NEWEST,
					 -1);
	pk_client_resolve_async (PK_CLIENT(priv->task),
				 filter,
				 (gchar **) packages,
				 priv->cancellable,
				 (PkProgressCallback) gpk_application_progress_cb, priv,
				 (GAsyncReadyCallback) gpk_application_search_cb, priv);
}

/**
 * gs_main_set_overview_mode:
 **/
static void
gs_main_set_overview_mode (GsMainPrivate *priv, GsMainMode mode)
{
	GtkWidget *widget;

	priv->mode = mode;
	switch (mode) {
	case GS_MAIN_MODE_NEW:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_widget_show (widget);
		gs_main_get_featured (priv);
		break;
	case GS_MAIN_MODE_INSTALLED:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_widget_show (widget);
		gs_main_get_installed_packages (priv);
		break;
	case GS_MAIN_MODE_UPDATES:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_all"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_widget_hide (widget);
		gs_main_get_updates (priv);
		break;
	default:
		g_assert_not_reached ();
	}

	/* fix sensitivities */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_new"));
	gtk_widget_set_sensitive (widget, mode != GS_MAIN_MODE_NEW);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_widget_set_sensitive (widget, mode != GS_MAIN_MODE_UPDATES);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	gtk_widget_set_sensitive (widget, mode != GS_MAIN_MODE_INSTALLED);


	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_main"));
	gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), mode);
}

/**
 * gs_main_overview_button_cb:
 **/
static void
gs_main_overview_button_cb (GtkWidget *widget, GsMainPrivate *priv)
{
	GsMainMode mode;
	mode = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
						   "gnome-software::overview-mode"));
	gs_main_set_overview_mode (priv, mode);
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
	GtkWidget *widget;

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
	gtk_window_set_default_size (GTK_WINDOW (main_window), 600, 400);

	/* setup callbacks */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_main"));
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (widget), FALSE);
	gs_main_set_overview_mode (priv, GS_MAIN_MODE_NEW);

	/* setup buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_new"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_MAIN_MODE_NEW));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_overview_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_MAIN_MODE_INSTALLED));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_overview_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_MAIN_MODE_UPDATES));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_overview_button_cb), priv);

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

	/* use PackageKit */
	priv->cancellable = g_cancellable_new ();
	priv->task = pk_task_new ();
	g_object_set (priv->task,
		      "background", FALSE,
		      NULL);

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	g_object_unref (priv->task);
	g_object_unref (priv->cancellable);
	g_object_unref (priv->application);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	g_free (priv);
	return status;
}
