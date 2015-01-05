/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "gs-cleanup.h"
#include "gs-utils.h"
#include "gs-shell.h"
#include "gs-shell-details.h"
#include "gs-shell-installed.h"
#include "gs-shell-search.h"
#include "gs-shell-overview.h"
#include "gs-shell-updates.h"
#include "gs-shell-category.h"
#include "gs-sources-dialog.h"
#include "gs-update-dialog.h"

static const gchar *page_name[] = {
	"overview",
	"installed",
	"search",
	"updates",
	"details",
	"category",
};

static void	gs_shell_finalize	(GObject	*object);

typedef struct {
	GsShellMode	 mode;
	GtkWidget	*focus;
	GsApp		*app;
	GsCategory	*category;
} BackEntry;

struct GsShellPrivate
{
	gboolean		 ignore_primary_buttons;
	GCancellable		*cancellable;
	GsPluginLoader		*plugin_loader;
	GsShellMode		 mode;
	GsShellOverview		*shell_overview;
	GsShellInstalled	*shell_installed;
	GsShellSearch		*shell_search;
	GsShellUpdates		*shell_updates;
	GsShellDetails		*shell_details;
	GsShellCategory		*shell_category;
	GtkBuilder		*builder;
	GtkWindow		*main_window;
	GQueue			*back_entry_stack;
	gboolean		 ignore_next_search_changed_signal;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsShell, gs_shell, G_TYPE_OBJECT)

enum {
	SIGNAL_LOADED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_shell_is_active:
 **/
gboolean
gs_shell_is_active (GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	return gtk_window_is_active (priv->main_window);
}

/**
 * gs_shell_get_window:
 **/
GtkWindow *
gs_shell_get_window (GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	return priv->main_window;
}

/**
 * gs_shell_activate:
 **/
void
gs_shell_activate (GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	gtk_window_present (priv->main_window);
}

static void
gs_shell_change_mode (GsShell *shell,
		      GsShellMode mode,
		      GsApp *app,
		      gpointer data,
		      gboolean scroll_up)
{
	GsShellPrivate *priv = shell->priv;
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_spinner_start"));
	gtk_spinner_stop (GTK_SPINNER (widget));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_select"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_selection_menu_button"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	gtk_widget_hide (widget);

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
	priv->ignore_primary_buttons = FALSE;

	/* switch page */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_main"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), page_name[mode]);

	/* do action for mode */
	priv->mode = mode;
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
		gs_shell_overview_switch_to (priv->shell_overview, scroll_up);
		break;
	case GS_SHELL_MODE_INSTALLED:
		gs_shell_installed_switch_to (priv->shell_installed, scroll_up);
		break;
	case GS_SHELL_MODE_SEARCH:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		text = gtk_entry_get_text (GTK_ENTRY (widget));
		gs_shell_search_switch_to (priv->shell_search, text, scroll_up);
		break;
	case GS_SHELL_MODE_UPDATES:
		gs_shell_updates_switch_to (priv->shell_updates, scroll_up);
		break;
	case GS_SHELL_MODE_DETAILS:
		if (app != NULL) {
			gs_shell_details_set_app (priv->shell_details, app);
			gs_shell_details_switch_to (priv->shell_details);
		}
		if (data != NULL)
			gs_shell_details_set_filename (priv->shell_details, data);
		break;
	case GS_SHELL_MODE_CATEGORY:
		gs_shell_category_set_category (priv->shell_category,
						GS_CATEGORY (data));
		gs_shell_category_switch_to (priv->shell_category);
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
	gs_shell_change_mode (shell, mode, NULL, NULL, TRUE);
}

static void
save_back_entry (GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	BackEntry *entry;
	GtkWidget *focus;

	entry = g_new0 (BackEntry, 1);
	entry->mode = priv->mode;

	focus = gtk_window_get_focus (priv->main_window);
	if (focus != NULL)
		entry->focus = g_object_ref (focus);

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
	g_clear_object (&entry->focus);
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
	GsShellPrivate *priv = shell->priv;
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

	g_signal_handlers_disconnect_by_func (shell_overview, initial_overview_load_done, data);

	gs_shell_updates_reload (shell->priv->shell_updates);
	gs_shell_installed_reload (shell->priv->shell_installed);

	g_signal_emit (shell, signals[SIGNAL_LOADED], 0);
}

static void
gs_shell_search_activated_cb (GtkEntry *entry, GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	const gchar *text;

	text = gtk_entry_get_text (entry);
	if (text[0] == '\0')
		return;

	if (gs_shell_get_mode (shell) == GS_SHELL_MODE_SEARCH) {
		gs_shell_search_switch_to (priv->shell_search, text, TRUE);
	} else {
		gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH, NULL, NULL, TRUE);
	}
}

static gboolean
is_keynav_event (GdkEvent *event, guint keyval)
{
	GdkModifierType state = 0;

	gdk_event_get_state (event, &state);

	if (keyval == GDK_KEY_Tab ||
	    keyval == GDK_KEY_KP_Tab ||
	    keyval == GDK_KEY_Up ||
	    keyval == GDK_KEY_KP_Up ||
	    keyval == GDK_KEY_Down ||
	    keyval == GDK_KEY_KP_Down ||
	    keyval == GDK_KEY_Left ||
	    keyval == GDK_KEY_KP_Left ||
	    keyval == GDK_KEY_Right ||
	    keyval == GDK_KEY_KP_Right ||
	    keyval == GDK_KEY_Home ||
	    keyval == GDK_KEY_KP_Home ||
	    keyval == GDK_KEY_End ||
	    keyval == GDK_KEY_KP_End ||
	    keyval == GDK_KEY_Page_Up ||
	    keyval == GDK_KEY_KP_Page_Up ||
	    keyval == GDK_KEY_Page_Down ||
	    keyval == GDK_KEY_KP_Page_Down ||
	    ((state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) != 0))
		return TRUE;

	return FALSE;
}

static gboolean
entry_keypress_handler (GtkWidget *widget, GdkEvent *event, GsShell *shell)
{
	guint keyval;
	GtkWidget *entry;

	if (!gdk_event_get_keyval (event, &keyval) ||
	    keyval != GDK_KEY_Escape)
		return GDK_EVENT_PROPAGATE;

	entry = GTK_WIDGET (gtk_builder_get_object (shell->priv->builder, "entry_search"));
	gtk_entry_set_text (GTK_ENTRY (entry), "");

	return GDK_EVENT_STOP;
}

static void
preedit_changed_cb (GtkEntry *entry, GtkWidget *popup, gboolean *preedit_changed)
{
	*preedit_changed = TRUE;
}

static gboolean
window_keypress_handler (GtkWidget *window, GdkEvent *event, GsShell *shell)
{
	GtkWidget *entry;
	guint keyval;
	gboolean handled;
	gboolean preedit_changed;
	guint preedit_change_id;
	gboolean res;
	_cleanup_free_ gchar *old_text = NULL;
	_cleanup_free_ gchar *new_text = NULL;

	if (gs_shell_get_mode (shell) != GS_SHELL_MODE_OVERVIEW &&
	    gs_shell_get_mode (shell) != GS_SHELL_MODE_SEARCH)
		return GDK_EVENT_PROPAGATE;

	if (!gdk_event_get_keyval (event, &keyval) ||
	    is_keynav_event (event, keyval) ||
	    keyval == GDK_KEY_space ||
	    keyval == GDK_KEY_Menu)
		return GDK_EVENT_PROPAGATE;

	entry = GTK_WIDGET (gtk_builder_get_object (shell->priv->builder, "entry_search"));

	handled = GDK_EVENT_PROPAGATE;
	preedit_changed = FALSE;
	preedit_change_id = g_signal_connect (entry, "preedit-changed",
					      G_CALLBACK (preedit_changed_cb), &preedit_changed);

	old_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	res = gtk_widget_event (entry, event);
	new_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));

	g_signal_handler_disconnect (entry, preedit_change_id);

	if ((res && g_strcmp0 (new_text, old_text) != 0) ||
	    preedit_changed)
		handled = GDK_EVENT_STOP;

	/* We set "editable" so the text in the  entry won't get selected on focus */
	g_object_set (entry, "editable", FALSE, NULL);
	gtk_widget_grab_focus (entry);
	g_object_set (entry, "editable", TRUE, NULL);

	return handled;
}

static void
search_changed_handler (GObject *entry, GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
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
		if (gs_shell_get_mode (shell) != GS_SHELL_MODE_SEARCH)
			gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH, NULL, NULL, TRUE);
		else
			gs_shell_search_switch_to (shell->priv->shell_search, text, TRUE);
	}
}

static gboolean
window_key_press_event (GtkWidget *win, GdkEventKey *event, GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
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
main_window_closed_cb (GtkWidget *dialog, GdkEvent *event, gpointer user_data)
{
	GsShell *shell = user_data;
	GsShellPrivate *priv = shell->priv;
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
	GsShellPrivate *priv = shell->priv;
	gs_shell_category_reload (priv->shell_category);
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
	GsShellPrivate *priv = shell->priv;
	gs_plugin_loader_set_scale (priv->plugin_loader,
				    gtk_widget_get_scale_factor (widget));
}

/**
 * gs_shell_setup:
 */
void
gs_shell_setup (GsShell *shell, GsPluginLoader *plugin_loader, GCancellable *cancellable)
{
	GsShellPrivate *priv = shell->priv;
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL (shell));

	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (priv->plugin_loader, "updates-changed",
			  G_CALLBACK (gs_shell_updates_changed_cb), shell);
	priv->cancellable = g_object_ref (cancellable);

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
	g_object_ref (widget);
	gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (widget)), widget);
	gtk_window_set_titlebar (GTK_WINDOW (priv->main_window), widget);
	g_object_unref (widget);

	/* global keynav */
	g_signal_connect_after (priv->main_window, "key_press_event",
				G_CALLBACK (window_key_press_event), shell);

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

	/* set up search */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (GTK_EDITABLE (widget), "activate",
			  G_CALLBACK (gs_shell_search_activated_cb), shell);
	g_signal_connect (priv->main_window, "key-press-event",
			  G_CALLBACK (window_keypress_handler), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (entry_keypress_handler), shell);
	g_signal_connect (widget, "search-changed",
			  G_CALLBACK (search_changed_handler), shell);

	/* load content */
	g_signal_connect (priv->shell_overview, "refreshed",
			  G_CALLBACK (initial_overview_load_done), shell);
}

/**
 * gs_shell_set_mode:
 **/
void
gs_shell_set_mode (GsShell *shell, GsShellMode mode)
{
	guint matched;

	/* if we're loading a different mode at startup then don't wait for
	 * the overview page to load before showing content */
	if (mode != GS_SHELL_MODE_OVERVIEW) {
		matched = g_signal_handlers_disconnect_by_func (shell->priv->shell_overview,
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
	GsShellPrivate *priv = shell->priv;

	return priv->mode;
}

const gchar *
gs_shell_get_mode_string (GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	return page_name[priv->mode];
}

static void
gs_shell_get_installed_updates_cb (GsPluginLoader *plugin_loader,
				   GAsyncResult *res,
				   GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	GList *list;
	GtkWidget *dialog;
	_cleanup_error_free_ GError *error = NULL;

	/* get the results */
	list = gs_plugin_loader_get_updates_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_NO_RESULTS)) {
			g_debug ("no updates to show");
		} else if (g_error_matches (error,
					    G_IO_ERROR,
					    G_IO_ERROR_CANCELLED)) {
			g_debug ("get updates cancelled");
		} else {
			g_warning ("failed to get updates: %s", error->message);
		}
		goto out;
	}

	dialog = gs_update_dialog_new ();
	gs_update_dialog_show_installed_updates (GS_UPDATE_DIALOG (dialog), list);

	gtk_window_set_transient_for (GTK_WINDOW (dialog), priv->main_window);
	gtk_window_present (GTK_WINDOW (dialog));

out:
	gs_plugin_list_free (list);
}


void
gs_shell_show_installed_updates (GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	guint64 refine_flags;

	refine_flags = GS_PLUGIN_REFINE_FLAGS_DEFAULT |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
		       GS_PLUGIN_REFINE_FLAGS_USE_HISTORY;

	gs_plugin_loader_get_updates_async (priv->plugin_loader,
					    refine_flags,
					    priv->cancellable,
					    (GAsyncReadyCallback) gs_shell_get_installed_updates_cb,
					    shell);
}

void
gs_shell_show_sources (GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	GtkWidget *dialog;

	dialog = gs_sources_dialog_new (priv->main_window, priv->plugin_loader);
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy),
				  dialog);
	gtk_window_present (GTK_WINDOW (dialog));
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

void
gs_shell_show_search (GsShell *shell, const gchar *search)
{
	GsShellPrivate *priv = shell->priv;
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
	GsShellPrivate *priv = shell->priv;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));

	/* ignore next "search-changed" signal to avoid getting a callback
	 * after 150 ms and messing up the state */
	priv->ignore_next_search_changed_signal = TRUE;
	gtk_entry_set_text (GTK_ENTRY (widget), search);

	gs_shell_search_set_appid_to_show (priv->shell_search, id);
	gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH, NULL, NULL, TRUE);
}

void
gs_shell_show_details (GsShell *shell, const gchar *id)
{
	_cleanup_object_unref_ GsApp *app = NULL;
	app = gs_app_new (id);
	gs_shell_show_app (shell, app);
}

/**
 * gs_shell_class_init:
 **/
static void
gs_shell_class_init (GsShellClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_finalize;

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
	shell->priv = gs_shell_get_instance_private (shell);
	shell->priv->back_entry_stack = g_queue_new ();
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

	g_queue_free_full (priv->back_entry_stack, (GDestroyNotify) free_back_entry);
	g_object_unref (priv->builder);
	g_object_unref (priv->cancellable);
	g_object_unref (priv->plugin_loader);

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

/* vim: set noexpandtab: */
