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

#include "gs-utils.h"
#include "gs-shell.h"
#include "gs-shell-details.h"
#include "gs-shell-installed.h"
#include "gs-shell-search.h"
#include "gs-shell-overview.h"
#include "gs-shell-updates.h"
#include "gs-shell-category.h"

static const gchar *page_name[] = {
	"overview",
	"installed",
	"search",
	"updates",
	"details",
	"category",
	"updates"
};

static void	gs_shell_finalize	(GObject	*object);

#define GS_SHELL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL, GsShellPrivate))

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
	GSList			*back_entry_stack;
};

G_DEFINE_TYPE (GsShell, gs_shell, G_TYPE_OBJECT)

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
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (shell->priv->builder,
						     "window_software"));
	return gtk_window_is_active (window);
}

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

static void
gs_shell_change_mode (GsShell *shell,
		      GsShellMode mode,
		      GsApp *app,
		      gpointer data,
		      gboolean scroll_up)
{
	GsShellPrivate *priv = shell->priv;
	GtkWidget *widget;
	GtkWindow *window;
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_spinner_end"));
	gtk_spinner_stop (GTK_SPINNER (widget));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_label"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_select"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
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
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	/* TRANSLATORS: this is the main window title */
	gtk_window_set_title (window, _("Software"));

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
		gs_shell_overview_refresh (priv->shell_overview, scroll_up);
		break;
	case GS_SHELL_MODE_INSTALLED:
		gs_shell_installed_refresh (priv->shell_installed, scroll_up);
		break;
	case GS_SHELL_MODE_SEARCH:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		text = gtk_entry_get_text (GTK_ENTRY (widget));
		gs_shell_search_refresh (priv->shell_search, text, scroll_up);
		break;
	case GS_SHELL_MODE_UPDATES:
		gs_shell_updates_refresh (priv->shell_updates, FALSE, scroll_up);
		break;
	case GS_SHELL_MODE_UPDATED:
		gs_shell_updates_refresh (priv->shell_updates, TRUE, scroll_up);
		break;
	case GS_SHELL_MODE_DETAILS:
		if (app != NULL) {
			gs_shell_details_set_app (priv->shell_details, app);
			gs_shell_details_refresh (priv->shell_details);
		}
		if (data != NULL)
			gs_shell_details_set_filename (priv->shell_details, data);
		break;
	case GS_SHELL_MODE_CATEGORY:
		gs_shell_category_set_category (priv->shell_category,
						GS_CATEGORY (data));
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
	gs_shell_change_mode (shell, mode, NULL, NULL, TRUE);
}

static void
save_back_entry (GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	BackEntry *entry;
	GtkWidget *window;

	entry = g_new0 (BackEntry, 1);
	entry->mode = priv->mode;

	if (priv->mode == GS_SHELL_MODE_CATEGORY) {
		g_clear_object (&entry->category);
		entry->category = gs_shell_category_get_category (priv->shell_category);
		g_object_ref (entry->category);
	}
	else if (priv->mode == GS_SHELL_MODE_DETAILS) {
		g_clear_object (&entry->app);
		entry->app = gs_shell_details_get_app (priv->shell_details);
		g_object_ref (entry->app);
	}

	window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_software"));
	entry->focus = gtk_window_get_focus (GTK_WINDOW (window));

	priv->back_entry_stack = g_slist_prepend (priv->back_entry_stack, entry);
}

static void
free_back_entry (BackEntry *entry)
{
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

	g_assert (priv->back_entry_stack);
	entry = priv->back_entry_stack->data;
	priv->back_entry_stack = g_slist_remove (priv->back_entry_stack, entry);

	gs_shell_change_mode (shell, entry->mode, entry->app, entry->category, FALSE);

	if (entry->focus) {
		gtk_widget_grab_focus (entry->focus);
	}

	free_back_entry (entry);
}

static void
initial_overview_load_done (GsShellOverview *shell_overview, gpointer data)
{
	GsShell *shell = data;

	g_signal_handlers_disconnect_by_func (shell_overview, initial_overview_load_done, data);

	gs_shell_updates_refresh (shell->priv->shell_updates, FALSE, TRUE);
	gs_shell_installed_refresh (shell->priv->shell_installed, TRUE);

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
		gs_shell_search_refresh (priv->shell_search, text, TRUE);
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
	gchar *old_text, *new_text;

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

	g_free (old_text);
	g_free (new_text);

	return handled;
}

static void
text_changed_handler (GObject *entry, GParamSpec *pspec, GsShell *shell)
{
	const gchar *text;

	if (gs_shell_get_mode (shell) != GS_SHELL_MODE_SEARCH)
		return;

	text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (text[0] == '\0')
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, NULL, TRUE);
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

/**
 * gs_shell_sources_list_header_func
 **/
static void
gs_shell_sources_list_header_func (GtkListBoxRow *row,
				   GtkListBoxRow *before,
				   gpointer user_data)
{
	GtkWidget *header = NULL;
	if (before != NULL)
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

/**
 * gs_shell_sources_list_sort_func:
 **/
static gint
gs_shell_sources_list_sort_func (GtkListBoxRow *a,
				 GtkListBoxRow *b,
				 gpointer user_data)
{
	return a < b;
}

/**
 * gs_shell_sources_add_app:
 **/
static void
gs_shell_sources_add_app (GtkListBox *listbox, GsApp *app)
{
	GtkWidget *box;
	GtkWidget *widget;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_top (box, 12);
	gtk_widget_set_margin_start (box, 12);
	gtk_widget_set_margin_bottom (box, 12);
	gtk_widget_set_margin_end (box, 12);

	widget = gtk_label_new (gs_app_get_name (app));
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

	gtk_list_box_prepend (listbox, box);
	gtk_widget_show (widget);
	gtk_widget_show (box);
}

/**
 * gs_shell_sources_list_row_activated_cb:
 **/
static void
gs_shell_sources_list_row_activated_cb (GtkListBox *list_box,
					GtkListBoxRow *row,
					GsShell *shell)
{
	GPtrArray *related;
	GsApp *app;
	GsShellPrivate *priv = shell->priv;
	GtkWidget *widget;
	guint cnt_apps = 0;
	guint i;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_sources"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), "source-details");

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_sources_back"));
	gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "listbox_sources_apps"));
	gs_container_remove_all (GTK_CONTAINER (widget));
	app = GS_APP (g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), 
					 "GsShell::app"));
	related = gs_app_get_related (app);
	for (i = 0; i < related->len; i++) {
		app = g_ptr_array_index (related, i);
		switch (gs_app_get_kind (app)) {
		case GS_APP_KIND_NORMAL:
		case GS_APP_KIND_SYSTEM:
			gs_shell_sources_add_app (GTK_LIST_BOX (widget), app);
			cnt_apps++;
			break;
		default:
			break;
		}
	}

	/* save this */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_sources"));
	g_object_set_data_full (G_OBJECT (widget), "GsShell::app",
				g_object_ref (app),
				(GDestroyNotify) g_object_unref);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_sources_apps"));
	gtk_widget_set_visible (widget, cnt_apps != 0);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sources2"));
	gtk_widget_set_visible (widget, cnt_apps != 0);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_sources_none"));
	gtk_widget_set_visible (widget, cnt_apps == 0);
}

/**
 * gs_shell_sources_back_button_cb:
 **/
static void
gs_shell_sources_back_button_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_sources_back"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_sources"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), "sources");
}


/**
 * gs_shell_sources_app_removed_cb:
 **/
static void
gs_shell_sources_app_removed_cb (GObject *source,
				 GAsyncResult *res,
				 gpointer user_data)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShell *shell = GS_SHELL (user_data);
	GsShellPrivate *priv = shell->priv;
	GtkWidget *widget;
	gboolean ret;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to remove: %s", error->message);
		g_error_free (error);
	} else {
		gs_shell_show_sources (shell);
	}

	/* enable button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_sources_remove"));
	gtk_widget_set_sensitive (widget, TRUE);
}

/**
 * gs_shell_sources_remove_button_cb:
 **/
static void
gs_shell_sources_remove_button_cb (GtkWidget *widget, GsShell *shell)
{
	GsApp *app;
	GsShellPrivate *priv = shell->priv;

	/* disable button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_sources_remove"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* remove source */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_sources"));
	app = GS_APP (g_object_get_data (G_OBJECT (widget), "GsShell::app"));
	gs_plugin_loader_app_action_async (priv->plugin_loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_REMOVE,
					   priv->cancellable,
					   gs_shell_sources_app_removed_cb,
					   shell);
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

	g_signal_connect (main_window, "delete-event",
			  G_CALLBACK (gtk_widget_hide_on_delete), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
	g_object_ref (widget);
	gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (widget)), widget);
	gtk_window_set_titlebar (GTK_WINDOW (main_window), widget);
	g_object_unref (widget);

	/* fix icon in RTL */
	if (gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL)
	{
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "back_image"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "go-previous-rtl-symbolic", GTK_ICON_SIZE_MENU);
	}

	/* global keynav */
	g_signal_connect_after (main_window, "key_press_event",
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_sources_remove"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_sources_remove_button_cb), shell);

	gs_shell_overview_setup (priv->shell_overview,
				 shell,
				 priv->plugin_loader,
				 priv->builder,
				 priv->cancellable);
	gs_shell_updates_setup (priv->shell_updates,
				shell,
				priv->plugin_loader,
				priv->builder,
				priv->cancellable);
	gs_shell_installed_setup (priv->shell_installed,
				  shell,
				  priv->plugin_loader,
				  priv->builder,
				  priv->cancellable);
	gs_shell_search_setup (priv->shell_search,
			       shell,
			       priv->plugin_loader,
			       priv->builder,
			       priv->cancellable);
	gs_shell_details_setup (priv->shell_details,
				shell,
				priv->plugin_loader,
				priv->builder,
				priv->cancellable);
	gs_shell_category_setup (priv->shell_category,
				 shell,
				 priv->plugin_loader,
				 priv->builder,
				 priv->cancellable);

	/* set up search */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (GTK_EDITABLE (widget), "activate",
			  G_CALLBACK (gs_shell_search_activated_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_software"));
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (window_keypress_handler), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (entry_keypress_handler), shell);
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (text_changed_handler), shell);

	/* set up sources */
	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_sources"));
	g_signal_connect (main_window, "delete-event",
			  G_CALLBACK (gtk_widget_hide_on_delete), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_sources"));
	g_object_ref (widget);
	gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (widget)), widget);
	gtk_window_set_titlebar (GTK_WINDOW (main_window), widget);
	g_object_unref (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "listbox_sources"));
	gtk_list_box_set_header_func (GTK_LIST_BOX (widget),
				      gs_shell_sources_list_header_func,
				      shell,
				      NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (widget),
				    gs_shell_sources_list_sort_func,
				    shell, NULL);
	g_signal_connect (widget, "row-activated",
			  G_CALLBACK (gs_shell_sources_list_row_activated_cb), shell);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "listbox_sources_apps"));
	gtk_list_box_set_header_func (GTK_LIST_BOX (widget),
				      gs_shell_sources_list_header_func,
				      shell,
				      NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (widget),
				    gs_shell_sources_list_sort_func,
				    shell, NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_sources_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_sources_back_button_cb), shell);

	/* load content */
	g_signal_connect (priv->shell_overview, "refreshed",
			  G_CALLBACK (initial_overview_load_done), shell);

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_software"));
	return GTK_WINDOW (main_window);
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

/**
 * gs_shell_sources_add_source:
 **/
static void
gs_shell_sources_add_source (GtkListBox *listbox, GsApp *app)
{
	GsApp *app_tmp;
	GtkWidget *widget;
	GtkWidget *box;
	GtkStyleContext *context;
	GPtrArray *related;
	gchar *text;
	guint cnt_addon = 0;
	guint cnt_apps = 0;
	guint i;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_top (box, 12);
	gtk_widget_set_margin_start (box, 12);
	gtk_widget_set_margin_bottom (box, 12);
	gtk_widget_set_margin_end (box, 12);

	widget = gtk_label_new (gs_app_get_name (app));
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);
	related = gs_app_get_related (app);

	/* split up the types */
	for (i = 0; i < related->len; i++) {
		app_tmp = g_ptr_array_index (related, i);
		switch (gs_app_get_id_kind (app_tmp)) {
		case GS_APP_ID_KIND_WEBAPP:
		case GS_APP_ID_KIND_DESKTOP:
			cnt_apps++;
			break;
		case GS_APP_ID_KIND_FONT:
		case GS_APP_ID_KIND_CODEC:
		case GS_APP_ID_KIND_INPUT_METHOD:
			cnt_addon++;
			break;
		default:
			break;
		}
	}
	if (cnt_apps == 0 && cnt_addon == 0) {
		/* TRANSLATORS: this source has no apps installed from it */
		text = g_strdup (_("No applications or addons installed"));
	} else if (cnt_addon == 0) {
		/* TRANSLATORS: this source has some apps installed from it */
		text = g_strdup_printf (ngettext ("%i application installed",
						  "%i applications installed",
						  cnt_apps), cnt_apps);
	} else if (cnt_apps == 0) {
		/* TRANSLATORS: this source has some apps installed from it */
		text = g_strdup_printf (ngettext ("%i addons installed",
						  "%i addons installed",
						  cnt_addon), cnt_addon);
	} else {
		/* TRANSLATORS: this source has some apps and addons installed from it */
		text = g_strdup_printf (ngettext ("%i application installed (and %i addons)",
						  "%i applications installed (and %i addons)",
						  cnt_apps),
					cnt_apps, cnt_addon);
	}
	widget = gtk_label_new (text);
	g_free (text);
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

	context = gtk_widget_get_style_context (widget);
	gtk_style_context_add_class (context, "dim-label");
	g_object_set_data_full (G_OBJECT (box), "GsShell::app",
				g_object_ref (app),
				(GDestroyNotify) g_object_unref);

	gtk_list_box_prepend (listbox, box);
	gtk_widget_show_all (box);
}

/**
 * gs_shell_sources_get_sources_cb:
 **/
static void
gs_shell_sources_get_sources_cb (GsPluginLoader *plugin_loader,
				 GAsyncResult *res,
				 GsShell *shell)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	GtkWidget *widget;
	GsShellPrivate *priv = shell->priv;

	/* show results */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_sources"));
	gtk_spinner_stop (GTK_SPINNER (widget));

	/* get the results */
	list = gs_plugin_loader_get_updates_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_NO_RESULTS)) {
			g_debug ("no sources to show");
		} else {
			g_warning ("failed to get sources: %s", error->message);
		}
		g_error_free (error);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_sources"));
		gtk_stack_set_visible_child_name (GTK_STACK (widget), "sources-empty");
		goto out;
	}

	/* add each */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_sources"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), "sources");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "listbox_sources"));
		gs_shell_sources_add_source (GTK_LIST_BOX (widget), app);
	}
out:
	if (list != NULL)
		gs_plugin_list_free (list);
}

void
gs_shell_show_sources (GsShell *shell)
{
	GsShellPrivate *priv = shell->priv;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_sources"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), "sources-waiting");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_sources"));
	gtk_spinner_start (GTK_SPINNER (widget));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_sources_back"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "listbox_sources"));
	gs_container_remove_all (GTK_CONTAINER (widget));

	/* get the list of non-core software sources */
	gs_plugin_loader_get_sources_async (priv->plugin_loader,
					    GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					    priv->cancellable,
					    (GAsyncReadyCallback) gs_shell_sources_get_sources_cb,
					    shell);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_sources"));
	gtk_window_present (GTK_WINDOW (widget));
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

typedef struct {
	GsShell *shell;
	GsApp *app;
} RefineData;

static void
refine_cb (GObject *source,
	   GAsyncResult *res,
	   gpointer data)
{
	RefineData *refine = data;
	GsShellPrivate *priv = refine->shell->priv;
	GError *error = NULL;

        if (!gs_plugin_loader_app_refine_finish (priv->plugin_loader,
                                                 res,
                                                 &error)) {
                g_warning ("%s", error->message);
                g_error_free (error);
		goto out;
        }

        refine->app = gs_plugin_loader_dedupe (priv->plugin_loader, refine->app);
        gs_shell_show_app (refine->shell, refine->app);

out:
        g_object_unref (refine->app);
	g_free (refine);
}

void
gs_shell_show_search_result (GsShell *shell, const gchar *id, const gchar *search)
{
	GsShellPrivate *priv = shell->priv;
	GtkWidget *widget;
	RefineData *refine;

	if (search != NULL && search[0] != '\0') {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_entry_set_text (GTK_ENTRY (widget), search);
		gs_shell_search_refresh (priv->shell_search, search, TRUE);

		gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH, NULL, NULL, TRUE);
	}

	refine = g_new0 (RefineData, 1);
	refine->shell = shell;
	refine->app = gs_app_new (id);
	gs_plugin_loader_app_refine_async (priv->plugin_loader,
					   refine->app,
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENCE |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL,
					   priv->cancellable,
					   refine_cb,
					   refine);
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
	shell->priv->shell_search = gs_shell_search_new ();
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

	g_slist_free_full (priv->back_entry_stack, (GDestroyNotify) free_back_entry);
	g_object_unref (priv->builder);
	g_object_unref (priv->cancellable);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->shell_overview);
	g_object_unref (priv->shell_installed);
	g_object_unref (priv->shell_updates);
	g_object_unref (priv->shell_details);
	g_object_unref (priv->shell_category);
	g_object_unref (priv->shell_search);

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
