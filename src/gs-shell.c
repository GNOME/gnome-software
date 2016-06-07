/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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

#include <string.h>
#include <glib/gi18n.h>

#include "gs-common.h"
#include "gs-shell.h"
#include "gs-shell-details.h"
#include "gs-shell-installed.h"
#include "gs-shell-moderate.h"
#include "gs-shell-loading.h"
#include "gs-shell-search.h"
#include "gs-shell-overview.h"
#include "gs-shell-updates.h"
#include "gs-shell-category.h"
#include "gs-shell-extras.h"
#include "gs-sources-dialog.h"
#include "gs-update-dialog.h"
#include "gs-update-monitor.h"

static const gchar *page_name[] = {
	"unknown",
	"overview",
	"installed",
	"search",
	"updates",
	"details",
	"category",
	"extras",
	"moderate",
	"loading",
};

typedef struct {
	GsShellMode	 mode;
	GtkWidget	*focus;
	GsApp		*app;
	GsCategory	*category;
} BackEntry;

typedef struct
{
	gboolean		 ignore_primary_buttons;
	GCancellable		*cancellable;
	GsPluginLoader		*plugin_loader;
	GsShellMode		 mode;
	GsShellOverview		*shell_overview;
	GsShellInstalled	*shell_installed;
	GsShellModerate		*shell_moderate;
	GsShellLoading		*shell_loading;
	GsShellSearch		*shell_search;
	GsShellUpdates		*shell_updates;
	GsShellDetails		*shell_details;
	GsShellCategory		*shell_category;
	GsShellExtras		*shell_extras;
	GtkWidget		*header_start_widget;
	GtkWidget		*header_end_widget;
	GtkBuilder		*builder;
	GtkWindow		*main_window;
	GQueue			*back_entry_stack;
	gboolean		 ignore_next_search_changed_signal;
	GPtrArray		*modal_dialogs;
} GsShellPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsShell, gs_shell, G_TYPE_OBJECT)

enum {
	SIGNAL_LOADED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

static void
modal_dialog_unmapped_cb (GtkWidget *dialog,
                          GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_debug ("modal dialog %p unmapped", dialog);
	g_ptr_array_remove (priv->modal_dialogs, dialog);
}

/**
 * gs_shell_modal_dialog_present:
 **/
void
gs_shell_modal_dialog_present (GsShell *shell, GtkDialog *dialog)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWindow *parent;

	/* show new modal on top of old modal */
	if (priv->modal_dialogs->len > 0) {
		parent = g_ptr_array_index (priv->modal_dialogs,
					    priv->modal_dialogs->len - 1);
		g_debug ("using old modal %p as parent", parent);
	} else {
		parent = priv->main_window;
		g_debug ("using main window");
	}
	gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

	/* add to stack, transfer ownership to here */
	g_ptr_array_add (priv->modal_dialogs, dialog);
	g_signal_connect (GTK_WIDGET (dialog), "unmap",
	                  G_CALLBACK (modal_dialog_unmapped_cb), shell);

	/* present the new one */
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_present (GTK_WINDOW (dialog));
}

/**
 * gs_shell_is_active:
 **/
gboolean
gs_shell_is_active (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	return gtk_window_is_active (priv->main_window);
}

/**
 * gs_shell_get_window:
 **/
GtkWindow *
gs_shell_get_window (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	return priv->main_window;
}

/**
 * gs_shell_activate:
 **/
void
gs_shell_activate (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gtk_window_present (priv->main_window);
}

static void
gs_shell_set_header_start_widget (GsShell *shell, GtkWidget *widget)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *old_widget;
	GtkWidget *header;

	old_widget = priv->header_start_widget;
	header = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));

	if (priv->header_start_widget == widget)
		return;

	if (widget != NULL) {
		g_object_ref (widget);
		gtk_header_bar_pack_start (GTK_HEADER_BAR (header), widget);
	}

	priv->header_start_widget = widget;

	if (old_widget != NULL) {
		gtk_container_remove (GTK_CONTAINER (header), old_widget);
		g_object_unref (old_widget);
	}
}

static void
gs_shell_set_header_end_widget (GsShell *shell, GtkWidget *widget)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *old_widget;
	GtkWidget *header;

	old_widget = priv->header_end_widget;
	header = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));

	if (priv->header_end_widget == widget)
		return;

	if (widget != NULL) {
		g_object_ref (widget);
		gtk_header_bar_pack_end (GTK_HEADER_BAR (header), widget);
	}

	priv->header_end_widget = widget;

	if (old_widget != NULL) {
		gtk_container_remove (GTK_CONTAINER (header), old_widget);
		g_object_unref (old_widget);
	}
}

void
gs_shell_change_mode (GsShell *shell,
		      GsShellMode mode,
		      GsApp *app,
		      gpointer data,
		      gboolean scroll_up)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GsPage *new_page;
	GtkWidget *widget;
	const gchar *text;
	GtkStyleContext *context;

	if (priv->ignore_primary_buttons)
		return;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
	gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (widget), TRUE);

	/* hide all mode specific header widgets here, they will be shown in the
	 * refresh functions
	 */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_selection_menu_button"));
	gtk_widget_hide (widget);

	/* hide unless we're going to search */
	if (mode != GS_SHELL_MODE_SEARCH) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (widget), FALSE);
	}

	context = gtk_widget_get_style_context (GTK_WIDGET (gtk_builder_get_object (priv->builder, "header")));
	gtk_style_context_remove_class (context, "selection-mode");
	/* set the window title back to default */
	/* TRANSLATORS: this is the main window title */
	gtk_window_set_title (priv->main_window, _("Software"));

	/* show the back button if needed */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	gtk_widget_set_visible (widget, !g_queue_is_empty (priv->back_entry_stack));

	/* update main buttons according to mode */
	priv->ignore_primary_buttons = TRUE;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_all"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_OVERVIEW);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_INSTALLED);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_UPDATES);
	gtk_widget_set_visible (widget, !gs_update_monitor_is_managed() || mode == GS_SHELL_MODE_UPDATES);

	priv->ignore_primary_buttons = FALSE;

	/* switch page */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_main"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), page_name[mode]);

	/* do action for mode */
	priv->mode = mode;
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
		new_page = GS_PAGE (priv->shell_overview);
		break;
	case GS_SHELL_MODE_INSTALLED:
		new_page = GS_PAGE (priv->shell_installed);
		break;
	case GS_SHELL_MODE_MODERATE:
		new_page = GS_PAGE (priv->shell_moderate);
		break;
	case GS_SHELL_MODE_LOADING:
		new_page = GS_PAGE (priv->shell_loading);
		break;
	case GS_SHELL_MODE_SEARCH:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		text = gtk_entry_get_text (GTK_ENTRY (widget));
		gs_shell_search_set_text (priv->shell_search, text);
		new_page = GS_PAGE (priv->shell_search);
		break;
	case GS_SHELL_MODE_UPDATES:
		new_page = GS_PAGE (priv->shell_updates);
		break;
	case GS_SHELL_MODE_DETAILS:
		if (app != NULL)
			gs_shell_details_set_app (priv->shell_details, app);
		if (data != NULL)
			gs_shell_details_set_filename (priv->shell_details, data);
		new_page = GS_PAGE (priv->shell_details);
		break;
	case GS_SHELL_MODE_CATEGORY:
		gs_shell_category_set_category (priv->shell_category,
						GS_CATEGORY (data));
		new_page = GS_PAGE (priv->shell_category);
		break;
	case GS_SHELL_MODE_EXTRAS:
		new_page = GS_PAGE (priv->shell_extras);
		break;
	default:
		g_assert_not_reached ();
	}

	gs_page_switch_to (new_page, scroll_up);

	/* update header bar widgets */
	widget = gs_page_get_header_start_widget (new_page);
	gs_shell_set_header_start_widget (shell, widget);

	widget = gs_page_get_header_end_widget (new_page);
	gs_shell_set_header_end_widget (shell, widget);

	/* destroy any existing modals */
	if (priv->modal_dialogs != NULL)
		g_ptr_array_set_size (priv->modal_dialogs, 0);
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
	gs_shell_change_mode (shell, mode, NULL, NULL, TRUE);
}

static void
save_back_entry (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	BackEntry *entry;

	entry = g_new0 (BackEntry, 1);
	entry->mode = priv->mode;

	entry->focus = gtk_window_get_focus (priv->main_window);
	if (entry->focus != NULL)
		g_object_add_weak_pointer (G_OBJECT (entry->focus),
					   (gpointer *) &entry->focus);

	if (priv->mode == GS_SHELL_MODE_CATEGORY) {
		entry->category = gs_shell_category_get_category (priv->shell_category);
		g_object_ref (entry->category);
	}
	else if (priv->mode == GS_SHELL_MODE_DETAILS) {
		entry->app = gs_shell_details_get_app (priv->shell_details);
		g_object_ref (entry->app);
	}

	g_queue_push_head (priv->back_entry_stack, entry);
}

static void
free_back_entry (BackEntry *entry)
{
	if (entry->focus != NULL)
		g_object_remove_weak_pointer (G_OBJECT (entry->focus),
		                              (gpointer *) &entry->focus);
	g_clear_object (&entry->category);
	g_clear_object (&entry->app);
	g_free (entry);
}

/**
 * gs_shell_back_button_cb:
 **/
static void
gs_shell_back_button_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	BackEntry *entry;

	g_return_if_fail (!g_queue_is_empty (priv->back_entry_stack));

	entry = g_queue_pop_head (priv->back_entry_stack);

	gs_shell_change_mode (shell, entry->mode, entry->app, entry->category, FALSE);

	if (entry->focus != NULL)
		gtk_widget_grab_focus (entry->focus);

	free_back_entry (entry);
}

static void
initial_overview_load_done (GsShellOverview *shell_overview, gpointer data)
{
	GsShell *shell = data;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	g_signal_handlers_disconnect_by_func (shell_overview, initial_overview_load_done, data);

	gs_shell_updates_reload (priv->shell_updates);
	gs_shell_installed_reload (priv->shell_installed);

	g_signal_emit (shell, signals[SIGNAL_LOADED], 0);
}

static gboolean
window_keypress_handler (GtkWidget *window, GdkEvent *event, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	return gtk_search_bar_handle_event (GTK_SEARCH_BAR (widget), event);
}

static void
search_changed_handler (GObject *entry, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const gchar *text;

	if (priv->ignore_next_search_changed_signal) {
		priv->ignore_next_search_changed_signal = FALSE;
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (entry));

	if (text[0] == '\0' && gs_shell_get_mode (shell) == GS_SHELL_MODE_SEARCH) {
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, NULL, TRUE);
		return;
	}

	if (strlen(text) > 2) {
		if (gs_shell_get_mode (shell) != GS_SHELL_MODE_SEARCH) {
			gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH, NULL, NULL, TRUE);
		} else {
			gs_shell_search_set_text (priv->shell_search, text);
			gs_page_switch_to (GS_PAGE (priv->shell_search), TRUE);
		}
	}
}

static gboolean
window_key_press_event (GtkWidget *win, GdkEventKey *event, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GdkKeymap *keymap;
	GdkModifierType state;
	gboolean is_rtl;
	GtkWidget *button;

	button = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	if (!gtk_widget_is_visible (button) || !gtk_widget_is_sensitive (button))
	    	return GDK_EVENT_PROPAGATE;

	state = event->state;
	keymap = gdk_keymap_get_default ();
	gdk_keymap_add_virtual_modifiers (keymap, &state);
	state = state & gtk_accelerator_get_default_mod_mask ();
	is_rtl = gtk_widget_get_direction (button) == GTK_TEXT_DIR_RTL;

	if ((!is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Left) ||
	    (is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Right) ||
	    event->keyval == GDK_KEY_Back) {
		gtk_widget_activate (button);
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static gboolean
window_button_press_event (GtkWidget *win, GdkEventButton *event, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *button;

	/* Mouse hardware back button is 8 */
	if (event->button != 8)
		return GDK_EVENT_PROPAGATE;

	button = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	if (!gtk_widget_is_visible (button) || !gtk_widget_is_sensitive (button))
		return GDK_EVENT_PROPAGATE;

	gtk_widget_activate (button);
	return GDK_EVENT_STOP;
}

static gboolean
main_window_closed_cb (GtkWidget *dialog, GdkEvent *event, gpointer user_data)
{
	GsShell *shell = user_data;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	BackEntry *entry;

	/* When the window is closed, reset the initial mode to overview */
	priv->mode = GS_SHELL_MODE_OVERVIEW;

	/* ... and clear any remaining entries in the back button stack */
	while ((entry = g_queue_pop_head (priv->back_entry_stack)) != NULL) {
		free_back_entry (entry);
	}

	gtk_widget_hide (dialog);
	return TRUE;
}

/**
 * gs_shell_updates_changed_cb:
 */
static void
gs_shell_updates_changed_cb (GsPluginLoader *plugin_loader, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gs_shell_category_reload (priv->shell_category);
	gs_shell_extras_reload (priv->shell_extras);
	gs_shell_details_reload (priv->shell_details);
	gs_shell_installed_reload (priv->shell_installed);
	gs_shell_overview_reload (priv->shell_overview);
	gs_shell_search_reload (priv->shell_search);
	gs_shell_updates_reload (priv->shell_updates);
}

/**
 * gs_shell_main_window_mapped_cb:
 */
static void
gs_shell_main_window_mapped_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gs_plugin_loader_set_scale (priv->plugin_loader,
				    gtk_widget_get_scale_factor (widget));
}

static void
on_permission_changed (GPermission *permission,
                       GParamSpec  *pspec,
                       gpointer     data)
{
        GsShell *shell = data;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_widget_set_visible (widget, !gs_update_monitor_is_managed() || priv->mode == GS_SHELL_MODE_UPDATES);
}

static void
gs_shell_monitor_permission (GsShell *shell)
{
        GPermission *permission;

        permission = gs_update_monitor_permission_get ();
	if (permission != NULL)
		g_signal_connect (permission, "notify",
				  G_CALLBACK (on_permission_changed), shell);
}

/**
 * gs_shell_setup:
 */
void
gs_shell_setup (GsShell *shell, GsPluginLoader *plugin_loader, GCancellable *cancellable)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL (shell));

	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (priv->plugin_loader, "updates-changed",
			  G_CALLBACK (gs_shell_updates_changed_cb), shell);
	priv->cancellable = g_object_ref (cancellable);

	gs_shell_monitor_permission (shell);

	/* get UI */
	priv->builder = gtk_builder_new_from_resource ("/org/gnome/Software/gnome-software.ui");
	priv->main_window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	g_signal_connect (priv->main_window, "map",
			  G_CALLBACK (gs_shell_main_window_mapped_cb), shell);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GS_DATA G_DIR_SEPARATOR_S "icons");

	g_signal_connect (priv->main_window, "delete-event",
			  G_CALLBACK (main_window_closed_cb), shell);

	/* fix up the header bar */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
	if (gs_utils_is_current_desktop ("Unity")) {
		gtk_header_bar_set_decoration_layout (GTK_HEADER_BAR (widget), "");
	} else {
		g_object_ref (widget);
		gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (widget)), widget);
		gtk_window_set_titlebar (GTK_WINDOW (priv->main_window), widget);
		g_object_unref (widget);
	}

	/* global keynav */
	g_signal_connect_after (priv->main_window, "key_press_event",
				G_CALLBACK (window_key_press_event), shell);
	/* mouse hardware back button */
	g_signal_connect_after (priv->main_window, "button_press_event",
				G_CALLBACK (window_button_press_event), shell);

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

	priv->shell_overview = GS_SHELL_OVERVIEW (gtk_builder_get_object (priv->builder, "shell_overview"));
	gs_shell_overview_setup (priv->shell_overview,
				 shell,
				 priv->plugin_loader,
				 priv->builder,
				 priv->cancellable);
	priv->shell_updates = GS_SHELL_UPDATES (gtk_builder_get_object (priv->builder, "shell_updates"));
	gs_shell_updates_setup (priv->shell_updates,
				shell,
				priv->plugin_loader,
				priv->builder,
				priv->cancellable);
	priv->shell_installed = GS_SHELL_INSTALLED (gtk_builder_get_object (priv->builder, "shell_installed"));
	gs_shell_installed_setup (priv->shell_installed,
				  shell,
				  priv->plugin_loader,
				  priv->builder,
				  priv->cancellable);
	priv->shell_moderate = GS_SHELL_MODERATE (gtk_builder_get_object (priv->builder, "shell_moderate"));
	gs_shell_moderate_setup (priv->shell_moderate,
				 shell,
				 priv->plugin_loader,
				 priv->builder,
				 priv->cancellable);
	priv->shell_loading = GS_SHELL_LOADING (gtk_builder_get_object (priv->builder, "shell_loading"));
	gs_shell_loading_setup (priv->shell_loading,
				shell,
				priv->plugin_loader,
				priv->builder,
				priv->cancellable);
	priv->shell_search = GS_SHELL_SEARCH (gtk_builder_get_object (priv->builder, "shell_search"));
	gs_shell_search_setup (priv->shell_search,
			       shell,
			       priv->plugin_loader,
			       priv->builder,
			       priv->cancellable);
	priv->shell_details = GS_SHELL_DETAILS (gtk_builder_get_object (priv->builder, "shell_details"));
	gs_shell_details_setup (priv->shell_details,
				shell,
				priv->plugin_loader,
				priv->builder,
				priv->cancellable);
	priv->shell_category = GS_SHELL_CATEGORY (gtk_builder_get_object (priv->builder, "shell_category"));
	gs_shell_category_setup (priv->shell_category,
				 shell,
				 priv->plugin_loader,
				 priv->builder,
				 priv->cancellable);
	priv->shell_extras = GS_SHELL_EXTRAS (gtk_builder_get_object (priv->builder, "shell_extras"));
	gs_shell_extras_setup (priv->shell_extras,
				 shell,
				 priv->plugin_loader,
				 priv->builder,
				 priv->cancellable);

	/* set up search */
	g_signal_connect (priv->main_window, "key-press-event",
			  G_CALLBACK (window_keypress_handler), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (widget, "search-changed",
			  G_CALLBACK (search_changed_handler), shell);

	/* load content */
	g_signal_connect (priv->shell_loading, "refreshed",
			  G_CALLBACK (initial_overview_load_done), shell);
}

/**
 * gs_shell_set_mode:
 **/
void
gs_shell_set_mode (GsShell *shell, GsShellMode mode)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	guint matched;

	/* if we're loading a different mode at startup then don't wait for
	 * the overview page to load before showing content */
	if (mode != GS_SHELL_MODE_OVERVIEW) {
		matched = g_signal_handlers_disconnect_by_func (priv->shell_overview,
								initial_overview_load_done,
								shell);
		if (matched > 0)
			g_signal_emit (shell, signals[SIGNAL_LOADED], 0);
	}
	gs_shell_change_mode (shell, mode, NULL, NULL, TRUE);
}

GsShellMode
gs_shell_get_mode (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	return priv->mode;
}

const gchar *
gs_shell_get_mode_string (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	return page_name[priv->mode];
}

void
gs_shell_show_installed_updates (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *dialog;

	dialog = gs_update_dialog_new (priv->plugin_loader);
	gs_update_dialog_show_installed_updates (GS_UPDATE_DIALOG (dialog));

	gtk_window_set_transient_for (GTK_WINDOW (dialog), priv->main_window);
	gtk_window_present (GTK_WINDOW (dialog));
}

void
gs_shell_show_sources (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *dialog;

	/* use if available */
	if (g_spawn_command_line_async ("software-properties-gtk", NULL))
		return;

	dialog = gs_sources_dialog_new (priv->main_window, priv->plugin_loader);
	gs_shell_modal_dialog_present (shell, GTK_DIALOG (dialog));
}

void
gs_shell_show_app (GsShell *shell, GsApp *app)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS, app, NULL, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_category (GsShell *shell, GsCategory *category)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_CATEGORY, NULL, category, TRUE);
}

void gs_shell_show_extras_search (GsShell *shell, const gchar *mode, gchar **resources)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	save_back_entry (shell);
	gs_shell_extras_search (priv->shell_extras, mode, resources);
	gs_shell_change_mode (shell, GS_SHELL_MODE_EXTRAS, NULL, NULL, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_search (GsShell *shell, const gchar *search)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	gtk_entry_set_text (GTK_ENTRY (widget), search);
	gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH, NULL, NULL, TRUE);
}

void
gs_shell_show_filename (GsShell *shell, const gchar *filename)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS, NULL, (gpointer) filename, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_search_result (GsShell *shell, const gchar *id, const gchar *search)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));

	/* ignore next "search-changed" signal to avoid getting a callback
	 * after 150 ms and messing up the state */
	priv->ignore_next_search_changed_signal = TRUE;
	gtk_entry_set_text (GTK_ENTRY (widget), search);

	gs_shell_search_set_appid_to_show (priv->shell_search, id);
	gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH, NULL, NULL, TRUE);
}

/**
 * gs_shell_dispose:
 **/
static void
gs_shell_dispose (GObject *object)
{
	GsShell *shell = GS_SHELL (object);
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	if (priv->back_entry_stack != NULL) {
		g_queue_free_full (priv->back_entry_stack, (GDestroyNotify) free_back_entry);
		priv->back_entry_stack = NULL;
	}
	g_clear_object (&priv->builder);
	g_clear_object (&priv->cancellable);
	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->header_start_widget);
	g_clear_object (&priv->header_end_widget);
	g_clear_pointer (&priv->modal_dialogs, (GDestroyNotify) g_ptr_array_unref);

	G_OBJECT_CLASS (gs_shell_parent_class)->dispose (object);
}

/**
 * gs_shell_class_init:
 **/
static void
gs_shell_class_init (GsShellClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_shell_dispose;

	signals [SIGNAL_LOADED] =
		g_signal_new ("loaded",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsShellClass, loaded),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gs_shell_init:
 **/
static void
gs_shell_init (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	priv->back_entry_stack = g_queue_new ();
	priv->ignore_primary_buttons = FALSE;
	priv->modal_dialogs = g_ptr_array_new_with_free_func ((GDestroyNotify) gtk_widget_destroy);
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

/* vim: set noexpandtab: */
