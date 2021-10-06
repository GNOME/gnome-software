/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <adwaita.h>
#include <string.h>
#include <glib/gi18n.h>

#ifdef HAVE_MOGWAI
#include <libmogwai-schedule-client/scheduler.h>
#endif

#include "gs-common.h"
#include "gs-shell.h"
#include "gs-basic-auth-dialog.h"
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

#define NARROW_WIDTH_THRESHOLD 800

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

struct _GsShell
{
	AdwApplicationWindow	 parent_object;

	GSettings		*settings;
	GCancellable		*cancellable;
	GsPluginLoader		*plugin_loader;
	GtkWidget		*header_start_widget;
	GtkWidget		*header_end_widget;
	GtkWidget		*details_header_end_widget;
	GQueue			*back_entry_stack;
	GPtrArray		*modal_dialogs;
	gchar			*events_info_uri;
	AdwLeaflet		*main_leaflet;
	AdwLeaflet		*details_leaflet;
	AdwViewStack		*stack_loading;
	AdwViewStack		*stack_main;
	AdwViewStack		*stack_sub;
	GsPage			*page;

	GBinding		*sub_page_header_title_binding;

#ifdef HAVE_MOGWAI
	MwscScheduler		*scheduler;
	gboolean		 scheduler_held;
	gulong			 scheduler_invalidated_handler;
#endif  /* HAVE_MOGWAI */

	GtkWidget		*main_header;
	GtkWidget		*details_header;
	GtkWidget		*metered_updates_bar;
	GtkWidget		*search_button;
	GtkWidget		*entry_search;
	GtkWidget		*search_bar;
	GtkWidget		*button_back;
	GtkWidget		*button_back2;
	GtkWidget		*notification_event;
	GtkWidget		*button_events_sources;
	GtkWidget		*button_events_no_space;
	GtkWidget		*button_events_network_settings;
	GtkWidget		*button_events_restart_required;
	GtkWidget		*button_events_more_info;
	GtkWidget		*button_events_dismiss;
	GtkWidget		*label_events;
	GtkWidget		*primary_menu;
	GtkWidget		*sub_page_header_title;

	gboolean		 is_narrow;
	guint			 allocation_changed_cb_id;

	GsPage			*pages[GS_SHELL_MODE_LAST];
};

G_DEFINE_TYPE (GsShell, gs_shell, ADW_TYPE_APPLICATION_WINDOW)

typedef enum {
	PROP_IS_NARROW = 1,
} GsShellProperty;

enum {
	SIGNAL_LOADED,
	SIGNAL_LAST
};

static GParamSpec *obj_props[PROP_IS_NARROW + 1] = { NULL, };

static guint signals [SIGNAL_LAST] = { 0 };

static void
modal_dialog_unmapped_cb (GtkWidget *dialog,
                          GsShell *shell)
{
	g_debug ("modal dialog %p unmapped", dialog);
	g_ptr_array_remove (shell->modal_dialogs, dialog);
}

void
gs_shell_modal_dialog_present (GsShell *shell, GtkWindow *window)
{
	GtkWindow *parent;

	/* show new modal on top of old modal */
	if (shell->modal_dialogs->len > 0) {
		parent = g_ptr_array_index (shell->modal_dialogs,
					    shell->modal_dialogs->len - 1);
		g_debug ("using old modal %p as parent", parent);
	} else {
		parent = GTK_WINDOW (shell);
		g_debug ("using main window");
	}
	gtk_window_set_transient_for (window, parent);

	/* add to stack, transfer ownership to here */
	g_ptr_array_add (shell->modal_dialogs, window);
	g_signal_connect (GTK_WIDGET (window), "unmap",
	                  G_CALLBACK (modal_dialog_unmapped_cb), shell);

	/* present the new one */
	gtk_window_set_modal (window, TRUE);
	gtk_window_present (window);
}

void
gs_shell_activate (GsShell *shell)
{
	gtk_window_present (GTK_WINDOW (shell));
}

static void
gs_shell_set_header_start_widget (GsShell *shell, GtkWidget *widget)
{
	GtkWidget *old_widget;

	old_widget = shell->header_start_widget;

	if (shell->header_start_widget == widget)
		return;

	if (widget != NULL) {
		g_object_ref (widget);
		adw_header_bar_pack_start (ADW_HEADER_BAR (shell->main_header), widget);
	}

	shell->header_start_widget = widget;

	if (old_widget != NULL) {
		adw_header_bar_remove (ADW_HEADER_BAR (shell->main_header), old_widget);
		g_object_unref (old_widget);
	}
}

static void
gs_shell_set_header_end_widget (GsShell *shell, GtkWidget *widget)
{
	GtkWidget *old_widget;

	old_widget = shell->header_end_widget;

	if (shell->header_end_widget == widget)
		return;

	if (widget != NULL) {
		g_object_ref (widget);
		adw_header_bar_pack_end (ADW_HEADER_BAR (shell->main_header), widget);
	}

	shell->header_end_widget = widget;

	if (old_widget != NULL) {
		adw_header_bar_remove (ADW_HEADER_BAR (shell->main_header), old_widget);
		g_object_unref (old_widget);
	}
}

static void
gs_shell_set_details_header_end_widget (GsShell *shell, GtkWidget *widget)
{
	GtkWidget *old_widget;

	old_widget = shell->details_header_end_widget;

	if (shell->details_header_end_widget == widget)
		return;

	if (widget != NULL) {
		g_object_ref (widget);
		adw_header_bar_pack_end (ADW_HEADER_BAR (shell->details_header), widget);
	}

	shell->details_header_end_widget = widget;

	if (old_widget != NULL) {
		adw_header_bar_remove (ADW_HEADER_BAR (shell->details_header), old_widget);
		g_object_unref (old_widget);
	}
}

static void
gs_shell_refresh_auto_updates_ui (GsShell *shell)
{
	gboolean automatic_updates_paused;
	gboolean automatic_updates_enabled;

	automatic_updates_enabled = g_settings_get_boolean (shell->settings, "download-updates");

#ifdef HAVE_MOGWAI
	automatic_updates_paused = (shell->scheduler == NULL || !mwsc_scheduler_get_allow_downloads (shell->scheduler));
#else
	automatic_updates_paused = gs_plugin_loader_get_network_metered (shell->plugin_loader);
#endif

	gtk_info_bar_set_revealed (GTK_INFO_BAR (shell->metered_updates_bar),
				   gs_shell_get_mode (shell) != GS_SHELL_MODE_LOADING &&
				   automatic_updates_enabled &&
				   automatic_updates_paused);
	gtk_info_bar_set_default_response (GTK_INFO_BAR (shell->metered_updates_bar), GTK_RESPONSE_OK);
}

static void
gs_shell_metered_updates_bar_response_cb (GtkInfoBar *info_bar,
					  gint        response_id,
					  gpointer    user_data)
{
	GsShell *shell = GS_SHELL (user_data);
	GtkDialog *dialog;

	dialog = GTK_DIALOG (gs_metered_data_dialog_new (GTK_WINDOW (shell)));
	gs_shell_modal_dialog_present (shell, GTK_WINDOW (dialog));

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_window_destroy), dialog);
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
	/* The scheduler shouldn’t normally be invalidated, since we Hold() it
	 * until we’re done with it. However, if the scheduler is stopped by
	 * systemd (`systemctl stop mogwai-scheduled`) this signal will be
	 * emitted. It may also be invalidated while our main window is hidden,
	 * as we release our Hold() then. */
	g_signal_handler_disconnect (shell->scheduler,
				     shell->scheduler_invalidated_handler);
	shell->scheduler_invalidated_handler = 0;
	shell->scheduler_held = FALSE;

	g_clear_object (&shell->scheduler);
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

	if (mwsc_scheduler_hold_finish (scheduler, result, &error_local)) {
		shell->scheduler_held = TRUE;
	} else if (!g_error_matches (error_local, G_DBUS_ERROR, G_DBUS_ERROR_FAILED)) {
		g_warning ("Couldn't hold the Mogwai Scheduler daemon: %s",
			   error_local->message);
	}

	g_clear_error (&error_local);

	shell->scheduler_invalidated_handler =
		g_signal_connect_swapped (scheduler, "invalidated",
					  (GCallback) scheduler_invalidated_cb,
					  shell);

	g_signal_connect_object (scheduler, "notify::allow-downloads",
				 (GCallback) scheduler_allow_downloads_changed_cb,
				 shell,
				 G_CONNECT_SWAPPED);

	g_assert (shell->scheduler == NULL);
	shell->scheduler = scheduler;

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
	g_autoptr(GError) error_local = NULL;

	if (!mwsc_scheduler_release_finish (scheduler, result, &error_local))
		g_warning ("Couldn't release the Mogwai Scheduler daemon: %s",
			   error_local->message);

	shell->scheduler_held = FALSE;
	g_clear_object (&shell->scheduler);
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
gs_shell_basic_auth_start_cb (GsPluginLoader *plugin_loader,
                              const gchar *remote,
                              const gchar *realm,
                              GsBasicAuthCallback callback,
                              gpointer callback_data,
                              GsShell *shell)
{
	GtkWidget *dialog;

	dialog = gs_basic_auth_dialog_new (GTK_WINDOW (shell), remote, realm, callback, callback_data);
	gs_shell_modal_dialog_present (shell, GTK_WINDOW (dialog));

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_window_destroy), dialog);
}

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
	BackEntry *entry;

	while ((entry = g_queue_pop_head (shell->back_entry_stack)) != NULL) {
		free_back_entry (entry);
	}
}

static gboolean
gs_shell_get_mode_is_main (GsShellMode mode)
{
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
	case GS_SHELL_MODE_INSTALLED:
	case GS_SHELL_MODE_SEARCH:
	case GS_SHELL_MODE_UPDATES:
	case GS_SHELL_MODE_LOADING:
		return TRUE;
	case GS_SHELL_MODE_DETAILS:
	case GS_SHELL_MODE_CATEGORY:
	case GS_SHELL_MODE_EXTRAS:
	case GS_SHELL_MODE_MODERATE:
		return FALSE;
	default:
		return TRUE;
	}
}

static void search_bar_search_mode_enabled_changed_cb (GtkSearchBar *search_bar,
                                                       GParamSpec   *pspec,
                                                       GsShell      *shell);
static void gs_overview_page_button_cb (GtkWidget *widget, GsShell *shell);

static void
update_header_widgets (GsShell *shell)
{
	GsShellMode mode = gs_shell_get_mode (shell);

	/* only show the search button in overview and search pages */
	g_signal_handlers_block_by_func (shell->search_bar, search_bar_search_mode_enabled_changed_cb, shell);

	/* hide unless we're going to search */
	gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (shell->search_bar),
					mode == GS_SHELL_MODE_SEARCH);

	g_signal_handlers_unblock_by_func (shell->search_bar, search_bar_search_mode_enabled_changed_cb, shell);
}

static void
stack_notify_visible_child_cb (GObject    *object,
                               GParamSpec *pspec,
                               gpointer    user_data)
{
	GsShell *shell = GS_SHELL (user_data);
	GsPage *page;
	GtkWidget *widget;
	GsShellMode mode = gs_shell_get_mode (shell);
	gsize i;

	update_header_widgets (shell);

	/* do action for mode */
	page = shell->pages[mode];

	if (mode == GS_SHELL_MODE_OVERVIEW ||
	    mode == GS_SHELL_MODE_INSTALLED ||
	    mode == GS_SHELL_MODE_UPDATES)
		gs_shell_clean_back_entry_stack (shell);

	if (shell->page != NULL)
		gs_page_switch_from (shell->page);
	g_set_object (&shell->page, page);
	gs_page_switch_to (page);

	/* update header bar widgets */
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
	case GS_SHELL_MODE_INSTALLED:
	case GS_SHELL_MODE_SEARCH:
		gtk_widget_show (shell->search_button);
		break;
	case GS_SHELL_MODE_UPDATES:
		gtk_widget_hide (shell->search_button);
		break;
	default:
		/* We don't care about changing the visibility of the search
		 * button in modes appearing in sub-pages.  */
		break;
	}

	widget = gs_page_get_header_start_widget (page);
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
	case GS_SHELL_MODE_INSTALLED:
	case GS_SHELL_MODE_UPDATES:
	case GS_SHELL_MODE_SEARCH:
		gs_shell_set_header_start_widget (shell, widget);
		break;
	default:
		g_assert (widget == NULL);
		break;
	}

	widget = gs_page_get_header_end_widget (page);
	switch (mode) {
	case GS_SHELL_MODE_OVERVIEW:
	case GS_SHELL_MODE_INSTALLED:
	case GS_SHELL_MODE_UPDATES:
	case GS_SHELL_MODE_SEARCH:
		gs_shell_set_header_end_widget (shell, widget);
		break;
	case GS_SHELL_MODE_DETAILS:
		gs_shell_set_details_header_end_widget (shell, widget);
		break;
	default:
		g_assert (widget == NULL);
		break;
	}

	g_clear_object (&shell->sub_page_header_title_binding);
	shell->sub_page_header_title_binding = g_object_bind_property (adw_view_stack_get_visible_child (shell->stack_sub), "title",
								       shell->sub_page_header_title, "label",
								       G_BINDING_SYNC_CREATE);

	/* refresh the updates bar when moving out of the loading mode, but only
	 * if the Mogwai scheduler state is already known, to avoid spuriously
	 * showing the updates bar */
#ifdef HAVE_MOGWAI
	if (shell->scheduler != NULL)
#else
	if (TRUE)
#endif
		gs_shell_refresh_auto_updates_ui (shell);

	/* destroy any existing modals */
	if (shell->modal_dialogs != NULL) {
		/* block signal emission of 'unmapped' since that will
		 * call g_ptr_array_remove_index. The unmapped signal may
		 * be emitted whilst running unref handlers for
		 * g_ptr_array_set_size */
		for (i = 0; i < shell->modal_dialogs->len; ++i) {
			GtkWidget *dialog = g_ptr_array_index (shell->modal_dialogs, i);
			g_signal_handlers_disconnect_by_func (dialog,
							      modal_dialog_unmapped_cb,
							      shell);
			gtk_window_destroy (GTK_WINDOW (dialog));
		}
		g_ptr_array_set_size (shell->modal_dialogs, 0);
	}
}

void
gs_shell_change_mode (GsShell *shell,
		      GsShellMode mode,
		      gpointer data,
		      gboolean scroll_up)
{
	GsApp *app;
	GsPage *page;
	gboolean mode_is_main = gs_shell_get_mode_is_main (mode);

	if (gs_shell_get_mode (shell) == mode)
		return;

	/* switch page */
	if (mode == GS_SHELL_MODE_LOADING) {
		adw_view_stack_set_visible_child_name (shell->stack_loading, "loading");
		return;
	}

	adw_view_stack_set_visible_child_name (shell->stack_loading, "main");
	if (mode == GS_SHELL_MODE_DETAILS) {
		adw_leaflet_set_visible_child_name (shell->details_leaflet, "details");
	} else {
		adw_leaflet_set_visible_child_name (shell->details_leaflet, "main");
		/* We only change the main leaflet when not reaching the details
		 * page to preserve the navigation history in the UI's state.
		 * First change the page, then the leaflet, to avoid load of
		 * the previously shown page, which will be changed shortly after. */
		adw_view_stack_set_visible_child_name (mode_is_main ? shell->stack_main : shell->stack_sub, page_name[mode]);
		adw_leaflet_set_visible_child_name (shell->main_leaflet, mode_is_main ? "main" : "sub");
	}

	/* do any mode-specific actions */
	page = shell->pages[mode];

	if (mode == GS_SHELL_MODE_SEARCH) {
		gs_search_page_set_text (GS_SEARCH_PAGE (page), data);
		gtk_editable_set_text (GTK_EDITABLE (shell->entry_search), data);
		gtk_editable_set_position (GTK_EDITABLE (shell->entry_search), -1);
	} else if (mode == GS_SHELL_MODE_DETAILS) {
		app = GS_APP (data);
		if (gs_app_get_local_file (app) != NULL) {
			gs_details_page_set_local_file (GS_DETAILS_PAGE (page),
			                                gs_app_get_local_file (app));
		} else if (gs_app_get_metadata_item (app, "GnomeSoftware::from-url") != NULL) {
			gs_details_page_set_url (GS_DETAILS_PAGE (page),
			                         gs_app_get_metadata_item (app, "GnomeSoftware::from-url"));
		} else {
			gs_details_page_set_app (GS_DETAILS_PAGE (page), data);
		}
	} else if (mode == GS_SHELL_MODE_CATEGORY) {
		gs_category_page_set_category (GS_CATEGORY_PAGE (page),
		                               GS_CATEGORY (data));
	}

	if (scroll_up)
		gs_page_scroll_up (page);
}

static gboolean
overlay_get_child_position_cb (GtkOverlay   *overlay,
                               GtkWidget    *widget,
                               GdkRectangle *allocation,
                               gpointer      user_data)
{
	GsShell *self = GS_SHELL (user_data);
	GtkRequisition overlay_natural_size;

	/* Override the default position of the in-app notification overlay
	 * to position it below the header bar. The overlay can’t easily be
	 * moved in the widget hierarchy so it doesn’t have the header bar as
	 * a child, since there are several header bars in different pages of
	 * a AdwLeaflet. */
	g_assert (gtk_widget_is_ancestor (self->main_header, GTK_WIDGET (overlay)));

	gtk_widget_get_preferred_size (widget, NULL, &overlay_natural_size);

	allocation->width = overlay_natural_size.width;
	allocation->height = overlay_natural_size.height;

	allocation->x = gtk_widget_get_allocated_width (GTK_WIDGET (overlay)) / 2 - overlay_natural_size.width / 2;
	allocation->y = gtk_widget_get_allocated_height (GTK_WIDGET (self->main_header));

	return TRUE;
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
	BackEntry *entry;

	entry = g_new0 (BackEntry, 1);
	entry->mode = gs_shell_get_mode (shell);

	entry->focus = gtk_window_get_focus (GTK_WINDOW (shell));
	if (entry->focus != NULL)
		g_object_add_weak_pointer (G_OBJECT (entry->focus),
					   (gpointer *) &entry->focus);

	switch (entry->mode) {
	case GS_SHELL_MODE_CATEGORY:
		entry->category = gs_category_page_get_category (GS_CATEGORY_PAGE (shell->pages[GS_SHELL_MODE_CATEGORY]));
		g_object_ref (entry->category);
		g_debug ("pushing back entry for %s with %s",
			 page_name[entry->mode],
			 gs_category_get_id (entry->category));
		break;
	case GS_SHELL_MODE_SEARCH:
		entry->search = g_strdup (gs_search_page_get_text (GS_SEARCH_PAGE (shell->pages[GS_SHELL_MODE_SEARCH])));
		g_debug ("pushing back entry for %s with %s",
			 page_name[entry->mode], entry->search);
		break;
	default:
		g_debug ("pushing back entry for %s", page_name[entry->mode]);
		break;
	}

	g_queue_push_head (shell->back_entry_stack, entry);
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
	g_autoptr(GError) error = NULL;
	if (!g_app_info_launch_default_for_uri (shell->events_info_uri, NULL, &error)) {
		g_warning ("failed to launch URI %s: %s",
			   shell->events_info_uri, error->message);
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
	BackEntry *entry;

	/* nothing to do */
	if (g_queue_is_empty (shell->back_entry_stack)) {
		g_debug ("no back stack, showing overview");
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, FALSE);
		return;
	}

	entry = g_queue_pop_head (shell->back_entry_stack);

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
		block_changed_signal (GTK_SEARCH_ENTRY (shell->entry_search));
		gtk_editable_set_text (GTK_EDITABLE (shell->entry_search), entry->search);
		gtk_editable_set_position (GTK_EDITABLE (shell->entry_search), -1);
		unblock_changed_signal (GTK_SEARCH_ENTRY (shell->entry_search));

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
gs_shell_details_back_button_cb (GtkWidget *widget, GsShell *shell)
{
	gs_shell_go_back (shell);
}

static void
gs_shell_back_button_cb (GtkWidget *widget, GsShell *shell)
{
	gs_shell_go_back (shell);
}

static void
gs_shell_reload_cb (GsPluginLoader *plugin_loader, GsShell *shell)
{
	for (gsize i = 0; i < G_N_ELEMENTS (shell->pages); i++) {
		GsPage *page = shell->pages[i];
		if (page != NULL)
			gs_page_reload (page);
	}
}

static gboolean
change_mode_idle (gpointer user_data)
{
	GsShell *shell = user_data;

	gs_page_reload (GS_PAGE (shell->pages[GS_SHELL_MODE_UPDATES]));
	gs_page_reload (GS_PAGE (shell->pages[GS_SHELL_MODE_INSTALLED]));

	/* Switch only when still on the loading page, otherwise the page
	   could be changed from the command line or such, which would mean
	   hiding the chosen page. */
	if (gs_shell_get_mode (shell) == GS_SHELL_MODE_LOADING)
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, TRUE);

	return G_SOURCE_REMOVE;
}

static void
overview_page_refresh_done (GsOverviewPage *overview_page, gpointer data)
{
	GsShell *shell = data;

	g_signal_handlers_disconnect_by_func (overview_page, overview_page_refresh_done, data);

	/* now that we're finished with the loading page, connect the reload signal handler */
	g_signal_connect (shell->plugin_loader, "reload",
	                  G_CALLBACK (gs_shell_reload_cb), shell);

	/* schedule to change the mode in an idle callback, since it can take a
	 * while and this callback handler is typically called at the end of a
	 * long main context iteration already */
	g_idle_add (change_mode_idle, shell);
}

static void
initial_refresh_done (GsLoadingPage *loading_page, gpointer data)
{
	GsShell *shell = data;
	gboolean been_overview;

	g_signal_handlers_disconnect_by_func (loading_page, initial_refresh_done, data);

	been_overview = gs_shell_get_mode (shell) == GS_SHELL_MODE_OVERVIEW;

	g_signal_emit (shell, signals[SIGNAL_LOADED], 0);

	/* if the "loaded" signal handler didn't change the mode, kick off async
	 * overview page refresh, and switch to the page once done */
	if (gs_shell_get_mode (shell) == GS_SHELL_MODE_LOADING || been_overview) {
		g_signal_connect (shell->pages[GS_SHELL_MODE_OVERVIEW], "refreshed",
		                  G_CALLBACK (overview_page_refresh_done), shell);
		gs_page_reload (GS_PAGE (shell->pages[GS_SHELL_MODE_OVERVIEW]));
		return;
	}

	/* now that we're finished with the loading page, connect the reload signal handler */
	g_signal_connect (shell->plugin_loader, "reload",
	                  G_CALLBACK (gs_shell_reload_cb), shell);
}

static gboolean
window_keypress_handler (GtkEventControllerKey *key_controller,
                         guint                  keyval,
                         guint                  keycode,
                         GdkModifierType        state,
                         GsShell               *shell)
{
	/* handle ctrl+f shortcut */
	if ((state & GDK_CONTROL_MASK) > 0 && keyval == GDK_KEY_f) {
		if (!gtk_search_bar_get_search_mode (GTK_SEARCH_BAR (shell->search_bar))) {
			GsShellMode mode = gs_shell_get_mode (shell);

			gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (shell->search_bar), TRUE);
			gtk_widget_grab_focus (shell->entry_search);

			/* If the mode doesn't have a search button,
			 * switch to the search page right away,
			 * otherwise we would show the search bar
			 * without a button to toggle it. */
			switch (mode) {
			case GS_SHELL_MODE_OVERVIEW:
			case GS_SHELL_MODE_INSTALLED:
			case GS_SHELL_MODE_SEARCH:
				break;
			default:
				gs_shell_show_search (shell, "");
				break;
			}
		} else {
			gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (shell->search_bar), FALSE);
		}
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static void
search_changed_handler (GObject *entry, GsShell *shell)
{
	g_autofree gchar *text = NULL;

	text = g_strdup (gtk_editable_get_text (GTK_EDITABLE (entry)));
	if (strlen (text) >= 2) {
		if (gs_shell_get_mode (shell) != GS_SHELL_MODE_SEARCH) {
			save_back_entry (shell);
			gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH,
					      (gpointer) text, TRUE);
		} else {
			gs_search_page_set_text (GS_SEARCH_PAGE (shell->pages[GS_SHELL_MODE_SEARCH]), text);
			gs_page_switch_to (shell->pages[GS_SHELL_MODE_SEARCH]);
			gs_page_scroll_up (shell->pages[GS_SHELL_MODE_SEARCH]);
		}
	}
}

static void
search_bar_search_mode_enabled_changed_cb (GtkSearchBar *search_bar,
					   GParamSpec *pspec,
					   GsShell *shell)
{
	/* go back when exiting the search view */
	if (gs_shell_get_mode (shell) == GS_SHELL_MODE_SEARCH &&
	    !gtk_search_bar_get_search_mode (search_bar))
		gs_shell_go_back (shell);
}

static gboolean
window_key_pressed_cb (GtkEventControllerKey *key_controller,
                       guint                  keyval,
                       guint                  keycode,
                       GdkModifierType        state,
                       GsShell               *shell)
{
	gboolean is_rtl = gtk_widget_get_direction (shell->button_back) == GTK_TEXT_DIR_RTL;

	if ((!is_rtl && state == GDK_ALT_MASK && keyval == GDK_KEY_Left) ||
	    (is_rtl && state == GDK_ALT_MASK && keyval == GDK_KEY_Right) ||
	    keyval == GDK_KEY_Back) {
		/* GTK will only actually activate the one which is visible */
		gtk_widget_activate (shell->button_back);
		gtk_widget_activate (shell->button_back2);
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static void
window_button_pressed_cb (GtkGestureClick *click_gesture,
                          gint             n_press,
                          gdouble          x,
                          gdouble          y,
                          GsShell         *shell)
{
	/* GTK will only actually activate the one which is visible */
	gtk_widget_activate (shell->button_back);
	gtk_widget_activate (shell->button_back2);

	gtk_gesture_set_state (GTK_GESTURE (click_gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static gboolean
main_window_closed_cb (GtkWidget *dialog, gpointer user_data)
{
	GsShell *shell = user_data;

	/* hide any notifications */
	g_application_withdraw_notification (g_application_get_default (),
					     "installed");
	g_application_withdraw_notification (g_application_get_default (),
					     "install-resources");

	/* clear any in-app notification */
	gtk_revealer_set_reveal_child (GTK_REVEALER (shell->notification_event), FALSE);

	/* release our hold on the download scheduler */
#ifdef HAVE_MOGWAI
	if (shell->scheduler != NULL) {
		if (shell->scheduler_invalidated_handler > 0)
			g_signal_handler_disconnect (shell->scheduler,
						     shell->scheduler_invalidated_handler);
		shell->scheduler_invalidated_handler = 0;

		if (shell->scheduler_held)
			mwsc_scheduler_release_async (shell->scheduler,
						      NULL,
						      scheduler_release_cb,
						      g_object_ref (shell));
		else
			g_clear_object (&shell->scheduler);
	}
#endif  /* HAVE_MOGWAI */

	gs_shell_clean_back_entry_stack (shell);
	gtk_widget_hide (dialog);
	return TRUE;
}

static void
gs_shell_main_window_mapped_cb (GtkWidget *widget, GsShell *shell)
{
	gs_plugin_loader_set_scale (shell->plugin_loader,
				    (guint) gtk_widget_get_scale_factor (widget));

	/* Set up the updates bar. Do this here rather than in gs_shell_setup()
	 * since we only want to hold the scheduler open while the gnome-software
	 * main window is visible, and not while we’re running in the background. */
#ifdef HAVE_MOGWAI
	if (shell->scheduler == NULL)
		mwsc_scheduler_new_async (shell->cancellable,
					  (GAsyncReadyCallback) scheduler_ready_cb,
					  g_object_ref (shell));
#else
	gs_shell_refresh_auto_updates_ui (shell);
#endif  /* HAVE_MOGWAI */
}

static void
gs_shell_main_window_realized_cb (GtkWidget *widget, GsShell *shell)
{
	GdkRectangle geometry;
	GdkSurface *surface;
	GdkDisplay *display;
	GdkMonitor *monitor;

	display = gtk_widget_get_display (GTK_WIDGET (shell));
	surface = gtk_native_get_surface (GTK_NATIVE (shell));
	monitor = gdk_display_get_monitor_at_surface (display, surface);

	/* adapt the window for low and medium resolution screens */
	if (monitor != NULL) {
		gdk_monitor_get_geometry (monitor, &geometry);
		if (geometry.width < 800 || geometry.height < 600) {
		} else if (geometry.width < 1366 || geometry.height < 768) {
			gtk_window_set_default_size (GTK_WINDOW (shell), 1050, 600);
		}
	}
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
	/* set visible */
	gtk_revealer_set_reveal_child (GTK_REVEALER (shell->notification_event), TRUE);

	/* sources button */
	gtk_widget_set_visible (shell->button_events_sources,
				(buttons & GS_SHELL_EVENT_BUTTON_SOURCES) > 0);

	/* no-space button */
	gtk_widget_set_visible (shell->button_events_no_space,
				(buttons & GS_SHELL_EVENT_BUTTON_NO_SPACE) > 0 &&
				gs_shell_has_disk_examination_app());

	/* network settings button */
	gtk_widget_set_visible (shell->button_events_network_settings,
				(buttons & GS_SHELL_EVENT_BUTTON_NETWORK_SETTINGS) > 0);

	/* restart button */
	gtk_widget_set_visible (shell->button_events_restart_required,
				(buttons & GS_SHELL_EVENT_BUTTON_RESTART_REQUIRED) > 0);

	/* more-info button */
	gtk_widget_set_visible (shell->button_events_more_info,
				(buttons & GS_SHELL_EVENT_BUTTON_MORE_INFO) > 0);

	/* dismiss button */
	gtk_widget_set_visible (shell->button_events_dismiss,
				(buttons & GS_SHELL_EVENT_BUTTON_RESTART_REQUIRED) == 0);

	/* set title */
	gtk_label_set_markup (GTK_LABEL (shell->label_events), title);
	gtk_widget_set_visible (shell->label_events, title != NULL);
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
			g_free (shell->events_info_uri);
			shell->events_info_uri = g_strdup (uri);
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
			g_free (shell->events_info_uri);
			shell->events_info_uri = g_strdup (uri);
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
			g_free (shell->events_info_uri);
			shell->events_info_uri = g_strdup (uri);
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
			g_free (shell->events_info_uri);
			shell->events_info_uri = g_strdup (uri);
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
			g_free (shell->events_info_uri);
			shell->events_info_uri = g_strdup (uri);
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
			g_free (shell->events_info_uri);
			shell->events_info_uri = g_strdup (uri);
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
	GsApp *origin = gs_plugin_event_get_origin (event);
	GsShellEventButtons buttons = GS_SHELL_EVENT_BUTTON_NONE;
	const GError *error = gs_plugin_event_get_error (event);
	g_autoptr(GString) str = g_string_new (NULL);
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
		/* TRANSLATORS: failure text for the in-app notification, where the 'Software' means this application, aka 'GNOME Software'. */
		g_string_append (str, _("Software needs to be restarted to use new plugins."));
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
			g_free (shell->events_info_uri);
			shell->events_info_uri = g_strdup (uri);
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
	case GS_PLUGIN_ACTION_INSTALL_REPO:
	case GS_PLUGIN_ACTION_ENABLE_REPO:
		return gs_shell_show_event_install (shell, event);
	case GS_PLUGIN_ACTION_UPDATE:
		return gs_shell_show_event_update (shell, event);
	case GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD:
		return gs_shell_show_event_upgrade (shell, event);
	case GS_PLUGIN_ACTION_REMOVE:
	case GS_PLUGIN_ACTION_REMOVE_REPO:
	case GS_PLUGIN_ACTION_DISABLE_REPO:
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
	g_autoptr(GsPluginEvent) event = NULL;

	/* find the first active event and show it */
	event = gs_plugin_loader_get_event_default (shell->plugin_loader);
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
	gtk_revealer_set_reveal_child (GTK_REVEALER (shell->notification_event), FALSE);
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
	guint i;
	g_autoptr(GPtrArray) events = NULL;

	/* mark any events currently showing as invalid */
	events = gs_plugin_loader_get_events (shell->plugin_loader);
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
	for (gsize i = 0; i < G_N_ELEMENTS (shell->pages); i++) {
		g_autoptr(GError) error = NULL;
		GsPage *page = shell->pages[i];
		if (page != NULL &&
		    !gs_page_setup (page, shell,
				    shell->plugin_loader,
				    shell->cancellable,
				    &error)) {
			g_warning ("Failed to setup panel: %s", error->message);
		}
	}
}

static void
gs_shell_add_about_menu_item (GsShell *shell)
{
	g_autoptr(GMenuItem) menu_item = NULL;

	/* TRANSLATORS: this is the menu item that opens the about window */
	menu_item = g_menu_item_new (_("About Software"), "app.about");
	g_menu_append_item (G_MENU (shell->primary_menu), menu_item);
}

static void
updates_page_notify_counter_cb (GObject    *obj,
                                GParamSpec *pspec,
                                gpointer    user_data)
{
	GsPage *page = GS_PAGE (obj);
	GsShell *shell = GS_SHELL (user_data);
	AdwViewStackPage *stack_page;
	gboolean needs_attention;

	/* Update the needs-attention child property of the page in the
	 * AdwViewStack. There’s no need to account for whether it’s the currently
	 * visible page, as the CSS rules do that for us. This can’t be a simple
	 * property binding, though, as it’s a binding between an object
	 * property and a child property. */
	needs_attention = (gs_page_get_counter (page) > 0);

	stack_page = adw_view_stack_get_page (shell->stack_main, GTK_WIDGET (page));
	adw_view_stack_page_set_needs_attention (stack_page, needs_attention);
}

static void
category_page_app_clicked_cb (GsCategoryPage *page,
                              GsApp          *app,
                              gpointer        user_data)
{
	GsShell *shell = GS_SHELL (user_data);

	gs_shell_show_app (shell, app);
}

void
gs_shell_setup (GsShell *shell, GsPluginLoader *plugin_loader, GCancellable *cancellable)
{
	GsOdrsProvider *odrs_provider;

	g_return_if_fail (GS_IS_SHELL (shell));

	shell->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect_object (shell->plugin_loader, "notify::events",
				 G_CALLBACK (gs_shell_events_notify_cb),
				 shell, 0);
	g_signal_connect_object (shell->plugin_loader, "notify::network-metered",
				 G_CALLBACK (gs_shell_network_metered_notify_cb),
				 shell, 0);
	g_signal_connect_object (shell->plugin_loader, "basic-auth-start",
				 G_CALLBACK (gs_shell_basic_auth_start_cb),
				 shell, 0);

	g_object_bind_property (shell->plugin_loader, "allow-updates",
				shell->pages[GS_SHELL_MODE_UPDATES], "visible",
				G_BINDING_SYNC_CREATE);

	shell->cancellable = g_object_ref (cancellable);

	shell->settings = g_settings_new ("org.gnome.software");

	/* set up pages */
	gs_shell_setup_pages (shell);

	/* set up the metered data info bar and mogwai */
	g_signal_connect (shell->settings, "changed::download-updates",
			  (GCallback) gs_shell_download_updates_changed_cb, shell);

	odrs_provider = gs_plugin_loader_get_odrs_provider (shell->plugin_loader);
	gs_details_page_set_odrs_provider (GS_DETAILS_PAGE (shell->pages[GS_SHELL_MODE_DETAILS]), odrs_provider);
	gs_moderate_page_set_odrs_provider (GS_MODERATE_PAGE (shell->pages[GS_SHELL_MODE_MODERATE]), odrs_provider);

	/* coldplug */
	gs_shell_rescan_events (shell);

	/* primary menu */
	gs_shell_add_about_menu_item (shell);

	if (g_settings_get_boolean (shell->settings, "download-updates")) {
		/* show loading page, which triggers the initial refresh */
		gs_shell_change_mode (shell, GS_SHELL_MODE_LOADING, NULL, TRUE);
	} else {
		g_debug ("Skipped refresh of the repositories due to 'download-updates' disabled");
		initial_refresh_done (GS_LOADING_PAGE (shell->pages[GS_SHELL_MODE_LOADING]), shell);
	}
}

void
gs_shell_reset_state (GsShell *shell)
{
	/* reset to overview, unless we're in the loading state which advances
	 * to overview on its own */
	if (gs_shell_get_mode (shell) != GS_SHELL_MODE_LOADING)
		gs_shell_change_mode (shell, GS_SHELL_MODE_OVERVIEW, NULL, TRUE);

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
	const gchar *name;

	if (g_strcmp0 (adw_view_stack_get_visible_child_name (shell->stack_loading), "loading") == 0)
		return GS_SHELL_MODE_LOADING;

	if (g_strcmp0 (adw_leaflet_get_visible_child_name (shell->details_leaflet), "details") == 0)
		return GS_SHELL_MODE_DETAILS;

	if (g_strcmp0 (adw_leaflet_get_visible_child_name (shell->main_leaflet), "main") == 0)
		name = adw_view_stack_get_visible_child_name (shell->stack_main);
	else
		name = adw_view_stack_get_visible_child_name (shell->stack_sub);

	for (gsize i = 0; i < G_N_ELEMENTS (page_name); i++)
		if (g_strcmp0 (page_name[i], name) == 0)
			return i;

	g_assert_not_reached ();
}

const gchar *
gs_shell_get_mode_string (GsShell *shell)
{
	GsShellMode mode = gs_shell_get_mode (shell);
	return page_name[mode];
}

void
gs_shell_install (GsShell *shell, GsApp *app, GsShellInteraction interaction)
{
	save_back_entry (shell);
	gs_shell_change_mode (shell, GS_SHELL_MODE_DETAILS,
			      (gpointer) app, TRUE);
	gs_page_install_app (shell->pages[GS_SHELL_MODE_DETAILS], app, interaction, shell->cancellable);
}

void
gs_shell_show_installed_updates (GsShell *shell)
{
	GtkWidget *dialog;

	dialog = gs_update_dialog_new (shell->plugin_loader);

	gs_shell_modal_dialog_present (shell, GTK_WINDOW (dialog));
}

void
gs_shell_show_sources (GsShell *shell)
{
	GtkWidget *dialog;

	/* use if available */
	if (g_spawn_command_line_async ("software-properties-gtk", NULL))
		return;

	dialog = gs_repos_dialog_new (GTK_WINDOW (shell), shell->plugin_loader);
	gs_shell_modal_dialog_present (shell, GTK_WINDOW (dialog));
}

void
gs_shell_show_prefs (GsShell *shell)
{
	GtkWidget *dialog;

	dialog = gs_prefs_dialog_new (GTK_WINDOW (shell), shell->plugin_loader);
	gs_shell_modal_dialog_present (shell, GTK_WINDOW (dialog));
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

void gs_shell_show_extras_search (GsShell *shell, const gchar *mode, gchar **resources, const gchar *desktop_id, const gchar *ident)
{
	save_back_entry (shell);
	gs_extras_page_search (GS_EXTRAS_PAGE (shell->pages[GS_SHELL_MODE_EXTRAS]), mode, resources, desktop_id, ident);
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
	save_back_entry (shell);
	gs_search_page_set_appid_to_show (GS_SEARCH_PAGE (shell->pages[GS_SHELL_MODE_SEARCH]), id);
	gs_shell_change_mode (shell, GS_SHELL_MODE_SEARCH,
			      (gpointer) search, TRUE);
}

void
gs_shell_show_uri (GsShell *shell, const gchar *url)
{
	gtk_show_uri (GTK_WINDOW (shell), url, GDK_CURRENT_TIME);
}

/**
 * gs_shell_get_is_narrow:
 * @shell: a #GsShell
 *
 * Get the value of #GsShell:is-narrow.
 *
 * Returns: %TRUE if the window is in narrow mode, %FALSE otherwise
 *
 * Since: 41
 */
gboolean
gs_shell_get_is_narrow (GsShell *shell)
{
	g_return_val_if_fail (GS_IS_SHELL (shell), FALSE);

	return shell->is_narrow;
}

static void
gs_shell_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsShell *shell = GS_SHELL (object);

	switch ((GsShellProperty) prop_id) {
	case PROP_IS_NARROW:
		g_value_set_boolean (value, gs_shell_get_is_narrow (shell));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_shell_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch ((GsShellProperty) prop_id) {
	case PROP_IS_NARROW:
		/* Read only. */
		g_assert_not_reached ();
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_shell_dispose (GObject *object)
{
	GsShell *shell = GS_SHELL (object);

	g_clear_object (&shell->sub_page_header_title_binding);

	if (shell->back_entry_stack != NULL) {
		g_queue_free_full (shell->back_entry_stack, (GDestroyNotify) free_back_entry);
		shell->back_entry_stack = NULL;
	}
	g_clear_object (&shell->cancellable);
	g_clear_object (&shell->plugin_loader);
	g_clear_object (&shell->header_start_widget);
	g_clear_object (&shell->header_end_widget);
	g_clear_object (&shell->page);
	g_clear_pointer (&shell->events_info_uri, g_free);
	g_clear_pointer (&shell->modal_dialogs, g_ptr_array_unref);
	g_clear_object (&shell->settings);

#ifdef HAVE_MOGWAI
	if (shell->scheduler != NULL) {
		if (shell->scheduler_invalidated_handler > 0)
			g_signal_handler_disconnect (shell->scheduler,
						     shell->scheduler_invalidated_handler);

		if (shell->scheduler_held)
			mwsc_scheduler_release_async (shell->scheduler,
						      NULL,
						      scheduler_release_cb,
						      g_object_ref (shell));
		else
			g_clear_object (&shell->scheduler);
	}
#endif  /* HAVE_MOGWAI */

	G_OBJECT_CLASS (gs_shell_parent_class)->dispose (object);
}

static gboolean
allocation_changed_cb (gpointer user_data)
{
	GsShell *shell = GS_SHELL (user_data);
	GtkAllocation allocation;
	gboolean is_narrow;
	GtkStyleContext *context;

	gtk_widget_get_allocation (GTK_WIDGET (shell), &allocation);

	is_narrow = allocation.width <= NARROW_WIDTH_THRESHOLD;

	if (shell->is_narrow != is_narrow) {
		shell->is_narrow = is_narrow;
		g_object_notify_by_pspec (G_OBJECT (shell), obj_props[PROP_IS_NARROW]);
	}

	shell->allocation_changed_cb_id = 0;

	context = gtk_widget_get_style_context (GTK_WIDGET (shell));

	if (is_narrow)
		gtk_style_context_add_class (context, "narrow");
	else
		gtk_style_context_remove_class (context, "narrow");

	return G_SOURCE_REMOVE;
}

static void
gs_shell_size_allocate (GtkWidget *widget,
                        gint       width,
                        gint       height,
                        gint       baseline)
{
	GsShell *shell = GS_SHELL (widget);

	GTK_WIDGET_CLASS (gs_shell_parent_class)->size_allocate (widget,
								 width,
								 height,
								 baseline);

	/* Delay updating is-narrow so children can adapt to it, which isn't
	 * possible during the widget's allocation phase as it would break their
	 * size request.
	 */
	if (shell->allocation_changed_cb_id == 0)
		shell->allocation_changed_cb_id = g_idle_add (allocation_changed_cb, shell);
}

static void
gs_shell_class_init (GsShellClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_shell_get_property;
	object_class->set_property = gs_shell_set_property;
	object_class->dispose = gs_shell_dispose;

	widget_class->size_allocate = gs_shell_size_allocate;

	/**
	 * GsShell:is-narrow:
	 *
	 * Whether the window is in narrow mode.
	 *
	 * Pages can track this property to adapt to the available width.
	 *
	 * Since: 41
	 */
	obj_props[PROP_IS_NARROW] =
		g_param_spec_boolean ("is-narrow", NULL, NULL,
				      FALSE,
				      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	signals [SIGNAL_LOADED] =
		g_signal_new ("loaded",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell.ui");

	gtk_widget_class_bind_template_child (widget_class, GsShell, main_header);
	gtk_widget_class_bind_template_child (widget_class, GsShell, main_leaflet);
	gtk_widget_class_bind_template_child (widget_class, GsShell, details_header);
	gtk_widget_class_bind_template_child (widget_class, GsShell, details_leaflet);
	gtk_widget_class_bind_template_child (widget_class, GsShell, stack_loading);
	gtk_widget_class_bind_template_child (widget_class, GsShell, stack_main);
	gtk_widget_class_bind_template_child (widget_class, GsShell, stack_sub);
	gtk_widget_class_bind_template_child (widget_class, GsShell, metered_updates_bar);
	gtk_widget_class_bind_template_child (widget_class, GsShell, search_button);
	gtk_widget_class_bind_template_child (widget_class, GsShell, entry_search);
	gtk_widget_class_bind_template_child (widget_class, GsShell, search_bar);
	gtk_widget_class_bind_template_child (widget_class, GsShell, button_back);
	gtk_widget_class_bind_template_child (widget_class, GsShell, button_back2);
	gtk_widget_class_bind_template_child (widget_class, GsShell, notification_event);
	gtk_widget_class_bind_template_child (widget_class, GsShell, button_events_sources);
	gtk_widget_class_bind_template_child (widget_class, GsShell, button_events_no_space);
	gtk_widget_class_bind_template_child (widget_class, GsShell, button_events_network_settings);
	gtk_widget_class_bind_template_child (widget_class, GsShell, button_events_restart_required);
	gtk_widget_class_bind_template_child (widget_class, GsShell, button_events_more_info);
	gtk_widget_class_bind_template_child (widget_class, GsShell, button_events_dismiss);
	gtk_widget_class_bind_template_child (widget_class, GsShell, label_events);
	gtk_widget_class_bind_template_child (widget_class, GsShell, primary_menu);
	gtk_widget_class_bind_template_child (widget_class, GsShell, sub_page_header_title);

	gtk_widget_class_bind_template_child_full (widget_class, "overview_page", FALSE, G_STRUCT_OFFSET (GsShell, pages[GS_SHELL_MODE_OVERVIEW]));
	gtk_widget_class_bind_template_child_full (widget_class, "updates_page", FALSE, G_STRUCT_OFFSET (GsShell, pages[GS_SHELL_MODE_UPDATES]));
	gtk_widget_class_bind_template_child_full (widget_class, "installed_page", FALSE, G_STRUCT_OFFSET (GsShell, pages[GS_SHELL_MODE_INSTALLED]));
	gtk_widget_class_bind_template_child_full (widget_class, "moderate_page", FALSE, G_STRUCT_OFFSET (GsShell, pages[GS_SHELL_MODE_MODERATE]));
	gtk_widget_class_bind_template_child_full (widget_class, "loading_page", FALSE, G_STRUCT_OFFSET (GsShell, pages[GS_SHELL_MODE_LOADING]));
	gtk_widget_class_bind_template_child_full (widget_class, "search_page", FALSE, G_STRUCT_OFFSET (GsShell, pages[GS_SHELL_MODE_SEARCH]));
	gtk_widget_class_bind_template_child_full (widget_class, "details_page", FALSE, G_STRUCT_OFFSET (GsShell, pages[GS_SHELL_MODE_DETAILS]));
	gtk_widget_class_bind_template_child_full (widget_class, "category_page", FALSE, G_STRUCT_OFFSET (GsShell, pages[GS_SHELL_MODE_CATEGORY]));
	gtk_widget_class_bind_template_child_full (widget_class, "extras_page", FALSE, G_STRUCT_OFFSET (GsShell, pages[GS_SHELL_MODE_EXTRAS]));

	gtk_widget_class_bind_template_callback (widget_class, gs_shell_main_window_mapped_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_main_window_realized_cb);
	gtk_widget_class_bind_template_callback (widget_class, main_window_closed_cb);
	gtk_widget_class_bind_template_callback (widget_class, window_key_pressed_cb);
	gtk_widget_class_bind_template_callback (widget_class, window_keypress_handler);
	gtk_widget_class_bind_template_callback (widget_class, window_button_pressed_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_details_back_button_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_back_button_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_overview_page_button_cb);
	gtk_widget_class_bind_template_callback (widget_class, updates_page_notify_counter_cb);
	gtk_widget_class_bind_template_callback (widget_class, category_page_app_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, search_bar_search_mode_enabled_changed_cb);
	gtk_widget_class_bind_template_callback (widget_class, search_changed_handler);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_plugin_events_sources_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_plugin_events_no_space_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_plugin_events_network_settings_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_plugin_events_restart_required_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_plugin_events_more_info_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_plugin_event_dismissed_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_shell_metered_updates_bar_response_cb);
	gtk_widget_class_bind_template_callback (widget_class, stack_notify_visible_child_cb);
	gtk_widget_class_bind_template_callback (widget_class, initial_refresh_done);
	gtk_widget_class_bind_template_callback (widget_class, overlay_get_child_position_cb);

	gtk_widget_class_add_binding_action (widget_class, GDK_KEY_q, GDK_CONTROL_MASK, "window.close", NULL);
}

static void
gs_shell_init (GsShell *shell)
{
	gtk_widget_init_template (GTK_WIDGET (shell));

	gtk_search_bar_connect_entry (GTK_SEARCH_BAR (shell->search_bar), GTK_EDITABLE (shell->entry_search));

	shell->back_entry_stack = g_queue_new ();
	shell->modal_dialogs = g_ptr_array_new ();
}

GsShell *
gs_shell_new (void)
{
	return GS_SHELL (g_object_new (GS_TYPE_SHELL, NULL));
}
