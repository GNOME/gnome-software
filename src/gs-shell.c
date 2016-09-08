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

#include "gd-notification.h"

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
	gchar		*search;
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
	GPtrArray		*modal_dialogs;
	gulong			 search_changed_id;
	gchar			*events_info_uri;
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

gboolean
gs_shell_is_active (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	return gtk_window_is_active (priv->main_window);
}

GtkWindow *
gs_shell_get_window (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	return priv->main_window;
}

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

static void
free_back_entry (BackEntry *entry)
{
	if (entry->focus != NULL)
		g_object_remove_weak_pointer (G_OBJECT (entry->focus),
		                              (gpointer *) &entry->focus);
	g_clear_object (&entry->category);
	g_clear_object (&entry->app);
	g_free (entry->search);
	g_free (entry);
}

static void
gs_shell_clean_back_entry_stack (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	BackEntry *entry;

	while ((entry = g_queue_pop_head (priv->back_entry_stack)) != NULL) {
		free_back_entry (entry);
	}
}

void
gs_shell_change_mode (GsShell *shell,
		      GsShellMode mode,
		      gpointer data,
		      gboolean scroll_up)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GsApp *app;
	GsPage *new_page;
	GtkWidget *widget;
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (widget),
					mode == GS_SHELL_MODE_SEARCH);

	context = gtk_widget_get_style_context (GTK_WIDGET (gtk_builder_get_object (priv->builder, "header")));
	gtk_style_context_remove_class (context, "selection-mode");
	/* set the window title back to default */
	/* TRANSLATORS: this is the main window title */
	gtk_window_set_title (priv->main_window, _("Software"));

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
		gs_shell_clean_back_entry_stack (shell);
		new_page = GS_PAGE (priv->shell_overview);
		break;
	case GS_SHELL_MODE_INSTALLED:
		gs_shell_clean_back_entry_stack (shell);
		new_page = GS_PAGE (priv->shell_installed);
		break;
	case GS_SHELL_MODE_MODERATE:
		new_page = GS_PAGE (priv->shell_moderate);
		break;
	case GS_SHELL_MODE_LOADING:
		new_page = GS_PAGE (priv->shell_loading);
		break;
	case GS_SHELL_MODE_SEARCH:
		gs_shell_search_set_text (priv->shell_search, data);
		new_page = GS_PAGE (priv->shell_search);
		break;
	case GS_SHELL_MODE_UPDATES:
		gs_shell_clean_back_entry_stack (shell);
		new_page = GS_PAGE (priv->shell_updates);
		break;
	case GS_SHELL_MODE_DETAILS:
		app = GS_APP (data);
		if (gs_app_get_kind (app) != AS_APP_KIND_GENERIC ||
		    gs_app_get_source_default (app) == NULL) {
			gs_shell_details_set_app (priv->shell_details, data);
		} else {
			const gchar *tmp = gs_app_get_source_default (app);
			gs_shell_details_set_filename (priv->shell_details, tmp);
		}
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

	/* show the back button if needed */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	gtk_widget_set_visible (widget, !g_queue_is_empty (priv->back_entry_stack));

	gs_page_switch_to (new_page, scroll_up);

	/* update header bar widgets */
	widget = gs_page_get_header_start_widget (new_page);
	gs_shell_set_header_start_widget (shell, widget);

	widget = gs_page_get_header_end_widget (new_page);
	gs_shell_set_header_end_widget (shell, widget);

	/* destroy any existing modals */
	if (priv->modal_dialogs != NULL) {
		gsize i = 0;
		/* block signal emission of 'unmapped' since that will
		 * call g_ptr_array_remove_index. The unmapped signal may
		 * be emitted whilst running unref handlers for
		 * g_ptr_array_set_size */
		for (i = 0; i < priv->modal_dialogs->len; ++i) {
			GtkWidget *dialog = g_ptr_array_index (priv->modal_dialogs, i);
			g_signal_handlers_disconnect_by_func (dialog,
							      modal_dialog_unmapped_cb,
							      shell);
		}
		g_ptr_array_set_size (priv->modal_dialogs, 0);
	}
}

static void
gs_shell_overview_button_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellMode mode;
	mode = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
						   "gnome-software::overview-mode"));
	gs_shell_change_mode (shell, mode, NULL, TRUE);
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

	switch (priv->mode) {
	case GS_SHELL_MODE_CATEGORY:
		entry->category = gs_shell_category_get_category (priv->shell_category);
		g_object_ref (entry->category);
		g_debug ("pushing back entry for %s with %s",
			 page_name[entry->mode],
			 gs_category_get_id (entry->category));
		break;
	case GS_SHELL_MODE_DETAILS:
		entry->app = gs_shell_details_get_app (priv->shell_details);
		g_object_ref (entry->app);
		g_debug ("pushing back entry for %s with %s",
			 page_name[entry->mode],
			 gs_app_get_id (entry->app));
		break;
	case GS_SHELL_MODE_SEARCH:
		entry->search = g_strdup (gs_shell_search_get_text (priv->shell_search));
		g_debug ("pushing back entry for %s with %s",
			 page_name[entry->mode], entry->search);
		break;
	default:
		g_debug ("pushing back entry for %s", page_name[entry->mode]);
		break;
	}

	g_queue_push_head (priv->back_entry_stack, entry);
}

static void
gs_shell_plugin_events_sources_cb (GtkWidget *widget, GsShell *shell)
{
	gs_shell_show_sources (shell);
}

static void
gs_shell_plugin_events_no_space_cb (GtkWidget *widget, GsShell *shell)
{
	g_autoptr(GError) error = NULL;
	if (!g_spawn_command_line_async ("baobab", &error))
		g_warning ("failed to exec baobab: %s", error->message);
}

static void
gs_shell_plugin_events_network_settings_cb (GtkWidget *widget, GsShell *shell)
{
	g_autoptr(GError) error = NULL;
	if (!g_spawn_command_line_async ("gnome-control-center network", &error))
		g_warning ("failed to exec gnome-control-center: %s", error->message);
}

static void
gs_shell_plugin_events_more_info_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_autoptr(GError) error = NULL;
	if (!g_app_info_launch_default_for_uri (priv->events_info_uri, NULL, &error)) {
		g_warning ("failed to launch URI %s: %s",
			   priv->events_info_uri, error->message);
	}
}

static void
gs_shell_back_button_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	BackEntry *entry;

	g_return_if_fail (!g_queue_is_empty (priv->back_entry_stack));

	entry = g_queue_pop_head (priv->back_entry_stack);

	switch (entry->mode) {
	case GS_SHELL_MODE_UNKNOWN:
		/* only happens when the user does --search foobar */
		g_debug ("popping back entry for %s", page_name[entry->mode]);
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, FALSE);
		break;
	case GS_SHELL_MODE_CATEGORY:
		g_debug ("popping back entry for %s with %s",
			 page_name[entry->mode],
			 gs_category_get_id (entry->category));
		gs_shell_change_mode (shell, entry->mode, entry->category, FALSE);
		break;
	case GS_SHELL_MODE_DETAILS:
		g_debug ("popping back entry for %s with %p",
			 page_name[entry->mode], entry->app);
		gs_shell_change_mode (shell, entry->mode, entry->app, FALSE);
		break;
	case GS_SHELL_MODE_SEARCH:
		g_debug ("popping back entry for %s with %s",
			 page_name[entry->mode], entry->search);

		/* set the text in the entry and move cursor to the end */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		g_signal_handler_block (widget, priv->search_changed_id);
		gtk_entry_set_text (GTK_ENTRY (widget), entry->search);
		gtk_editable_set_position (GTK_EDITABLE (widget), -1);
		g_signal_handler_unblock (widget, priv->search_changed_id);

		/* set the mode directly */
		gs_shell_change_mode (shell, entry->mode,
				      (gpointer) entry->search, FALSE);
		break;
	default:
		g_debug ("popping back entry for %s", page_name[entry->mode]);
		gs_shell_change_mode (shell, entry->mode, NULL, FALSE);
		break;
	}

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

	gs_page_reload (GS_PAGE (priv->shell_updates));
	gs_page_reload (GS_PAGE (priv->shell_installed));

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

	text = gtk_entry_get_text (GTK_ENTRY (entry));
	if (strlen (text) > 2) {
		if (gs_shell_get_mode (shell) != GS_SHELL_MODE_SEARCH) {
			save_back_entry (shell);
			gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH,
					      (gpointer) text, TRUE);
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

	/* When the window is closed, reset the initial mode to overview */
	priv->mode = GS_SHELL_MODE_OVERVIEW;

	gs_shell_clean_back_entry_stack (shell);
	gtk_widget_hide (dialog);
	return TRUE;
}

static void
gs_shell_reload_cb (GsPluginLoader *plugin_loader, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gs_page_reload (GS_PAGE (priv->shell_category));
	gs_page_reload (GS_PAGE (priv->shell_extras));
	gs_page_reload (GS_PAGE (priv->shell_details));
	gs_page_reload (GS_PAGE (priv->shell_installed));
	gs_page_reload (GS_PAGE (priv->shell_overview));
	gs_page_reload (GS_PAGE (priv->shell_search));
	gs_page_reload (GS_PAGE (priv->shell_updates));
}

static void
gs_shell_main_window_mapped_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gs_plugin_loader_set_scale (priv->plugin_loader,
				    (guint) gtk_widget_get_scale_factor (widget));
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

typedef enum {
	GS_SHELL_EVENT_BUTTON_NONE		= 0,
	GS_SHELL_EVENT_BUTTON_SOURCES		= 1 << 0,
	GS_SHELL_EVENT_BUTTON_NO_SPACE		= 1 << 1,
	GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS	= 1 << 2,
	GS_SHELL_EVENT_BUTTON_MORE_INFO		= 1 << 3,
	GS_SHELL_EVENT_BUTTON_LAST
} GsShellEventButtons;

static void
gs_shell_show_event_app_notify (GsShell *shell,
				const gchar *title,
				GsShellEventButtons buttons)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;

	/* set visible */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notification_event"));
	gtk_widget_set_visible (widget, TRUE);

	/* sources button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_sources"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_SOURCES) > 0);

	/* no-space button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_no_space"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_NO_SPACE) > 0);

	/* no-space button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_network_settings"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS) > 0);

	/* more-info button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_more_info"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_MORE_INFO) > 0);

	/* set title */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_events"));
	gtk_label_set_markup (GTK_LABEL (widget), title);
	gtk_widget_set_visible (widget, title != NULL);
}

static gchar *
gs_shell_get_title_from_origin (GsApp *app)
{
	/* get a title, falling back */
	if (gs_app_get_origin_ui (app) != NULL &&
	    gs_app_get_origin_hostname (app) != NULL) {
		return g_strdup_printf ("“%s” [%s]",
					gs_app_get_origin_ui (app),
					gs_app_get_origin_hostname (app));
	} else if (gs_app_get_origin_ui (app) != NULL) {
		return g_strdup_printf ("“%s”", gs_app_get_origin_ui (app));
	} else if (gs_app_get_origin_hostname (app) != NULL) {
		return g_strdup_printf ("“%s”", gs_app_get_origin_hostname (app));
	}
	return g_strdup_printf ("“%s”", gs_app_get_id (app));
}

static gchar *
gs_shell_get_title_from_app (GsApp *app)
{
	/* get a title, falling back */
	if (gs_app_get_name (app) != NULL)
		return g_strdup_printf ("“%s”", gs_app_get_name (app));
	return g_strdup_printf ("“%s”", gs_app_get_id (app));
}

static gboolean
gs_shell_show_event_refresh (GsShell *shell, GsPluginEvent *event)
{
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autofree gchar *str_origin = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			if (gs_app_get_bundle_kind (origin) == AS_BUNDLE_KIND_CABINET) {
				/* TRANSLATORS: failure text for the in-app notification */
				g_string_append_printf (str, _("Unable to download "
							       "firmware updates from %s"),
							str_origin);
			} else {
				/* TRANSLATORS: failure text for the in-app notification */
				g_string_append_printf (str, _("Unable to download updates from %s"),
							str_origin);
				if (gs_app_get_management_plugin (origin) != NULL)
					buttons |= GS_SHELL_EVENT_BUTTON_SOURCES;
			}
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("Unable to download updates"));
		}
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to download updates: "
				       "internet access was required but wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to download updates "
					         "from %s: not enough disk space"),
					       str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("Unable to download updates: "
					        "not enough disk space"));
		}
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to download updates: "
				        "authentication was required"));
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to download updates: "
				        "authentication was invalid"));
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to download updates: you do not have"
				        " permission to install software"));
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to get list of updates"));
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_install (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autofree gchar *msg = NULL;
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	str_app = gs_shell_get_title_from_app (app);
	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to install %s as "
						       "download failed from %s"),
					       str_app, str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to install %s "
						       "as download failed"),
						str_app);
		}
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to install: internet access was "
				        "required but wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to install %s: "
					       "not enough disk space"),
					str_app);
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to install %s: "
					       "authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to install %s: "
					       "authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to install %s: "
					       "you do not have permission to "
					       "install software"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_ACCOUNT_SUSPENDED:
	case GS_PLUGIN_ERROR_ACCOUNT_DEACTIVATED:
		if (origin != NULL) {
			const gchar *url_homepage;

			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Your %s account has been suspended."),
						gs_app_get_name (origin));
			g_string_append (str, " ");
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("It is not possible to install "
						"software until this has been resolved."));
			url_homepage = gs_app_get_url (origin, AS_URL_KIND_HOMEPAGE);
			if (url_homepage != NULL) {
				g_autofree gchar *url = NULL;
				url = g_strdup_printf ("<a href=\"%s\">%s</a>",
						       url_homepage,
						       url_homepage);
				/* TRANSLATORS: failure text for the in-app notification */
				msg = g_strdup_printf (_("For more information, visit %s."),
							url);
			}
		}
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to install %s"), str_app);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_update (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	str_app = gs_shell_get_title_from_app (app);
	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to update %s from %s"),
					       str_app, str_origin);
			buttons = TRUE;
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to update %s as download failed"),
						str_app);
		}
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to update: "
					"internet access was required but "
					"wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to update %s: "
					       "not enough disk space"),
					str_app);
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to update %s: "
					       "authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to update %s: "
					       "authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to update %s: "
					       "you do not have permission to "
					       "update software"),
					str_app);
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to update %s"), str_app);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_upgrade (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;

	str_app = gs_shell_get_title_from_app (app);
	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to upgrade to %s from %s"),
					       str_app, str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to upgrade to %s "
						       "as download failed"),
						str_app);
		}
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to upgrade: "
					"internet access was required but "
					"wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "not enough disk space"),
					str_app);
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "you do not have permission to upgrade"),
					str_app);
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to upgrade to %s"), str_app);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_remove (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autofree gchar *str_app = NULL;

	str_app = gs_shell_get_title_from_app (app);
	switch (error->code) {
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
	case GS_PLUGIN_ERROR_PIN_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to remove %s: authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to remove %s: authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to remove %s: you do not have"
					       " permission to remove software"),
					str_app);
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to remove %s"), str_app);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_fallback (GsShell *shell, GsPluginEvent *event)
{
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autofree gchar *str_origin = NULL;

	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to contact %s"),
					        str_origin);
		}
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Not enough disk space — free up some space "
					"and try again"));
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	default:
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* add more-info button */
	if (origin != NULL) {
		const gchar *uri = gs_app_get_url (origin, AS_URL_KIND_HELP);
		if (uri != NULL) {
			g_free (priv->events_info_uri);
			priv->events_info_uri = g_strdup (uri);
			buttons |= GS_SHELL_EVENT_BUTTON_MORE_INFO;
		}
	}

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event (GsShell *shell, GsPluginEvent *event)
{
	const GError *error;
	GsPluginAction action;

	/* get error */
	error = gs_plugin_event_get_error (event);
	if (error == NULL)
		return FALSE;

	/* split up the events by action */
	action = gs_plugin_event_get_action (event);
	switch (action) {
	case GS_PLUGIN_ACTION_REFRESH:
		return gs_shell_show_event_refresh (shell, event);
	case GS_PLUGIN_ACTION_INSTALL:
		return gs_shell_show_event_install (shell, event);
	case GS_PLUGIN_ACTION_UPDATE:
		return gs_shell_show_event_update (shell, event);
	case GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD:
		return gs_shell_show_event_upgrade (shell, event);
	case GS_PLUGIN_ACTION_REMOVE:
		return gs_shell_show_event_remove (shell, event);
	default:
		break;
	}

	/* capture some warnings every time */
	return gs_shell_show_event_fallback (shell, event);
}

static void
gs_shell_rescan_events (GsShell *shell)
{
	GsPluginEvent *event;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;
	guint i;

	/* find the first active event and show it */
	event = gs_plugin_loader_get_event_default (priv->plugin_loader);
	if (event != NULL) {
		if (!gs_shell_show_event (shell, event)) {
			GsPluginAction action = gs_plugin_event_get_action (event);
			const GError *error = gs_plugin_event_get_error (event);
			g_warning ("not handling error %s for action %s: %s",
				   gs_plugin_error_to_string (error->code),
				   gs_plugin_action_to_string (action),
				   error->message);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INVALID);
			return;
		}
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_VISIBLE);
		return;
	}

	/* nothing to show */
	g_debug ("no events to show");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notification_event"));
	gtk_widget_set_visible (widget, FALSE);
}

static void
gs_shell_events_notify_cb (GsPluginLoader *plugin_loader,
			   GParamSpec *pspec,
			   GsShell *shell)
{
	gs_shell_rescan_events (shell);
}

static void
gs_shell_plugin_event_dismissed_cb (GdNotification *notification, GsShell *shell)
{
	GPtrArray *events;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	guint i;

	/* mark any events currently showing as invalid */
	events = gs_plugin_loader_get_events (priv->plugin_loader);
	for (i = 0; i < events->len; i++) {
		GsPluginEvent *event = g_ptr_array_index (events, i);
		if (gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_VISIBLE)) {
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INVALID);
			gs_plugin_event_remove_flag (event, GS_PLUGIN_EVENT_FLAG_VISIBLE);
		}
	}

	/* show the next event */
	gs_shell_rescan_events (shell);
}

void
gs_shell_setup (GsShell *shell, GsPluginLoader *plugin_loader, GCancellable *cancellable)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL (shell));

	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (priv->plugin_loader, "reload",
			  G_CALLBACK (gs_shell_reload_cb), shell);
	g_signal_connect_object (priv->plugin_loader, "notify::events",
				 G_CALLBACK (gs_shell_events_notify_cb),
				 shell, 0);
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

	/* set up in-app notification controls */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notification_event"));
	g_signal_connect (widget, "dismissed",
			  G_CALLBACK (gs_shell_plugin_event_dismissed_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_sources"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_sources_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_no_space"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_no_space_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_network_settings"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_network_settings_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_more_info"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_more_info_cb), shell);

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
	priv->search_changed_id =
		g_signal_connect (widget, "search-changed",
				  G_CALLBACK (search_changed_handler), shell);

	/* load content */
	g_signal_connect (priv->shell_loading, "refreshed",
			  G_CALLBACK (initial_overview_load_done), shell);

	/* coldplug */
	gs_shell_rescan_events (shell);
}

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
	gs_shell_change_mode (shell, mode, NULL, TRUE);
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

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);
}

void
gs_shell_show_app (GsShell *shell, GsApp *app)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS, app, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_category (GsShell *shell, GsCategory *category)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_CATEGORY, category, TRUE);
}

void gs_shell_show_extras_search (GsShell *shell, const gchar *mode, gchar **resources)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gs_shell_extras_search (priv->shell_extras, mode, resources);
	gs_shell_change_mode (shell, GS_SHELL_MODE_EXTRAS, NULL, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_search (GsShell *shell, const gchar *search)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH,
			      (gpointer) search, TRUE);
}

void
gs_shell_show_filename (GsShell *shell, const gchar *filename)
{
	g_autoptr(GsApp) app = gs_app_new (NULL);
	save_back_entry (shell);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_add_source (app, filename);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS,
			      (gpointer) app, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_search_result (GsShell *shell, const gchar *id, const gchar *search)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	save_back_entry (shell);
	gs_shell_search_set_appid_to_show (priv->shell_search, id);
	gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH,
			      (gpointer) search, TRUE);
}

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
	g_clear_pointer (&priv->events_info_uri, (GDestroyNotify) g_free);
	g_clear_pointer (&priv->modal_dialogs, (GDestroyNotify) g_ptr_array_unref);

	G_OBJECT_CLASS (gs_shell_parent_class)->dispose (object);
}

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

static void
gs_shell_init (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	priv->back_entry_stack = g_queue_new ();
	priv->ignore_primary_buttons = FALSE;
	priv->modal_dialogs = g_ptr_array_new_with_free_func ((GDestroyNotify) gtk_widget_destroy);
}

GsShell *
gs_shell_new (void)
{
	GsShell *shell;
	shell = g_object_new (GS_TYPE_SHELL, NULL);
	return GS_SHELL (shell);
}

/* vim: set noexpandtab: */
