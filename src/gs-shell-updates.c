/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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
#include <packagekit-glib2/packagekit.h>

#include "gs-shell.h"
#include "gs-shell-updates.h"
#include "gs-utils.h"
#include "gs-offline-updates.h"
#include "gs-app.h"
#include "gs-app-row.h"
#include "gs-markdown.h"
#include "gs-update-dialog.h"
#include "gs-update-list.h"
#include "gs-application.h"

#include <gdesktop-enums.h>
#include <langinfo.h>

typedef enum {
	GS_SHELL_UPDATES_STATE_STARTUP,
	GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES,
	GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES,
	GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES,
	GS_SHELL_UPDATES_STATE_NO_UPDATES,
	GS_SHELL_UPDATES_STATE_MANAGED,
	GS_SHELL_UPDATES_STATE_HAS_UPDATES,
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
	GSettings		*settings;
	GSettings		*desktop_settings;
	gboolean		 cache_valid;
	gboolean		 in_progress;
	GsShell			*shell;
	PkControl		*control;
	GsPluginStatus		 last_status;
	GsShellUpdatesState	 state;
	gboolean		 has_agreed_to_mobile_data;
	gboolean		 ampm_available;

	GtkWidget		*button_updates_mobile;
	GtkWidget		*button_updates_offline;
	GtkWidget		*label_updates_failed;
	GtkWidget		*label_updates_last_checked;
	GtkWidget		*label_updates_spinner;
	GtkWidget		*list_box_updates;
	GtkWidget		*scrolledwindow_updates;
	GtkWidget		*spinner_updates;
	GtkWidget		*stack_updates;
};

enum {
	COLUMN_UPDATE_APP,
	COLUMN_UPDATE_NAME,
	COLUMN_UPDATE_VERSION,
	COLUMN_UPDATE_LAST
};

G_DEFINE_TYPE (GsShellUpdates, gs_shell_updates, GS_TYPE_PAGE)

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
	GtkWidget *widget;
	PkNetworkEnum network_state;
	gboolean is_free_connection;
	g_autofree gchar *checked_str = NULL;
	g_autofree gchar *spinner_str = NULL;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_UPDATES)
		return;

	/* get the current network state */
	g_object_get (self->control, "network-state", &network_state, NULL);
	switch (network_state) {
	case PK_NETWORK_ENUM_ONLINE:
	case PK_NETWORK_ENUM_WIFI:
	case PK_NETWORK_ENUM_WIRED:
		is_free_connection = TRUE;
		break;
	default:
		is_free_connection = FALSE;
		break;
	}

	/* main spinner */
	switch (self->state) {
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
		gs_start_spinner (GTK_SPINNER (self->spinner_updates));
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_MANAGED:
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_FAILED:
		gs_stop_spinner (GTK_SPINNER (self->spinner_updates));
		break;
	default:
		g_assert_not_reached ();
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
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
		spinner_str = g_strdup_printf ("%s\n%s",
				       gs_shell_updates_get_state_string (self->last_status),
				       /* TRANSLATORS: the updates panel is starting up */
				       _("(This could take a while)"));
		gtk_label_set_label (GTK_LABEL (self->label_updates_spinner), spinner_str);
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_MANAGED:
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_FAILED:
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* headerbar spinner */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "header_spinner_start"));
	switch (self->state) {
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
		gtk_widget_show (widget);
		gtk_spinner_start (GTK_SPINNER (widget));
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_MANAGED:
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_FAILED:
		gtk_spinner_stop (GTK_SPINNER (widget));
		gtk_widget_hide (widget);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* headerbar refresh icon */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_refresh_image"));
	switch (self->state) {
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
		gtk_image_set_from_icon_name (GTK_IMAGE (widget),
					      "media-playback-stop-symbolic", GTK_ICON_SIZE_MENU);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_refresh"));
		gtk_widget_show (widget);
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_MANAGED:
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_refresh"));
		gtk_widget_hide (widget);
		break;
	case GS_SHELL_UPDATES_STATE_FAILED:
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
		gtk_image_set_from_icon_name (GTK_IMAGE (widget),
					      "view-refresh-symbolic", GTK_ICON_SIZE_MENU);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_refresh"));
		gtk_widget_show (widget);
		break;
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
		gtk_image_set_from_icon_name (GTK_IMAGE (widget),
					      "view-refresh-symbolic", GTK_ICON_SIZE_MENU);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_refresh"));
		gtk_widget_set_visible (widget,
					is_free_connection || self->has_agreed_to_mobile_data);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* headerbar update button */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_update_all"));
	switch (self->state) {
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
		gtk_widget_show (widget);
		break;
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_MANAGED:
	case GS_SHELL_UPDATES_STATE_FAILED:
		gtk_widget_hide (widget);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* stack */
	switch (self->state) {
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "spinner");
		break;
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
		/* check we have a "free" network connection */
		switch (network_state) {
		case PK_NETWORK_ENUM_ONLINE:
		case PK_NETWORK_ENUM_WIFI:
		case PK_NETWORK_ENUM_WIRED:
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "uptodate");
			break;
		case PK_NETWORK_ENUM_OFFLINE:
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "offline");
			break;
		case PK_NETWORK_ENUM_MOBILE:
			if (self->has_agreed_to_mobile_data) {
				gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "uptodate");
			} else {
				gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "mobile");
			}
			break;
		default:
			break;
		}
		break;
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "view");
		break;
	case GS_SHELL_UPDATES_STATE_MANAGED:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "managed");
		break;
	case GS_SHELL_UPDATES_STATE_FAILED:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "failed");
		break;
	default:
		g_assert_not_reached ();
		break;
	}

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
gs_shell_updates_set_state (GsShellUpdates *self,
			    GsShellUpdatesState state)
{
	self->state = state;
	if (gs_updates_are_managed ())
		self->state = GS_SHELL_UPDATES_STATE_MANAGED;
	gs_shell_updates_update_ui_state (self);
}

/**
 * gs_shell_updates_notify_network_state_cb:
 **/
static void
gs_shell_updates_notify_network_state_cb (PkControl *control,
					  GParamSpec *pspec,
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
	GList *l;
	GtkWidget *widget;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	self->cache_valid = TRUE;

	/* get the results */
	list = gs_plugin_loader_get_updates_finish (plugin_loader, res, &error);
	for (l = list; l != NULL; l = l->next) {
		gs_update_list_add_app (GS_UPDATE_LIST (self->list_box_updates),
					GS_APP (l->data));
	}

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_updates_counter"));
	if (list != NULL && !gs_updates_are_managed ()) {
		g_autofree gchar *text = NULL;
		text = g_strdup_printf ("%d", g_list_length (list));
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
		if (g_error_matches (error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_NO_RESULTS)) {
			g_debug ("no updates to show");
			gs_shell_updates_set_state (self,
						    GS_SHELL_UPDATES_STATE_NO_UPDATES);
		} else {
			if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
				g_warning ("failed to get updates: %s", error->message);
			gtk_label_set_label (GTK_LABEL (self->label_updates_failed),
					     error->message);
			gs_shell_updates_set_state (self,
						    GS_SHELL_UPDATES_STATE_FAILED);
		}
	} else {
		gs_shell_updates_set_state (self,
					    GS_SHELL_UPDATES_STATE_HAS_UPDATES);
	}

	self->in_progress = FALSE;
}

/**
 * gs_shell_updates_load:
 */
static void
gs_shell_updates_load (GsShellUpdates *self)
{
	guint64 refine_flags;

	if (self->in_progress)
		return;
	self->in_progress = TRUE;
	gs_container_remove_all (GTK_CONTAINER (self->list_box_updates));
	refine_flags = GS_PLUGIN_REFINE_FLAGS_DEFAULT |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION;
	gs_shell_updates_set_state (self,
				    GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES);
	gs_plugin_loader_get_updates_async (self->plugin_loader,
					    refine_flags,
					    self->cancellable,
					    (GAsyncReadyCallback) gs_shell_updates_get_updates_cb,
					    self);
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
void
gs_shell_updates_switch_to (GsShellUpdates *self,
			    gboolean scroll_up)
{
	GtkWidget *widget;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_UPDATES) {
		g_warning ("Called switch_to(updates) when in mode %s",
			   gs_shell_get_mode_string (self->shell));
		return;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "buttonbox_main"));
	gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_refresh"));
	gtk_widget_set_visible (widget, TRUE);

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

	gtk_window_set_transient_for (GTK_WINDOW (dialog), gs_shell_get_window (self->shell));
	gtk_window_present (GTK_WINDOW (dialog));
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
			switch (self->state) {
			case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
				gs_shell_updates_set_state (self,
							    GS_SHELL_UPDATES_STATE_HAS_UPDATES);
				break;
			case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
				gs_shell_updates_set_state (self,
							    GS_SHELL_UPDATES_STATE_NO_UPDATES);
				break;
			default:
				g_assert_not_reached ();
				break;
			}
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
	gs_shell_updates_switch_to (self, TRUE);
}

/**
 * gs_shell_updates_get_new_updates:
 **/
static void
gs_shell_updates_get_new_updates (GsShellUpdates *self)
{
	/* force a check for updates and download */
	gs_shell_updates_set_state (self,
				    self->state == GS_SHELL_UPDATES_STATE_HAS_UPDATES ?
				    GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES :
				    GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES);

	if (self->cancellable_refresh != NULL) {
		g_cancellable_cancel (self->cancellable_refresh);
		g_object_unref (self->cancellable_refresh);
	}
	self->cancellable_refresh = g_cancellable_new ();

	gs_plugin_loader_refresh_async (self->plugin_loader,
					10 * 60,
					GS_PLUGIN_REFRESH_FLAGS_UPDATES,
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
	PkNetworkEnum network_state;

	/* cancel existing action? */
	if (self->state == GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES ||
	    self->state == GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES) {
		g_cancellable_cancel (self->cancellable_refresh);
		g_clear_object (&self->cancellable_refresh);
		return;
	}

	/* check we have a "free" network connection */
	g_object_get (self->control,
		      "network-state", &network_state,
		      NULL);
	switch (network_state) {
	case PK_NETWORK_ENUM_ONLINE:
	case PK_NETWORK_ENUM_WIFI:
	case PK_NETWORK_ENUM_WIRED:
		gs_shell_updates_get_new_updates (self);
		break;
	case PK_NETWORK_ENUM_OFFLINE:
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
		gtk_window_present (GTK_WINDOW (dialog));
		break;
	case PK_NETWORK_ENUM_MOBILE:
		if (self->has_agreed_to_mobile_data) {
			gs_shell_updates_get_new_updates (self);
			break;
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
		gtk_window_present (GTK_WINDOW (dialog));
		break;
	default:
		g_assert_not_reached ();
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

static void
gs_offline_updates_cancel (void)
{
	g_autoptr(GError) error = NULL;
	if (!pk_offline_cancel (NULL, &error))
		g_warning ("failed to cancel the offline update: %s", error->message);
}

/**
 * gs_shell_updates_offline_update_cb:
 **/
static void
gs_shell_updates_offline_update_cb (GsPluginLoader *plugin_loader,
                                    GAsyncResult *res,
                                    GsShellUpdates *self)
{
	g_autoptr(GError) error = NULL;

	/* get the results */
	if (!gs_plugin_loader_offline_update_finish (plugin_loader, res, &error)) {
		g_warning ("Failed to trigger offline update: %s", error->message);
		return;
	}
	gs_reboot (gs_offline_updates_cancel);
}

static void
gs_shell_updates_button_update_all_cb (GtkButton      *button,
				       GsShellUpdates *self)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GList) apps = NULL;

	/* do the offline update */
	apps = gs_update_list_get_apps (GS_UPDATE_LIST (self->list_box_updates));
	gs_plugin_loader_offline_update_async (self->plugin_loader,
	                                       apps,
	                                       self->cancellable,
	                                       (GAsyncReadyCallback) gs_shell_updates_offline_update_cb,
	                                       self);
}

/**
 * gs_shell_updates_get_properties_cb:
 **/
static void
gs_shell_updates_get_properties_cb (GObject *source,
				    GAsyncResult *res,
				    gpointer user_data)
{
	GsShellUpdates *self = GS_SHELL_UPDATES (user_data);
	PkControl *control = PK_CONTROL (source);
	g_autoptr(GError) error = NULL;

	/* get result */
	if (!pk_control_get_properties_finish (control, res, &error))
		g_warning ("failed to get properties: %s", error->message);
	gs_shell_updates_update_ui_state (self);
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
	self->last_status = status;
	gs_shell_updates_update_ui_state (self);
}

static void
on_permission_changed (GPermission *permission,
                       GParamSpec  *pspec,
                       gpointer     data)
{
        GsShellUpdates *self = data;

	if (gs_updates_are_managed()) {
		gs_shell_updates_set_state (self, GS_SHELL_UPDATES_STATE_MANAGED);
	}
	else if (self->state == GS_SHELL_UPDATES_STATE_MANAGED) {
		gs_shell_updates_set_state (self, GS_SHELL_UPDATES_STATE_NO_UPDATES);
	}
}

static void
gs_shell_updates_monitor_permission (GsShellUpdates *self)
{
        GPermission *permission;

        permission = gs_offline_updates_permission_get ();
        g_signal_connect (permission, "notify",
                          G_CALLBACK (on_permission_changed), self);
}

void
gs_shell_updates_setup (GsShellUpdates *self,
			GsShell *shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL_UPDATES (self));

	self->shell = shell;

	self->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (self->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_shell_updates_pending_apps_changed_cb),
			  self);
	g_signal_connect (self->plugin_loader, "status-changed",
			  G_CALLBACK (gs_shell_updates_status_changed_cb),
			  self);
	self->builder = g_object_ref (builder);
	self->cancellable = g_object_ref (cancellable);

	/* setup updates */
	g_signal_connect (self->list_box_updates, "row-activated",
			  G_CALLBACK (gs_shell_updates_activated_cb), self);
	g_signal_connect (self->list_box_updates, "button-clicked",
			  G_CALLBACK (gs_shell_updates_button_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_update_all"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gs_shell_updates_button_update_all_cb), self);

	/* setup update details window */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_refresh"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_updates_button_refresh_cb),
			  self);
	g_signal_connect (self->button_updates_mobile, "clicked",
			  G_CALLBACK (gs_shell_updates_button_mobile_refresh_cb),
			  self);
	g_signal_connect (self->button_updates_offline, "clicked",
			  G_CALLBACK (gs_shell_updates_button_network_settings_cb),
			  self);

	gs_shell_updates_monitor_permission (self);

	g_signal_connect (self->control, "notify::network-state",
			  G_CALLBACK (gs_shell_updates_notify_network_state_cb),
			  self);

	/* get the initial network state */
	pk_control_get_properties_async (self->control, cancellable,
					 gs_shell_updates_get_properties_cb,
					 self);

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

	if (self->cancellable_refresh != NULL) {
		g_cancellable_cancel (self->cancellable_refresh);
		g_clear_object (&self->cancellable_refresh);
	}

	g_clear_object (&self->builder);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);
	g_clear_object (&self->control);
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
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_updates_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-updates.ui");

	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, button_updates_mobile);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, button_updates_offline);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, label_updates_failed);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, label_updates_last_checked);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, label_updates_spinner);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, list_box_updates);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, scrolledwindow_updates);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, spinner_updates);
	gtk_widget_class_bind_template_child (widget_class, GsShellUpdates, stack_updates);
}

/**
 * gs_shell_updates_init:
 **/
static void
gs_shell_updates_init (GsShellUpdates *self)
{
	const char *ampm;

	gtk_widget_init_template (GTK_WIDGET (self));

	self->control = pk_control_new ();
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
