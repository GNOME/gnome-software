/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#ifdef HAVE_MOGWAI
#include <libmogwai-schedule-client/scheduler.h>
#endif

#include "gs-common.h"
#include "gs-shell.h"
#include "gs-details-page.h"
#include "gs-installed-page.h"
#include "gs-metered-data-dialog.h"
#include "gs-moderate-page.h"
#include "gs-loading-page.h"
#include "gs-search-page.h"
#include "gs-overview-page.h"
#include "gs-updates-page.h"
#include "gs-category-page.h"
#include "gs-extras-page.h"
#include "gs-repos-dialog.h"
#include "gs-prefs-dialog.h"
#include "gs-update-dialog.h"
#include "gs-update-monitor.h"
#include "gs-utils.h"

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
	GsCategory	*category;
	gchar		*search;
} BackEntry;

typedef struct
{
	GSettings		*settings;
	gboolean		 ignore_primary_buttons;
	GCancellable		*cancellable;
	GsPluginLoader		*plugin_loader;
	GsShellMode		 mode;
	GHashTable		*pages;
	GtkWidget		*header_start_widget;
	GtkWidget		*header_end_widget;
	GtkBuilder		*builder;
	GtkWindow		*main_window;
	GQueue			*back_entry_stack;
	GPtrArray		*modal_dialogs;
	gulong			 search_changed_id;
	gchar			*events_info_uri;
	gboolean		 in_mode_change;
	GsPage			*page;

#ifdef HAVE_MOGWAI
	MwscScheduler		*scheduler;
	gulong			 scheduler_invalidated_handler;
#endif  /* HAVE_MOGWAI */
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
gs_shell_refresh_auto_updates_ui (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gboolean automatic_updates_paused;
	gboolean automatic_updates_enabled;
	GtkInfoBar *metered_updates_bar;

	automatic_updates_enabled = g_settings_get_boolean (priv->settings, "download-updates");

	metered_updates_bar = GTK_INFO_BAR (gtk_builder_get_object (priv->builder, "metered_updates_bar"));

#ifdef HAVE_MOGWAI
	automatic_updates_paused = (priv->scheduler == NULL || !mwsc_scheduler_get_allow_downloads (priv->scheduler));
#else
	automatic_updates_paused = gs_plugin_loader_get_network_metered (priv->plugin_loader);
#endif

	gtk_info_bar_set_revealed (metered_updates_bar,
				   priv->mode != GS_SHELL_MODE_LOADING &&
				   automatic_updates_enabled &&
				   automatic_updates_paused);
	gtk_info_bar_set_default_response (metered_updates_bar, GTK_RESPONSE_OK);
}

static void
gs_shell_metered_updates_bar_response_cb (GtkInfoBar *info_bar,
					  gint        response_id,
					  gpointer    user_data)
{
	GsShell *shell = GS_SHELL (user_data);
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkDialog *dialog;

	dialog = GTK_DIALOG (gs_metered_data_dialog_new (priv->main_window));
	gs_shell_modal_dialog_present (shell, dialog);

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);
}

static void
gs_shell_download_updates_changed_cb (GSettings   *settings,
				      const gchar *key,
				      gpointer     user_data)
{
	GsShell *shell = user_data;

	gs_shell_refresh_auto_updates_ui (shell);
}

static void
gs_shell_network_metered_notify_cb (GsPluginLoader *plugin_loader,
				    GParamSpec     *pspec,
				    gpointer        user_data)
{
#ifndef HAVE_MOGWAI
	GsShell *shell = user_data;

	/* @automatic_updates_paused only depends on network-metered if we’re
	 * compiled without Mogwai. */
	gs_shell_refresh_auto_updates_ui (shell);
#endif
}

#ifdef HAVE_MOGWAI
static void
scheduler_invalidated_cb (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	/* The scheduler shouldn’t normally be invalidated, since we Hold() it
	 * until we’re done with it. However, if the scheduler is stopped by
	 * systemd (`systemctl stop mogwai-scheduled`) this signal will be
	 * emitted. It may also be invalidated while our main window is hidden,
	 * as we release our Hold() then. */
	g_signal_handler_disconnect (priv->scheduler,
				     priv->scheduler_invalidated_handler);
	priv->scheduler_invalidated_handler = 0;

	g_clear_object (&priv->scheduler);
}

static void
scheduler_allow_downloads_changed_cb (GsShell *shell)
{
	gs_shell_refresh_auto_updates_ui (shell);
}

static void
scheduler_hold_cb (GObject *source_object,
		   GAsyncResult *result,
		   gpointer data)
{
	g_autoptr(GError) error_local = NULL;
	MwscScheduler *scheduler = (MwscScheduler *) source_object;
	g_autoptr(GsShell) shell = data;  /* reference added when starting the async operation */
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	if (!mwsc_scheduler_hold_finish (scheduler, result, &error_local)) {
		g_warning ("Couldn't hold the Mogwai Scheduler daemon: %s",
			   error_local->message);
		return;
	}

	priv->scheduler_invalidated_handler =
		g_signal_connect_swapped (scheduler, "invalidated",
					  (GCallback) scheduler_invalidated_cb,
					  shell);

	g_signal_connect_object (scheduler, "notify::allow-downloads",
				 (GCallback) scheduler_allow_downloads_changed_cb,
				 shell,
				 G_CONNECT_SWAPPED);

	g_assert (priv->scheduler == NULL);
	priv->scheduler = scheduler;

	/* Update the UI accordingly. */
	gs_shell_refresh_auto_updates_ui (shell);
}

static void
scheduler_release_cb (GObject *source_object,
		      GAsyncResult *result,
		      gpointer data)
{
	MwscScheduler *scheduler = (MwscScheduler *) source_object;
	g_autoptr(GsShell) shell = data;  /* reference added when starting the async operation */
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_autoptr(GError) error_local = NULL;

	if (!mwsc_scheduler_release_finish (scheduler, result, &error_local))
		g_warning ("Couldn't release the Mogwai Scheduler daemon: %s",
			   error_local->message);

	g_clear_object (&priv->scheduler);
}

static void
scheduler_ready_cb (GObject *source_object,
		    GAsyncResult *result,
		    gpointer data)
{
	MwscScheduler *scheduler;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GsShell) shell = data;  /* reference added when starting the async operation */

	scheduler = mwsc_scheduler_new_finish (result, &error_local);

	if (scheduler == NULL) {
		g_warning ("%s: Error getting Mogwai Scheduler: %s", G_STRFUNC,
			   error_local->message);
		return;
	}

	mwsc_scheduler_hold_async (scheduler,
				   "monitoring allow-downloads property",
				   NULL,
				   scheduler_hold_cb,
				   g_object_ref (shell));
}
#endif  /* HAVE_MOGWAI */

static void
free_back_entry (BackEntry *entry)
{
	if (entry->focus != NULL)
		g_object_remove_weak_pointer (G_OBJECT (entry->focus),
		                              (gpointer *) &entry->focus);
	g_clear_object (&entry->category);
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
	GsPage *page;
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "menu_button"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_selection_menu_button"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "origin_box"));
	gtk_widget_hide (widget);

	priv->in_mode_change = TRUE;
	/* only show the search button in overview and search pages */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_button"));
	gtk_widget_set_visible (widget, mode == GS_SHELL_MODE_OVERVIEW ||
					mode == GS_SHELL_MODE_SEARCH);
	/* hide unless we're going to search */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (widget),
					mode == GS_SHELL_MODE_SEARCH);
	priv->in_mode_change = FALSE;

	context = gtk_widget_get_style_context (GTK_WIDGET (gtk_builder_get_object (priv->builder, "header")));
	gtk_style_context_remove_class (context, "selection-mode");

	/* set the window title back to default */
	gtk_window_set_title (priv->main_window, g_get_application_name ());

	/* update main buttons according to mode */
	priv->ignore_primary_buttons = TRUE;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_explore"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_OVERVIEW);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_INSTALLED);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_SHELL_MODE_UPDATES);
	gtk_widget_set_visible (widget, gs_plugin_loader_get_allow_updates (priv->plugin_loader) ||
					mode == GS_SHELL_MODE_UPDATES);

	priv->ignore_primary_buttons = FALSE;

	/* switch page */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_main"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), page_name[mode]);

	/* do action for mode */
	priv->mode = mode;
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
		gs_shell_clean_back_entry_stack (shell);
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "overview"));
		break;
	case GS_SHELL_MODE_INSTALLED:
		gs_shell_clean_back_entry_stack (shell);
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "installed"));
		break;
	case GS_SHELL_MODE_MODERATE:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "moderate"));
		break;
	case GS_SHELL_MODE_LOADING:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "loading"));
		break;
	case GS_SHELL_MODE_SEARCH:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "search"));
		gs_search_page_set_text (GS_SEARCH_PAGE (page), data);
		break;
	case GS_SHELL_MODE_UPDATES:
		gs_shell_clean_back_entry_stack (shell);
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "updates"));
		break;
	case GS_SHELL_MODE_DETAILS:
		app = GS_APP (data);
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "details"));
		if (gs_app_get_local_file (app) != NULL) {
			gs_details_page_set_local_file (GS_DETAILS_PAGE (page),
			                                gs_app_get_local_file (app));
		} else if (gs_app_get_metadata_item (app, "GnomeSoftware::from-url") != NULL) {
			gs_details_page_set_url (GS_DETAILS_PAGE (page),
			                         gs_app_get_metadata_item (app, "GnomeSoftware::from-url"));
		} else {
			gs_details_page_set_app (GS_DETAILS_PAGE (page), data);
		}
		break;
	case GS_SHELL_MODE_CATEGORY:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "category"));
		gs_category_page_set_category (GS_CATEGORY_PAGE (page),
		                               GS_CATEGORY (data));
		break;
	case GS_SHELL_MODE_EXTRAS:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "extras"));
		break;
	default:
		g_assert_not_reached ();
	}

	/* show the back button if needed */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	gtk_widget_set_visible (widget,
				mode != GS_SHELL_MODE_SEARCH &&
				!g_queue_is_empty (priv->back_entry_stack));

	priv->in_mode_change = TRUE;

	if (priv->page != NULL)
		gs_page_switch_from (priv->page);
	g_set_object (&priv->page, page);
	gs_page_switch_to (page, scroll_up);
	priv->in_mode_change = FALSE;

	/* update header bar widgets */
	widget = gs_page_get_header_start_widget (page);
	gs_shell_set_header_start_widget (shell, widget);

	widget = gs_page_get_header_end_widget (page);
	gs_shell_set_header_end_widget (shell, widget);

	/* refresh the updates bar when moving out of the loading mode, but only
	 * if the Mogwai scheduler state is already known, to avoid spuriously
	 * showing the updates bar */
#ifdef HAVE_MOGWAI
	if (priv->scheduler != NULL)
#else
	if (TRUE)
#endif
		gs_shell_refresh_auto_updates_ui (shell);

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
gs_overview_page_button_cb (GtkWidget *widget, GsShell *shell)
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
	GsPage *page;

	entry = g_new0 (BackEntry, 1);
	entry->mode = priv->mode;

	entry->focus = gtk_window_get_focus (priv->main_window);
	if (entry->focus != NULL)
		g_object_add_weak_pointer (G_OBJECT (entry->focus),
					   (gpointer *) &entry->focus);

	switch (priv->mode) {
	case GS_SHELL_MODE_CATEGORY:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "category"));
		entry->category = gs_category_page_get_category (GS_CATEGORY_PAGE (page));
		g_object_ref (entry->category);
		g_debug ("pushing back entry for %s with %s",
			 page_name[entry->mode],
			 gs_category_get_id (entry->category));
		break;
	case GS_SHELL_MODE_SEARCH:
		page = GS_PAGE (g_hash_table_lookup (priv->pages, "search"));
		entry->search = g_strdup (gs_search_page_get_text (GS_SEARCH_PAGE (page)));
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
gs_shell_plugin_events_restart_required_cb (GtkWidget *widget, GsShell *shell)
{
	g_autoptr(GError) error = NULL;
	if (!g_spawn_command_line_async (LIBEXECDIR "/gnome-software-restarter", &error))
		g_warning ("failed to restart: %s", error->message);
}

/* this is basically a workaround for GtkSearchEntry. Due to delayed emission of the search-changed
 * signal it can't be blocked during insertion of text into the entry. Therefore we block the
 * precursor of that signal to be able to add text to the entry without firing the handlers
 * connected to "search-changed"
 */
static void
block_changed (GtkEditable *editable,
               gpointer     user_data)
{
	g_signal_stop_emission_by_name (editable, "changed");
}

static void
block_changed_signal (GtkSearchEntry *entry)
{
	g_signal_connect (entry, "changed", G_CALLBACK (block_changed), NULL);
}

static void
unblock_changed_signal (GtkSearchEntry *entry)
{
	g_signal_handlers_disconnect_by_func (entry, G_CALLBACK (block_changed), NULL);
}

static void
gs_shell_go_back (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	BackEntry *entry;
	GtkWidget *widget;

	/* nothing to do */
	if (g_queue_is_empty (priv->back_entry_stack)) {
		g_debug ("no back stack, showing overview");
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, FALSE);
		return;
	}

	entry = g_queue_pop_head (priv->back_entry_stack);

	switch (entry->mode) {
	case GS_SHELL_MODE_UNKNOWN:
	case GS_SHELL_MODE_LOADING:
		/* happens when using --search, --details, --install, etc. options */
		g_debug ("popping back entry for %s", page_name[entry->mode]);
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, FALSE);
		break;
	case GS_SHELL_MODE_CATEGORY:
		g_debug ("popping back entry for %s with %s",
			 page_name[entry->mode],
			 gs_category_get_id (entry->category));
		gs_shell_change_mode (shell, entry->mode, entry->category, FALSE);
		break;
	case GS_SHELL_MODE_SEARCH:
		g_debug ("popping back entry for %s with %s",
			 page_name[entry->mode], entry->search);

		/* set the text in the entry and move cursor to the end */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		block_changed_signal (GTK_SEARCH_ENTRY (widget));
		gtk_entry_set_text (GTK_ENTRY (widget), entry->search);
		gtk_editable_set_position (GTK_EDITABLE (widget), -1);
		unblock_changed_signal (GTK_SEARCH_ENTRY (widget));

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
gs_shell_back_button_cb (GtkWidget *widget, GsShell *shell)
{
	gs_shell_go_back (shell);
}

static void
gs_shell_reload_cb (GsPluginLoader *plugin_loader, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_autoptr(GList) keys = g_hash_table_get_keys (priv->pages);
	for (GList *l = keys; l != NULL; l = l->next) {
		GsPage *page = GS_PAGE (g_hash_table_lookup (priv->pages, l->data));
		gs_page_reload (page);
	}
}

static void
overview_page_refresh_done (GsOverviewPage *overview_page, gpointer data)
{
	GsShell *shell = data;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GsPage *page;

	g_signal_handlers_disconnect_by_func (overview_page, overview_page_refresh_done, data);

	page = GS_PAGE (gtk_builder_get_object (priv->builder, "updates_page"));
	gs_page_reload (page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "installed_page"));
	gs_page_reload (page);

	gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, TRUE);

	/* now that we're finished with the loading page, connect the reload signal handler */
	g_signal_connect (priv->plugin_loader, "reload",
	                  G_CALLBACK (gs_shell_reload_cb), shell);
}

static void
initial_refresh_done (GsLoadingPage *loading_page, gpointer data)
{
	GsShell *shell = data;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	g_signal_handlers_disconnect_by_func (loading_page, initial_refresh_done, data);

	g_signal_emit (shell, signals[SIGNAL_LOADED], 0);

	/* if the "loaded" signal handler didn't change the mode, kick off async
	 * overview page refresh, and switch to the page once done */
	if (priv->mode == GS_SHELL_MODE_LOADING) {
		GsPage *page;

		page = GS_PAGE (gtk_builder_get_object (priv->builder, "overview_page"));
		g_signal_connect (page, "refreshed",
		                  G_CALLBACK (overview_page_refresh_done), shell);
		gs_page_reload (page);
		return;
	}

	/* now that we're finished with the loading page, connect the reload signal handler */
	g_signal_connect (priv->plugin_loader, "reload",
	                  G_CALLBACK (gs_shell_reload_cb), shell);
}

static gboolean
window_keypress_handler (GtkWidget *window, GdkEvent *event, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *w;

	/* handle ctrl+f shortcut */
	if (event->type == GDK_KEY_PRESS) {
		GdkEventKey *e = (GdkEventKey *) event;
		if ((e->state & GDK_CONTROL_MASK) > 0 &&
		    e->keyval == GDK_KEY_f) {
			w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
			if (!gtk_search_bar_get_search_mode (GTK_SEARCH_BAR (w))) {
				gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (w), TRUE);
				w = GTK_WIDGET (gtk_builder_get_object (priv->builder,
								        "entry_search"));
				gtk_widget_grab_focus (w);
			} else {
				gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (w), FALSE);
			}
			return GDK_EVENT_STOP;
		}
	}

	/* pass to search bar */
	w = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	return gtk_search_bar_handle_event (GTK_SEARCH_BAR (w), event);
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
			GsPage *page = GS_PAGE (g_hash_table_lookup (priv->pages, "search"));
			gs_search_page_set_text (GS_SEARCH_PAGE (page), text);
			gs_page_switch_to (page, TRUE);
		}
	}
}

static void
search_button_clicked_cb (GtkToggleButton *toggle_button, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *search_bar;

	search_bar = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (search_bar),
	                                gtk_toggle_button_get_active (toggle_button));

	if (priv->in_mode_change)
		return;

	/* go back when exiting the search view */
	if (priv->mode == GS_SHELL_MODE_SEARCH &&
	    !gtk_toggle_button_get_active (toggle_button))
		gs_shell_go_back (shell);
}

static void
search_mode_enabled_cb (GtkSearchBar *search_bar, GParamSpec *pspec, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *search_button;

	search_button = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_button"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (search_button),
	                              gtk_search_bar_get_search_mode (search_bar));
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
	keymap = gdk_keymap_get_for_display (gtk_widget_get_display (win));
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
	GtkWidget *widget;

	/* hide any notifications */
	g_application_withdraw_notification (g_application_get_default (),
					     "installed");
	g_application_withdraw_notification (g_application_get_default (),
					     "install-resources");

	/* clear any in-app notification */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notification_event"));
	gtk_revealer_set_reveal_child (GTK_REVEALER (widget), FALSE);

	/* release our hold on the download scheduler */
#ifdef HAVE_MOGWAI
	if (priv->scheduler != NULL) {
		if (priv->scheduler_invalidated_handler > 0)
			g_signal_handler_disconnect (priv->scheduler,
						     priv->scheduler_invalidated_handler);
		priv->scheduler_invalidated_handler = 0;

		mwsc_scheduler_release_async (priv->scheduler,
					      NULL,
					      scheduler_release_cb,
					      g_object_ref (shell));
	}
#endif  /* HAVE_MOGWAI */

	gs_shell_clean_back_entry_stack (shell);
	gtk_widget_hide (dialog);
	return TRUE;
}

static void
gs_shell_main_window_mapped_cb (GtkWidget *widget, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	gs_plugin_loader_set_scale (priv->plugin_loader,
				    (guint) gtk_widget_get_scale_factor (widget));

	/* Set up the updates bar. Do this here rather than in gs_shell_setup()
	 * since we only want to hold the scheduler open while the gnome-software
	 * main window is visible, and not while we’re running in the background. */
#ifdef HAVE_MOGWAI
	if (priv->scheduler == NULL)
		mwsc_scheduler_new_async (priv->cancellable,
					  (GAsyncReadyCallback) scheduler_ready_cb,
					  g_object_ref (shell));
#else
	gs_shell_refresh_auto_updates_ui (shell);
#endif  /* HAVE_MOGWAI */
}

static void
gs_shell_main_window_realized_cb (GtkWidget *widget, GsShell *shell)
{

	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GdkRectangle geometry;
	GdkDisplay *display;
	GdkMonitor *monitor;

	display = gtk_widget_get_display (GTK_WIDGET (priv->main_window));
	monitor = gdk_display_get_monitor_at_window (display,
						     gtk_widget_get_window (GTK_WIDGET (priv->main_window)));

	/* adapt the window for low and medium resolution screens */
	gdk_monitor_get_geometry (monitor, &geometry);
	if (geometry.width < 800 || geometry.height < 600) {
		    GtkWidget *buttonbox = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));

		    gtk_container_child_set (GTK_CONTAINER (buttonbox),
					     GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_explore")),
					     "non-homogeneous", TRUE,
					     NULL);
		    gtk_container_child_set (GTK_CONTAINER (buttonbox),
					     GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed")),
					     "non-homogeneous", TRUE,
					     NULL);
		    gtk_container_child_set (GTK_CONTAINER (buttonbox),
					     GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates")),
					     "non-homogeneous", TRUE,
					     NULL);
	} else if (geometry.width < 1366 || geometry.height < 768) {
		gtk_window_set_default_size (priv->main_window, 1050, 600);
	}
}

static void
gs_shell_allow_updates_notify_cb (GsPluginLoader *plugin_loader,
				    GParamSpec *pspec,
				    GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_widget_set_visible (widget, gs_plugin_loader_get_allow_updates (plugin_loader) ||
					priv->mode == GS_SHELL_MODE_UPDATES);
}

typedef enum {
	GS_SHELL_EVENT_BUTTON_NONE		= 0,
	GS_SHELL_EVENT_BUTTON_SOURCES		= 1 << 0,
	GS_SHELL_EVENT_BUTTON_NO_SPACE		= 1 << 1,
	GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS	= 1 << 2,
	GS_SHELL_EVENT_BUTTON_MORE_INFO		= 1 << 3,
	GS_SHELL_EVENT_BUTTON_RESTART_REQUIRED	= 1 << 4,
	GS_SHELL_EVENT_BUTTON_LAST
} GsShellEventButtons;

static gboolean
gs_shell_has_disk_examination_app (void)
{
	g_autofree gchar *baobab = g_find_program_in_path ("baobab");
	return (baobab != NULL);
}

static void
gs_shell_show_event_app_notify (GsShell *shell,
				const gchar *title,
				GsShellEventButtons buttons)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;

	/* set visible */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notification_event"));
	gtk_revealer_set_reveal_child (GTK_REVEALER (widget), TRUE);

	/* sources button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_sources"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_SOURCES) > 0);

	/* no-space button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_no_space"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_NO_SPACE) > 0 &&
					gs_shell_has_disk_examination_app());

	/* network settings button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_network_settings"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS) > 0);

	/* restart button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_restart_required"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_RESTART_REQUIRED) > 0);

	/* more-info button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_more_info"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_MORE_INFO) > 0);

	/* dismiss button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_dismiss"));
	gtk_widget_set_visible (widget, (buttons & GS_SHELL_EVENT_BUTTON_RESTART_REQUIRED) == 0);

	/* set title */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_events"));
	gtk_label_set_markup (GTK_LABEL (widget), title);
	gtk_widget_set_visible (widget, title != NULL);
}

void
gs_shell_show_notification (GsShell *shell, const gchar *title)
{
	gs_shell_show_event_app_notify (shell, title, GS_SHELL_EVENT_BUTTON_NONE);
}

static gchar *
gs_shell_get_title_from_origin (GsApp *app)
{
	/* get a title, falling back */
	if (gs_app_get_origin_hostname (app) != NULL) {
		/* TRANSLATORS: this is part of the in-app notification,
		 * where the %s is the truncated hostname, e.g.
		 * 'alt.fedoraproject.org' */
		return g_strdup_printf (_("“%s”"), gs_app_get_origin_hostname (app));
	}
	if (gs_app_get_origin (app) != NULL) {
		/* TRANSLATORS: this is part of the in-app notification,
		 * where the %s is the origin id, e.g. 'fedora' */
		return g_strdup_printf (_("“%s”"), gs_app_get_origin (app));
	}
	return g_strdup_printf ("“%s”", gs_app_get_id (app));
}

/* return a name for the app, using quotes if the name is more than one word */
static gchar *
gs_shell_get_title_from_app (GsApp *app)
{
	const gchar *tmp = gs_app_get_name (app);
	if (tmp != NULL) {
		if (g_strstr_len (tmp, -1, " ") != NULL) {
			/* TRANSLATORS: this is part of the in-app notification,
			 * where the %s is a multi-word localised app name
			 * e.g. 'Getting things GNOME!" */
			return g_strdup_printf (_("“%s”"), tmp);
		}
		return g_strdup (tmp);
	}
	return g_strdup_printf (_("“%s”"), gs_app_get_id (app));
}

static gchar *
get_first_line (const gchar *str)
{
	g_auto(GStrv) lines = NULL;

	lines = g_strsplit (str, "\n", 2);
	if (lines != NULL && g_strv_length (lines) != 0)
		return g_strdup (lines[0]);

	return NULL;
}

static void
gs_shell_append_detailed_error (GsShell *shell, GString *str, const GError *error)
{
	g_autofree gchar *first_line = get_first_line (error->message);
	if (first_line != NULL) {
		g_autofree gchar *escaped = g_markup_escape_text (first_line, -1);
		g_string_append_printf (str, ":\n%s", escaped);
	}
}

static gboolean
gs_shell_show_event_refresh (GsShell *shell, GsPluginEvent *event)
{
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	GsPluginAction action = gs_plugin_event_get_action (event);
	g_autofree gchar *str_origin = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	/* ignore any errors from background downloads */
	if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
		return FALSE;

	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			if (gs_app_get_bundle_kind (origin) == AS_BUNDLE_KIND_CABINET) {
				/* TRANSLATORS: failure text for the in-app notification,
				 * where the %s is the source (e.g. "alt.fedoraproject.org") */
				g_string_append_printf (str, _("Unable to download "
							       "firmware updates from %s"),
							str_origin);
			} else {
				/* TRANSLATORS: failure text for the in-app notification,
				 * where the %s is the source (e.g. "alt.fedoraproject.org") */
				g_string_append_printf (str, _("Unable to download updates from %s"),
							str_origin);
				if (gs_app_get_management_plugin (origin) != NULL)
					buttons |= GS_SHELL_EVENT_BUTTON_SOURCES;
			}
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("Unable to download updates"));
		}
		gs_shell_append_detailed_error (shell, str, error);
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
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the source (e.g. "alt.fedoraproject.org") */
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
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		if (action == GS_PLUGIN_ACTION_DOWNLOAD) {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("Unable to download updates"));
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("Unable to get list of updates"));
		}
		gs_shell_append_detailed_error (shell, str, error);
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
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	str_app = gs_shell_get_title_from_app (app);
	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the application name (e.g. "GIMP") and
			 * the second %s is the origin, e.g. "Fedora Project [fedoraproject.org]"  */
			g_string_append_printf (str, _("Unable to install %s as "
						       "download failed from %s"),
					       str_app, str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to install %s "
						       "as download failed"),
						str_app);
		}
		gs_shell_append_detailed_error (shell, str, error);
		break;
	case GS_PLUGIN_ERROR_NOT_SUPPORTED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the application name (e.g. "GIMP")
			 * and the second %s is the name of the runtime, e.g.
			 * "GNOME SDK [flatpak.gnome.org]" */
			g_string_append_printf (str, _("Unable to install %s as "
						       "runtime %s not available"),
					       str_app, str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to install %s "
						       "as not supported"),
						str_app);
		}
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to install: internet access was "
				        "required but wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_INVALID_FORMAT:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to install: the application has an invalid format"));
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to install %s: "
					       "not enough disk space"),
					str_app);
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append_printf (str, _("Unable to install %s: "
					       "authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to install %s: "
					       "authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to install %s: "
					       "you do not have permission to "
					       "install software"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "Dell XPS 13") */
		g_string_append_printf (str, _("Unable to install %s: "
					       "AC power is required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_BATTERY_LEVEL_TOO_LOW:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "Dell XPS 13") */
		g_string_append_printf (str, _("Unable to install %s: "
					       "The battery level is too low"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to install %s"), str_app);
		gs_shell_append_detailed_error (shell, str, error);
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

	/* ignore any errors from background downloads */
	if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
		return FALSE;

	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (app != NULL && origin != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the app name (e.g. "GIMP") and
			 * the second %s is the origin, e.g. "Fedora" or
			 * "Fedora Project [fedoraproject.org]" */
			g_string_append_printf (str, _("Unable to update %s from %s as download failed"),
					       str_app, str_origin);
			buttons = TRUE;
		} else if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to update %s as download failed"),
						str_app);
		} else if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the origin, e.g. "Fedora" or
			 * "Fedora Project [fedoraproject.org]" */
			g_string_append_printf (str, _("Unable to install updates from %s as download failed"),
						str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to install updates as download failed"));
		}
		gs_shell_append_detailed_error (shell, str, error);
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Unable to update: "
					"internet access was required but "
					"wasn’t available"));
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to update %s: "
						       "not enough disk space"),
						str_app);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to install updates: "
						       "not enough disk space"));
		}
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
		if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to update %s: "
						       "authentication was required"),
						str_app);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to install updates: "
						       "authentication was required"));
		}
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to update %s: "
						       "authentication was invalid"),
						str_app);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to install updates: "
						       "authentication was invalid"));
		}
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to update %s: "
						       "you do not have permission to "
						       "update software"),
						str_app);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to install updates: "
						       "you do not have permission to "
						       "update software"));
		}
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "Dell XPS 13") */
			g_string_append_printf (str, _("Unable to update %s: "
						       "AC power is required"),
						str_app);
		} else {
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "Dell XPS 13") */
			g_string_append_printf (str, _("Unable to install updates: "
						       "AC power is required"));
		}
		break;
	case GS_PLUGIN_ERROR_BATTERY_LEVEL_TOO_LOW:
		if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "Dell XPS 13") */
			g_string_append_printf (str, _("Unable to update %s: "
						       "The battery level is too low"),
						str_app);
		} else {
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "Dell XPS 13") */
			g_string_append_printf (str, _("Unable to install updates: "
						       "The battery level is too low"));
		}
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to update %s"), str_app);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append_printf (str, _("Unable to install updates"));
		}
		gs_shell_append_detailed_error (shell, str, error);
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

	str_app = g_strdup_printf ("%s %s", gs_app_get_name (app), gs_app_get_version (app));
	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the distro name (e.g. "Fedora 25") and
			 * the second %s is the origin, e.g. "Fedora Project [fedoraproject.org]" */
			g_string_append_printf (str, _("Unable to upgrade to %s from %s"),
					       str_app, str_origin);
		} else {
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the app name (e.g. "GIMP") */
			g_string_append_printf (str, _("Unable to upgrade to %s "
						       "as download failed"),
						str_app);
		}
		gs_shell_append_detailed_error (shell, str, error);
		break;
	case GS_PLUGIN_ERROR_NO_NETWORK:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "internet access was required but "
					       "wasn’t available"),
					str_app);
		buttons |= GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS;
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "not enough disk space"),
					str_app);
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_AUTH_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "you do not have permission to upgrade"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "AC power is required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_BATTERY_LEVEL_TOO_LOW:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s: "
					       "The battery level is too low"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the distro name (e.g. "Fedora 25") */
		g_string_append_printf (str, _("Unable to upgrade to %s"), str_app);
		gs_shell_append_detailed_error (shell, str, error);
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
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s: authentication was required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AUTH_INVALID:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s: authentication was invalid"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s: you do not have"
					       " permission to remove software"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s: "
					       "AC power is required"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_BATTERY_LEVEL_TOO_LOW:
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s: "
					       "The battery level is too low"),
					str_app);
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: failure text for the in-app notification,
		 * where the %s is the application name (e.g. "GIMP") */
		g_string_append_printf (str, _("Unable to remove %s"), str_app);
		gs_shell_append_detailed_error (shell, str, error);
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
gs_shell_show_event_launch (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;

	switch (error->code) {
	case GS_PLUGIN_ERROR_NOT_SUPPORTED:
		if (app != NULL && origin != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the first %s is the application name (e.g. "GIMP")
			 * and the second %s is the name of the runtime, e.g.
			 * "GNOME SDK [flatpak.gnome.org]" */
			g_string_append_printf (str, _("Unable to launch %s: %s is not installed"),
					        str_app,
					        str_origin);
		}
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Not enough disk space — free up some space "
					"and try again"));
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: we failed to get a proper error code */
		g_string_append (str, _("Sorry, something went wrong"));
		gs_shell_append_detailed_error (shell, str, error);
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
gs_shell_show_event_file_to_app (GsShell *shell, GsPluginEvent *event)
{
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);

	switch (error->code) {
	case GS_PLUGIN_ERROR_NOT_SUPPORTED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Failed to install file: not supported"));
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Failed to install file: authentication failed"));
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Not enough disk space — free up some space "
					"and try again"));
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: we failed to get a proper error code */
		g_string_append (str, _("Sorry, something went wrong"));
		gs_shell_append_detailed_error (shell, str, error);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_url_to_app (GsShell *shell, GsPluginEvent *event)
{
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);

	switch (error->code) {
	case GS_PLUGIN_ERROR_NOT_SUPPORTED:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Failed to install: not supported"));
		break;
	case GS_PLUGIN_ERROR_NO_SECURITY:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Failed to install: authentication failed"));
		break;
	case GS_PLUGIN_ERROR_NO_SPACE:
		/* TRANSLATORS: failure text for the in-app notification */
		g_string_append (str, _("Not enough disk space — free up some space "
					"and try again"));
		buttons |= GS_SHELL_EVENT_BUTTON_NO_SPACE;
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: we failed to get a proper error code */
		g_string_append (str, _("Sorry, something went wrong"));
		gs_shell_append_detailed_error (shell, str, error);
		break;
	}
	if (str->len == 0)
		return FALSE;

	/* show in-app notification */
	gs_shell_show_event_app_notify (shell, str->str, buttons);
	return TRUE;
}

static gboolean
gs_shell_show_event_fallback (GsShell *shell, GsPluginEvent *event)
{
	GsApp *app = gs_plugin_event_get_app (event);
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autofree gchar *str_app = NULL;
	g_autofree gchar *str_origin = NULL;

	switch (error->code) {
	case GS_PLUGIN_ERROR_DOWNLOAD_FAILED:
		if (origin != NULL) {
			str_origin = gs_shell_get_title_from_origin (origin);
			/* TRANSLATORS: failure text for the in-app notification,
			 * the %s is the origin, e.g. "Fedora" or
			 * "Fedora Project [fedoraproject.org]" */
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
	case GS_PLUGIN_ERROR_RESTART_REQUIRED:
		if (app != NULL) {
			str_app = gs_shell_get_title_from_app (app);
			/* TRANSLATORS: failure text for the in-app notification,
			 * where the %s is the application name (e.g. "GIMP") */
			g_string_append_printf (str, _("%s needs to be restarted "
						       "to use new plugins."),
						str_app);
		} else {
			/* TRANSLATORS: failure text for the in-app notification */
			g_string_append (str, _("This application needs to be "
						"restarted to use new plugins."));
		}
		buttons |= GS_SHELL_EVENT_BUTTON_RESTART_REQUIRED;
		break;
	case GS_PLUGIN_ERROR_AC_POWER_REQUIRED:
		/* TRANSLATORS: need to be connected to the AC power */
		g_string_append (str, _("AC power is required"));
		break;
	case GS_PLUGIN_ERROR_BATTERY_LEVEL_TOO_LOW:
		/* TRANSLATORS: not enough juice to do this safely */
		g_string_append (str, _("The battery level is too low"));
		break;
	case GS_PLUGIN_ERROR_CANCELLED:
		break;
	default:
		/* non-interactive generic */
		if (!gs_plugin_event_has_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE))
			return FALSE;
		/* TRANSLATORS: we failed to get a proper error code */
		g_string_append (str, _("Sorry, something went wrong"));
		gs_shell_append_detailed_error (shell, str, error);
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

	/* name and shame the plugin */
	if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_TIMED_OUT)) {
		gs_shell_show_event_app_notify (shell, error->message,
						GS_SHELL_EVENT_BUTTON_NONE);
		return TRUE;
	}

	/* split up the events by action */
	action = gs_plugin_event_get_action (event);
	switch (action) {
	case GS_PLUGIN_ACTION_REFRESH:
	case GS_PLUGIN_ACTION_DOWNLOAD:
		return gs_shell_show_event_refresh (shell, event);
	case GS_PLUGIN_ACTION_INSTALL:
		return gs_shell_show_event_install (shell, event);
	case GS_PLUGIN_ACTION_UPDATE:
		return gs_shell_show_event_update (shell, event);
	case GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD:
		return gs_shell_show_event_upgrade (shell, event);
	case GS_PLUGIN_ACTION_REMOVE:
		return gs_shell_show_event_remove (shell, event);
	case GS_PLUGIN_ACTION_LAUNCH:
		return gs_shell_show_event_launch (shell, event);
	case GS_PLUGIN_ACTION_FILE_TO_APP:
		return gs_shell_show_event_file_to_app (shell, event);
	case GS_PLUGIN_ACTION_URL_TO_APP:
		return gs_shell_show_event_url_to_app (shell, event);
	default:
		break;
	}

	/* capture some warnings every time */
	return gs_shell_show_event_fallback (shell, event);
}

static void
gs_shell_rescan_events (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;
	g_autoptr(GsPluginEvent) event = NULL;

	/* find the first active event and show it */
	event = gs_plugin_loader_get_event_default (priv->plugin_loader);
	if (event != NULL) {
		if (!gs_shell_show_event (shell, event)) {
			GsPluginAction action = gs_plugin_event_get_action (event);
			const GError *error = gs_plugin_event_get_error (event);
			if (error != NULL &&
			    !g_error_matches (error,
					      GS_PLUGIN_ERROR,
					      GS_PLUGIN_ERROR_CANCELLED) &&
			    !g_error_matches (error,
					      G_IO_ERROR,
					      G_IO_ERROR_CANCELLED)) {
				g_warning ("not handling error %s for action %s: %s",
					   gs_plugin_error_to_string (error->code),
					   gs_plugin_action_to_string (action),
					   error->message);
			}
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INVALID);
			return;
		}
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_VISIBLE);
		return;
	}

	/* nothing to show */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notification_event"));
	gtk_revealer_set_reveal_child (GTK_REVEALER (widget), FALSE);
}

static void
gs_shell_events_notify_cb (GsPluginLoader *plugin_loader,
			   GParamSpec *pspec,
			   GsShell *shell)
{
	gs_shell_rescan_events (shell);
}

static void
gs_shell_plugin_event_dismissed_cb (GtkButton *button, GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	guint i;
	g_autoptr(GPtrArray) events = NULL;

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

static void
gs_shell_setup_pages (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_autoptr(GList) keys = g_hash_table_get_keys (priv->pages);
	for (GList *l = keys; l != NULL; l = l->next) {
		g_autoptr(GError) error = NULL;
		GsPage *page = GS_PAGE (g_hash_table_lookup (priv->pages, l->data));
		if (!gs_page_setup (page, shell,
				    priv->plugin_loader,
				    priv->builder,
				    priv->cancellable,
				    &error)) {
			g_warning ("Failed to setup panel: %s", error->message);
		}
	}
}

static void
gs_shell_add_about_menu_item (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GMenu *primary_menu;
	g_autoptr(GMenuItem) menu_item = NULL;
	g_autofree gchar *label = NULL;

	primary_menu = G_MENU (gtk_builder_get_object (priv->builder, "primary_menu"));

	/* TRANSLATORS: this is the menu item that opens the about window, e.g.
	 * 'About Software' or 'About Application Installer' where the %s is
	 * the application name chosen by the distro */
	label = g_strdup_printf (_("About %s"), g_get_application_name ());
	menu_item = g_menu_item_new (label, "app.about");
	g_menu_append_item (primary_menu, menu_item);
}

void
gs_shell_setup (GsShell *shell, GsPluginLoader *plugin_loader, GCancellable *cancellable)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *widget;
	GtkStyleContext *style_context;
	GsPage *page;

	g_return_if_fail (GS_IS_SHELL (shell));

	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect_object (priv->plugin_loader, "notify::events",
				 G_CALLBACK (gs_shell_events_notify_cb),
				 shell, 0);
	g_signal_connect_object (priv->plugin_loader, "notify::allow-updates",
				 G_CALLBACK (gs_shell_allow_updates_notify_cb),
				 shell, 0);
	g_signal_connect_object (priv->plugin_loader, "notify::network-metered",
				 G_CALLBACK (gs_shell_network_metered_notify_cb),
				 shell, 0);
	priv->cancellable = g_object_ref (cancellable);

	priv->settings = g_settings_new ("org.gnome.software");

	/* get UI */
	priv->builder = gtk_builder_new_from_resource ("/org/gnome/Software/gnome-software.ui");
	priv->main_window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	g_signal_connect (priv->main_window, "map",
			  G_CALLBACK (gs_shell_main_window_mapped_cb), shell);
	g_signal_connect (priv->main_window, "realize",
			  G_CALLBACK (gs_shell_main_window_realized_cb), shell);

	g_signal_connect (priv->main_window, "delete-event",
			  G_CALLBACK (main_window_closed_cb), shell);

	/* fix up the header bar */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
	if (gs_utils_is_current_desktop ("Unity")) {
		style_context = gtk_widget_get_style_context (widget);
		gtk_style_context_remove_class (style_context, GTK_STYLE_CLASS_TITLEBAR);
		gtk_style_context_add_class (style_context, GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
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

	/* show the search bar when clicking on the search button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_button"));
	g_signal_connect (widget, "clicked",
	                  G_CALLBACK (search_button_clicked_cb),
	                  shell);
	/* set the search button enabled when search bar appears */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	g_signal_connect (widget, "notify::search-mode-enabled",
	                  G_CALLBACK (search_mode_enabled_cb),
	                  shell);

	/* show the account popover when clicking on the account button */
	/* widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "menu_button"));
	g_signal_connect (widget, "clicked",
	                  G_CALLBACK (menu_button_clicked_cb),
	                  shell); */

	/* setup buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_back_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_explore"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_SHELL_MODE_OVERVIEW));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_overview_page_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_SHELL_MODE_INSTALLED));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_overview_page_button_cb), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_SHELL_MODE_UPDATES));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_overview_page_button_cb), shell);

	/* set up in-app notification controls */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_dismiss"));
	g_signal_connect (widget, "clicked",
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_events_restart_required"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_plugin_events_restart_required_cb), shell);

	/* add pages to hash */
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "overview_page"));
	g_hash_table_insert (priv->pages, g_strdup ("overview"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "updates_page"));
	g_hash_table_insert (priv->pages, g_strdup ("updates"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "installed_page"));
	g_hash_table_insert (priv->pages, g_strdup ("installed"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "moderate_page"));
	g_hash_table_insert (priv->pages, g_strdup ("moderate"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "loading_page"));
	g_hash_table_insert (priv->pages, g_strdup ("loading"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "search_page"));
	g_hash_table_insert (priv->pages, g_strdup ("search"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "details_page"));
	g_hash_table_insert (priv->pages, g_strdup ("details"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "category_page"));
	g_hash_table_insert (priv->pages, g_strdup ("category"), page);
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "extras_page"));
	g_hash_table_insert (priv->pages, g_strdup ("extras"), page);
	gs_shell_setup_pages (shell);

	/* set up the metered data info bar and mogwai */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "metered_updates_bar"));
	g_signal_connect (widget, "response",
			  (GCallback) gs_shell_metered_updates_bar_response_cb, shell);

	g_signal_connect (priv->settings, "changed::download-updates",
			  (GCallback) gs_shell_download_updates_changed_cb, shell);

	/* set up search */
	g_signal_connect (priv->main_window, "key-press-event",
			  G_CALLBACK (window_keypress_handler), shell);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	priv->search_changed_id =
		g_signal_connect (widget, "search-changed",
				  G_CALLBACK (search_changed_handler), shell);

	/* load content */
	page = GS_PAGE (gtk_builder_get_object (priv->builder, "loading_page"));
	g_signal_connect (page, "refreshed",
			  G_CALLBACK (initial_refresh_done), shell);

	/* coldplug */
	gs_shell_rescan_events (shell);

	/* primary menu */
	gs_shell_add_about_menu_item (shell);

	/* show loading page, which triggers the initial refresh */
	gs_shell_change_mode (shell, GS_SHELL_MODE_LOADING, NULL, TRUE);
}

void
gs_shell_reset_state (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);

	/* reset to overview, unless we're in the loading state which advances
	 * to overview on its own */
	if (priv->mode != GS_SHELL_MODE_LOADING)
		priv->mode = GS_SHELL_MODE_OVERVIEW;

	gs_shell_clean_back_entry_stack (shell);
}

void
gs_shell_set_mode (GsShell *shell, GsShellMode mode)
{
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
gs_shell_install (GsShell *shell, GsApp *app, GsShellInteraction interaction)
{
	GsPage *page;
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS,
			      (gpointer) app, TRUE);
	page = GS_PAGE (g_hash_table_lookup (priv->pages, "details"));
	gs_page_install_app (page, app, interaction, priv->cancellable);
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

	dialog = gs_repos_dialog_new (priv->main_window, priv->plugin_loader);
	gs_shell_modal_dialog_present (shell, GTK_DIALOG (dialog));

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);
}

void
gs_shell_show_prefs (GsShell *shell)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GtkWidget *dialog;

	dialog = gs_prefs_dialog_new (priv->main_window, priv->plugin_loader);
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
	GsPage *page;

	page = GS_PAGE (gtk_builder_get_object (priv->builder, "extras_page"));

	save_back_entry (shell);
	gs_extras_page_search (GS_EXTRAS_PAGE (page), mode, resources);
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
gs_shell_show_local_file (GsShell *shell, GFile *file)
{
	g_autoptr(GsApp) app = gs_app_new (NULL);
	save_back_entry (shell);
	gs_app_set_local_file (app, file);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS,
			      (gpointer) app, TRUE);
	gs_shell_activate (shell);
}

void
gs_shell_show_search_result (GsShell *shell, const gchar *id, const gchar *search)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	GsPage *page;

	save_back_entry (shell);
	page = GS_PAGE (g_hash_table_lookup (priv->pages, "search"));
	gs_search_page_set_appid_to_show (GS_SEARCH_PAGE (page), id);
	gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH,
			      (gpointer) search, TRUE);
}

void
gs_shell_show_uri (GsShell *shell, const gchar *url)
{
	GsShellPrivate *priv = gs_shell_get_instance_private (shell);
	g_autoptr(GError) error = NULL;

	if (!gtk_show_uri_on_window (priv->main_window,
	                             url,
	                             GDK_CURRENT_TIME,
	                             &error)) {
		g_warning ("failed to show URI %s: %s",
		           url, error->message);
	}
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
	g_clear_object (&priv->page);
	g_clear_pointer (&priv->pages, g_hash_table_unref);
	g_clear_pointer (&priv->events_info_uri, g_free);
	g_clear_pointer (&priv->modal_dialogs, g_ptr_array_unref);
	g_clear_object (&priv->settings);

#ifdef HAVE_MOGWAI
	if (priv->scheduler != NULL) {
		if (priv->scheduler_invalidated_handler > 0)
			g_signal_handler_disconnect (priv->scheduler,
						     priv->scheduler_invalidated_handler);

		mwsc_scheduler_release_async (priv->scheduler,
					      NULL,
					      scheduler_release_cb,
					      g_object_ref (shell));
	}
#endif  /* HAVE_MOGWAI */

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

	priv->pages = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
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
