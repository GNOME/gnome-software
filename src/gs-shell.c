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
#include "gs-shell-installed.h"
#include "gs-shell-overview.h"
#include "gs-shell-updates.h"

static void	gs_shell_finalize	(GObject	*object);

#define GS_SHELL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL, GsShellPrivate))

struct GsShellPrivate
{
	gboolean		 ignore_primary_buttons;
	GCancellable		*cancellable;
	GsPluginLoader		*plugin_loader;
	GsShellInstalled	*shell_installed;
	GsShellMode		 app_startup_mode;
	GsShellMode		 mode;
	GsShellOverview		*shell_overview;
	GsShellUpdates		*shell_updates;
	GtkBuilder		*builder;
	guint			 tab_back_id;
};

G_DEFINE_TYPE (GsShell, gs_shell, G_TYPE_OBJECT)

static void gs_shell_set_overview_mode_ui (GsShell *shell, GsShellMode mode, GsApp *app);
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

#if 0
/**
 * gs_shell_show_waiting_tab_cb:
 **/
static gboolean
gs_shell_show_waiting_tab_cb (gpointer user_data)
{
	GsShell *shell = (GsShell *) user_data;
	gs_shell_set_overview_mode_ui (shell, GS_SHELL_MODE_WAITING, NULL);
	priv->waiting_tab_id = 0;
	return FALSE;
}
#endif

/**
 * gs_shell_set_overview_mode_ui:
 **/
static void
gs_shell_set_overview_mode_ui (GsShell *shell, GsShellMode mode, GsApp *app)
{
	GtkWidget *widget;
	GsAppState state;
	GsShellPrivate *priv = shell->priv;

	priv->ignore_primary_buttons = TRUE;

	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
	case GS_SHELL_MODE_INSTALLED:
	case GS_SHELL_MODE_UPDATES:
	case GS_SHELL_MODE_WAITING:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
		gtk_widget_set_visible (widget, FALSE);
		break;

	case GS_SHELL_MODE_DETAILS:
	case GS_SHELL_MODE_CATEGORY:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
		gtk_widget_set_visible (widget, TRUE);
		if (app) {
			state = gs_app_get_state (app);
		}
		else {
			state = GS_APP_STATE_UNKNOWN;
		}
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
		gtk_widget_set_visible (widget, state == GS_APP_STATE_AVAILABLE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
		gtk_widget_set_visible (widget, state == GS_APP_STATE_INSTALLED);
#ifdef SEARCH
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_set_visible (widget, FALSE);
#endif
		break;

	default:
		g_assert_not_reached ();
		break;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_all"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_OVERVIEW);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_INSTALLED);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_UPDATES);
	priv->ignore_primary_buttons = FALSE;

	widget = NULL;

	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
#ifdef SEARCH
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_entry_set_text (GTK_ENTRY (widget), "");
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_show (widget);
#endif
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_stop (GTK_SPINNER (widget));
		break;

	case GS_SHELL_MODE_INSTALLED:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
#ifdef SEARCH
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_entry_set_text (GTK_ENTRY (widget), "");
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_show (widget);
#endif
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_stop (GTK_SPINNER (widget));
		break;

	case GS_SHELL_MODE_UPDATES:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_show (widget);
#ifdef SEARCH
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_hide (widget);
#endif
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_stop (GTK_SPINNER (widget));
		break;

	case GS_SHELL_MODE_WAITING:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_start (GTK_SPINNER (widget));
		break;

	case GS_SHELL_MODE_DETAILS:
	case GS_SHELL_MODE_CATEGORY:
		break;
	default:
		g_assert_not_reached ();
	}

	/* set panel */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_main"));
	gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), mode);
}

/**
 * gs_shell_set_overview_mode:
 **/
static void
gs_shell_set_overview_mode (GsShell *shell, GsShellMode mode, GsApp *app, const gchar *category)
{
	const gchar *tmp;
	GdkPixbuf *pixbuf;
	GsShellPrivate *priv = shell->priv;
	GtkWidget *widget;
	GtkWidget *widget2;

	if (priv->ignore_primary_buttons)
		return;

	/* set controls */
	gs_shell_set_overview_mode_ui (shell, mode, app);

	/* do action for mode */
	priv->mode = mode;
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
		gs_shell_overview_refresh (priv->shell_overview, priv->cancellable);
		break;
	case GS_SHELL_MODE_INSTALLED:
		gs_shell_installed_refresh (priv->shell_installed, priv->cancellable);
		break;
	case GS_SHELL_MODE_UPDATES:
		gs_shell_updates_refresh (priv->shell_updates, priv->cancellable);
		break;
	case GS_SHELL_MODE_WAITING:
		break;
	case GS_SHELL_MODE_DETAILS:
		tmp = gs_app_get_name (app);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_title"));
		widget2 = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
		if (tmp != NULL && tmp[0] != '\0') {
			gtk_label_set_label (GTK_LABEL (widget), tmp);
			gtk_label_set_label (GTK_LABEL (widget2), tmp);
			gtk_widget_set_visible (widget, TRUE);
		}
		else {
			gtk_widget_set_visible (widget, FALSE);
			gtk_label_set_label (GTK_LABEL (widget2), "");
		}
		tmp = gs_app_get_summary (app);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_summary"));
		if (tmp != NULL && tmp[0] != '\0') {
			gtk_label_set_label (GTK_LABEL (widget), tmp);
			gtk_widget_set_visible (widget, TRUE);
		}
		else {
			gtk_widget_set_visible (widget, FALSE);
		}
		tmp = gs_app_get_description (app);
		if (tmp == NULL)
			tmp = _("The author of this software has not included a 'Description' in the desktop file...");

		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_description"));
		gtk_label_set_label (GTK_LABEL (widget), tmp);
		gtk_widget_set_visible (widget, TRUE);

		pixbuf = gs_app_get_pixbuf (app);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_icon"));
		if (pixbuf != NULL) {
			gtk_image_set_from_pixbuf (GTK_IMAGE (widget), pixbuf);
			gtk_widget_set_visible (widget, TRUE);
		}
		else {
			gtk_widget_set_visible (widget, FALSE);
		}

		tmp = gs_app_get_url (app);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_url"));
		if (tmp != NULL && tmp[0] != '\0') {
			gtk_link_button_set_uri (GTK_LINK_BUTTON (widget), tmp);
			gtk_widget_set_visible (widget, TRUE);
		}
		else {
			gtk_widget_set_visible (widget, FALSE);
		}

		break;
	case GS_SHELL_MODE_CATEGORY:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
		gtk_label_set_label (GTK_LABEL (widget), category);
		gs_shell_overview_set_category (priv->shell_overview, category);
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

#if 0
/**
 * gs_shell_refresh:
 **/
void
gs_shell_refresh (GsShell *shell, GCancellable *cancellable)
{
	GsShellPrivate *priv = shell->priv;

}
#endif

/**
 * gs_shell_set_overview_mode_cb:
 **/
static void
gs_shell_set_overview_mode_cb (GsShellOverview *shell_overview,
			       GsShellMode mode,
			       GsApp *app,
			       const gchar *cat,
			       GsShell *shell)
{
	g_return_if_fail (GS_IS_SHELL (shell));
	gs_shell_set_overview_mode (shell, mode, app, cat);
}

/**
 * gs_shell_setup:
 */
GtkWindow *
gs_shell_setup (GsShell *shell, GsPluginLoader *plugin_loader, GCancellable *cancellable)
{
	GError *error = NULL;
	gint retval;
	GsShellPrivate *priv = shell->priv;
	GtkWidget *main_window = NULL;
	GtkWidget *widget;

	g_return_val_if_fail (GS_IS_SHELL (shell), NULL);

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->cancellable = g_object_ref (cancellable);

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

	/* Hide window first so that the dialogue resizes itself without redrawing */
	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_software"));
	gtk_widget_hide (main_window);
	gtk_window_set_default_size (GTK_WINDOW (main_window), 1200, 800);

	/* setup callbacks */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_main"));
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (widget), FALSE);

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

	gs_shell_updates_setup (priv->shell_updates,
				priv->plugin_loader,
				priv->builder);
	gs_shell_installed_setup (priv->shell_installed,
				  priv->plugin_loader,
				  priv->builder);
	gs_shell_overview_setup (priv->shell_overview,
				 priv->plugin_loader,
				 priv->builder);
	g_signal_connect (priv->shell_overview, "set-overview-mode",
			  G_CALLBACK (gs_shell_set_overview_mode_cb), shell);

	/* show main UI */
	gtk_widget_show (main_window);
	gs_shell_set_overview_mode (shell, priv->app_startup_mode, NULL, NULL);
out:
	return GTK_WINDOW (main_window);
}

/**
 * gs_shell_set_default_mode:
 **/
void
gs_shell_set_default_mode (GsShell *shell, GsShellMode mode)
{
	shell->priv->app_startup_mode = mode;
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
	shell->priv->shell_updates = gs_shell_updates_new ();
	shell->priv->shell_installed = gs_shell_installed_new ();
	shell->priv->shell_overview = gs_shell_overview_new ();
	shell->priv->app_startup_mode = GS_SHELL_MODE_OVERVIEW;
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
