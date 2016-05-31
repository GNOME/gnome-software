/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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
#include <gio/gio.h>

#include "gs-shell.h"
#include "gs-shell-updates.h"
#include "gs-common.h"
#include "gs-app-private.h"
#include "gs-app-row.h"
#include "gs-update-dialog.h"
#include "gs-update-list.h"
#include "gs-update-monitor.h"
#include "gs-upgrade-banner.h"
#include "gs-application.h"

#include <gdesktop-enums.h>
#include <langinfo.h>

typedef enum {
	GS_SHELL_UPDATES_FLAG_NONE		= 0,
	GS_SHELL_UPDATES_FLAG_HAS_UPDATES	= 1 << 0,
	GS_SHELL_UPDATES_FLAG_HAS_UPGRADES	= 1 << 1,
	GS_SHELL_UPDATES_FLAG_LAST
} GsShellUpdatesFlags;

typedef enum {
	GS_SHELL_UPDATES_STATE_STARTUP,
	GS_SHELL_UPDATES_STATE_ACTION_REFRESH,
	GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES,
	GS_SHELL_UPDATES_STATE_MANAGED,
	GS_SHELL_UPDATES_STATE_IDLE,
	GS_SHELL_UPDATES_STATE_FAILED,
	GS_SHELL_UPDATES_STATE_LAST,
} GsShellUpdatesState;

struct _GsShellUpdates
{
	GsPage			 parent_instance;

	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GCancellable		*cancellable_refresh;
	GCancellable		*cancellable_upgrade_download;
	GSettings		*settings;
	GSettings		*desktop_settings;
	gboolean		 cache_valid;
	guint			 in_flight;
	gboolean		 all_updates_are_live;
	gboolean		 any_require_reboot;
	GsShell			*shell;
	GNetworkMonitor		*network_monitor;
	gulong			 network_changed_handler;
	GsPluginStatus		 last_status;
	GsShellUpdatesState	 state;
	GsShellUpdatesFlags	 result_flags;
	GtkWidget		*button_refresh;
	GtkWidget		*button_update_all;
	GtkWidget		*header_spinner_start;
	GtkWidget		*header_start_box;
	GtkWidget		*header_end_box;
	gboolean		 has_agreed_to_mobile_data;
	gboolean		 ampm_available;

	GtkWidget		*updates_box;
	GtkWidget		*button_updates_mobile;
	GtkWidget		*button_updates_offline;
	GtkWidget		*fake_header_bar;
	GtkWidget		*label_updates_failed;
	GtkWidget		*label_updates_last_checked;
	GtkWidget		*label_updates_spinner;
	GtkWidget		*list_box_updates;
	GtkWidget		*scrolledwindow_updates;
	GtkWidget		*spinner_updates;
	GtkWidget		*stack_updates;
	GtkWidget		*upgrade_banner;
};

enum {
	COLUMN_UPDATE_APP,
	COLUMN_UPDATE_NAME,
	COLUMN_UPDATE_VERSION,
	COLUMN_UPDATE_LAST
};

G_DEFINE_TYPE (GsShellUpdates, gs_shell_updates, GS_TYPE_PAGE)

/**
 * gs_shell_updates_set_flag:
 **/
static void
gs_shell_updates_set_flag (GsShellUpdates *self, GsShellUpdatesFlags flag)
{
	self->result_flags |= flag;
}

/**
 * gs_shell_updates_clear_flag:
 **/
static void
gs_shell_updates_clear_flag (GsShellUpdates *self, GsShellUpdatesFlags flag)
{
	self->result_flags &= ~flag;
}

/**
 * gs_shell_updates_state_to_string:
 **/
static const gchar *
gs_shell_updates_state_to_string (GsShellUpdatesState state)
{
	if (state == GS_SHELL_UPDATES_STATE_STARTUP)
		return "startup";
	if (state == GS_SHELL_UPDATES_STATE_ACTION_REFRESH)
		return "action-refresh";
	if (state == GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES)
		return "action-get-updates";
	if (state == GS_SHELL_UPDATES_STATE_MANAGED)
		return "managed";
	if (state == GS_SHELL_UPDATES_STATE_IDLE)
		return "idle";
	if (state == GS_SHELL_UPDATES_STATE_FAILED)
		return "failed";
	return NULL;
}

/**
 * gs_shell_updates_invalidate:
 **/
static void
gs_shell_updates_invalidate (GsShellUpdates *self)
{
	self->cache_valid = FALSE;
}

static GDateTime *
time_next_midnight (void)
{
	GDateTime *next_midnight;
	GTimeSpan since_midnight;
	g_autoptr(GDateTime) now = NULL;

	now = g_date_time_new_now_local ();
	since_midnight = g_date_time_get_hour (now) * G_TIME_SPAN_HOUR +
			 g_date_time_get_minute (now) * G_TIME_SPAN_MINUTE +
			 g_date_time_get_second (now) * G_TIME_SPAN_SECOND +
			 g_date_time_get_microsecond (now);
	next_midnight = g_date_time_add (now, G_TIME_SPAN_DAY - since_midnight);

	return next_midnight;
}

static gchar *
gs_shell_updates_last_checked_time_string (GsShellUpdates *self)
{
	GDesktopClockFormat clock_format;
	const gchar *format_string;
	gchar *time_string;
	gboolean use_24h_time;
	gint64 tmp;
	gint days_ago;
	g_autoptr(GDateTime) last_checked = NULL;
	g_autoptr(GDateTime) midnight = NULL;

	g_settings_get (self->settings, "check-timestamp", "x", &tmp);
	if (tmp == 0)
		return NULL;
	last_checked = g_date_time_new_from_unix_local (tmp);

	midnight = time_next_midnight ();
	days_ago = g_date_time_difference (midnight, last_checked) / G_TIME_SPAN_DAY;

	clock_format = g_settings_get_enum (self->desktop_settings, "clock-format");
	use_24h_time = (clock_format == G_DESKTOP_CLOCK_FORMAT_24H || self->ampm_available == FALSE);

	if (days_ago < 1) { // today
		if (use_24h_time) {
			/* TRANSLATORS: Time in 24h format */
			format_string = _("%R");
		} else {
			/* TRANSLATORS: Time in 12h format */
			format_string = _("%l:%M %p");
		}
	} else if (days_ago < 2) { // yesterday
		if (use_24h_time) {
			/* TRANSLATORS: This is the word "Yesterday" followed by a
			   time string in 24h format. i.e. "Yesterday, 14:30" */
			format_string = _("Yesterday, %R");
		} else {
			/* TRANSLATORS: This is the word "Yesterday" followed by a
			   time string in 12h format. i.e. "Yesterday, 2:30 PM" */
			format_string = _("Yesterday, %l:%M %p");
		}
	} else if (days_ago < 3) {
		format_string = _("Two days ago");
	} else if (days_ago < 4) {
		format_string = _("Three days ago");
	} else if (days_ago < 5) {
		format_string = _("Four days ago");
	} else if (days_ago < 6) {
		format_string = _("Five days ago");
	} else if (days_ago < 7) {
		format_string = _("Six days ago");
	} else if (days_ago < 8) {
		format_string = _("One week ago");
	} else if (days_ago < 15) {
		format_string = _("Two weeks ago");
	} else {
		/* TRANSLATORS: This is the date string with: day number, month name, year.
		   i.e. "25 May 2012" */
		format_string = _("%e %B %Y");
	}

	time_string = g_date_time_format (last_checked, format_string);

	return time_string;
}

/**
 * gs_shell_updates_get_state_string:
 **/
static const gchar *
gs_shell_updates_get_state_string (GsPluginStatus status)
{
	if (status == GS_PLUGIN_STATUS_DOWNLOADING) {
		/* TRANSLATORS: the updates are being downloaded */
		return _("Downloading new updates…");
	}

	/* TRANSLATORS: the update panel is doing *something* vague */
	return _("Looking for new updates…");
}

/**
 * gs_shell_updates_update_ui_state:
 **/
static void
gs_shell_updates_update_ui_state (GsShellUpdates *self)
{
	GtkWidget *old_parent;
	gboolean allow_mobile_refresh = TRUE;
	g_autofree gchar *checked_str = NULL;
	g_autofree gchar *spinner_str = NULL;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_UPDATES)
		return;

	/* spinners */
	switch (self->state) {
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH:
		/* if we have updates, avoid clearing the page with a spinner */
		if (self->result_flags != GS_SHELL_UPDATES_FLAG_NONE) {
			gs_stop_spinner (GTK_SPINNER (self->spinner_updates));
			gtk_spinner_start (GTK_SPINNER (self->header_spinner_start));
			gtk_widget_show (self->header_spinner_start);
		} else {
			gs_start_spinner (GTK_SPINNER (self->spinner_updates));
		}
		break;
	default:
		gs_stop_spinner (GTK_SPINNER (self->spinner_updates));
		gtk_spinner_stop (GTK_SPINNER (self->header_spinner_start));
		gtk_widget_hide (self->header_spinner_start);
		break;
	}

	/* spinner text */
	switch (self->state) {
	case GS_SHELL_UPDATES_STATE_STARTUP:
		spinner_str = g_strdup_printf ("%s\n%s",
				       /* TRANSLATORS: the updates panel is starting up */
				       _("Setting up updates…"),
				       _("(This could take a while)"));
		gtk_label_set_label (GTK_LABEL (self->label_updates_spinner), spinner_str);
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH:
		spinner_str = g_strdup_printf ("%s\n%s",
				       gs_shell_updates_get_state_string (self->last_status),
				       /* TRANSLATORS: the updates panel is starting up */
				       _("(This could take a while)"));
		gtk_label_set_label (GTK_LABEL (self->label_updates_spinner), spinner_str);
		break;
	default:
		break;
	}

	/* headerbar refresh icon */
	switch (self->state) {
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH:
		gtk_image_set_from_icon_name (GTK_IMAGE (gtk_button_get_image (GTK_BUTTON (self->button_refresh))),
					      "media-playback-stop-symbolic", GTK_ICON_SIZE_MENU);
		gtk_widget_show (self->button_refresh);
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_MANAGED:
		gtk_widget_hide (self->button_refresh);
		break;
	default:
		gtk_image_set_from_icon_name (GTK_IMAGE (gtk_button_get_image (GTK_BUTTON (self->button_refresh))),
					      "view-refresh-symbolic", GTK_ICON_SIZE_MENU);
		if (self->result_flags != GS_SHELL_UPDATES_FLAG_NONE) {
			gtk_widget_show (self->button_refresh);
		} else {
			if (self->network_monitor != NULL &&
			    g_network_monitor_get_network_metered (self->network_monitor) &&
			    !self->has_agreed_to_mobile_data)
				allow_mobile_refresh = FALSE;
			gtk_widget_set_visible (self->button_refresh, allow_mobile_refresh);
		}
		break;
	}

	/* headerbar update button */
	gtk_widget_set_visible (self->button_update_all,
				self->result_flags != GS_SHELL_UPDATES_FLAG_NONE);

	/* stack */
	switch (self->state) {
	case GS_SHELL_UPDATES_STATE_MANAGED:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "managed");
		break;
	case GS_SHELL_UPDATES_STATE_FAILED:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "failed");
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH:
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
		if (self->result_flags != GS_SHELL_UPDATES_FLAG_NONE) {
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "view");
		} else {
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "spinner");
		}
		break;
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_IDLE:

		/* if have updates, just show the view, otherwise show network */
		if (self->result_flags != GS_SHELL_UPDATES_FLAG_NONE) {
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "view");
			break;
		}

		/* we just don't know */
		if (self->network_monitor == NULL) {
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "uptodate");
			break;
		}

		/* check we have a "free" network connection */
		if (g_network_monitor_get_network_available (self->network_monitor) &&
			   !g_network_monitor_get_network_metered (self->network_monitor)) {
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "uptodate");
			break;
		}

		/* expensive network connection */
		if (g_network_monitor_get_network_metered (self->network_monitor)) {
			if (self->has_agreed_to_mobile_data) {
				gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "uptodate");
			} else {
				gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "mobile");
			}
			break;
		}

		/* no network connection */
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "offline");
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* any upgrades? */
	if (self->result_flags & GS_SHELL_UPDATES_FLAG_HAS_UPGRADES) {
		/* move header bar buttons to the fake header bar */
		g_object_ref (self->button_update_all);
		old_parent = gtk_widget_get_parent (self->button_update_all);
		if (old_parent != NULL)
			gtk_container_remove (GTK_CONTAINER (old_parent), self->button_update_all);
		gtk_header_bar_pack_end (GTK_HEADER_BAR (self->fake_header_bar), self->button_update_all);
		g_object_unref (self->button_update_all);

		gtk_widget_show (self->fake_header_bar);
		gtk_widget_show (self->upgrade_banner);
	} else {
		/* move header bar buttons to the real header bar */
		g_object_ref (self->button_update_all);
		old_parent = gtk_widget_get_parent (self->button_update_all);
		if (old_parent != NULL)
			gtk_container_remove (GTK_CONTAINER (old_parent), self->button_update_all);
		gtk_container_add (GTK_CONTAINER (self->header_end_box), self->button_update_all);
		g_object_unref (self->button_update_all);

		gtk_widget_hide (self->fake_header_bar);
		gtk_widget_hide (self->upgrade_banner);
	}

	/* any updates? */
	gtk_widget_set_visible (self->updates_box,
				self->result_flags & GS_SHELL_UPDATES_FLAG_HAS_UPDATES);

	/* last checked label */
	if (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (self->stack_updates)), "uptodate") == 0) {
		checked_str = gs_shell_updates_last_checked_time_string (self);
		if (checked_str != NULL) {
			g_autofree gchar *last_checked = NULL;

			/* TRANSLATORS: This is the time when we last checked for updates */
			last_checked = g_strdup_printf (_("Last checked: %s"), checked_str);
			gtk_label_set_label (GTK_LABEL (self->label_updates_last_checked),
					     last_checked);
		}
		gtk_widget_set_visible (self->label_updates_last_checked, checked_str != NULL);
	}
}

/**
 * gs_shell_updates_set_state:
 **/
static void
gs_shell_updates_set_state (GsShellUpdates *self, GsShellUpdatesState state)
{
	g_debug ("setting state from %s to %s (has-update:%i, has-upgrade:%i)",
		 gs_shell_updates_state_to_string (self->state),
		 gs_shell_updates_state_to_string (state),
		 (self->result_flags & GS_SHELL_UPDATES_FLAG_HAS_UPDATES) > 0,
		 (self->result_flags & GS_SHELL_UPDATES_FLAG_HAS_UPGRADES) > 0);
	self->state = state;
	gs_shell_updates_update_ui_state (self);
}

/**
 * gs_shell_updates_notify_network_state_cb:
 **/
static void
gs_shell_updates_notify_network_state_cb (GNetworkMonitor *network_monitor,
					  gboolean available,
					  GsShellUpdates *self)
{
	gs_shell_updates_update_ui_state (self);
}

/**
 * gs_shell_updates_get_updates_cb:
 **/
static void
gs_shell_updates_get_updates_cb (GsPluginLoader *plugin_loader,
				 GAsyncResult *res,
				 GsShellUpdates *self)
{
	guint i;
	GtkWidget *widget;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	self->cache_valid = TRUE;

	/* get the results */
	list = gs_plugin_loader_get_updates_finish (plugin_loader, res, &error);
	self->all_updates_are_live = TRUE;
	self->any_require_reboot = FALSE;
	for (i = 0; list != NULL && i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE_LIVE)
			self->all_updates_are_live = FALSE;
		if (gs_app_has_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT))
			self->any_require_reboot = TRUE;
		gs_update_list_add_app (GS_UPDATE_LIST (self->list_box_updates), app);
	}

	/* change the button as to whether a reboot is required to
	 * apply all the updates */
	if (self->all_updates_are_live) {
		gtk_button_set_label (GTK_BUTTON (self->button_update_all),
				      /* TRANSLATORS: all updates will be installed */
				      _("_Install All"));
	} else {
		gtk_button_set_label (GTK_BUTTON (self->button_update_all),
				      /* TRANSLATORS: this is an offline update */
				      _("Restart & _Install"));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_updates_counter"));
	if (list != NULL && !gs_update_monitor_is_managed ()) {
		g_autofree gchar *text = NULL;
		text = g_strdup_printf ("%d", gs_app_list_length (list));
		gtk_label_set_label (GTK_LABEL (widget), text);
		gtk_widget_show (widget);
	} else {
		gtk_widget_hide (widget);
	}

	if (list != NULL &&
	    gs_shell_get_mode (self->shell) != GS_SHELL_MODE_UPDATES)
		gtk_style_context_add_class (gtk_widget_get_style_context (widget), "needs-attention");
	else
		gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "needs-attention");

	if (list == NULL) {
		gs_shell_updates_clear_flag (self, GS_SHELL_UPDATES_FLAG_HAS_UPDATES);
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("updates-shell: failed to get updates: %s", error->message);
		gtk_label_set_label (GTK_LABEL (self->label_updates_failed),
				     error->message);
		gs_shell_updates_set_state (self,
					    GS_SHELL_UPDATES_STATE_FAILED);
	}

	/* no results */
	if (gs_app_list_length (list) == 0) {
		g_debug ("updates-shell: no updates to show");
		gs_shell_updates_clear_flag (self, GS_SHELL_UPDATES_FLAG_HAS_UPDATES);
	} else {
		gs_shell_updates_set_flag (self, GS_SHELL_UPDATES_FLAG_HAS_UPDATES);
	}

	/* only when both set */
	if (--self->in_flight == 0)
		gs_shell_updates_set_state (self, GS_SHELL_UPDATES_STATE_IDLE);
}

static void
gs_shell_updates_get_upgrades_cb (GObject *source_object,
				  GAsyncResult *res,
				  gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsShellUpdates *self = GS_SHELL_UPDATES (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get the results */
	list = gs_plugin_loader_get_distro_upgrades_finish (plugin_loader, res, &error);
	if (list == NULL) {
		gs_shell_updates_clear_flag (self, GS_SHELL_UPDATES_FLAG_HAS_UPGRADES);
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_warning ("updates-shell: failed to get upgrades: %s",
				   error->message);
		}
	} else if (gs_app_list_length (list) == 0) {
		g_debug ("updates-shell: no upgrades to show");
	} else {
		GsApp *app = gs_app_list_index (list, 0);
		g_debug ("got upgrade %s", gs_app_get_id (app));
		gs_upgrade_banner_set_app (GS_UPGRADE_BANNER (self->upgrade_banner), app);
		gs_shell_updates_set_flag (self, GS_SHELL_UPDATES_FLAG_HAS_UPGRADES);
	}

	/* only when both set */
	if (--self->in_flight == 0)
		gs_shell_updates_set_state (self, GS_SHELL_UPDATES_STATE_IDLE);
}

/**
 * gs_shell_updates_load:
 */
static void
gs_shell_updates_load (GsShellUpdates *self)
{
	guint64 refine_flags;

	if (self->in_flight > 0)
		return;
	gs_container_remove_all (GTK_CONTAINER (self->list_box_updates));
	refine_flags = GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION;
	gs_shell_updates_set_state (self,
				    GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES);
	self->in_flight++;
	gs_plugin_loader_get_updates_async (self->plugin_loader,
					    refine_flags,
					    self->cancellable,
					    (GAsyncReadyCallback) gs_shell_updates_get_updates_cb,
					    self);

	/* don't refresh every each time */
	if ((self->result_flags & GS_SHELL_UPDATES_FLAG_HAS_UPGRADES) == 0) {
		gs_plugin_loader_get_distro_upgrades_async (self->plugin_loader,
							    refine_flags,
							    self->cancellable,
							    gs_shell_updates_get_upgrades_cb,
							    self);
		self->in_flight++;
	}
}

/**
 * gs_shell_updates_reload:
 */
void
gs_shell_updates_reload (GsShellUpdates *self)
{
	gs_shell_updates_invalidate (self);
	gs_shell_updates_load (self);
}

/**
 * gs_shell_updates_switch_to:
 **/
static void
gs_shell_updates_switch_to (GsPage *page,
			    gboolean scroll_up)
{
	GsShellUpdates *self = GS_SHELL_UPDATES (page);
	GtkWidget *widget;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_UPDATES) {
		g_warning ("Called switch_to(updates) when in mode %s",
			   gs_shell_get_mode_string (self->shell));
		return;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "buttonbox_main"));
	gtk_widget_show (widget);

	gtk_widget_set_visible (self->button_refresh, TRUE);

	if (scroll_up) {
		GtkAdjustment *adj;
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_updates));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	/* no need to refresh */
	if (self->cache_valid) {
		gs_shell_updates_update_ui_state (self);
		return;
	}

	if (self->state == GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES) {
		gs_shell_updates_update_ui_state (self);
		return;
	}
	gs_shell_updates_load (self);
}

static void
show_update_details (GsApp *app, GsShellUpdates *self)
{
	GtkWidget *dialog;

	dialog = gs_update_dialog_new (self->plugin_loader);
	gs_update_dialog_show_update_details (GS_UPDATE_DIALOG (dialog), app);
	gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));
}

/**
 * gs_shell_updates_activated_cb:
 **/
static void
gs_shell_updates_activated_cb (GtkListBox *list_box,
			       GtkListBoxRow *row,
			       GsShellUpdates *self)
{
	GsApp *app;

	app = gs_app_row_get_app (GS_APP_ROW (row));

	show_update_details (app, self);
}

/**
 * gs_shell_updates_button_clicked_cb:
 **/
static void
gs_shell_updates_button_clicked_cb (GsUpdateList *update_list,
				    GsApp *app,
				    GsShellUpdates *self)
{
	if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE_LIVE)
		gs_page_update_app (GS_PAGE (self), app);
}

/**
 * gs_shell_updates_refresh_cb:
 **/
static void
gs_shell_updates_refresh_cb (GsPluginLoader *plugin_loader,
			     GAsyncResult *res,
			     GsShellUpdates *self)
{
	gboolean ret;
	g_autoptr(GDateTime) now = NULL;
	g_autoptr(GError) error = NULL;

	/* get the results */
	ret = gs_plugin_loader_refresh_finish (plugin_loader, res, &error);
	if (!ret) {
		/* user cancel */
		if (g_error_matches (error,
				     G_IO_ERROR,
				     G_IO_ERROR_CANCELLED)) {
			gs_shell_updates_set_state (self, GS_SHELL_UPDATES_STATE_IDLE);
			return;
		}
		g_warning ("failed to refresh: %s", error->message);
		gtk_label_set_label (GTK_LABEL (self->label_updates_failed),
				     error->message);
		gs_shell_updates_set_state (self,
					    GS_SHELL_UPDATES_STATE_FAILED);
		return;
	}

	/* update the last checked timestamp */
	now = g_date_time_new_now_local ();
	g_settings_set (self->settings, "check-timestamp", "x",
			g_date_time_to_unix (now));

	/* get the new list */
	gs_shell_updates_invalidate (self);
	gs_page_switch_to (GS_PAGE (self), TRUE);
}

/**
 * gs_shell_updates_get_new_updates:
 **/
static void
gs_shell_updates_get_new_updates (GsShellUpdates *self)
{
	/* force a check for updates and download */
	gs_shell_updates_set_state (self, GS_SHELL_UPDATES_STATE_ACTION_REFRESH);

	if (self->cancellable_refresh != NULL) {
		g_cancellable_cancel (self->cancellable_refresh);
		g_object_unref (self->cancellable_refresh);
	}
	self->cancellable_refresh = g_cancellable_new ();

	gs_plugin_loader_refresh_async (self->plugin_loader,
					10 * 60,
					GS_PLUGIN_REFRESH_FLAGS_INTERACTIVE |
					GS_PLUGIN_REFRESH_FLAGS_METADATA |
					GS_PLUGIN_REFRESH_FLAGS_PAYLOAD,
					self->cancellable_refresh,
					(GAsyncReadyCallback) gs_shell_updates_refresh_cb,
					self);
}

/**
 * gs_shell_updates_show_network_settings:
 **/
static void
gs_shell_updates_show_network_settings (GsShellUpdates *self)
{
	g_autoptr(GError) error = NULL;
	if (!g_spawn_command_line_async ("gnome-control-center network", &error))
		g_warning ("Failed to open the control center: %s", error->message);
}

/**
 * gs_shell_updates_refresh_confirm_cb:
 **/
static void
gs_shell_updates_refresh_confirm_cb (GtkDialog *dialog,
				     GtkResponseType response_type,
				     GsShellUpdates *self)
{
	switch (response_type) {
	case GTK_RESPONSE_REJECT:
		/* open the control center */
		gs_shell_updates_show_network_settings (self);
		break;
	case GTK_RESPONSE_ACCEPT:
		self->has_agreed_to_mobile_data = TRUE;
		gs_shell_updates_get_new_updates (self);
		break;
	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
		break;
	default:
		g_assert_not_reached ();
	}
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

/**
 * gs_shell_updates_button_network_settings_cb:
 **/
static void
gs_shell_updates_button_network_settings_cb (GtkWidget *widget,
					     GsShellUpdates *self)
{
	gs_shell_updates_show_network_settings (self);
}

/**
 * gs_shell_updates_button_mobile_refresh_cb:
 **/
static void
gs_shell_updates_button_mobile_refresh_cb (GtkWidget *widget,
					   GsShellUpdates *self)
{
	self->has_agreed_to_mobile_data = TRUE;
	gs_shell_updates_get_new_updates (self);
}

/**
 * gs_shell_updates_button_refresh_cb:
 **/
static void
gs_shell_updates_button_refresh_cb (GtkWidget *widget,
				    GsShellUpdates *self)
{
	GtkWidget *dialog;

	/* cancel existing action? */
	if (self->state == GS_SHELL_UPDATES_STATE_ACTION_REFRESH) {
		g_cancellable_cancel (self->cancellable_refresh);
		g_clear_object (&self->cancellable_refresh);
		return;
	}

	/* we don't know the network state */
	if (self->network_monitor == NULL) {
		gs_shell_updates_get_new_updates (self);
		return;
	}

	/* check we have a "free" network connection */
	if (g_network_monitor_get_network_available (self->network_monitor) &&
	    !g_network_monitor_get_network_metered (self->network_monitor)) {
		gs_shell_updates_get_new_updates (self);

	/* expensive network connection */
	} else if (g_network_monitor_get_network_metered (self->network_monitor)) {
		if (self->has_agreed_to_mobile_data) {
			gs_shell_updates_get_new_updates (self);
			return;
		}
		dialog = gtk_message_dialog_new (gs_shell_get_window (self->shell),
						 GTK_DIALOG_MODAL |
						 GTK_DIALOG_USE_HEADER_BAR |
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CANCEL,
						 /* TRANSLATORS: this is to explain that downloading updates may cost money */
						 _("Charges may apply"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  /* TRANSLATORS: we need network
							   * to do the updates check */
							  _("Checking for updates while using mobile broadband could cause you to incur charges."));
		gtk_dialog_add_button (GTK_DIALOG (dialog),
				       /* TRANSLATORS: this is a link to the
					* control-center network panel */
				       _("Check Anyway"),
				       GTK_RESPONSE_ACCEPT);
		g_signal_connect (dialog, "response",
				  G_CALLBACK (gs_shell_updates_refresh_confirm_cb),
				  self);
		gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));

	/* no network connection */
	} else {
		dialog = gtk_message_dialog_new (gs_shell_get_window (self->shell),
						 GTK_DIALOG_MODAL |
						 GTK_DIALOG_USE_HEADER_BAR |
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CANCEL,
						 /* TRANSLATORS: can't do updates check */
						 _("No Network"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  /* TRANSLATORS: we need network
							   * to do the updates check */
							  _("Internet access is required to check for updates."));
		gtk_dialog_add_button (GTK_DIALOG (dialog),
				       /* TRANSLATORS: this is a link to the
					* control-center network panel */
				       _("Network Settings"),
				       GTK_RESPONSE_REJECT);
		g_signal_connect (dialog, "response",
				  G_CALLBACK (gs_shell_updates_refresh_confirm_cb),
				  self);
		gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));
	}
}

/**
 * gs_shell_updates_pending_apps_changed_cb:
 */
static void
gs_shell_updates_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
					  GsShellUpdates *self)
{
	gs_shell_updates_invalidate (self);
}

/**
 * cancel_trigger_failed_cb:
 **/
static void
cancel_trigger_failed_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GsShellUpdates *self = GS_SHELL_UPDATES (user_data);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_app_action_finish (self->plugin_loader, res, &error)) {
		g_warning ("failed to cancel trigger: %s", error->message);
		return;
	}
}

/**
 * gs_shell_updates_reboot_failed_cb:
 **/
static void
gs_shell_updates_reboot_failed_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GsShellUpdates *self = GS_SHELL_UPDATES (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) apps = NULL;
	g_autoptr(GVariant) retval = NULL;

	/* get result */
	retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
	if (retval != NULL)
		return;

	if (error != NULL) {
		g_warning ("Calling org.gnome.SessionManager.Reboot failed: %s",
			   error->message);
	}

	/* cancel trigger */
	apps = gs_update_list_get_apps (GS_UPDATE_LIST (self->list_box_updates));
	gs_plugin_loader_app_action_async (self->plugin_loader,
					   gs_app_list_index (apps, 0),
					   GS_PLUGIN_LOADER_ACTION_UPDATE_CANCEL,
					   self->cancellable,
					   cancel_trigger_failed_cb,
					   self);
}

/**
 * gs_shell_updates_perform_update_cb:
 **/
static void
gs_shell_updates_perform_update_cb (GsPluginLoader *plugin_loader,
                                    GAsyncResult *res,
                                    GsShellUpdates *self)
{
	g_autoptr(GError) error = NULL;

	/* unconditionally re-enable this */
	gtk_widget_set_sensitive (GTK_WIDGET (self->button_update_all), TRUE);

	/* get the results */
	if (!gs_plugin_loader_update_finish (plugin_loader, res, &error)) {
		g_warning ("Failed to perform update: %s", error->message);
		return;
	}

	/* trigger reboot if any application was not updatable live */
	if (!self->all_updates_are_live) {
		g_autoptr(GDBusConnection) bus = NULL;
		bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
		g_dbus_connection_call (bus,
					"org.gnome.SessionManager",
					"/org/gnome/SessionManager",
					"org.gnome.SessionManager",
					"Reboot",
					NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
					G_MAXINT, NULL,
					gs_shell_updates_reboot_failed_cb,
					self);

	/* when we are not doing an offline update, show a notification
	 * if any application requires a reboot */
	} else if (self->any_require_reboot) {
		g_autoptr(GNotification) n = NULL;
		/* TRANSLATORS: we've just live-updated some apps */
		n = g_notification_new (_("Updates have been installed"));
		/* TRANSLATORS: the new apps will not be run until we restart */
		g_notification_set_body (n, _("A restart is required for them to take effect."));
		/* TRANSLATORS: button text */
		g_notification_add_button (n, _("Not Now"), "app.nop");
		/* TRANSLATORS: button text */
		g_notification_add_button_with_target (n, _("Restart"), "app.reboot", NULL);
		g_notification_set_default_action_and_target (n, "app.set-mode", "s", "updates");
		g_application_send_notification (g_application_get_default (), "restart-required", n);
	}
}

static void
gs_shell_updates_button_update_all_cb (GtkButton      *button,
				       GsShellUpdates *self)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) apps = NULL;

	/* do the offline update */
	apps = gs_update_list_get_apps (GS_UPDATE_LIST (self->list_box_updates));
	gs_plugin_loader_update_async (self->plugin_loader,
				       apps,
				       self->cancellable,
				       (GAsyncReadyCallback) gs_shell_updates_perform_update_cb,
				       self);
	gtk_widget_set_sensitive (GTK_WIDGET (self->button_update_all), FALSE);
}

typedef struct {
	GsApp		*app;
	GsShellUpdates	*self;
} GsPageHelper;

static void
gs_page_helper_free (GsPageHelper *helper)
{
	if (helper->app != NULL)
		g_object_unref (helper->app);
	if (helper->self != NULL)
		g_object_unref (helper->self);
	g_slice_free (GsPageHelper, helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsPageHelper, gs_page_helper_free);

static void
upgrade_download_finished_cb (GObject *source,
                              GAsyncResult *res,
                              gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GError *last_error;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;

	if (!gs_plugin_loader_app_action_finish (plugin_loader, res, &error)) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			return;
		g_warning ("failed to upgrade-download: %s", error->message);
	}

	last_error = gs_app_get_last_error (helper->app);
	if (last_error != NULL) {
		g_warning ("failed to upgrade-download %s: %s",
		           gs_app_get_id (helper->app),
		           last_error->message);
		gs_app_notify_failed_modal (helper->app,
					    gs_shell_get_window (helper->self->shell),
					    GS_PLUGIN_LOADER_ACTION_UPGRADE_DOWNLOAD,
					    last_error);
		return;
	}
}

static void
gs_shell_updates_upgrade_download_cb (GsUpgradeBanner *upgrade_banner,
                                      GsShellUpdates *self)
{
	GsApp *app;
	GsPageHelper *helper;

	app = gs_upgrade_banner_get_app (upgrade_banner);
	if (app == NULL) {
		g_warning ("no upgrade available to download");
		return;
	}

	helper = g_slice_new0 (GsPageHelper);
	helper->app = g_object_ref (app);
	helper->self = g_object_ref (self);

	if (self->cancellable_upgrade_download != NULL)
		g_object_unref (self->cancellable_upgrade_download);
	self->cancellable_upgrade_download = g_cancellable_new ();
	gs_plugin_loader_app_action_async (self->plugin_loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_UPGRADE_DOWNLOAD,
					   self->cancellable_upgrade_download,
					   upgrade_download_finished_cb,
					   helper);
}

static void
upgrade_reboot_failed_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data)
{
	GsShellUpdates *self = (GsShellUpdates *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) apps = NULL;
	g_autoptr(GVariant) retval = NULL;

	/* get result */
	retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
	if (retval != NULL)
		return;

	if (error != NULL) {
		g_warning ("Calling org.gnome.SessionManager.Reboot failed: %s",
			   error->message);
	}

	/* cancel trigger */
	apps = gs_update_list_get_apps (GS_UPDATE_LIST (self->list_box_updates));
	gs_plugin_loader_app_action_async (self->plugin_loader,
					   gs_app_list_index (apps, 0),
					   GS_PLUGIN_LOADER_ACTION_UPDATE_CANCEL,
					   self->cancellable,
					   cancel_trigger_failed_cb,
					   self);
}

static void
upgrade_trigger_finished_cb (GObject *source,
                             GAsyncResult *res,
                             gpointer user_data)
{
	GsShellUpdates *self = (GsShellUpdates *) user_data;
	g_autoptr(GDBusConnection) bus = NULL;
	g_autoptr(GError) error = NULL;

	/* get the results */
	if (!gs_plugin_loader_update_finish (self->plugin_loader, res, &error)) {
		g_warning ("Failed to trigger offline update: %s", error->message);
		return;
	}

	/* trigger reboot */
	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	g_dbus_connection_call (bus,
				"org.gnome.SessionManager",
				"/org/gnome/SessionManager",
				"org.gnome.SessionManager",
				"Reboot",
				NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
				G_MAXINT, NULL,
				upgrade_reboot_failed_cb,
				self);
}

static void
gs_shell_updates_upgrade_install_cb (GsUpgradeBanner *upgrade_banner,
                                     GsShellUpdates *self)
{
	GsApp *app;

	app = gs_upgrade_banner_get_app (upgrade_banner);
	if (app == NULL) {
		g_warning ("no upgrade available to install");
		return;
	}

	gs_plugin_loader_app_action_async (self->plugin_loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_UPGRADE_TRIGGER,
					   self->cancellable,
					   upgrade_trigger_finished_cb,
					   self);
}

static void
gs_shell_updates_upgrade_help_cb (GsUpgradeBanner *upgrade_banner,
				  GsShellUpdates *self)
{
	GsApp *app;
	const gchar *uri;
	g_autoptr(GError) error = NULL;

	app = gs_upgrade_banner_get_app (upgrade_banner);
	if (app == NULL) {
		g_warning ("no upgrade available to launch");
		return;
	}

	/* open the link */
	uri = gs_app_get_url (app, AS_URL_KIND_HOMEPAGE);
	if (!gtk_show_uri (NULL, uri, GDK_CURRENT_TIME, &error))
		g_warning ("failed to open %s: %s", uri, error->message);
}

static void
gs_shell_updates_invalidate_downloaded_upgrade (GsShellUpdates *self)
{
	GsApp *app;
	app = gs_upgrade_banner_get_app (GS_UPGRADE_BANNER (self->upgrade_banner));
	if (app == NULL)
		return;
	if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE)
		return;
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	g_debug ("resetting %s to AVAILABLE as the updates have changed",
		 gs_app_get_id (app));
}

static void
gs_shell_updates_changed_cb (GsPluginLoader *plugin_loader,
			     GsShellUpdates *self)
{
	/* if we do a live update and the upgrade is waiting to be deployed
	 * then make sure all new packages are downloaded */
	gs_shell_updates_invalidate_downloaded_upgrade (self);
}

/**
 * gs_shell_updates_status_changed_cb:
 **/
static void
gs_shell_updates_status_changed_cb (GsPluginLoader *plugin_loader,
				    GsApp *app,
				    GsPluginStatus status,
				    GsShellUpdates *self)
{
	switch (status) {
	case GS_PLUGIN_STATUS_INSTALLING:
	case GS_PLUGIN_STATUS_REMOVING:
		if (gs_app_get_kind (app) != AS_APP_KIND_OS_UPGRADE &&
		    gs_app_get_id (app) != NULL) {
			/* if we do a install or remove then make sure all new
			 * packages are downloaded */
			gs_shell_updates_invalidate_downloaded_upgrade (self);
		}
		break;
	default:
		break;
	}

	self->last_status = status;
	gs_shell_updates_update_ui_state (self);
}

static void
on_permission_changed (GPermission *permission,
                       GParamSpec  *pspec,
                       gpointer     data)
{
	GsShellUpdates *self = GS_SHELL_UPDATES (data);
	if (gs_update_monitor_is_managed()) {
		gs_shell_updates_set_state (self, GS_SHELL_UPDATES_STATE_MANAGED);
		return;
	}
	gs_shell_updates_set_state (self, GS_SHELL_UPDATES_STATE_IDLE);
}

static void
gs_shell_updates_monitor_permission (GsShellUpdates *self)
{
        GPermission *permission;

        permission = gs_update_monitor_permission_get ();
	if (permission != NULL)
		g_signal_connect (permission, "notify",
				  G_CALLBACK (on_permission_changed), self);
}

static void
gs_shell_updates_upgrade_cancel_cb (GsUpgradeBanner *upgrade_banner,
				    GsShellUpdates *self)
{
	g_cancellable_cancel (self->cancellable_upgrade_download);
}

void
gs_shell_updates_setup (GsShellUpdates *self,
			GsShell *shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	AtkObject *accessible;

	g_return_if_fail (GS_IS_SHELL_UPDATES (self));

	self->shell = shell;

	self->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (self->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_shell_updates_pending_apps_changed_cb),
			  self);
	g_signal_connect (self->plugin_loader, "status-changed",
			  G_CALLBACK (gs_shell_updates_status_changed_cb),
			  self);
	g_signal_connect (self->plugin_loader, "updates-changed",
			  G_CALLBACK (gs_shell_updates_changed_cb),
			  self);
	self->builder = g_object_ref (builder);
	self->cancellable = g_object_ref (cancellable);

	/* setup updates */
	g_signal_connect (self->list_box_updates, "row-activated",
			  G_CALLBACK (gs_shell_updates_activated_cb), self);
	g_signal_connect (self->list_box_updates, "button-clicked",
			  G_CALLBACK (gs_shell_updates_button_clicked_cb), self);

	/* setup system upgrades */
	g_signal_connect (self->upgrade_banner, "download-clicked",
			  G_CALLBACK (gs_shell_updates_upgrade_download_cb), self);
	g_signal_connect (self->upgrade_banner, "install-clicked",
			  G_CALLBACK (gs_shell_updates_upgrade_install_cb), self);
	g_signal_connect (self->upgrade_banner, "cancel-clicked",
			  G_CALLBACK (gs_shell_updates_upgrade_cancel_cb), self);
	g_signal_connect (self->upgrade_banner, "help-clicked",
			  G_CALLBACK (gs_shell_updates_upgrade_help_cb), self);

	self->header_end_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_visible (self->header_end_box, TRUE);
	gs_page_set_header_end_widget (GS_PAGE (self), self->header_end_box);

	self->button_update_all = gtk_button_new_with_mnemonic (_("Restart & _Install"));
	gtk_widget_set_valign (self->button_update_all, GTK_ALIGN_CENTER);
	gtk_widget_set_visible (self->button_update_all, TRUE);
	gtk_style_context_add_class (gtk_widget_get_style_context (self->button_update_all), "suggested-action");
	gtk_container_add (GTK_CONTAINER (self->header_end_box), self->button_update_all);
	g_signal_connect (self->button_update_all, "clicked", G_CALLBACK (gs_shell_updates_button_update_all_cb), self);

	self->header_start_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_visible (self->header_start_box, TRUE);
	gs_page_set_header_start_widget (GS_PAGE (self), self->header_start_box);

	self->header_spinner_start = gtk_spinner_new ();
	gtk_box_pack_end (GTK_BOX (self->header_start_box), self->header_spinner_start, FALSE, FALSE, 0);

	/* setup update details window */
	self->button_refresh = gtk_button_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_MENU);
	accessible = gtk_widget_get_accessible (self->button_refresh);
	if (accessible != NULL)
		atk_object_set_name (accessible, _("Check for updates"));
	gtk_box_pack_start (GTK_BOX (self->header_start_box), self->button_refresh, FALSE, FALSE, 0);
	g_signal_connect (self->button_refresh, "clicked",
			  G_CALLBACK (gs_shell_updates_button_refresh_cb),
			  self);

	g_signal_connect (self->button_updates_mobile, "clicked",
			  G_CALLBACK (gs_shell_updates_button_mobile_refresh_cb),
			  self);
	g_signal_connect (self->button_updates_offline, "clicked",
			  G_CALLBACK (gs_shell_updates_button_network_settings_cb),
			  self);

	gs_shell_updates_monitor_permission (self);

	/* set initial state */
	if (gs_update_monitor_is_managed ())
		self->state = GS_SHELL_UPDATES_STATE_MANAGED;

	if (self->network_monitor != NULL) {
		self->network_changed_handler = g_signal_connect (self->network_monitor, "network-changed",
						G_CALLBACK (gs_shell_updates_notify_network_state_cb),
						self);
	}

	/* chain up */
	gs_page_setup (GS_PAGE (self),
	               shell,
	               plugin_loader,
	               cancellable);
}

/**
 * gs_shell_updates_dispose:
 **/
static void
gs_shell_updates_dispose (GObject *object)
{
	GsShellUpdates *self = GS_SHELL_UPDATES (object);

	if (self->network_changed_handler != 0) {
		g_signal_handler_disconnect (self->network_monitor, self->network_changed_handler);
		self->network_changed_handler = 0;
	}
	if (self->cancellable_refresh != NULL) {
		g_cancellable_cancel (self->cancellable_refresh);
		g_clear_object (&self->cancellable_refresh);
	}

	g_clear_object (&self->builder);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);
	g_clear_object (&self->settings);
	g_clear_object (&self->desktop_settings);

	G_OBJECT_CLASS (gs_shell_updates_parent_class)->dispose (object);
}

/**
 * gs_shell_updates_class_init:
 **/
static void
gs_shell_updates_class_init (GsShellUpdatesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_updates_dispose;
	page_class->switch_to = gs_shell_updates_switch_to;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-updates.ui");

	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, updates_box);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, button_updates_mobile);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, button_updates_offline);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, fake_header_bar);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, label_updates_failed);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, label_updates_last_checked);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, label_updates_spinner);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, list_box_updates);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, scrolledwindow_updates);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, spinner_updates);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, stack_updates);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, upgrade_banner);
}

/**
 * gs_shell_updates_init:
 **/
static void
gs_shell_updates_init (GsShellUpdates *self)
{
	const char *ampm;

	gtk_widget_init_template (GTK_WIDGET (self));

	self->network_monitor = g_network_monitor_get_default ();
	self->state = GS_SHELL_UPDATES_STATE_STARTUP;
	self->settings = g_settings_new ("org.gnome.software");
	self->desktop_settings = g_settings_new ("org.gnome.desktop.interface");

	ampm = nl_langinfo (AM_STR);
	if (ampm != NULL && *ampm != '\0')
		self->ampm_available = TRUE;
}

/**
 * gs_shell_updates_new:
 **/
GsShellUpdates *
gs_shell_updates_new (void)
{
	GsShellUpdates *self;
	self = g_object_new (GS_TYPE_SHELL_UPDATES, NULL);
	return GS_SHELL_UPDATES (self);
}

/* vim: set noexpandtab: */
