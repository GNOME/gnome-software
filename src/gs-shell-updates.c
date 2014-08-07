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

#include "gs-shell.h"
#include "gs-shell-updates.h"
#include "gs-utils.h"
#include "gs-offline-updates.h"
#include "gs-app.h"
#include "gs-app-row.h"
#include "gs-markdown.h"
#include "gs-update-dialog.h"
#include "gs-update-list.h"

#include <gdesktop-enums.h>
#include <langinfo.h>
/* this isn't ideal, as PK should be abstracted away in a plugin, but
 * GNetworkMonitor doesn't provide us with a connection type */
#include <packagekit-glib2/packagekit.h>

static void	gs_shell_updates_finalize	(GObject	*object);

typedef enum {
	GS_SHELL_UPDATES_STATE_STARTUP,
	GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES,
	GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES,
	GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES,
	GS_SHELL_UPDATES_STATE_NO_UPDATES,
	GS_SHELL_UPDATES_STATE_HAS_UPDATES,
	GS_SHELL_UPDATES_STATE_FAILED,
	GS_SHELL_UPDATES_STATE_LAST,
} GsShellUpdatesState;

struct GsShellUpdatesPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GCancellable		*cancellable_refresh;
	GSettings		*settings;
	GSettings		*desktop_settings;
	gboolean		 cache_valid;
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

G_DEFINE_TYPE_WITH_PRIVATE (GsShellUpdates, gs_shell_updates, GTK_TYPE_BIN)

/**
 * gs_shell_updates_invalidate:
 **/
void
gs_shell_updates_invalidate (GsShellUpdates *shell_updates)
{
	shell_updates->priv->cache_valid = FALSE;
}

static GDateTime *
time_next_midnight (void)
{
	GDateTime *now;
	GDateTime *next_midnight;
	GTimeSpan since_midnight;

	now = g_date_time_new_now_local ();
	since_midnight = g_date_time_get_hour (now) * G_TIME_SPAN_HOUR +
	                 g_date_time_get_minute (now) * G_TIME_SPAN_MINUTE +
	                 g_date_time_get_second (now) * G_TIME_SPAN_SECOND +
	                 g_date_time_get_microsecond (now);
	next_midnight = g_date_time_add (now, G_TIME_SPAN_DAY - since_midnight);
	g_date_time_unref (now);

	return next_midnight;
}

static gchar *
gs_shell_updates_last_checked_time_string (GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GDesktopClockFormat clock_format;
	GDateTime *last_checked;
	GDateTime *midnight;
	const gchar *format_string;
	gchar *time_string;
	gboolean use_24h_time;
	gint64 tmp;
	gint days_ago;

	g_settings_get (priv->settings, "check-timestamp", "x", &tmp);
	if (tmp == 0)
		return NULL;
	last_checked = g_date_time_new_from_unix_local (tmp);

	midnight = time_next_midnight ();
	days_ago = g_date_time_difference (midnight, last_checked) / G_TIME_SPAN_DAY;

	clock_format = g_settings_get_enum (priv->desktop_settings, "clock-format");
	use_24h_time = (clock_format == G_DESKTOP_CLOCK_FORMAT_24H || priv->ampm_available == FALSE);

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

	g_date_time_unref (last_checked);
	g_date_time_unref (midnight);

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
gs_shell_updates_update_ui_state (GsShellUpdates *shell_updates)
{
	gchar *tmp;
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *widget;
	PkNetworkEnum network_state;
	gboolean is_free_connection;

	/* get the current network state */
	g_object_get (priv->control, "network-state", &network_state, NULL);
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
	switch (priv->state) {
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
		gs_start_spinner (GTK_SPINNER (priv->spinner_updates));
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_FAILED:
		gs_stop_spinner (GTK_SPINNER (priv->spinner_updates));
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* spinner text */
	switch (priv->state) {
	case GS_SHELL_UPDATES_STATE_STARTUP:
		tmp = g_strdup_printf ("%s\n%s",
				       /* TRANSLATORS: the updates panel is starting up */
				       _("Setting up updates…"),
				       _("(This could take a while)"));
		gtk_label_set_label (GTK_LABEL (priv->label_updates_spinner), tmp);
		g_free (tmp);
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
		tmp = g_strdup_printf ("%s\n%s",
				       gs_shell_updates_get_state_string (priv->last_status),
				       /* TRANSLATORS: the updates panel is starting up */
				       _("(This could take a while)"));
		gtk_label_set_label (GTK_LABEL (priv->label_updates_spinner), tmp);
		g_free (tmp);
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
		/* TRANSLATORS: this is when the updates panel is starting up */
		gtk_label_set_label (GTK_LABEL (priv->label_updates_spinner), _("Checking for updates…"));
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_FAILED:
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* headerbar spinner */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_spinner_start"));
	switch (priv->state) {
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
		gtk_widget_show (widget);
		gtk_spinner_start (GTK_SPINNER (widget));
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh_image"));
	switch (priv->state) {
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
		gtk_image_set_from_icon_name (GTK_IMAGE (widget),
					      "media-playback-stop-symbolic", GTK_ICON_SIZE_MENU);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
		gtk_widget_show (widget);
		break;
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
	case GS_SHELL_UPDATES_STATE_STARTUP:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
		gtk_widget_hide (widget);
		break;
	case GS_SHELL_UPDATES_STATE_FAILED:
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
		gtk_image_set_from_icon_name (GTK_IMAGE (widget),
					      "view-refresh-symbolic", GTK_ICON_SIZE_MENU);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
		gtk_widget_show (widget);
		break;
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
		gtk_image_set_from_icon_name (GTK_IMAGE (widget),
					      "view-refresh-symbolic", GTK_ICON_SIZE_MENU);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
		gtk_widget_set_visible (widget,
					is_free_connection || priv->has_agreed_to_mobile_data);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* stack */
	switch (priv->state) {
	case GS_SHELL_UPDATES_STATE_STARTUP:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES:
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_updates), "spinner");
		break;
	case GS_SHELL_UPDATES_STATE_NO_UPDATES:
		/* check we have a "free" network connection */
		switch (network_state) {
		case PK_NETWORK_ENUM_ONLINE:
		case PK_NETWORK_ENUM_WIFI:
		case PK_NETWORK_ENUM_WIRED:
			gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_updates), "uptodate");
			break;
		case PK_NETWORK_ENUM_OFFLINE:
			gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_updates), "offline");
			break;
		case PK_NETWORK_ENUM_MOBILE:
			if (priv->has_agreed_to_mobile_data) {
				gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_updates), "uptodate");
			} else {
				gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_updates), "mobile");
			}
			break;
		default:
			break;
		}
		break;
	case GS_SHELL_UPDATES_STATE_HAS_UPDATES:
	case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_updates), "view");
		break;
	case GS_SHELL_UPDATES_STATE_FAILED:
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_updates), "failed");
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* last checked label */
	if (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (priv->stack_updates)), "uptodate") == 0) {
		tmp = gs_shell_updates_last_checked_time_string (shell_updates);
		if (tmp != NULL) {
			gchar *last_checked;

			/* TRANSLATORS: This is the time when we last checked for updates */
			last_checked = g_strdup_printf (_("Last checked: %s"), tmp);
			gtk_label_set_label (GTK_LABEL (priv->label_updates_last_checked),
			                     last_checked);
			g_free (last_checked);
		}
		gtk_widget_set_visible (priv->label_updates_last_checked, tmp != NULL);
		g_free (tmp);
	}
}

/**
 * gs_shell_updates_set_state:
 **/
static void
gs_shell_updates_set_state (GsShellUpdates *shell_updates,
			    GsShellUpdatesState state)
{
	shell_updates->priv->state = state;
	if (gs_shell_get_mode (shell_updates->priv->shell) == GS_SHELL_MODE_UPDATES)
		gs_shell_updates_update_ui_state (shell_updates);
}

/**
 * gs_shell_updates_notify_network_state_cb:
 **/
static void
gs_shell_updates_notify_network_state_cb (PkControl *control,
					  GParamSpec *pspec,
					  GsShellUpdates *shell_updates)
{
	gs_shell_updates_update_ui_state (shell_updates);
}

/**
 * gs_shell_updates_get_updates_cb:
 **/
static void
gs_shell_updates_get_updates_cb (GsPluginLoader *plugin_loader,
				 GAsyncResult *res,
				 GsShellUpdates *shell_updates)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *widget;

	priv->cache_valid = TRUE;

	/* get the results */
	list = gs_plugin_loader_get_updates_finish (plugin_loader, res, &error);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates_counter"));
	if (list != NULL) {
		gchar *text;
		text = g_strdup_printf ("%d", g_list_length (list));
		gtk_label_set_label (GTK_LABEL (widget), text);
		g_free (text);
		gtk_widget_show (widget);
	} else {
		gtk_widget_hide (widget);
	}

	if (list != NULL &&
            gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_UPDATES)
		gtk_style_context_add_class (gtk_widget_get_style_context (widget), "needs-attention");
	else
		gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "needs-attention");

	if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_UPDATES) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_set_visible (widget, list != NULL);
	}
	if (list == NULL) {
		if (g_error_matches (error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_NO_RESULTS)) {
			g_debug ("no updates to show");
			gs_shell_updates_set_state (shell_updates,
						    GS_SHELL_UPDATES_STATE_NO_UPDATES);
		} else {
			g_warning ("failed to get updates: %s", error->message);
			gtk_label_set_label (GTK_LABEL (priv->label_updates_failed),
			                     error->message);
			gs_shell_updates_set_state (shell_updates,
						    GS_SHELL_UPDATES_STATE_FAILED);
		}
		g_error_free (error);
		goto out;
	} else {
		gs_shell_updates_set_state (shell_updates,
					    GS_SHELL_UPDATES_STATE_HAS_UPDATES);
	}
	for (l = list; l != NULL; l = l->next) {
		gs_update_list_add_app (GS_UPDATE_LIST (priv->list_box_updates),
		                        GS_APP (l->data));
	}

out:
	gs_plugin_list_free (list);
}

/**
 * gs_shell_updates_refresh:
 **/
void
gs_shell_updates_refresh (GsShellUpdates *shell_updates,
			  gboolean scroll_up)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *widget;
	GList *list;
	guint64 refine_flags;

	if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_UPDATES) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_show (widget);

		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
		gtk_widget_set_visible (widget, TRUE);
	}

	if (scroll_up) {
		GtkAdjustment *adj;
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_updates));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	/* no need to refresh */
	if (FALSE && priv->cache_valid) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
		gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "needs-attention");
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		list = gtk_container_get_children (GTK_CONTAINER (priv->list_box_updates));
		gtk_widget_set_visible (widget, list != NULL);
		g_list_free (list);
		return;
	}

	if (priv->state == GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES)
		return;

	gs_container_remove_all (GTK_CONTAINER (priv->list_box_updates));

	refine_flags = GS_PLUGIN_REFINE_FLAGS_DEFAULT |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION;
	gs_shell_updates_set_state (shell_updates,
				    GS_SHELL_UPDATES_STATE_ACTION_GET_UPDATES);
	gs_plugin_loader_get_updates_async (priv->plugin_loader,
					    refine_flags,
					    priv->cancellable,
					    (GAsyncReadyCallback) gs_shell_updates_get_updates_cb,
					    shell_updates);
}

static void
show_update_details (GsApp *app, GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *dialog;

	dialog = gs_update_dialog_new ();
	gs_update_dialog_show_update_details (GS_UPDATE_DIALOG (dialog), app);

	gtk_window_set_transient_for (GTK_WINDOW (dialog), gs_shell_get_window (priv->shell));
	gtk_window_present (GTK_WINDOW (dialog));
}

/**
 * gs_shell_updates_activated_cb:
 **/
static void
gs_shell_updates_activated_cb (GtkListBox *list_box,
			       GtkListBoxRow *row,
			       GsShellUpdates *shell_updates)
{
	GsApp *app;

	app = gs_app_row_get_app (GS_APP_ROW (row));

	show_update_details (app, shell_updates);
}

/**
 * gs_shell_updates_refresh_cb:
 **/
static void
gs_shell_updates_refresh_cb (GsPluginLoader *plugin_loader,
			     GAsyncResult *res,
			     GsShellUpdates *shell_updates)
{
	GError *error = NULL;
	gboolean ret;
	GDateTime *now;
	GsShellUpdatesPrivate *priv = shell_updates->priv;

	/* get the results */
	ret = gs_plugin_loader_refresh_finish (plugin_loader, res, &error);
	if (!ret) {
		/* user cancel */
		if (g_error_matches (error,
				     G_IO_ERROR,
				     G_IO_ERROR_CANCELLED)) {
			switch (priv->state) {
			case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES:
				gs_shell_updates_set_state (shell_updates,
							    GS_SHELL_UPDATES_STATE_HAS_UPDATES);
				break;
			case GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES:
				gs_shell_updates_set_state (shell_updates,
							    GS_SHELL_UPDATES_STATE_NO_UPDATES);
				break;
			default:
				g_assert_not_reached ();
				break;
			}
			return;
		}
		g_warning ("failed to refresh: %s", error->message);
		gtk_label_set_label (GTK_LABEL (priv->label_updates_failed),
		                     error->message);
		gs_shell_updates_set_state (shell_updates,
					    GS_SHELL_UPDATES_STATE_FAILED);
		g_error_free (error);
		return;
	}

	/* update the last checked timestamp */
	now = g_date_time_new_now_local ();
	g_settings_set (priv->settings, "check-timestamp", "x",
	                g_date_time_to_unix (now));
	g_date_time_unref (now);

	/* get the new list */
	gs_shell_updates_refresh (shell_updates, TRUE);
}

/**
 * gs_shell_updates_get_new_updates:
 **/
static void
gs_shell_updates_get_new_updates (GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;

	/* force a check for updates and download */
	gs_shell_updates_set_state (shell_updates,
				    priv->state == GS_SHELL_UPDATES_STATE_HAS_UPDATES ?
				    GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES :
				    GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES);
	g_cancellable_reset (priv->cancellable_refresh);
	gs_plugin_loader_refresh_async (priv->plugin_loader,
					10 * 60,
					GS_PLUGIN_REFRESH_FLAGS_UPDATES,
					priv->cancellable_refresh,
					(GAsyncReadyCallback) gs_shell_updates_refresh_cb,
					shell_updates);
}

/**
 * gs_shell_updates_show_network_settings:
 **/
static void
gs_shell_updates_show_network_settings (GsShellUpdates *shell_updates)
{
	gboolean ret;
	GError *error = NULL;

	ret = g_spawn_command_line_async ("gnome-control-center network", &error);
	if (!ret) {
		g_warning ("Failed to open the control center: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gs_shell_updates_refresh_confirm_cb:
 **/
static void
gs_shell_updates_refresh_confirm_cb (GtkDialog *dialog,
				     GtkResponseType response_type,
				     GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;

	switch (response_type) {
	case GTK_RESPONSE_REJECT:
		/* open the control center */
		gs_shell_updates_show_network_settings (shell_updates);
		break;
	case GTK_RESPONSE_ACCEPT:
		priv->has_agreed_to_mobile_data = TRUE;
		gs_shell_updates_get_new_updates (shell_updates);
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
					     GsShellUpdates *shell_updates)
{
	gs_shell_updates_show_network_settings (shell_updates);
}

/**
 * gs_shell_updates_button_mobile_refresh_cb:
 **/
static void
gs_shell_updates_button_mobile_refresh_cb (GtkWidget *widget,
					   GsShellUpdates *shell_updates)
{
	shell_updates->priv->has_agreed_to_mobile_data = TRUE;
	gs_shell_updates_get_new_updates (shell_updates);
}

/**
 * gs_shell_updates_button_refresh_cb:
 **/
static void
gs_shell_updates_button_refresh_cb (GtkWidget *widget,
				    GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *dialog;
	PkNetworkEnum network_state;

	/* cancel existing action? */
	if (priv->state == GS_SHELL_UPDATES_STATE_ACTION_REFRESH_HAS_UPDATES ||
	    priv->state == GS_SHELL_UPDATES_STATE_ACTION_REFRESH_NO_UPDATES) {
		g_cancellable_cancel (priv->cancellable_refresh);
		return;
	}

	/* check we have a "free" network connection */
	g_object_get (priv->control,
		      "network-state", &network_state,
		      NULL);
	switch (network_state) {
	case PK_NETWORK_ENUM_ONLINE:
	case PK_NETWORK_ENUM_WIFI:
	case PK_NETWORK_ENUM_WIRED:
		gs_shell_updates_get_new_updates (shell_updates);
		break;
	case PK_NETWORK_ENUM_OFFLINE:
		dialog = gtk_message_dialog_new (gs_shell_get_window (priv->shell),
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
				  shell_updates);
		gtk_window_present (GTK_WINDOW (dialog));
		break;
	case PK_NETWORK_ENUM_MOBILE:
		if (priv->has_agreed_to_mobile_data) {
			gs_shell_updates_get_new_updates (shell_updates);
			break;
		}
		dialog = gtk_message_dialog_new (gs_shell_get_window (priv->shell),
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
				  shell_updates);
		gtk_window_present (GTK_WINDOW (dialog));
		break;
	default:
		g_assert_not_reached ();
	}
}

/**
 * gs_shell_updates_changed_cb:
 */
static void
gs_shell_updates_changed_cb (GsPluginLoader *plugin_loader,
			     GsShellUpdates *shell_updates)
{
	gs_shell_updates_invalidate (shell_updates);
	gs_shell_updates_refresh (shell_updates, TRUE);
}

/**
 * gs_shell_updates_pending_apps_changed_cb:
 */
static void
gs_shell_updates_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
					  GsShellUpdates *shell_updates)
{
	gs_shell_updates_invalidate (shell_updates);
}

static void
gs_shell_updates_button_update_all_cb (GtkButton      *button,
				       GsShellUpdates *updates)
{
	gs_offline_updates_trigger ();
	gs_reboot (gs_offline_updates_cancel);
}

/**
 * gs_shell_updates_get_properties_cb:
 **/
static void
gs_shell_updates_get_properties_cb (GObject *source,
				    GAsyncResult *res,
				    gpointer user_data)
{
	gboolean ret;
	GError *error = NULL;
	GsShellUpdates *shell_updates = GS_SHELL_UPDATES (user_data);
	PkControl *control = PK_CONTROL (source);

	/* get result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		g_warning ("failed to get properties: %s", error->message);
		g_error_free (error);
	}
	gs_shell_updates_update_ui_state (shell_updates);
}

/**
 * gs_shell_updates_status_changed_cb:
 **/
static void
gs_shell_updates_status_changed_cb (GsPluginLoader *plugin_loader,
				    GsApp *app,
				    GsPluginStatus status,
				    GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	priv->last_status = status;
	if (gs_shell_get_mode (shell_updates->priv->shell) == GS_SHELL_MODE_UPDATES)
		gs_shell_updates_update_ui_state (shell_updates);
}

void
gs_shell_updates_setup (GsShellUpdates *shell_updates,
			GsShell *shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL_UPDATES (shell_updates));

	priv->shell = shell;

	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (priv->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_shell_updates_pending_apps_changed_cb),
			  shell_updates);
	g_signal_connect (priv->plugin_loader, "updates-changed",
			  G_CALLBACK (gs_shell_updates_changed_cb),
			  shell_updates);
	g_signal_connect (priv->plugin_loader, "status-changed",
			  G_CALLBACK (gs_shell_updates_status_changed_cb),
			  shell_updates);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* setup updates */
	g_signal_connect (priv->list_box_updates, "row-activated",
			  G_CALLBACK (gs_shell_updates_activated_cb), shell_updates);

       widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gs_shell_updates_button_update_all_cb), shell_updates);

	/* setup update details window */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_refresh"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_updates_button_refresh_cb),
			  shell_updates);
	g_signal_connect (priv->button_updates_mobile, "clicked",
			  G_CALLBACK (gs_shell_updates_button_mobile_refresh_cb),
			  shell_updates);
	g_signal_connect (priv->button_updates_offline, "clicked",
			  G_CALLBACK (gs_shell_updates_button_network_settings_cb),
			  shell_updates);

	g_signal_connect (priv->control, "notify::network-state",
			  G_CALLBACK (gs_shell_updates_notify_network_state_cb),
			  shell_updates);

	/* get the initial network state */
	pk_control_get_properties_async (priv->control, cancellable,
					 gs_shell_updates_get_properties_cb,
					 shell_updates);
}

/**
 * gs_shell_updates_class_init:
 **/
static void
gs_shell_updates_class_init (GsShellUpdatesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = gs_shell_updates_finalize;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-updates.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsShellUpdates, button_updates_mobile);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellUpdates, button_updates_offline);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellUpdates, label_updates_failed);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellUpdates, label_updates_last_checked);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellUpdates, label_updates_spinner);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellUpdates, list_box_updates);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellUpdates, scrolledwindow_updates);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellUpdates, spinner_updates);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellUpdates, stack_updates);
}

/**
 * gs_shell_updates_init:
 **/
static void
gs_shell_updates_init (GsShellUpdates *shell_updates)
{
	const char *ampm;

	gtk_widget_init_template (GTK_WIDGET (shell_updates));

	shell_updates->priv = gs_shell_updates_get_instance_private (shell_updates);
	shell_updates->priv->control = pk_control_new ();
	shell_updates->priv->cancellable_refresh = g_cancellable_new ();
	shell_updates->priv->state = GS_SHELL_UPDATES_STATE_STARTUP;
	shell_updates->priv->settings = g_settings_new ("org.gnome.software");
	shell_updates->priv->desktop_settings = g_settings_new ("org.gnome.desktop.interface");

	ampm = nl_langinfo (AM_STR);
	if (ampm != NULL && *ampm != '\0')
		shell_updates->priv->ampm_available = TRUE;
}

/**
 * gs_shell_updates_finalize:
 **/
static void
gs_shell_updates_finalize (GObject *object)
{
	GsShellUpdates *shell_updates = GS_SHELL_UPDATES (object);
	GsShellUpdatesPrivate *priv = shell_updates->priv;

	g_cancellable_cancel (priv->cancellable_refresh);

	g_object_unref (priv->cancellable_refresh);
	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);
	g_object_unref (priv->control);
	g_object_unref (priv->settings);
	g_object_unref (priv->desktop_settings);

	G_OBJECT_CLASS (gs_shell_updates_parent_class)->finalize (object);
}

/**
 * gs_shell_updates_new:
 **/
GsShellUpdates *
gs_shell_updates_new (void)
{
	GsShellUpdates *shell_updates;
	shell_updates = g_object_new (GS_TYPE_SHELL_UPDATES, NULL);
	return GS_SHELL_UPDATES (shell_updates);
}

/* vim: set noexpandtab: */
