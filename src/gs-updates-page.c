/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
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
#include "gs-updates-page.h"
#include "gs-common.h"
#include "gs-app-row.h"
#include "gs-plugin-private.h"
#include "gs-removal-dialog.h"
#include "gs-update-monitor.h"
#include "gs-updates-section.h"
#include "gs-upgrade-banner.h"
#include "gs-application.h"

#ifdef HAVE_GNOME_DESKTOP
#include <gdesktop-enums.h>
#endif

#include <langinfo.h>

#define GS_REFRESH_MIN_AGE 10 /* sec */

typedef enum {
	GS_UPDATES_PAGE_FLAG_NONE		= 0,
	GS_UPDATES_PAGE_FLAG_HAS_UPDATES	= 1 << 0,
	GS_UPDATES_PAGE_FLAG_HAS_UPGRADES	= 1 << 1,
	GS_UPDATES_PAGE_FLAG_LAST
} GsUpdatesPageFlags;

typedef enum {
	GS_UPDATES_PAGE_STATE_STARTUP,
	GS_UPDATES_PAGE_STATE_ACTION_REFRESH,
	GS_UPDATES_PAGE_STATE_ACTION_GET_UPDATES,
	GS_UPDATES_PAGE_STATE_MANAGED,
	GS_UPDATES_PAGE_STATE_IDLE,
	GS_UPDATES_PAGE_STATE_FAILED,
	GS_UPDATES_PAGE_STATE_LAST,
} GsUpdatesPageState;

struct _GsUpdatesPage
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
	guint			 action_cnt;
	GsShell			*shell;
	GsPluginStatus		 last_status;
	GsUpdatesPageState	 state;
	GsUpdatesPageFlags	 result_flags;
	GtkWidget		*button_refresh;
	GtkWidget		*header_spinner_start;
	GtkWidget		*header_checking_label;
	GtkWidget		*header_start_box;
	GtkWidget		*header_end_box;
	gboolean		 has_agreed_to_mobile_data;
	gboolean		 ampm_available;

	GtkWidget		*updates_box;
	GtkWidget		*button_updates_mobile;
	GtkWidget		*button_updates_offline;
	GtkWidget		*label_updates_failed;
	GtkWidget		*label_updates_last_checked;
	GtkWidget		*label_updates_spinner;
	GtkWidget		*scrolledwindow_updates;
	GtkWidget		*spinner_updates;
	GtkWidget		*stack_updates;
	GtkWidget		*upgrade_banner;
	GtkWidget		*box_end_of_life;
	GtkWidget		*label_end_of_life;

	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	GtkSizeGroup		*sizegroup_desc;
	GtkSizeGroup		*sizegroup_button;
	GtkSizeGroup		*sizegroup_header;
	GtkListBox		*sections[GS_UPDATES_SECTION_KIND_LAST];
};

enum {
	COLUMN_UPDATE_APP,
	COLUMN_UPDATE_NAME,
	COLUMN_UPDATE_VERSION,
	COLUMN_UPDATE_LAST
};

G_DEFINE_TYPE (GsUpdatesPage, gs_updates_page, GS_TYPE_PAGE)

static void
gs_updates_page_set_flag (GsUpdatesPage *self, GsUpdatesPageFlags flag)
{
	self->result_flags |= flag;
}

static void
gs_updates_page_clear_flag (GsUpdatesPage *self, GsUpdatesPageFlags flag)
{
	self->result_flags &= ~flag;
}

static const gchar *
gs_updates_page_state_to_string (GsUpdatesPageState state)
{
	if (state == GS_UPDATES_PAGE_STATE_STARTUP)
		return "startup";
	if (state == GS_UPDATES_PAGE_STATE_ACTION_REFRESH)
		return "action-refresh";
	if (state == GS_UPDATES_PAGE_STATE_ACTION_GET_UPDATES)
		return "action-get-updates";
	if (state == GS_UPDATES_PAGE_STATE_MANAGED)
		return "managed";
	if (state == GS_UPDATES_PAGE_STATE_IDLE)
		return "idle";
	if (state == GS_UPDATES_PAGE_STATE_FAILED)
		return "failed";
	return NULL;
}

static void
gs_updates_page_invalidate (GsUpdatesPage *self)
{
	self->cache_valid = FALSE;
}

static GsUpdatesSectionKind
_get_app_section (GsApp *app)
{
	if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE_LIVE) {
		if (gs_app_get_kind (app) == AS_APP_KIND_FIRMWARE)
			return GS_UPDATES_SECTION_KIND_ONLINE_FIRMWARE;
		return GS_UPDATES_SECTION_KIND_ONLINE;
	}
	if (gs_app_get_kind (app) == AS_APP_KIND_FIRMWARE)
		return GS_UPDATES_SECTION_KIND_OFFLINE_FIRMWARE;
	return GS_UPDATES_SECTION_KIND_OFFLINE;
}

static GsAppList *
_get_all_apps (GsUpdatesPage *self)
{
	GsAppList *apps = gs_app_list_new ();
	for (guint i = 0; i < GS_UPDATES_SECTION_KIND_LAST; i++) {
		GsAppList *list = gs_updates_section_get_list (GS_UPDATES_SECTION (self->sections[i]));
		gs_app_list_add_list (apps, list);
	}
	return apps;
}

static guint
_get_num_updates (GsUpdatesPage *self)
{
	guint count = 0;
	g_autoptr(GsAppList) apps = _get_all_apps (self);

	for (guint i = 0; i < gs_app_list_length (apps); ++i) {
		GsApp *app = gs_app_list_index (apps, i);
		if (gs_app_is_updatable (app) ||
		    gs_app_get_state (app) == AS_APP_STATE_INSTALLING)
			++count;
	}
	return count;
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
gs_updates_page_last_checked_time_string (GsUpdatesPage *self)
{
#ifdef HAVE_GNOME_DESKTOP
	GDesktopClockFormat clock_format;
#endif
	const gchar *format_string;
	gchar *time_string;
	gboolean use_24h_time = FALSE;
	gint64 tmp;
	gint days_ago;
	g_autoptr(GDateTime) last_checked = NULL;
	g_autoptr(GDateTime) midnight = NULL;

	g_settings_get (self->settings, "check-timestamp", "x", &tmp);
	if (tmp == 0)
		return NULL;
	last_checked = g_date_time_new_from_unix_local (tmp);

	midnight = time_next_midnight ();
	days_ago = (gint) (g_date_time_difference (midnight, last_checked) / G_TIME_SPAN_DAY);

#ifdef HAVE_GNOME_DESKTOP
	clock_format = g_settings_get_enum (self->desktop_settings, "clock-format");
	use_24h_time = (clock_format == G_DESKTOP_CLOCK_FORMAT_24H || self->ampm_available == FALSE);
#endif

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

static const gchar *
gs_updates_page_get_state_string (GsPluginStatus status)
{
	if (status == GS_PLUGIN_STATUS_DOWNLOADING) {
		/* TRANSLATORS: the updates are being downloaded */
		return _("Downloading new updates…");
	}

	/* TRANSLATORS: the update panel is doing *something* vague */
	return _("Looking for new updates…");
}

static void
refresh_headerbar_updates_counter (GsUpdatesPage *self)
{
	GtkWidget *widget;
	guint num_updates;

	num_updates = _get_num_updates (self);

	/* update the counter */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_updates_counter"));
	if (num_updates > 0 &&
	    gs_plugin_loader_get_allow_updates (self->plugin_loader)) {
		g_autofree gchar *text = NULL;
		text = g_strdup_printf ("%u", num_updates);
		gtk_label_set_label (GTK_LABEL (widget), text);
		gtk_widget_show (widget);
	} else {
		gtk_widget_hide (widget);
	}

	/* update the tab style */
	if (num_updates > 0 &&
	    gs_shell_get_mode (self->shell) != GS_SHELL_MODE_UPDATES)
		gtk_style_context_add_class (gtk_widget_get_style_context (widget), "needs-attention");
	else
		gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "needs-attention");
}

static void
gs_updates_page_update_ui_state (GsUpdatesPage *self)
{
	gboolean allow_mobile_refresh = TRUE;
	g_autofree gchar *checked_str = NULL;
	g_autofree gchar *spinner_str = NULL;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_UPDATES)
		return;

	/* spinners */
	switch (self->state) {
	case GS_UPDATES_PAGE_STATE_STARTUP:
	case GS_UPDATES_PAGE_STATE_ACTION_GET_UPDATES:
	case GS_UPDATES_PAGE_STATE_ACTION_REFRESH:
		/* if we have updates, avoid clearing the page with a spinner */
		if (self->result_flags != GS_UPDATES_PAGE_FLAG_NONE) {
			gs_stop_spinner (GTK_SPINNER (self->spinner_updates));
			gtk_spinner_start (GTK_SPINNER (self->header_spinner_start));
			gtk_widget_show (self->header_spinner_start);
			gtk_widget_show (self->header_checking_label);
		} else {
			gs_start_spinner (GTK_SPINNER (self->spinner_updates));
		}
		break;
	default:
		gs_stop_spinner (GTK_SPINNER (self->spinner_updates));
		gtk_spinner_stop (GTK_SPINNER (self->header_spinner_start));
		gtk_widget_hide (self->header_spinner_start);
		gtk_widget_hide (self->header_checking_label);
		break;
	}

	/* spinner text */
	switch (self->state) {
	case GS_UPDATES_PAGE_STATE_STARTUP:
		spinner_str = g_strdup_printf ("%s\n%s",
				       /* TRANSLATORS: the updates panel is starting up */
				       _("Setting up updates…"),
				       _("(This could take a while)"));
		gtk_label_set_label (GTK_LABEL (self->label_updates_spinner), spinner_str);
		break;
	case GS_UPDATES_PAGE_STATE_ACTION_REFRESH:
		spinner_str = g_strdup_printf ("%s\n%s",
				       gs_updates_page_get_state_string (self->last_status),
				       /* TRANSLATORS: the updates panel is starting up */
				       _("(This could take a while)"));
		gtk_label_set_label (GTK_LABEL (self->label_updates_spinner), spinner_str);
		break;
	default:
		break;
	}

	/* headerbar refresh icon */
	switch (self->state) {
	case GS_UPDATES_PAGE_STATE_ACTION_REFRESH:
	case GS_UPDATES_PAGE_STATE_ACTION_GET_UPDATES:
		gtk_image_set_from_icon_name (GTK_IMAGE (gtk_button_get_image (GTK_BUTTON (self->button_refresh))),
					      "media-playback-stop-symbolic", GTK_ICON_SIZE_MENU);
		gtk_widget_show (self->button_refresh);
		break;
	case GS_UPDATES_PAGE_STATE_STARTUP:
	case GS_UPDATES_PAGE_STATE_MANAGED:
		gtk_widget_hide (self->button_refresh);
		break;
	default:
		gtk_image_set_from_icon_name (GTK_IMAGE (gtk_button_get_image (GTK_BUTTON (self->button_refresh))),
					      "view-refresh-symbolic", GTK_ICON_SIZE_MENU);
		if (self->result_flags != GS_UPDATES_PAGE_FLAG_NONE) {
			gtk_widget_show (self->button_refresh);
		} else {
			if (gs_plugin_loader_get_network_metered (self->plugin_loader) &&
			    !self->has_agreed_to_mobile_data)
				allow_mobile_refresh = FALSE;
			gtk_widget_set_visible (self->button_refresh, allow_mobile_refresh);
		}
		break;
	}
	gtk_widget_set_sensitive (self->button_refresh,
				  gs_plugin_loader_get_network_available (self->plugin_loader));

	/* stack */
	switch (self->state) {
	case GS_UPDATES_PAGE_STATE_MANAGED:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "managed");
		break;
	case GS_UPDATES_PAGE_STATE_FAILED:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "failed");
		break;
	case GS_UPDATES_PAGE_STATE_ACTION_GET_UPDATES:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates),
						  "spinner");
		break;
	case GS_UPDATES_PAGE_STATE_ACTION_REFRESH:
		if (self->result_flags != GS_UPDATES_PAGE_FLAG_NONE) {
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "view");
		} else {
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "spinner");
		}
		break;
	case GS_UPDATES_PAGE_STATE_STARTUP:
	case GS_UPDATES_PAGE_STATE_IDLE:

		/* if have updates, just show the view, otherwise show network */
		if (self->result_flags != GS_UPDATES_PAGE_FLAG_NONE) {
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "view");
			break;
		}

		/* check we have a "free" network connection */
		if (gs_plugin_loader_get_network_available (self->plugin_loader) &&
		    !gs_plugin_loader_get_network_metered (self->plugin_loader)) {
			gtk_stack_set_visible_child_name (GTK_STACK (self->stack_updates), "uptodate");
			break;
		}

		/* expensive network connection */
		if (gs_plugin_loader_get_network_metered (self->plugin_loader)) {
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

	/* any updates? */
	gtk_widget_set_visible (self->updates_box,
				self->result_flags & GS_UPDATES_PAGE_FLAG_HAS_UPDATES);

	/* last checked label */
	if (g_strcmp0 (gtk_stack_get_visible_child_name (GTK_STACK (self->stack_updates)), "uptodate") == 0) {
		checked_str = gs_updates_page_last_checked_time_string (self);
		if (checked_str != NULL) {
			g_autofree gchar *last_checked = NULL;

			/* TRANSLATORS: This is the time when we last checked for updates */
			last_checked = g_strdup_printf (_("Last checked: %s"), checked_str);
			gtk_label_set_label (GTK_LABEL (self->label_updates_last_checked),
					     last_checked);
		}
		gtk_widget_set_visible (self->label_updates_last_checked, checked_str != NULL);
	}

	/* update the counter in headerbar */
	refresh_headerbar_updates_counter (self);
}

static void
gs_updates_page_set_state (GsUpdatesPage *self, GsUpdatesPageState state)
{
	g_debug ("setting state from %s to %s (has-update:%i, has-upgrade:%i)",
		 gs_updates_page_state_to_string (self->state),
		 gs_updates_page_state_to_string (state),
		 (self->result_flags & GS_UPDATES_PAGE_FLAG_HAS_UPDATES) > 0,
		 (self->result_flags & GS_UPDATES_PAGE_FLAG_HAS_UPGRADES) > 0);
	self->state = state;
	gs_updates_page_update_ui_state (self);
}

static void
gs_updates_page_decrement_refresh_count (GsUpdatesPage *self)
{
	/* every job increments this */
	if (self->action_cnt == 0) {
		g_warning ("action_cnt already zero!");
		return;
	}
	if (--self->action_cnt > 0)
		return;

	/* all done */
	gs_updates_page_set_state (self, GS_UPDATES_PAGE_STATE_IDLE);

	/* seems a good place */
	gs_shell_profile_dump (self->shell);
}

static void
gs_updates_page_network_available_notify_cb (GsPluginLoader *plugin_loader,
                                             GParamSpec *pspec,
                                             GsUpdatesPage *self)
{
	gs_updates_page_update_ui_state (self);
}

static void
gs_updates_page_get_updates_cb (GsPluginLoader *plugin_loader,
                                GAsyncResult *res,
                                GsUpdatesPage *self)
{
	GtkWidget *widget;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	self->cache_valid = TRUE;

	/* get the results */
	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (list == NULL) {
		gs_updates_page_clear_flag (self, GS_UPDATES_PAGE_FLAG_HAS_UPDATES);
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("updates-shell: failed to get updates: %s", error->message);
		gs_utils_error_strip_unique_id (error);
		gtk_label_set_label (GTK_LABEL (self->label_updates_failed),
				     error->message);
		gs_updates_page_set_state (self, GS_UPDATES_PAGE_STATE_FAILED);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder,
							     "button_updates_counter"));
		gtk_widget_hide (widget);
		return;
	}

	/* add the results */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsUpdatesSectionKind section = _get_app_section (app);
		gs_updates_section_add_app (GS_UPDATES_SECTION (self->sections[section]), app);
	}

	/* invalidate the headers */
	for (guint i = 0; i < GS_UPDATES_SECTION_KIND_LAST; i++) {
		if (self->sections[i] != NULL)
			gtk_list_box_invalidate_headers (self->sections[i]);
	}

	/* update the counter in headerbar */
	refresh_headerbar_updates_counter (self);

	/* no results */
	if (gs_app_list_length (list) == 0) {
		g_debug ("updates-shell: no updates to show");
		gs_updates_page_clear_flag (self, GS_UPDATES_PAGE_FLAG_HAS_UPDATES);
	} else {
		gs_updates_page_set_flag (self, GS_UPDATES_PAGE_FLAG_HAS_UPDATES);
	}

	/* only when both set */
	gs_updates_page_decrement_refresh_count (self);
}

static void
gs_updates_page_get_upgrades_cb (GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsUpdatesPage *self = GS_UPDATES_PAGE (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get the results */
	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (list == NULL) {
		gs_updates_page_clear_flag (self, GS_UPDATES_PAGE_FLAG_HAS_UPGRADES);
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED)) {
			g_warning ("updates-shell: failed to get upgrades: %s",
				   error->message);
		}
	} else if (gs_app_list_length (list) == 0) {
		g_debug ("updates-shell: no upgrades to show");
		gs_updates_page_clear_flag (self, GS_UPDATES_PAGE_FLAG_HAS_UPGRADES);
		gtk_widget_set_visible (self->upgrade_banner, FALSE);
	} else {
		/* rely on the app list already being sorted */
		GsApp *app = gs_app_list_index (list, gs_app_list_length (list) - 1);
		g_debug ("got upgrade %s", gs_app_get_id (app));
		gs_upgrade_banner_set_app (GS_UPGRADE_BANNER (self->upgrade_banner), app);
		gs_updates_page_set_flag (self, GS_UPDATES_PAGE_FLAG_HAS_UPGRADES);
		gtk_widget_set_visible (self->upgrade_banner, TRUE);
	}

	/* only when both set */
	gs_updates_page_decrement_refresh_count (self);
}

static void
gs_updates_page_get_system_finished_cb (GObject *source_object,
					GAsyncResult *res,
					gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsUpdatesPage *self = GS_UPDATES_PAGE (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	/* get result */
	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get system: %s", error->message);
		return;
	}

	/* show or hide the end of life notification */
	app = gs_plugin_loader_get_system_app (plugin_loader);
	if (app == NULL) {
		g_warning ("failed to get system app");
		gtk_widget_set_visible (self->box_end_of_life, FALSE);
		return;
	}
	if (gs_app_get_state (app) != AS_APP_STATE_UNAVAILABLE) {
		gtk_widget_set_visible (self->box_end_of_life, FALSE);
		return;
	}

	/* construct a sufficiently scary message */
	if (gs_app_get_name (app) != NULL && gs_app_get_version (app) != NULL) {
		/* TRANSLATORS:  the first %s is the distro name, e.g. 'Fedora'
		 * and the second %s is the distro version, e.g. '25' */
		g_string_append_printf (str, _("%s %s is no longer supported."),
					gs_app_get_name (app),
					gs_app_get_version (app));
	} else {
		/* TRANSLATORS: OS refers to operating system, e.g. Fedora */
		g_string_append (str, _("Your OS is no longer supported."));
	}
	g_string_append (str, " ");

	/* TRANSLATORS: EOL distros do not get important updates */
	g_string_append (str, _("This means that it does not receive security updates."));
	g_string_append (str, " ");

	/* TRANSLATORS: upgrade refers to a major update, e.g. Fedora 25 to 26 */
	g_string_append (str, _("It is recommended that you upgrade to a more recent version."));

	gtk_label_set_label (GTK_LABEL (self->label_end_of_life), str->str);
	gtk_widget_set_visible (self->box_end_of_life, TRUE);

}

static void
gs_updates_page_load (GsUpdatesPage *self)
{
	guint64 refine_flags;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (self->action_cnt > 0)
		return;

	/* remove all existing apps */
	for (guint i = 0; i < GS_UPDATES_SECTION_KIND_LAST; i++)
		gs_updates_section_remove_all (GS_UPDATES_SECTION (self->sections[i]));

	refine_flags = GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
		       GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION;
	gs_updates_page_set_state (self, GS_UPDATES_PAGE_STATE_ACTION_GET_UPDATES);
	self->action_cnt++;
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_UPDATES,
					 "refine-flags", refine_flags,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    (GAsyncReadyCallback) gs_updates_page_get_updates_cb,
					    self);

	/* get the system state */
	g_object_unref (plugin_job);
	app = gs_plugin_loader_get_system_app (self->plugin_loader);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
					 "app", app,
					 "refine-flags", refine_flags,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    gs_updates_page_get_system_finished_cb,
					    self);

	/* don't refresh every each time */
	if ((self->result_flags & GS_UPDATES_PAGE_FLAG_HAS_UPGRADES) == 0) {
		refine_flags |= GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED;
		g_object_unref (plugin_job);
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_DISTRO_UPDATES,
						 "refine-flags", refine_flags,
							 NULL);
		gs_plugin_loader_job_process_async (self->plugin_loader,
						    plugin_job,
						    self->cancellable,
						    gs_updates_page_get_upgrades_cb,
						    self);
		self->action_cnt++;
	}
}

static void
gs_updates_page_reload (GsPage *page)
{
	GsUpdatesPage *self = GS_UPDATES_PAGE (page);
	gs_updates_page_invalidate (self);
	gs_updates_page_load (self);
}

static void
gs_updates_page_switch_to (GsPage *page,
                           gboolean scroll_up)
{
	GsUpdatesPage *self = GS_UPDATES_PAGE (page);
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
		gs_updates_page_update_ui_state (self);
		return;
	}

	if (self->state == GS_UPDATES_PAGE_STATE_ACTION_GET_UPDATES) {
		gs_updates_page_update_ui_state (self);
		return;
	}
	gs_updates_page_load (self);
}

static void
gs_updates_page_refresh_cb (GsPluginLoader *plugin_loader,
                            GAsyncResult *res,
                            GsUpdatesPage *self)
{
	gboolean ret;
	g_autoptr(GDateTime) now = NULL;
	g_autoptr(GError) error = NULL;

	/* get the results */
	ret = gs_plugin_loader_job_action_finish (plugin_loader, res, &error);
	if (!ret) {
		/* user cancel */
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_CANCELLED)) {
			gs_updates_page_set_state (self, GS_UPDATES_PAGE_STATE_IDLE);
			return;
		}
		g_warning ("failed to refresh: %s", error->message);
		gs_utils_error_strip_unique_id (error);
		gtk_label_set_label (GTK_LABEL (self->label_updates_failed),
				     error->message);
		gs_updates_page_set_state (self, GS_UPDATES_PAGE_STATE_FAILED);
		return;
	}

	/* update the last checked timestamp */
	now = g_date_time_new_now_local ();
	g_settings_set (self->settings, "check-timestamp", "x",
			g_date_time_to_unix (now));

	/* get the new list */
	gs_updates_page_invalidate (self);
	gs_page_switch_to (GS_PAGE (self), TRUE);
}

static void
gs_updates_page_get_new_updates (GsUpdatesPage *self)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* force a check for updates and download */
	gs_updates_page_set_state (self, GS_UPDATES_PAGE_STATE_ACTION_REFRESH);

	if (self->cancellable_refresh != NULL) {
		g_cancellable_cancel (self->cancellable_refresh);
		g_object_unref (self->cancellable_refresh);
	}
	self->cancellable_refresh = g_cancellable_new ();

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "interactive", TRUE,
					 "age", (guint64) GS_REFRESH_MIN_AGE,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable_refresh,
					    (GAsyncReadyCallback) gs_updates_page_refresh_cb,
					    self);
}

static void
gs_updates_page_show_network_settings (GsUpdatesPage *self)
{
	g_autoptr(GError) error = NULL;
	if (!g_spawn_command_line_async ("gnome-control-center wifi", &error))
		g_warning ("Failed to open the control center: %s", error->message);
}

static void
gs_updates_page_refresh_confirm_cb (GtkDialog *dialog,
                                    GtkResponseType response_type,
                                    GsUpdatesPage *self)
{
	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (dialog));

	switch (response_type) {
	case GTK_RESPONSE_REJECT:
		/* open the control center */
		gs_updates_page_show_network_settings (self);
		break;
	case GTK_RESPONSE_ACCEPT:
		self->has_agreed_to_mobile_data = TRUE;
		gs_updates_page_get_new_updates (self);
		break;
	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
gs_updates_page_button_network_settings_cb (GtkWidget *widget,
                                            GsUpdatesPage *self)
{
	gs_updates_page_show_network_settings (self);
}

static void
gs_updates_page_button_mobile_refresh_cb (GtkWidget *widget,
                                          GsUpdatesPage *self)
{
	self->has_agreed_to_mobile_data = TRUE;
	gs_updates_page_get_new_updates (self);
}

static void
gs_updates_page_button_refresh_cb (GtkWidget *widget,
                                   GsUpdatesPage *self)
{
	GtkWidget *dialog;

	/* cancel existing action? */
	if (self->state == GS_UPDATES_PAGE_STATE_ACTION_REFRESH) {
		g_cancellable_cancel (self->cancellable_refresh);
		g_clear_object (&self->cancellable_refresh);
		return;
	}

	/* check we have a "free" network connection */
	if (gs_plugin_loader_get_network_available (self->plugin_loader) &&
	    !gs_plugin_loader_get_network_metered (self->plugin_loader)) {
		gs_updates_page_get_new_updates (self);

	/* expensive network connection */
	} else if (gs_plugin_loader_get_network_metered (self->plugin_loader)) {
		if (self->has_agreed_to_mobile_data) {
			gs_updates_page_get_new_updates (self);
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
				  G_CALLBACK (gs_updates_page_refresh_confirm_cb),
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
				  G_CALLBACK (gs_updates_page_refresh_confirm_cb),
				  self);
		gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));
	}
}

static void
gs_updates_page_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
                                         GsUpdatesPage *self)
{
	gs_updates_page_invalidate (self);
}

typedef struct {
	GsApp		*app;
	GsUpdatesPage	*self;
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
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;

	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			return;
		g_warning ("failed to upgrade-download: %s", error->message);
	}
}

static void
gs_updates_page_upgrade_download_cb (GsUpgradeBanner *upgrade_banner,
                                     GsUpdatesPage *self)
{
	GsApp *app;
	GsPageHelper *helper;
	g_autoptr(GsPluginJob) plugin_job = NULL;

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
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD,
					 "interactive", TRUE,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable_upgrade_download,
					    upgrade_download_finished_cb,
					    helper);
}

static void
_cancel_trigger_failed_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GsUpdatesPage *self = GS_UPDATES_PAGE (user_data);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_job_action_finish (self->plugin_loader, res, &error)) {
		g_warning ("failed to cancel trigger: %s", error->message);
		return;
	}
}

static void
upgrade_reboot_failed_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data)
{
	GsUpdatesPage *self = (GsUpdatesPage *) user_data;
	GsApp *app;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GVariant) retval = NULL;

	/* get result */
	retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
	if (retval != NULL)
		return;

	if (error != NULL) {
		g_warning ("Calling org.gnome.SessionManager.Reboot failed: %s",
			   error->message);
	}

	app = gs_upgrade_banner_get_app (GS_UPGRADE_BANNER (self->upgrade_banner));
	if (app == NULL) {
		g_warning ("no upgrade to cancel");
		return;
	}

	/* cancel trigger */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPDATE_CANCEL,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    _cancel_trigger_failed_cb,
					    self);
}

static void
upgrade_trigger_finished_cb (GObject *source,
                             GAsyncResult *res,
                             gpointer user_data)
{
	GsUpdatesPage *self = (GsUpdatesPage *) user_data;
	g_autoptr(GDBusConnection) bus = NULL;
	g_autoptr(GError) error = NULL;

	/* get the results */
	if (!gs_plugin_loader_job_action_finish (self->plugin_loader, res, &error)) {
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
trigger_upgrade (GsUpdatesPage *self)
{
	GsApp *upgrade;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	upgrade = gs_upgrade_banner_get_app (GS_UPGRADE_BANNER (self->upgrade_banner));
	if (upgrade == NULL) {
		g_warning ("no upgrade available to install");
		return;
	}

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPGRADE_TRIGGER,
					 "interactive", TRUE,
					 "app", upgrade,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    upgrade_trigger_finished_cb,
					    self);
}

static void
gs_updates_page_upgrade_confirm_cb (GtkDialog *dialog,
                                    GtkResponseType response_type,
                                    GsUpdatesPage *self)
{
	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (dialog));

	switch (response_type) {
	case GTK_RESPONSE_ACCEPT:
		g_debug ("agreed to upgrade removing apps");
		trigger_upgrade (self);
		break;
	case GTK_RESPONSE_CANCEL:
		g_debug ("cancelled removal dialog");
		break;
	case GTK_RESPONSE_DELETE_EVENT:
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
gs_updates_page_upgrade_install_cb (GsUpgradeBanner *upgrade_banner,
                                    GsUpdatesPage *self)
{
	GsAppList *removals;
	GsApp *upgrade;
	GtkWidget *dialog;
	guint cnt = 0;
	guint i;

	upgrade = gs_upgrade_banner_get_app (GS_UPGRADE_BANNER (self->upgrade_banner));
	if (upgrade == NULL) {
		g_warning ("no upgrade available to install");
		return;
	}

	/* count the removals */
	removals = gs_app_get_related (upgrade);
	for (i = 0; i < gs_app_list_length (removals); i++) {
		GsApp *app = gs_app_list_index (removals, i);
		if (gs_app_get_state (app) != AS_APP_STATE_UNAVAILABLE)
			continue;
		cnt++;
	}

	if (cnt == 0) {
		/* no need for a removal confirmation dialog */
		trigger_upgrade (self);
		return;
	}

	dialog = gs_removal_dialog_new ();
	g_signal_connect (dialog, "response",
	                  G_CALLBACK (gs_updates_page_upgrade_confirm_cb),
	                  self);
	gs_removal_dialog_show_upgrade_removals (GS_REMOVAL_DIALOG (dialog),
	                                         upgrade);
	gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));
}

static void
gs_updates_page_upgrade_help_cb (GsUpgradeBanner *upgrade_banner,
                                 GsUpdatesPage *self)
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
	gs_shell_show_uri (self->shell, uri);
}

static void
gs_updates_page_invalidate_downloaded_upgrade (GsUpdatesPage *self)
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

static gboolean
gs_shell_update_are_updates_in_progress (GsUpdatesPage *self)
{
	g_autoptr(GsAppList) list = _get_all_apps (self);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		switch (gs_app_get_state (app)) {
		case AS_APP_STATE_INSTALLING:
		case AS_APP_STATE_REMOVING:
		case AS_APP_STATE_PURCHASING:
			return TRUE;
			break;
		default:
			break;
		}
	}
	return FALSE;
}

static void
gs_updates_page_changed_cb (GsPluginLoader *plugin_loader,
                            GsUpdatesPage *self)
{
	/* if we do a live update and the upgrade is waiting to be deployed
	 * then make sure all new packages are downloaded */
	gs_updates_page_invalidate_downloaded_upgrade (self);

	/* check to see if any apps in the app list are in a processing state */
	if (gs_shell_update_are_updates_in_progress (self)) {
		g_debug ("ignoring updates-changed as updates in progress");
		return;
	}

	/* refresh updates list */
	gs_updates_page_reload (GS_PAGE (self));
}

static void
gs_updates_page_status_changed_cb (GsPluginLoader *plugin_loader,
                                   GsApp *app,
                                   GsPluginStatus status,
                                   GsUpdatesPage *self)
{
	switch (status) {
	case GS_PLUGIN_STATUS_INSTALLING:
	case GS_PLUGIN_STATUS_REMOVING:
		if (gs_app_get_kind (app) != AS_APP_KIND_OS_UPGRADE &&
		    gs_app_get_id (app) != NULL) {
			/* if we do a install or remove then make sure all new
			 * packages are downloaded */
			gs_updates_page_invalidate_downloaded_upgrade (self);
		}
		break;
	default:
		break;
	}

	self->last_status = status;
	gs_updates_page_update_ui_state (self);
}

static void
gs_updates_page_allow_updates_notify_cb (GsPluginLoader *plugin_loader,
                                         GParamSpec *pspec,
                                         GsUpdatesPage *self)
{
	if (!gs_plugin_loader_get_allow_updates (plugin_loader)) {
		gs_updates_page_set_state (self, GS_UPDATES_PAGE_STATE_MANAGED);
		return;
	}
	gs_updates_page_set_state (self, GS_UPDATES_PAGE_STATE_IDLE);
}

static void
gs_updates_page_upgrade_cancel_cb (GsUpgradeBanner *upgrade_banner,
                                   GsUpdatesPage *self)
{
	g_cancellable_cancel (self->cancellable_upgrade_download);
}

static gboolean
gs_updates_page_setup (GsPage *page,
                       GsShell *shell,
                       GsPluginLoader *plugin_loader,
                       GtkBuilder *builder,
                       GCancellable *cancellable,
                       GError **error)
{
	GsUpdatesPage *self = GS_UPDATES_PAGE (page);
	AtkObject *accessible;

	g_return_val_if_fail (GS_IS_UPDATES_PAGE (self), TRUE);

	for (guint i = 0; i < GS_UPDATES_SECTION_KIND_LAST; i++) {
		self->sections[i] = gs_updates_section_new (i, plugin_loader, page);
		gs_updates_section_set_size_groups (GS_UPDATES_SECTION (self->sections[i]),
						    self->sizegroup_image,
						    self->sizegroup_name,
						    self->sizegroup_desc,
						    self->sizegroup_button,
						    self->sizegroup_header);
		gtk_box_pack_start (GTK_BOX (self->updates_box),
				    GTK_WIDGET (self->sections[i]),
				    TRUE, TRUE, 0);
	}

	self->shell = shell;

	self->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (self->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_updates_page_pending_apps_changed_cb),
			  self);
	g_signal_connect (self->plugin_loader, "status-changed",
			  G_CALLBACK (gs_updates_page_status_changed_cb),
			  self);
	g_signal_connect (self->plugin_loader, "updates-changed",
			  G_CALLBACK (gs_updates_page_changed_cb),
			  self);
	g_signal_connect_object (self->plugin_loader, "notify::allow-updates",
				 G_CALLBACK (gs_updates_page_allow_updates_notify_cb),
				 self, 0);
	g_signal_connect_object (self->plugin_loader, "notify::network-available",
				 G_CALLBACK (gs_updates_page_network_available_notify_cb),
				 self, 0);
	self->builder = g_object_ref (builder);
	self->cancellable = g_object_ref (cancellable);

	/* setup system upgrades */
	g_signal_connect (self->upgrade_banner, "download-clicked",
			  G_CALLBACK (gs_updates_page_upgrade_download_cb), self);
	g_signal_connect (self->upgrade_banner, "install-clicked",
			  G_CALLBACK (gs_updates_page_upgrade_install_cb), self);
	g_signal_connect (self->upgrade_banner, "cancel-clicked",
			  G_CALLBACK (gs_updates_page_upgrade_cancel_cb), self);
	g_signal_connect (self->upgrade_banner, "help-clicked",
			  G_CALLBACK (gs_updates_page_upgrade_help_cb), self);

	self->header_end_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_visible (self->header_end_box, TRUE);
	gs_page_set_header_end_widget (GS_PAGE (self), self->header_end_box);

	self->header_start_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_visible (self->header_start_box, TRUE);
	gs_page_set_header_start_widget (GS_PAGE (self), self->header_start_box);

	/* This label indicates that the update check is in progress */
	self->header_checking_label = gtk_label_new (_("Checking…"));
	gtk_box_pack_end (GTK_BOX (self->header_start_box), self->header_checking_label, FALSE, FALSE, 0);
	self->header_spinner_start = gtk_spinner_new ();
	gtk_box_pack_end (GTK_BOX (self->header_start_box), self->header_spinner_start, FALSE, FALSE, 0);

	/* setup update details window */
	self->button_refresh = gtk_button_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_MENU);
	accessible = gtk_widget_get_accessible (self->button_refresh);
	if (accessible != NULL)
		atk_object_set_name (accessible, _("Check for updates"));
	gtk_box_pack_start (GTK_BOX (self->header_start_box), self->button_refresh, FALSE, FALSE, 0);
	g_signal_connect (self->button_refresh, "clicked",
			  G_CALLBACK (gs_updates_page_button_refresh_cb),
			  self);

	g_signal_connect (self->button_updates_mobile, "clicked",
			  G_CALLBACK (gs_updates_page_button_mobile_refresh_cb),
			  self);
	g_signal_connect (self->button_updates_offline, "clicked",
			  G_CALLBACK (gs_updates_page_button_network_settings_cb),
			  self);

	/* visually aligned */
	self->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_desc = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_button = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_header = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);

	/* set initial state */
	if (!gs_plugin_loader_get_allow_updates (self->plugin_loader))
		self->state = GS_UPDATES_PAGE_STATE_MANAGED;
	return TRUE;
}

static void
gs_updates_page_dispose (GObject *object)
{
	GsUpdatesPage *self = GS_UPDATES_PAGE (object);

	if (self->cancellable_refresh != NULL) {
		g_cancellable_cancel (self->cancellable_refresh);
		g_clear_object (&self->cancellable_refresh);
	}
	if (self->cancellable_upgrade_download != NULL) {
		g_cancellable_cancel (self->cancellable_upgrade_download);
		g_clear_object (&self->cancellable_upgrade_download);
	}

	for (guint i = 0; i < GS_UPDATES_SECTION_KIND_LAST; i++) {
		if (self->sections[i] != NULL) {
			gtk_widget_destroy (GTK_WIDGET (self->sections[i]));
			self->sections[i] = NULL;
		}
	}

	g_clear_object (&self->builder);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);
	g_clear_object (&self->settings);
	g_clear_object (&self->desktop_settings);

	g_clear_object (&self->sizegroup_image);
	g_clear_object (&self->sizegroup_name);
	g_clear_object (&self->sizegroup_desc);
	g_clear_object (&self->sizegroup_button);
	g_clear_object (&self->sizegroup_header);

	G_OBJECT_CLASS (gs_updates_page_parent_class)->dispose (object);
}

static void
gs_updates_page_class_init (GsUpdatesPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_updates_page_dispose;
	page_class->switch_to = gs_updates_page_switch_to;
	page_class->reload = gs_updates_page_reload;
	page_class->setup = gs_updates_page_setup;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-updates-page.ui");

	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, updates_box);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, button_updates_mobile);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, button_updates_offline);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, label_updates_failed);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, label_updates_last_checked);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, label_updates_spinner);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, scrolledwindow_updates);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, spinner_updates);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, stack_updates);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, upgrade_banner);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, box_end_of_life);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesPage, label_end_of_life);
}

static void
gs_updates_page_init (GsUpdatesPage *self)
{
	const char *ampm;

	gtk_widget_init_template (GTK_WIDGET (self));

	self->state = GS_UPDATES_PAGE_STATE_STARTUP;
	self->settings = g_settings_new ("org.gnome.software");
	self->desktop_settings = g_settings_new ("org.gnome.desktop.interface");

	self->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_desc = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_button = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_header = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);

	ampm = nl_langinfo (AM_STR);
	if (ampm != NULL && *ampm != '\0')
		self->ampm_available = TRUE;
}

GsUpdatesPage *
gs_updates_page_new (void)
{
	GsUpdatesPage *self;
	self = g_object_new (GS_TYPE_UPDATES_PAGE, NULL);
	return GS_UPDATES_PAGE (self);
}

/* vim: set noexpandtab: */
