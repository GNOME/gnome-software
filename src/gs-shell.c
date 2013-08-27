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

#include <string.h>
#include <glib/gi18n.h>

#include "gs-shell.h"
#include "gs-shell-details.h"
#include "gs-shell-installed.h"
#include "gs-shell-overview.h"
#include "gs-shell-updates.h"
#include "gs-shell-category.h"

static void	gs_shell_finalize	(GObject	*object);

#define GS_SHELL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL, GsShellPrivate))

struct GsShellPrivate
{
	gboolean		 ignore_primary_buttons;
	GCancellable		*cancellable;
	GsPluginLoader		*plugin_loader;
	GsShellInstalled	*shell_installed;
	GsShellMode		 mode;
	GsShellOverview		*shell_overview;
	GsShellUpdates		*shell_updates;
	GsShellDetails		*shell_details;
	GsShellCategory         *shell_category;
	GtkBuilder		*builder;
	guint			 tab_back_id;
};

G_DEFINE_TYPE (GsShell, gs_shell, G_TYPE_OBJECT)

static void gs_shell_set_overview_mode (GsShell *shell, GsShellMode mode, GsApp *app, const gchar *category);

/**
 * gs_shell_activate:
 **/
void
gs_shell_activate (GsShell *shell)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (shell->priv->builder, "window_software"));
	gtk_window_present (window);
}

/**
 * gs_shell_set_overview_mode:
 **/
static void
gs_shell_set_overview_mode (GsShell *shell, GsShellMode mode, GsApp *app, const gchar *category)
{
	GsShellPrivate *priv = shell->priv;
        GtkWidget *widget;

	if (priv->ignore_primary_buttons)
		return;

        /* hide all mode specific header widgets here, they will be shown in the
         * refresh functions
         */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_spinner"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_wait"));
	gtk_widget_hide (widget);
        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
	gtk_widget_hide (widget);

        /* update main buttons according to mode */
	priv->ignore_primary_buttons = TRUE;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_all"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_OVERVIEW);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_INSTALLED);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_UPDATES);
	priv->ignore_primary_buttons = FALSE;

	/* switch page */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_main"));
	gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), mode);

	/* do action for mode */
	priv->mode = mode;
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
		gs_shell_overview_refresh (priv->shell_overview);
		break;
	case GS_SHELL_MODE_INSTALLED:
		gs_shell_installed_refresh (priv->shell_installed);
		break;
	case GS_SHELL_MODE_UPDATES:
		gs_shell_updates_refresh (priv->shell_updates);
		break;
	case GS_SHELL_MODE_DETAILS:
		gs_shell_details_set_app (priv->shell_details, app);
		gs_shell_details_refresh (priv->shell_details);
		break;
	case GS_SHELL_MODE_CATEGORY:
		gs_shell_category_set_category (priv->shell_category, category);
		gs_shell_category_refresh (priv->shell_category);
		break;
	default:
		g_assert_not_reached ();
	}
}

/**
 * gs_shell_overview_button_cb:
 **/
static void
gs_shell_overview_button_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellMode mode;
	mode = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
						   "gnome-software::overview-mode"));
	gs_shell_set_overview_mode (shell, mode, NULL, NULL);
}

/**
 * gs_shell_back_button_cb:
 **/
static void
gs_shell_back_button_cb (GtkWidget *widget, GsShell *shell)
{
	gs_shell_set_overview_mode (shell, shell->priv->tab_back_id, NULL, NULL);
}

/**
 * gs_shell_setup:
 */
GtkWindow *
gs_shell_setup (GsShell *shell, GsPluginLoader *plugin_loader, GCancellable *cancellable)
{
	GsShellPrivate *priv = shell->priv;
	GtkWidget *main_window = NULL;
	GtkWidget *widget;

	g_return_val_if_fail (GS_IS_SHELL (shell), NULL);

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->cancellable = g_object_ref (cancellable);

	/* get UI */
	priv->builder = gtk_builder_new_from_resource ("/org/gnome/software/gnome-software.ui");

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GS_DATA G_DIR_SEPARATOR_S "icons");

        /* fix up the header bar */
	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_software"));
        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
        g_object_ref (widget);
        gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (widget)), widget);
        gtk_window_set_titlebar (GTK_WINDOW (main_window), widget);
        g_object_unref (widget);

	/* setup buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_back_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_all"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_SHELL_MODE_OVERVIEW));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_overview_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_SHELL_MODE_INSTALLED));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_overview_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_SHELL_MODE_UPDATES));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_overview_button_cb), shell);

	gs_shell_overview_setup (priv->shell_overview,
                                 shell,
				 priv->plugin_loader,
				 priv->builder,
				 priv->cancellable);
	gs_shell_updates_setup (priv->shell_updates,
				priv->plugin_loader,
				priv->builder,
				priv->cancellable);
	gs_shell_installed_setup (priv->shell_installed,
				  priv->plugin_loader,
				  priv->builder,
				  priv->cancellable);
	gs_shell_details_setup (priv->shell_details,
				priv->plugin_loader,
				priv->builder,
				priv->cancellable);
	gs_shell_category_setup (priv->shell_category,
                                 shell,
				 priv->builder);

	/* show main UI */
        gs_shell_set_mode (shell, GS_SHELL_MODE_OVERVIEW);

	return GTK_WINDOW (main_window);
}

/**
 * gs_shell_set_mode:
 **/
void
gs_shell_set_mode (GsShell *shell, GsShellMode mode)
{
        gs_shell_set_overview_mode (shell, mode, NULL, NULL);
}

void
gs_shell_show_details (GsShell *shell, GsApp *app)
{
        gs_shell_set_overview_mode (shell, GS_SHELL_MODE_DETAILS, app, NULL);
}

void
gs_shell_show_category (GsShell *shell, const gchar *category)
{
        gs_shell_set_overview_mode (shell, GS_SHELL_MODE_CATEGORY, NULL, category);
}

/**
 * gs_shell_class_init:
 **/
static void
gs_shell_class_init (GsShellClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_finalize;

	g_type_class_add_private (klass, sizeof (GsShellPrivate));
}

/**
 * gs_shell_init:
 **/
static void
gs_shell_init (GsShell *shell)
{
	shell->priv = GS_SHELL_GET_PRIVATE (shell);
	shell->priv->shell_overview = gs_shell_overview_new ();
	shell->priv->shell_updates = gs_shell_updates_new ();
	shell->priv->shell_installed = gs_shell_installed_new ();
	shell->priv->shell_details = gs_shell_details_new ();
	shell->priv->shell_category = gs_shell_category_new ();
	shell->priv->ignore_primary_buttons = FALSE;
}

/**
 * gs_shell_finalize:
 **/
static void
gs_shell_finalize (GObject *object)
{
	GsShell *shell = GS_SHELL (object);
	GsShellPrivate *priv = shell->priv;

	g_object_unref (priv->builder);
	g_object_unref (priv->cancellable);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->shell_overview);
	g_object_unref (priv->shell_updates);
	g_object_unref (priv->shell_installed);
	g_object_unref (priv->shell_details);
	g_object_unref (priv->shell_overview);

	G_OBJECT_CLASS (gs_shell_parent_class)->finalize (object);
}

/**
 * gs_shell_new:
 **/
GsShell *
gs_shell_new (void)
{
	GsShell *shell;
	shell = g_object_new (GS_TYPE_SHELL, NULL);
	return GS_SHELL (shell);
}
