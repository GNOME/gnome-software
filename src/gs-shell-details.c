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

#include "gs-shell-details.h"
#include "gs-app-private.h"
#include "gs-app-addon-row.h"
#include "gs-auth-dialog.h"
#include "gs-history-dialog.h"
#include "gs-screenshot-image.h"
#include "gs-star-widget.h"
#include "gs-review-histogram.h"
#include "gs-review-dialog.h"
#include "gs-review-row.h"

/* the number of reviews to show before clicking the 'More Reviews' button */
#define SHOW_NR_REVIEWS_INITIAL		4

static void gs_shell_details_refresh_all (GsShellDetails *self);

typedef enum {
	GS_SHELL_DETAILS_STATE_LOADING,
	GS_SHELL_DETAILS_STATE_READY,
	GS_SHELL_DETAILS_STATE_FAILED
} GsShellDetailsState;

struct _GsShellDetails
{
	GsPage			 parent_instance;

	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GsApp			*app;
	GsShell			*shell;
	SoupSession		*session;
	gboolean		 enable_reviews;
	GSettings		*settings;

	GtkWidget		*application_details_icon;
	GtkWidget		*application_details_summary;
	GtkWidget		*application_details_title;
	GtkWidget		*box_addons;
	GtkWidget		*box_details;
	GtkWidget		*box_details_description;
	GtkWidget		*box_progress;
	GtkWidget		*star;
	GtkWidget		*label_review_count;
	GtkWidget		*box_details_screenshot;
	GtkWidget		*box_details_screenshot_main;
	GtkWidget		*box_details_screenshot_thumbnails;
	GtkWidget		*box_details_license_list;
	GtkWidget		*button_details_launch;
	GtkWidget		*button_details_add_shortcut;
	GtkWidget		*button_details_remove_shortcut;
	GtkWidget		*button_details_website;
	GtkWidget		*button_install;
	GtkWidget		*button_remove;
	GtkWidget		*button_cancel;
	GtkWidget		*button_more_reviews;
	GtkWidget		*infobar_details_app_norepo;
	GtkWidget		*infobar_details_app_repo;
	GtkWidget		*infobar_details_package_baseos;
	GtkWidget		*infobar_details_repo;
	GtkWidget		*label_progress_percentage;
	GtkWidget		*label_progress_status;
	GtkWidget		*label_addons_uninstalled_app;
	GtkWidget		*label_details_category_title;
	GtkWidget		*label_details_category_value;
	GtkWidget		*label_details_developer_title;
	GtkWidget		*label_details_developer_value;
	GtkWidget		*button_details_license_free;
	GtkWidget		*button_details_license_nonfree;
	GtkWidget		*button_details_license_unknown;
	GtkWidget		*label_details_origin_title;
	GtkWidget		*label_details_origin_value;
	GtkWidget		*label_details_size_installed_title;
	GtkWidget		*label_details_size_installed_value;
	GtkWidget		*label_details_size_download_title;
	GtkWidget		*label_details_size_download_value;
	GtkWidget		*label_details_updated_value;
	GtkWidget		*label_details_version_value;
	GtkWidget		*label_failed;
	GtkWidget		*label_pending;
	GtkWidget		*label_license_nonfree_details;
	GtkWidget		*label_licenses_intro;
	GtkWidget		*list_box_addons;
	GtkWidget		*box_reviews;
	GtkWidget		*box_details_screenshot_fallback;
	GtkWidget		*histogram;
	GtkWidget		*button_review;
	GtkWidget		*list_box_reviews;
	GtkWidget		*scrolledwindow_details;
	GtkWidget		*spinner_details;
	GtkWidget		*spinner_remove;
	GtkWidget		*stack_details;
	GtkWidget		*grid_details_kudo;
	GtkWidget		*image_details_kudo_docs;
	GtkWidget		*image_details_kudo_sandboxed;
	GtkWidget		*image_details_kudo_integration;
	GtkWidget		*image_details_kudo_translated;
	GtkWidget		*image_details_kudo_updated;
	GtkWidget		*label_details_kudo_docs;
	GtkWidget		*label_details_kudo_sandboxed;
	GtkWidget		*label_details_kudo_integration;
	GtkWidget		*label_details_kudo_translated;
	GtkWidget		*label_details_kudo_updated;
	GtkWidget		*progressbar_top;
	GtkWidget		*popover_license_free;
	GtkWidget		*popover_license_nonfree;
	GtkWidget		*popover_license_unknown;
};

G_DEFINE_TYPE (GsShellDetails, gs_shell_details, GS_TYPE_PAGE)

static void
gs_shell_details_set_state (GsShellDetails *self,
			    GsShellDetailsState state)
{
	/* spinner */
	switch (state) {
	case GS_SHELL_DETAILS_STATE_LOADING:
		gs_start_spinner (GTK_SPINNER (self->spinner_details));
		gtk_widget_show (self->spinner_details);
		break;
	case GS_SHELL_DETAILS_STATE_READY:
	case GS_SHELL_DETAILS_STATE_FAILED:
		gs_stop_spinner (GTK_SPINNER (self->spinner_details));
		gtk_widget_hide (self->spinner_details);
		break;
	default:
		g_assert_not_reached ();
	}

	/* stack */
	switch (state) {
	case GS_SHELL_DETAILS_STATE_LOADING:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_details), "spinner");
		break;
	case GS_SHELL_DETAILS_STATE_READY:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_details), "ready");
		break;
	case GS_SHELL_DETAILS_STATE_FAILED:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_details), "failed");
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
gs_shell_details_update_shortcut_button (GsShellDetails *self)
{
	gboolean add_shortcut_func;
	gboolean remove_shortcut_func;
	gboolean has_shortcut;

	gtk_widget_set_visible (self->button_details_add_shortcut,
				FALSE);
	gtk_widget_set_visible (self->button_details_remove_shortcut,
				FALSE);

	if (gs_app_get_kind (self->app) != AS_APP_KIND_DESKTOP)
		return;

	/* only consider the shortcut button if the app is installed */
	switch (gs_app_get_state (self->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		break;
	default:
		return;
	}

	add_shortcut_func =
		gs_plugin_loader_get_plugin_supported (self->plugin_loader,
						       "gs_plugin_add_shortcut");
	remove_shortcut_func =
		gs_plugin_loader_get_plugin_supported (self->plugin_loader,
						       "gs_plugin_remove_shortcut");

	has_shortcut = gs_app_has_quirk (self->app, AS_APP_QUIRK_HAS_SHORTCUT);

	if (add_shortcut_func) {
		gtk_widget_set_visible (self->button_details_add_shortcut,
					!has_shortcut || !remove_shortcut_func);
		gtk_widget_set_sensitive (self->button_details_add_shortcut,
					  !has_shortcut);
	}

	if (remove_shortcut_func) {
		gtk_widget_set_visible (self->button_details_remove_shortcut,
					has_shortcut || !add_shortcut_func);
		gtk_widget_set_sensitive (self->button_details_remove_shortcut,
					  has_shortcut);
	}
}

static void
gs_shell_details_switch_to (GsPage *page, gboolean scroll_up)
{
	GsShellDetails *self = GS_SHELL_DETAILS (page);
	AsAppState state;
	GtkWidget *widget;
	GtkStyleContext *sc;
	GtkAdjustment *adj;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_DETAILS) {
		g_warning ("Called switch_to(details) when in mode %s",
			   gs_shell_get_mode_string (self->shell));
		return;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "application_details_header"));
	gtk_widget_show (widget);

	/* not set, perhaps file-to-app */
	if (self->app == NULL)
		return;

	state = gs_app_get_state (self->app);

	/* label */
	switch (state) {
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (self->label_pending, TRUE);
		break;
	default:
		gtk_widget_set_visible (self->label_pending, FALSE);
		break;
	}

	/* install button */
	switch (state) {
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
		gtk_widget_set_visible (self->button_install, TRUE);
		gtk_style_context_add_class (gtk_widget_get_style_context (self->button_install), "suggested-action");
		/* TRANSLATORS: button text in the header when an application
		 * can be installed */
		gtk_button_set_label (GTK_BUTTON (self->button_install), _("_Install"));
		break;
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (self->button_install, FALSE);
		break;
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (self->button_install, FALSE);
		break;
	case AS_APP_STATE_UNKNOWN:
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_UPDATABLE:
		gtk_widget_set_visible (self->button_install, FALSE);
		break;
	case AS_APP_STATE_UPDATABLE_LIVE:
		gtk_widget_set_visible (self->button_install, TRUE);
		sc = gtk_widget_get_style_context (self->button_install);
		if (gs_app_get_kind (self->app) == AS_APP_KIND_FIRMWARE) {
			/* TRANSLATORS: button text in the header when firmware
			 * can be live-installed */
			gtk_button_set_label (GTK_BUTTON (self->button_install), _("_Install"));
			gtk_style_context_add_class (sc, "suggested-action");
		} else {
			/* TRANSLATORS: button text in the header when an application
			 * can be live-updated */
			gtk_button_set_label (GTK_BUTTON (self->button_install), _("_Update"));
			gtk_style_context_remove_class (sc, "suggested-action");
		}
		break;
	case AS_APP_STATE_UNAVAILABLE:
		if (gs_app_get_url (self->app, AS_URL_KIND_MISSING) != NULL) {
			gtk_widget_set_visible (self->button_install, FALSE);
		} else {
			gtk_widget_set_visible (self->button_install, TRUE);
			/* TRANSLATORS: this is a button that allows the apps to
			 * be installed.
			 * The ellipsis indicates that further steps are required,
			 * e.g. enabling software sources or the like */
			gtk_button_set_label (GTK_BUTTON (self->button_install), _("_Install…"));
		}
		break;
	default:
		g_warning ("App unexpectedly in state %s",
			   as_app_state_to_string (state));
		g_assert_not_reached ();
	}

	/* launch button */
	switch (gs_app_get_state (self->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		if (!gs_app_has_quirk (self->app, AS_APP_QUIRK_NOT_LAUNCHABLE)) {
			gtk_widget_set_visible (self->button_details_launch, TRUE);
		} else {
			gtk_widget_set_visible (self->button_details_launch, FALSE);
		}
		break;
	default:
		gtk_widget_set_visible (self->button_details_launch, FALSE);
		break;
	}

	/* don't show the launch and shortcut buttons if the app doesn't have a desktop ID */
	if (gs_app_get_id (self->app) == NULL) {
		gtk_widget_set_visible (self->button_details_launch, FALSE);
	}

	/* remove button */
	if (gs_app_has_quirk (self->app, AS_APP_QUIRK_COMPULSORY) ||
	    gs_app_get_kind (self->app) == AS_APP_KIND_FIRMWARE) {
		gtk_widget_set_visible (self->button_remove, FALSE);
	} else {
		switch (state) {
		case AS_APP_STATE_INSTALLED:
		case AS_APP_STATE_UPDATABLE:
		case AS_APP_STATE_UPDATABLE_LIVE:
			gtk_widget_set_visible (self->button_remove, TRUE);
			gtk_widget_set_sensitive (self->button_remove, TRUE);
			/* Mark the button as destructive only if Launch is not visible */
			if (gtk_widget_get_visible (self->button_details_launch))
				gtk_style_context_remove_class (gtk_widget_get_style_context (self->button_remove), "destructive-action");
			else
				gtk_style_context_add_class (gtk_widget_get_style_context (self->button_remove), "destructive-action");
			/* TRANSLATORS: button text in the header when an application can be erased */
			gtk_button_set_label (GTK_BUTTON (self->button_remove), _("_Remove"));
			break;
		case AS_APP_STATE_QUEUED_FOR_INSTALL:
			gtk_widget_set_visible (self->button_remove, TRUE);
			gtk_widget_set_sensitive (self->button_remove, TRUE);
			gtk_style_context_remove_class (gtk_widget_get_style_context (self->button_remove), "destructive-action");
			gtk_button_set_label (GTK_BUTTON (self->button_remove), _("_Cancel"));
			break;
		case AS_APP_STATE_AVAILABLE_LOCAL:
		case AS_APP_STATE_AVAILABLE:
		case AS_APP_STATE_INSTALLING:
		case AS_APP_STATE_REMOVING:
		case AS_APP_STATE_UNAVAILABLE:
		case AS_APP_STATE_UNKNOWN:
			gtk_widget_set_visible (self->button_remove, FALSE);
			break;
		default:
			g_warning ("App unexpectedly in state %s",
				   as_app_state_to_string (state));
			g_assert_not_reached ();
		}
	}

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_details));
	gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));

	gs_grab_focus_when_mapped (self->scrolledwindow_details);
}

static void
gs_shell_details_refresh_progress (GsShellDetails *self)
{
	guint percentage;
	AsAppState state;

	/* cancel button */
	state = gs_app_get_state (self->app);
	switch (state) {
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (self->button_cancel, TRUE);
		gtk_widget_set_sensitive (self->button_cancel,
					  !g_cancellable_is_cancelled (self->cancellable));
		break;
	default:
		gtk_widget_set_visible (self->button_cancel, FALSE);
		break;
	}

	/* progress status label */
	switch (state) {
	case AS_APP_STATE_REMOVING:
		gtk_widget_set_visible (self->label_progress_status, TRUE);
		gtk_label_set_label (GTK_LABEL (self->label_progress_status),
				     _("Removing…"));
		break;
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (self->label_progress_status, TRUE);
		gtk_label_set_label (GTK_LABEL (self->label_progress_status),
				     _("Installing"));
		break;
	default:
		gtk_widget_set_visible (self->label_progress_status, FALSE);
		break;
	}

	/* percentage bar */
	switch (state) {
	case AS_APP_STATE_INSTALLING:
		percentage = gs_app_get_progress (self->app);
		if (percentage > 0) {
			g_autofree gchar *str = g_strdup_printf ("%u%%", percentage);
			gtk_label_set_label (GTK_LABEL (self->label_progress_percentage), str);
			gtk_widget_set_visible (self->label_progress_percentage, TRUE);
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progressbar_top),
						       (gdouble) percentage / 100.f);
			gtk_widget_set_visible (self->progressbar_top, TRUE);
			break;
		}
	default:
		gtk_widget_set_visible (self->label_progress_percentage, FALSE);
		gtk_widget_set_visible (self->progressbar_top, FALSE);
		break;
	}

	/* spinner */
	switch (state) {
	case AS_APP_STATE_REMOVING:
		gtk_spinner_start (GTK_SPINNER (self->spinner_remove));
		gtk_widget_set_visible (self->spinner_remove, TRUE);
		break;
	default:
		gtk_widget_set_visible (self->spinner_remove, FALSE);
		gtk_spinner_stop (GTK_SPINNER (self->spinner_remove));
		break;
	}

	/* progress box */
	switch (state) {
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (self->box_progress, TRUE);
		break;
	default:
		gtk_widget_set_visible (self->box_progress, FALSE);
		break;
	}
}

static gboolean
gs_shell_details_refresh_progress_idle (gpointer user_data)
{
	GsShellDetails *self = GS_SHELL_DETAILS (user_data);
	gs_shell_details_refresh_progress (self);
	g_object_unref (self);
	return G_SOURCE_REMOVE;
}

static void
gs_shell_details_progress_changed_cb (GsApp *app,
                                      GParamSpec *pspec,
                                      GsShellDetails *self)
{
	g_idle_add (gs_shell_details_refresh_progress_idle, g_object_ref (self));
}

static gboolean
gs_shell_details_switch_to_idle (gpointer user_data)
{
	GsShellDetails *self = GS_SHELL_DETAILS (user_data);

	if (gs_shell_get_mode (self->shell) == GS_SHELL_MODE_DETAILS)
		gs_page_switch_to (GS_PAGE (self), TRUE);

	/* update widgets */
	gs_shell_details_refresh_all (self);

	g_object_unref (self);
	return G_SOURCE_REMOVE;
}

static void
gs_shell_details_notify_state_changed_cb (GsApp *app,
					  GParamSpec *pspec,
					  GsShellDetails *self)
{
	g_idle_add (gs_shell_details_switch_to_idle, g_object_ref (self));
}

static void
gs_shell_details_screenshot_selected_cb (GtkListBox *list,
					 GtkListBoxRow *row,
					 GsShellDetails *self)
{
	GsScreenshotImage *ssmain;
	GsScreenshotImage *ssthumb;
	AsScreenshot *ss;
	g_autoptr(GList) children = NULL;

	if (row == NULL)
		return;

	children = gtk_container_get_children (GTK_CONTAINER (self->box_details_screenshot_main));
	ssmain = GS_SCREENSHOT_IMAGE (children->data);

	ssthumb = GS_SCREENSHOT_IMAGE (gtk_bin_get_child (GTK_BIN (row)));
	ss = gs_screenshot_image_get_screenshot (ssthumb);
	gs_screenshot_image_set_screenshot (ssmain, ss);
	gs_screenshot_image_load_async (ssmain, NULL);
}

static void
gs_shell_details_refresh_screenshots (GsShellDetails *self)
{
	GPtrArray *screenshots;
	AsScreenshot *ss;
	GtkWidget *label;
	GtkWidget *list;
	GtkWidget *ssimg;
	guint i;

	/* treat screenshots differently */
	if (gs_app_get_kind (self->app) == AS_APP_KIND_FONT) {
		gs_container_remove_all (GTK_CONTAINER (self->box_details_screenshot_thumbnails));
		gs_container_remove_all (GTK_CONTAINER (self->box_details_screenshot_main));
		screenshots = gs_app_get_screenshots (self->app);
		for (i = 0; i < screenshots->len; i++) {
			ss = g_ptr_array_index (screenshots, i);

			/* set caption */
			label = gtk_label_new (as_screenshot_get_caption (ss, NULL));
			g_object_set (label,
				      "xalign", 0.0,
				      "max-width-chars", 10,
				      "wrap", TRUE,
				      NULL);
			gtk_box_pack_start (GTK_BOX (self->box_details_screenshot_main), label, FALSE, FALSE, 0);
			gtk_widget_set_visible (label, TRUE);

			/* set images */
			ssimg = gs_screenshot_image_new (self->session);
			gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), ss);
			gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg),
						      640,
						      48);
			gs_screenshot_image_set_use_desktop_background (GS_SCREENSHOT_IMAGE (ssimg), FALSE);
			gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg), NULL);
			gtk_box_pack_start (GTK_BOX (self->box_details_screenshot_main), ssimg, FALSE, FALSE, 0);
			gtk_widget_set_visible (ssimg, TRUE);
		}
		return;
	}

	/* fallback warning */
	screenshots = gs_app_get_screenshots (self->app);
	switch (gs_app_get_kind (self->app)) {
	case AS_APP_KIND_CODEC:
	case AS_APP_KIND_ADDON:
	case AS_APP_KIND_SOURCE:
	case AS_APP_KIND_FIRMWARE:
	case AS_APP_KIND_DRIVER:
		gtk_widget_set_visible (self->box_details_screenshot_fallback, FALSE);
		break;
	default:
		gtk_widget_set_visible (self->box_details_screenshot_fallback,
					screenshots->len == 0);
		break;
	}

	/* set screenshots */
	gs_container_remove_all (GTK_CONTAINER (self->box_details_screenshot_main));
	if (screenshots->len == 0) {
		gs_container_remove_all (GTK_CONTAINER (self->box_details_screenshot_thumbnails));
		return;
	}

	/* set the default image */
	ss = g_ptr_array_index (screenshots, 0);
	ssimg = gs_screenshot_image_new (self->session);
	gtk_widget_set_can_focus (gtk_bin_get_child (GTK_BIN (ssimg)), FALSE);
	gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), ss);

	/* use a slightly larger screenshot if it's the only screenshot */
	if (screenshots->len == 1) {
		gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg),
					      AS_IMAGE_LARGE_WIDTH,
					      AS_IMAGE_LARGE_HEIGHT);
	} else {
		gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg),
					      AS_IMAGE_NORMAL_WIDTH,
					      AS_IMAGE_NORMAL_HEIGHT);
	}
	gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg), NULL);
	gtk_box_pack_start (GTK_BOX (self->box_details_screenshot_main), ssimg, FALSE, FALSE, 0);
	gtk_widget_set_visible (ssimg, TRUE);

	/* set all the thumbnails */
	gs_container_remove_all (GTK_CONTAINER (self->box_details_screenshot_thumbnails));
	if (screenshots->len < 2)
		return;

	list = gtk_list_box_new ();
	gtk_style_context_add_class (gtk_widget_get_style_context (list), "image-list");
	gtk_widget_show (list);
	gtk_box_pack_start (GTK_BOX (self->box_details_screenshot_thumbnails), list, FALSE, FALSE, 0);
	for (i = 0; i < screenshots->len; i++) {
		ss = g_ptr_array_index (screenshots, i);
		ssimg = gs_screenshot_image_new (self->session);
		gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), ss);
		gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg),
					      AS_IMAGE_THUMBNAIL_WIDTH,
					      AS_IMAGE_THUMBNAIL_HEIGHT);
		gtk_style_context_add_class (gtk_widget_get_style_context (ssimg),
					     "screenshot-image-thumb");
		gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg), NULL);
		gtk_list_box_insert (GTK_LIST_BOX (list), ssimg, -1);
		gtk_widget_set_visible (ssimg, TRUE);
	}

	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_BROWSE);
	gtk_list_box_select_row (GTK_LIST_BOX (list),
				 gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), 0));
	g_signal_connect (list, "row-selected",
			  G_CALLBACK (gs_shell_details_screenshot_selected_cb),
			  self);
}

static void
gs_shell_details_website_cb (GtkWidget *widget, GsShellDetails *self)
{
	gs_app_show_url (self->app, AS_URL_KIND_HOMEPAGE);
}

static void
gs_shell_details_set_description (GsShellDetails *self, const gchar *tmp)
{
	GtkStyleContext *style_context;
	GtkWidget *para;
	guint i;
	g_auto(GStrv) split = NULL;

	/* does the description exist? */
	gtk_widget_set_visible (self->box_details_description, tmp != NULL);
	if (tmp == NULL)
		return;

	/* add each paragraph as a new GtkLabel which lets us get the 24px
	 * paragraph spacing */
	gs_container_remove_all (GTK_CONTAINER (self->box_details_description));
	split = g_strsplit (tmp, "\n\n", -1);
	for (i = 0; split[i] != NULL; i++) {
		para = gtk_label_new (split[i]);
		gtk_label_set_line_wrap (GTK_LABEL (para), TRUE);
		gtk_label_set_max_width_chars (GTK_LABEL (para), 40);
		gtk_label_set_selectable (GTK_LABEL (para), TRUE);
		gtk_widget_set_visible (para, TRUE);
		gtk_widget_set_can_focus (para, FALSE);
		g_object_set (para,
			      "xalign", 0.0,
			      NULL);

		/* add style class for theming */
		style_context = gtk_widget_get_style_context (para);
		gtk_style_context_add_class (style_context,
					     "application-details-description");

		gtk_box_pack_start (GTK_BOX (self->box_details_description), para, FALSE, FALSE, 0);
	}

	/* show the webapp warning */
	if (gs_app_get_kind (self->app) == AS_APP_KIND_WEB_APP) {
		GtkWidget *label;
		/* TRANSLATORS: this is the warning box */
		label = gtk_label_new (_("This application can only be used when there is an active internet connection."));
		gtk_widget_set_visible (label, TRUE);
		gtk_label_set_xalign (GTK_LABEL (label), 0.f);
		gtk_style_context_add_class (gtk_widget_get_style_context (label),
					     "application-details-webapp-warning");
		gtk_box_pack_start (GTK_BOX (self->box_details_description),
				    label, FALSE, FALSE, 0);
	}
}

static void
gs_shell_details_set_sensitive (GtkWidget *widget, gboolean is_active)
{
	GtkStyleContext *style_context;
	style_context = gtk_widget_get_style_context (widget);
	if (!is_active) {
		gtk_style_context_add_class (style_context, "dim-label");
	} else {
		gtk_style_context_remove_class (style_context, "dim-label");
	}
}

static gboolean
gs_shell_details_history_cb (GtkLabel *label,
			     gchar *uri,
			     GsShellDetails *self)
{
	GtkWidget *dialog;

	dialog = gs_history_dialog_new ();
	gs_history_dialog_set_app (GS_HISTORY_DIALOG (dialog), self->app);
	gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);

	return TRUE;
}

static void
gs_shell_details_refresh_all (GsShellDetails *self)
{
	GPtrArray *history;
	GdkPixbuf *pixbuf = NULL;
	GList *addons;
	GtkWidget *widget;
	const gchar *tmp;
	gboolean ret;
	gchar **menu_path;
	guint64 kudos;
	guint64 updated;
	guint64 user_integration_bf;
	g_autoptr(GError) error = NULL;

	/* change widgets */
	tmp = gs_app_get_name (self->app);
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "application_details_header"));
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_label_set_label (GTK_LABEL (self->application_details_title), tmp);
		gtk_label_set_label (GTK_LABEL (widget), tmp);
		gtk_widget_set_visible (self->application_details_title, TRUE);
	} else {
		gtk_widget_set_visible (self->application_details_title, FALSE);
		gtk_label_set_label (GTK_LABEL (widget), "");
	}
	tmp = gs_app_get_summary (self->app);
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_label_set_label (GTK_LABEL (self->application_details_summary), tmp);
		gtk_widget_set_visible (self->application_details_summary, TRUE);
	} else {
		gtk_widget_set_visible (self->application_details_summary, FALSE);
	}

	/* set the description */
	tmp = gs_app_get_description (self->app);
	gs_shell_details_set_description (self, tmp);

	/* set the icon */
	pixbuf = gs_app_get_pixbuf (self->app);
	if (pixbuf != NULL) {
		gs_image_set_from_pixbuf (GTK_IMAGE (self->application_details_icon), pixbuf);
		gtk_widget_set_visible (self->application_details_icon, TRUE);
	} else {
		gtk_widget_set_visible (self->application_details_icon, FALSE);
	}

	tmp = gs_app_get_url (self->app, AS_URL_KIND_HOMEPAGE);
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_widget_set_visible (self->button_details_website, TRUE);
	} else {
		gtk_widget_set_visible (self->button_details_website, FALSE);
	}

	/* set the project group */
	tmp = gs_app_get_project_group (self->app);
	if (tmp == NULL) {
		gtk_widget_set_visible (self->label_details_developer_title, FALSE);
		gtk_widget_set_visible (self->label_details_developer_value, FALSE);
	} else {
		gtk_widget_set_visible (self->label_details_developer_title, TRUE);
		gtk_label_set_label (GTK_LABEL (self->label_details_developer_value), tmp);
		gtk_widget_set_visible (self->label_details_developer_value, TRUE);
	}

	/* set the license buttons */
	tmp = gs_app_get_license (self->app);
	if (tmp == NULL) {
		gtk_widget_set_visible (self->button_details_license_free, FALSE);
		gtk_widget_set_visible (self->button_details_license_nonfree, FALSE);
		gtk_widget_set_visible (self->button_details_license_unknown, TRUE);
	} else if (gs_app_get_license_is_free (self->app)) {
		gtk_widget_set_visible (self->button_details_license_free, TRUE);
		gtk_widget_set_visible (self->button_details_license_nonfree, FALSE);
		gtk_widget_set_visible (self->button_details_license_unknown, FALSE);
	} else {
		gtk_widget_set_visible (self->button_details_license_free, FALSE);
		gtk_widget_set_visible (self->button_details_license_nonfree, TRUE);
		gtk_widget_set_visible (self->button_details_license_unknown, FALSE);
	}

	/* set version */
	tmp = gs_app_get_version (self->app);
	if (tmp != NULL){
		gtk_label_set_label (GTK_LABEL (self->label_details_version_value), tmp);
	} else {
		/* TRANSLATORS: this is where the version is not known */
		gtk_label_set_label (GTK_LABEL (self->label_details_version_value), C_("version", "Unknown"));
	}

	/* set the installed size */
	if (gs_app_get_size_installed (self->app) != GS_APP_SIZE_UNKNOWABLE &&
	    gs_app_get_size_installed (self->app) != 0) {
		g_autofree gchar *size = NULL;
		size = g_format_size (gs_app_get_size_installed (self->app));
		gtk_label_set_label (GTK_LABEL (self->label_details_size_installed_value), size);
		gtk_widget_show (self->label_details_size_installed_title);
		gtk_widget_show (self->label_details_size_installed_value);
	} else {
		gtk_widget_hide (self->label_details_size_installed_title);
		gtk_widget_hide (self->label_details_size_installed_value);
	}

	/* set the download size */
	if (!gs_app_is_installed (self->app) &&
	    gs_app_get_size_download (self->app) != GS_APP_SIZE_UNKNOWABLE) {
		g_autofree gchar *size = NULL;
		size = g_format_size (gs_app_get_size_download (self->app));
		gtk_label_set_label (GTK_LABEL (self->label_details_size_download_value), size);
		gtk_widget_show (self->label_details_size_download_title);
		gtk_widget_show (self->label_details_size_download_value);
	} else {
		gtk_widget_hide (self->label_details_size_download_title);
		gtk_widget_hide (self->label_details_size_download_value);
	}

	/* set the updated date */
	updated = gs_app_get_install_date (self->app);
	if (updated == GS_APP_INSTALL_DATE_UNKNOWN ||
	    updated == GS_APP_INSTALL_DATE_UNSET) {
		/* TRANSLATORS: this is where the updated date is not known */
		gtk_label_set_label (GTK_LABEL (self->label_details_updated_value), C_("updated", "Never"));
	} else {
		g_autoptr(GDateTime) dt = NULL;
		g_autofree gchar *updated_str = NULL;
		dt = g_date_time_new_from_unix_utc ((gint64) updated);
		updated_str = g_date_time_format (dt, "%x");

		history = gs_app_get_history (self->app);

		if (history->len == 0) {
			gtk_label_set_label (GTK_LABEL (self->label_details_updated_value), updated_str);
		} else {
			GString *url;

			url = g_string_new (NULL);
			g_string_printf (url, "<a href=\"show-history\">%s</a>", updated_str);
			gtk_label_set_markup (GTK_LABEL (self->label_details_updated_value), url->str);
			g_string_free (url, TRUE);
		}
	}

	/* set the category */
	menu_path = gs_app_get_menu_path (self->app);
	if (menu_path == NULL || menu_path[0] == NULL || menu_path[0][0] == '\0') {
		gtk_widget_set_visible (self->label_details_category_title, FALSE);
		gtk_widget_set_visible (self->label_details_category_value, FALSE);
	} else {
		g_autofree gchar *path = NULL;
		if (gtk_widget_get_direction (self->label_details_category_value) == GTK_TEXT_DIR_RTL)
			path = g_strjoinv (" ← ", menu_path);
		else
			path = g_strjoinv (" → ", menu_path);
		gtk_label_set_label (GTK_LABEL (self->label_details_category_value), path);
		gtk_widget_set_visible (self->label_details_category_title, TRUE);
		gtk_widget_set_visible (self->label_details_category_value, TRUE);
	}

	/* set the origin */
	tmp = gs_app_get_origin_ui (self->app);
	if (tmp == NULL)
		tmp = gs_app_get_origin_hostname (self->app);
	if (tmp == NULL)
		tmp = gs_app_get_origin (self->app);
	if (tmp == NULL || tmp[0] == '\0') {
		/* TRANSLATORS: this is where we don't know the origin of the
		 * application */
		gtk_label_set_label (GTK_LABEL (self->label_details_origin_value), C_("origin", "Unknown"));
	} else {
		gtk_label_set_label (GTK_LABEL (self->label_details_origin_value), tmp);
	}

	/* set MyLanguage kudo */
	kudos = gs_app_get_kudos (self->app);
	ret = (kudos & GS_APP_KUDO_MY_LANGUAGE) > 0;
	gtk_widget_set_sensitive (self->image_details_kudo_translated, ret);
	gs_shell_details_set_sensitive (self->label_details_kudo_translated, ret);

	/* set RecentRelease kudo */
	ret = (kudos & GS_APP_KUDO_RECENT_RELEASE) > 0;
	gtk_widget_set_sensitive (self->image_details_kudo_updated, ret);
	gs_shell_details_set_sensitive (self->label_details_kudo_updated, ret);

	/* set UserDocs kudo */
	ret = (kudos & GS_APP_KUDO_INSTALLS_USER_DOCS) > 0;
	gtk_widget_set_sensitive (self->image_details_kudo_docs, ret);
	gs_shell_details_set_sensitive (self->label_details_kudo_docs, ret);

	/* set sandboxed kudo */
	ret = (kudos & GS_APP_KUDO_SANDBOXED) > 0;
	gtk_widget_set_sensitive (self->image_details_kudo_sandboxed, ret);
	gs_shell_details_set_sensitive (self->label_details_kudo_sandboxed, ret);

	/* any of the various integration kudos */
	user_integration_bf = GS_APP_KUDO_SEARCH_PROVIDER |
			      GS_APP_KUDO_USES_NOTIFICATIONS |
			      GS_APP_KUDO_USES_APP_MENU |
			      GS_APP_KUDO_HIGH_CONTRAST;
	ret = (kudos & user_integration_bf) > 0;
	gtk_widget_set_sensitive (self->image_details_kudo_integration, ret);
	gs_shell_details_set_sensitive (self->label_details_kudo_integration, ret);

	/* hide the kudo details for non-desktop software */
	switch (gs_app_get_kind (self->app)) {
	case AS_APP_KIND_DESKTOP:
		gtk_widget_set_visible (self->grid_details_kudo, TRUE);
		break;
	default:
		gtk_widget_set_visible (self->grid_details_kudo, FALSE);
		break;
	}

	/* are we trying to replace something in the baseos */
	gtk_widget_set_visible (self->infobar_details_package_baseos,
				gs_app_has_quirk (self->app, AS_APP_QUIRK_COMPULSORY) &&
				gs_app_get_state (self->app) == AS_APP_STATE_AVAILABLE_LOCAL);

	/* is this a repo-release */
	switch (gs_app_get_kind (self->app)) {
	case AS_APP_KIND_SOURCE:
		gtk_widget_set_visible (self->infobar_details_repo, gs_app_get_state (self->app) == AS_APP_STATE_AVAILABLE_LOCAL);
		break;
	default:
		gtk_widget_set_visible (self->infobar_details_repo, FALSE);
		break;
	}

	/* installing a app with a repo file */
	switch (gs_app_get_kind (self->app)) {
	case AS_APP_KIND_DESKTOP:
		gtk_widget_set_visible (self->infobar_details_app_repo,
					gs_app_has_quirk (self->app,
							  AS_APP_QUIRK_HAS_SOURCE) &&
					gs_app_get_state (self->app) == AS_APP_STATE_AVAILABLE_LOCAL);
		break;
	default:
		gtk_widget_set_visible (self->infobar_details_app_repo, FALSE);
		break;
	}

	/* installing a app without a repo file */
	switch (gs_app_get_kind (self->app)) {
	case AS_APP_KIND_DESKTOP:
		if (gs_app_get_kind (self->app) == AS_APP_KIND_FIRMWARE) {
			gtk_widget_set_visible (self->infobar_details_app_norepo, FALSE);
		} else {
			gtk_widget_set_visible (self->infobar_details_app_norepo,
						!gs_app_has_quirk (self->app,
							  AS_APP_QUIRK_HAS_SOURCE) &&
						gs_app_get_state (self->app) == AS_APP_STATE_AVAILABLE_LOCAL);
		}
		break;
	default:
		gtk_widget_set_visible (self->infobar_details_app_norepo, FALSE);
		break;
	}

	/* only show the "select addons" string if the app isn't yet installed */
	switch (gs_app_get_state (self->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		gtk_widget_set_visible (self->label_addons_uninstalled_app, FALSE);
		break;
	default:
		gtk_widget_set_visible (self->label_addons_uninstalled_app, TRUE);
		break;
	}

	gs_shell_details_update_shortcut_button (self);

	/* update progress */
	gs_shell_details_refresh_progress (self);

	addons = gtk_container_get_children (GTK_CONTAINER (self->list_box_addons));
	gtk_widget_set_visible (self->box_addons, addons != NULL);
	g_list_free (addons);
}

static void
list_header_func (GtkListBoxRow *row,
		  GtkListBoxRow *before,
		  gpointer user_data)
{
	GtkWidget *header = NULL;
	if (before != NULL)
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

static gint
list_sort_func (GtkListBoxRow *a,
		GtkListBoxRow *b,
		gpointer user_data)
{
	GsApp *a1 = gs_app_addon_row_get_addon (GS_APP_ADDON_ROW (a));
	GsApp *a2 = gs_app_addon_row_get_addon (GS_APP_ADDON_ROW (b));

	return g_strcmp0 (gs_app_get_name (a1),
			  gs_app_get_name (a2));
}

static void gs_shell_details_addon_selected_cb (GsAppAddonRow *row, GParamSpec *pspec, GsShellDetails *self);

static void
gs_shell_details_refresh_addons (GsShellDetails *self)
{
	GPtrArray *addons;
	guint i;

	gs_container_remove_all (GTK_CONTAINER (self->list_box_addons));

	addons = gs_app_get_addons (self->app);
	for (i = 0; i < addons->len; i++) {
		GsApp *addon;
		GtkWidget *row;

		addon = g_ptr_array_index (addons, i);
		if (gs_app_get_state (addon) == AS_APP_STATE_UNAVAILABLE)
			continue;

		row = gs_app_addon_row_new (addon);

		gtk_container_add (GTK_CONTAINER (self->list_box_addons), row);
		gtk_widget_show (row);

		g_signal_connect (row, "notify::selected",
				  G_CALLBACK (gs_shell_details_addon_selected_cb),
				  self);
	}
}

static void gs_shell_details_refresh_reviews (GsShellDetails *self);

typedef struct {
	GsShellDetails		*self;
	AsReview		*review;
	GsApp			*app;
	GsPluginAction		 action;
} GsShellDetailsReviewHelper;

static void
gs_shell_details_review_helper_free (GsShellDetailsReviewHelper *helper)
{
	g_object_unref (helper->self);
	g_object_unref (helper->review);
	g_object_unref (helper->app);
	g_free (helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsShellDetailsReviewHelper, gs_shell_details_review_helper_free);

static void
gs_shell_details_app_set_review_cb (GObject *source,
				    GAsyncResult *res,
				    gpointer user_data);

static void
gs_shell_details_authenticate_cb (GtkDialog *dialog,
				  GtkResponseType response_type,
				  GsShellDetailsReviewHelper *helper)
{
	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (response_type != GTK_RESPONSE_OK) {
		gs_shell_details_review_helper_free (helper);
		return;
	}
	gs_plugin_loader_review_action_async (helper->self->plugin_loader,
					      helper->app,
					      helper->review,
					      helper->action,
					      helper->self->cancellable,
					      gs_shell_details_app_set_review_cb,
					      helper);
}

static void
gs_shell_details_app_set_review_cb (GObject *source,
				    GAsyncResult *res,
				    gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	g_autoptr(GsShellDetailsReviewHelper) helper = (GsShellDetailsReviewHelper *) user_data;
	g_autoptr(GError) error = NULL;

	if (!gs_plugin_loader_app_action_finish (plugin_loader, res, &error)) {
		/* try to authenticate then retry */
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_AUTH_REQUIRED)) {
			g_autoptr(GError) error_local = NULL;
			GtkWidget *dialog;
			dialog = gs_auth_dialog_new (helper->self->plugin_loader,
						     helper->app,
						     gs_utils_get_error_value (error),
						     &error_local);
			if (dialog == NULL) {
				g_warning ("%s", error_local->message);
				return;
			}
			gs_shell_modal_dialog_present (helper->self->shell, GTK_DIALOG (dialog));
			g_signal_connect (dialog, "response",
					  G_CALLBACK (gs_shell_details_authenticate_cb),
					  g_steal_pointer (&helper));
			return;
		}
		g_warning ("failed to set review on %s: %s",
			   gs_app_get_id (helper->app), error->message);
		return;
	}
	gs_shell_details_refresh_reviews (helper->self);
}

static void
gs_shell_details_review_button_clicked_cb (GsReviewRow *row,
					   GsPluginAction action,
					   GsShellDetails *self)
{
	GsShellDetailsReviewHelper *helper = g_new0 (GsShellDetailsReviewHelper, 1);
	helper->self = g_object_ref (self);
	helper->app = g_object_ref (self->app);
	helper->review = g_object_ref (gs_review_row_get_review (row));
	helper->action = action;
	gs_plugin_loader_review_action_async (self->plugin_loader,
					      helper->app,
					      helper->review,
					      helper->action,
					      self->cancellable,
					      gs_shell_details_app_set_review_cb,
					      helper);
}

static void
gs_shell_details_refresh_reviews (GsShellDetails *self)
{
	GArray *review_ratings = NULL;
	GPtrArray *reviews;
	gboolean show_review_button = TRUE;
	gboolean show_reviews = FALSE;
	guint n_reviews = 0;
	guint64 possible_actions = 0;
	guint i;
	struct {
		GsPluginAction action;
		const gchar *plugin_func;
	} plugin_vfuncs[] = {
		{ GS_PLUGIN_ACTION_REVIEW_UPVOTE,	"gs_plugin_review_upvote" },
		{ GS_PLUGIN_ACTION_REVIEW_DOWNVOTE,	"gs_plugin_review_downvote" },
		{ GS_PLUGIN_ACTION_REVIEW_REPORT,	"gs_plugin_review_report" },
		{ GS_PLUGIN_ACTION_REVIEW_SUBMIT,	"gs_plugin_review_submit" },
		{ GS_PLUGIN_ACTION_REVIEW_REMOVE,	"gs_plugin_review_remove" },
		{ GS_PLUGIN_ACTION_LAST,	NULL }
	};

	/* show or hide the entire reviews section */
	switch (gs_app_get_kind (self->app)) {
	case AS_APP_KIND_DESKTOP:
	case AS_APP_KIND_FONT:
	case AS_APP_KIND_INPUT_METHOD:
	case AS_APP_KIND_WEB_APP:
	case AS_APP_KIND_SHELL_EXTENSION:
		/* don't show a missing rating on a local file */
		if (gs_app_get_state (self->app) != AS_APP_STATE_AVAILABLE_LOCAL &&
		    self->enable_reviews)
			show_reviews = TRUE;
		break;
	default:
		break;
	}

	/* some apps are unreviewable */
	if (gs_app_has_quirk (self->app, AS_APP_QUIRK_NOT_REVIEWABLE))
		show_reviews = FALSE;

	/* set the star rating */
	if (show_reviews) {
		if (gs_app_get_rating (self->app) >= 0) {
			gs_star_widget_set_rating (GS_STAR_WIDGET (self->star),
						   gs_app_get_rating (self->app));
		}
		review_ratings = gs_app_get_review_ratings (self->app);
		if (review_ratings != NULL) {
			gs_review_histogram_set_ratings (GS_REVIEW_HISTOGRAM (self->histogram),
						         review_ratings);
		}
		if (review_ratings != NULL) {
			for (i = 0; i < review_ratings->len; i++)
				n_reviews += (guint) g_array_index (review_ratings, gint, i);
		} else if (gs_app_get_reviews (self->app) != NULL) {
			n_reviews = gs_app_get_reviews (self->app)->len;
		}
	}

	/* enable appropriate widgets */
	gtk_widget_set_visible (self->star, show_reviews);
	gtk_widget_set_visible (self->box_reviews, show_reviews);
	gtk_widget_set_visible (self->histogram, review_ratings != NULL);
	gtk_widget_set_visible (self->label_review_count, n_reviews > 0);

	/* update the review label next to the star widget */
	if (n_reviews > 0) {
		g_autofree gchar *text = NULL;
		gtk_widget_set_visible (self->label_review_count, TRUE);
		text = g_strdup_printf ("(%u)", n_reviews);
		gtk_label_set_text (GTK_LABEL (self->label_review_count), text);
	}

	/* no point continuing */
	if (!show_reviews)
		return;

	/* find what the plugins support */
	for (i = 0; plugin_vfuncs[i].action != GS_PLUGIN_ACTION_LAST; i++) {
		if (gs_plugin_loader_get_plugin_supported (self->plugin_loader,
							   plugin_vfuncs[i].plugin_func)) {
			possible_actions |= 1u << plugin_vfuncs[i].action;
		}
	}

	/* only show the button if there are more to show */
	gtk_widget_set_visible (self->button_more_reviews,
				n_reviews > SHOW_NR_REVIEWS_INITIAL);

	/* add all the reviews */
	gs_container_remove_all (GTK_CONTAINER (self->list_box_reviews));
	reviews = gs_app_get_reviews (self->app);
	for (i = 0; i < reviews->len; i++) {
		AsReview *review = g_ptr_array_index (reviews, i);
		GtkWidget *row = gs_review_row_new (review);
		guint64 actions;

		g_signal_connect (row, "button-clicked",
				  G_CALLBACK (gs_shell_details_review_button_clicked_cb), self);
		if (as_review_get_flags (review) & AS_REVIEW_FLAG_SELF) {
			actions = possible_actions & 1 << GS_PLUGIN_ACTION_REVIEW_REMOVE;
			show_review_button = FALSE;
		} else {
			actions = possible_actions & ~(1u << GS_PLUGIN_ACTION_REVIEW_REMOVE);
		}
		gs_review_row_set_actions (GS_REVIEW_ROW (row), actions);
		gtk_container_add (GTK_CONTAINER (self->list_box_reviews), row);
		gtk_widget_set_visible (row, i < SHOW_NR_REVIEWS_INITIAL);
	}

	/* show the button only if the user never reviewed */
	gtk_widget_set_visible (self->button_review, show_review_button);
}

static void
gs_shell_details_app_refine2_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellDetails *self = GS_SHELL_DETAILS (user_data);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_app_refine_finish (plugin_loader, res, &error)) {
		g_warning ("failed to refine %s: %s",
			   gs_app_get_id (self->app),
			   error->message);
		return;
	}
	gs_shell_details_refresh_reviews (self);
}

static void
gs_shell_details_app_refine2 (GsShellDetails *self)
{
	gs_plugin_loader_app_refine_async (self->plugin_loader, self->app,
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS,
					   self->cancellable,
					   gs_shell_details_app_refine2_cb,
					   self);
}

static void
gs_shell_details_app_refine_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellDetails *self = GS_SHELL_DETAILS (user_data);
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *app_dump = NULL;

	ret = gs_plugin_loader_app_refine_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to refine %s: %s",
			   gs_app_get_id (self->app),
			   error->message);
	}

	if (gs_app_get_kind (self->app) == AS_APP_KIND_UNKNOWN ||
	    gs_app_get_state (self->app) == AS_APP_STATE_UNKNOWN) {
		g_autofree gchar *str = NULL;

		str = g_strdup_printf (_("Could not find '%s'"), gs_app_get_id (self->app));
		gtk_label_set_text (GTK_LABEL (self->label_failed), str);
		gs_shell_details_set_state (self, GS_SHELL_DETAILS_STATE_FAILED);
		return;
	}

	/* show some debugging */
	app_dump = gs_app_to_string (self->app);
	g_debug ("%s", app_dump);

	gs_shell_details_refresh_screenshots (self);
	gs_shell_details_refresh_addons (self);
	gs_shell_details_refresh_reviews (self);
	gs_shell_details_refresh_all (self);
	gs_shell_details_set_state (self, GS_SHELL_DETAILS_STATE_READY);

	/* do 2nd stage refine */
	gs_shell_details_app_refine2 (self);
}

static void
gs_shell_details_failed_response_cb (GtkDialog *dialog,
				     gint response,
				     GsShellDetails *self)
{
	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (dialog));

	/* switch away from the details view that failed to load */
	gs_shell_set_mode (self->shell, GS_SHELL_MODE_OVERVIEW);
}

static void
gs_shell_details_file_to_app_cb (GObject *source,
				 GAsyncResult *res,
				 gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellDetails *self = GS_SHELL_DETAILS (user_data);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *tmp = NULL;

	/* disconnect the old handlers */
	if (self->app != NULL) {
		g_signal_handlers_disconnect_by_func (self->app, gs_shell_details_notify_state_changed_cb, self);
		g_signal_handlers_disconnect_by_func (self->app, gs_shell_details_progress_changed_cb, self);
	}
	/* save app */
	g_set_object (&self->app,
		      gs_plugin_loader_file_to_app_finish (plugin_loader,
							   res,
							   &error));
	if (self->app == NULL) {
		GtkWidget *dialog;
		const gchar *msg;

		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED)) {
			/* TRANSLATORS: the file format was not recognised by
			 * any plugin, e.g. if you try installing a .tar.gz */
			msg = _("The file is not supported.");
		} else {
			msg = error->message;
		}

		dialog = gtk_message_dialog_new (gs_shell_get_window (self->shell),
		                                 GTK_DIALOG_MODAL |
		                                 GTK_DIALOG_DESTROY_WITH_PARENT,
		                                 GTK_MESSAGE_ERROR,
		                                 GTK_BUTTONS_CLOSE,
		                                 _("Sorry, this did not work"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
		                                          "%s", msg);
		g_signal_connect (dialog, "response",
				  G_CALLBACK (gs_shell_details_failed_response_cb), self);
		gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));

		g_warning ("failed to convert to GsApp: %s", error->message);
		return;
	}

	g_signal_connect_object (self->app, "notify::state",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::size",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::license",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::progress",
				 G_CALLBACK (gs_shell_details_progress_changed_cb),
				 self, 0);

	/* print what we've got */
	tmp = gs_app_to_string (self->app);
	g_debug ("%s", tmp);

	/* change widgets */
	gs_page_switch_to (GS_PAGE (self), TRUE);
	gs_shell_details_refresh_screenshots (self);
	gs_shell_details_refresh_addons (self);
	gs_shell_details_refresh_reviews (self);
	gs_shell_details_refresh_all (self);
	gs_shell_details_set_state (self, GS_SHELL_DETAILS_STATE_READY);

	/* do 2nd stage refine */
	gs_shell_details_app_refine2 (self);
}

void
gs_shell_details_set_filename (GsShellDetails *self, const gchar *filename)
{
	g_autoptr(GFile) file = NULL;

	gs_shell_details_set_state (self, GS_SHELL_DETAILS_STATE_LOADING);
	file = g_file_new_for_path (filename);
	gs_plugin_loader_file_to_app_async (self->plugin_loader,
					    file,
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS,
					    self->cancellable,
					    gs_shell_details_file_to_app_cb,
					    self);
}

static void
gs_shell_details_load (GsShellDetails *self)
{
	gs_plugin_loader_app_refine_async (self->plugin_loader, self->app,
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_UI |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS,
					   self->cancellable,
					   gs_shell_details_app_refine_cb,
					   self);
}

static void
gs_shell_details_reload (GsPage *page)
{
	GsShellDetails *self = GS_SHELL_DETAILS (page);
	if (self->app != NULL)
		gs_shell_details_load (self);
}

static void
settings_changed_cb (GsShellDetails *self,
		     const gchar *key,
		     gpointer data)
{
	if (g_strcmp0 (key, "show-nonfree-ui") == 0) {
		gs_shell_details_refresh_all (self);
	}
}

void
gs_shell_details_set_app (GsShellDetails *self, GsApp *app)
{
	g_return_if_fail (GS_IS_SHELL_DETAILS (self));
	g_return_if_fail (GS_IS_APP (app));

	/* get extra details about the app */
	gs_shell_details_set_state (self, GS_SHELL_DETAILS_STATE_LOADING);

	/* disconnect the old handlers */
	if (self->app != NULL) {
		g_signal_handlers_disconnect_by_func (self->app, gs_shell_details_notify_state_changed_cb, self);
		g_signal_handlers_disconnect_by_func (self->app, gs_shell_details_progress_changed_cb, self);
		g_signal_handlers_disconnect_by_func (self->settings,
						      settings_changed_cb,
						      self);
	}
	/* save app */
	g_set_object (&self->app, app);

	g_signal_connect_object (self->app, "notify::state",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::size",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::license",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::quirk",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 self, 0);
	g_signal_connect_object (self->app, "notify::progress",
				 G_CALLBACK (gs_shell_details_progress_changed_cb),
				 self, 0);
	gs_shell_details_load (self);

	/* change widgets */
	gs_shell_details_refresh_all (self);

	g_signal_connect_swapped (self->settings, "changed",
				  G_CALLBACK (settings_changed_cb),
				  self);
}

GsApp *
gs_shell_details_get_app (GsShellDetails *self)
{
	return self->app;
}

static void
gs_shell_details_app_remove_button_cb (GtkWidget *widget, GsShellDetails *self)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();
	g_set_object (&self->cancellable, cancellable);
	gs_page_remove_app (GS_PAGE (self), self->app, self->cancellable);
}

static void
gs_shell_details_app_cancel_button_cb (GtkWidget *widget, GsShellDetails *self)
{
	g_cancellable_cancel (self->cancellable);
	gtk_widget_set_sensitive (widget, FALSE);
}

static void
gs_shell_details_app_install_button_cb (GtkWidget *widget, GsShellDetails *self)
{
	GList *l;
	g_autoptr(GList) addons = NULL;
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();

	/* Mark ticked addons to be installed together with the app */
	addons = gtk_container_get_children (GTK_CONTAINER (self->list_box_addons));
	for (l = addons; l; l = l->next) {
		if (gs_app_addon_row_get_selected (l->data)) {
			GsApp *addon = gs_app_addon_row_get_addon (l->data);

			if (gs_app_get_state (addon) == AS_APP_STATE_AVAILABLE)
				gs_app_set_to_be_installed (addon, TRUE);
		}
	}

	g_set_object (&self->cancellable, cancellable);

	if (gs_app_get_state (self->app) == AS_APP_STATE_UPDATABLE_LIVE) {
		gs_page_update_app (GS_PAGE (self), self->app, self->cancellable);
		return;
	}

	gs_page_install_app (GS_PAGE (self), self->app, self->cancellable);
}

static void
gs_shell_details_addon_selected_cb (GsAppAddonRow *row,
				    GParamSpec *pspec,
				    GsShellDetails *self)
{
	GsApp *addon;

	addon = gs_app_addon_row_get_addon (row);

	/* If the main app is already installed, ticking the addon checkbox
	 * triggers an immediate install. Otherwise we'll install the addon
	 * together with the main app. */
	switch (gs_app_get_state (self->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		if (gs_app_addon_row_get_selected (row)) {
			gs_page_install_app (GS_PAGE (self), addon, self->cancellable);
		} else {
			gs_page_remove_app (GS_PAGE (self), addon, self->cancellable);
			/* make sure the addon checkboxes are synced if the
			 * user clicks cancel in the remove confirmation dialog */
			gs_shell_details_refresh_addons (self);
			gs_shell_details_refresh_all (self);
		}
		break;
	default:
		break;
	}
}

static void
gs_shell_details_app_launch_button_cb (GtkWidget *widget, GsShellDetails *self)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();
	g_set_object (&self->cancellable, cancellable);
	gs_page_launch_app (GS_PAGE (self), self->app, self->cancellable);
}

static void
gs_shell_details_app_add_shortcut_button_cb (GtkWidget *widget,
					     GsShellDetails *self)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();
	g_set_object (&self->cancellable, cancellable);
	gs_page_shortcut_add (GS_PAGE (self), self->app, self->cancellable);
}

static void
gs_shell_details_app_remove_shortcut_button_cb (GtkWidget *widget,
						GsShellDetails *self)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();
	g_set_object (&self->cancellable, cancellable);
	gs_page_shortcut_remove (GS_PAGE (self), self->app, self->cancellable);
}

static void
gs_shell_details_review_response_cb (GtkDialog *dialog,
				     gint response,
				     GsShellDetails *self)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GDateTime) now = NULL;
	g_autoptr(AsReview) review = NULL;
	GsShellDetailsReviewHelper *helper;
	GsReviewDialog *rdialog = GS_REVIEW_DIALOG (dialog);

	/* not agreed */
	if (response != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	review = as_review_new ();
	as_review_set_summary (review, gs_review_dialog_get_summary (rdialog));
	text = gs_review_dialog_get_text (rdialog);
	as_review_set_description (review, text);
	as_review_set_rating (review, gs_review_dialog_get_rating (rdialog));
	as_review_set_version (review, gs_app_get_version (self->app));
	now = g_date_time_new_now_local ();
	as_review_set_date (review, now);

	/* call into the plugins to set the new value */
	helper = g_new0 (GsShellDetailsReviewHelper, 1);
	helper->self = g_object_ref (self);
	helper->app = g_object_ref (self->app);
	helper->review = g_object_ref (review);
	helper->action = GS_PLUGIN_ACTION_REVIEW_SUBMIT;
	gs_plugin_loader_review_action_async (self->plugin_loader,
					      helper->app,
					      helper->review,
					      helper->action,
					      self->cancellable,
					      gs_shell_details_app_set_review_cb,
					      helper);

	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
gs_shell_details_write_review_cb (GtkButton *button,
				  GsShellDetails *self)
{
	GtkWidget *dialog;
	dialog = gs_review_dialog_new ();
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gs_shell_details_review_response_cb), self);
	gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));
}

static void
gs_shell_details_app_installed (GsPage *page, GsApp *app)
{
	gs_shell_details_reload (page);
}

static void
gs_shell_details_app_removed (GsPage *page, GsApp *app)
{
	gs_shell_details_reload (page);
}

static void
gs_shell_details_more_reviews_button_cb (GtkWidget *widget, GsShellDetails *self)
{
	gtk_container_foreach (GTK_CONTAINER (self->list_box_reviews),
			       (GtkCallback) gtk_widget_show, NULL);
	gtk_widget_set_visible (self->button_more_reviews, FALSE);
}

static gboolean
gs_shell_details_activate_link_cb (GtkLabel *label,
				   const gchar *uri,
				   GsShellDetails *self)
{
	gtk_show_uri (NULL, uri, GDK_CURRENT_TIME, NULL);
	return TRUE;
}

static GtkWidget *
gs_shell_details_label_widget (GsShellDetails *self,
			       const gchar *title,
			       const gchar *url)
{
	GtkWidget *w;
	g_autofree gchar *markup = NULL;

	markup = g_strdup_printf ("<a href=\"%s\">%s</a>", url, title);
	w = gtk_label_new (markup);
	g_signal_connect (w, "activate-link",
			  G_CALLBACK (gs_shell_details_activate_link_cb),
			  self);
	gtk_label_set_use_markup (GTK_LABEL (w), TRUE);
	gtk_label_set_xalign (GTK_LABEL (w), 0.f);
	gtk_widget_set_visible (w, TRUE);
	return w;
}

static GtkWidget *
gs_shell_details_license_widget_for_token (GsShellDetails *self, const gchar *token)
{
	/* public domain */
	if (g_strcmp0 (token, "@LicenseRef-public-domain") == 0) {
		/* TRANSLATORS: see the wikipedia page */
		return gs_shell_details_label_widget (self, _("Public domain"),
			"https://en.wikipedia.org/wiki/Public_domain");
	}

	/* free software, license unspecified */
	if (g_str_has_prefix (token, "@LicenseRef-free")) {
		const gchar *url = "https://www.gnu.org/philosophy/free-sw";
		gchar *tmp;

		/* we support putting a custom URL in the
		 * token string, e.g. @LicenseRef-free=http://ubuntu.com */
		tmp = g_strstr_len (token, -1, "=");
		if (tmp != NULL)
			url = tmp + 1;

		/* TRANSLATORS: see GNU page */
		return gs_shell_details_label_widget (self, _("Free Software"), url);
	}

	/* SPDX value */
	if (g_str_has_prefix (token, "@")) {
		g_autofree gchar *uri = NULL;
		uri = g_strdup_printf ("http://spdx.org/licenses/%s",
				       token + 1);
		return gs_shell_details_label_widget (self, token + 1, uri);
	}

	/* new SPDX value the extractor didn't know about */
	if (as_utils_is_spdx_license_id (token)) {
		g_autofree gchar *uri = NULL;
		uri = g_strdup_printf ("http://spdx.org/licenses/%s",
				       token);
		return gs_shell_details_label_widget (self, token, uri);
	}

	/* nothing to show */
	return NULL;
}

static void
gs_shell_details_license_free_cb (GtkWidget *widget, GsShellDetails *self)
{
	guint cnt = 0;
	guint i;
	g_auto(GStrv) tokens = NULL;

	/* URLify any SPDX IDs */
	gs_container_remove_all (GTK_CONTAINER (self->box_details_license_list));
	tokens = as_utils_spdx_license_tokenize (gs_app_get_license (self->app));
	for (i = 0; tokens[i] != NULL; i++) {
		GtkWidget *w = NULL;

		/* translated join */
		if (g_strcmp0 (tokens[i], "&") == 0)
			continue;
		if (g_strcmp0 (tokens[i], "|") == 0)
			continue;

		/* add widget */
		w = gs_shell_details_license_widget_for_token (self, tokens[i]);
		if (w == NULL)
			continue;
		gtk_container_add (GTK_CONTAINER (self->box_details_license_list), w);

		/* one more license */
		cnt++;
	}

	/* use the correct plural */
	gtk_label_set_label (GTK_LABEL (self->label_licenses_intro),
			     /* TRANSLATORS: for the free software popover */
			     ngettext ("Users are bound by the following license:",
				       "Users are bound by the following licenses:",
				       cnt));
	gtk_widget_set_visible (self->label_licenses_intro, cnt > 0);

	gtk_widget_show (self->popover_license_free);
}

static void
gs_shell_details_license_nonfree_cb (GtkWidget *widget, GsShellDetails *self)
{
	g_autofree gchar *str = NULL;
	g_autofree gchar *uri = NULL;
	uri = g_settings_get_string (self->settings, "nonfree-software-uri");
	str = g_strdup_printf ("<a href=\"%s\">%s</a>",
			       uri,
			       _("More information"));
	gtk_label_set_label (GTK_LABEL (self->label_license_nonfree_details), str);
	gtk_widget_show (self->popover_license_nonfree);
}

static void
gs_shell_details_license_unknown_cb (GtkWidget *widget, GsShellDetails *self)
{
	gtk_widget_show (self->popover_license_unknown);
}

void
gs_shell_details_setup (GsShellDetails *self,
			GsShell	*shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	GtkAdjustment *adj;

	g_return_if_fail (GS_IS_SHELL_DETAILS (self));

	self->shell = shell;

	self->plugin_loader = g_object_ref (plugin_loader);
	self->builder = g_object_ref (builder);
	self->cancellable = g_object_ref (cancellable);

	/* show review widgets if we have plugins that provide them */
	self->enable_reviews =
		gs_plugin_loader_get_plugin_supported (plugin_loader,
						       "gs_plugin_review_submit");
	g_signal_connect (self->button_review, "clicked",
			  G_CALLBACK (gs_shell_details_write_review_cb),
			  self);

	/* setup details */
	g_signal_connect (self->button_install, "clicked",
			  G_CALLBACK (gs_shell_details_app_install_button_cb),
			  self);
	g_signal_connect (self->button_remove, "clicked",
			  G_CALLBACK (gs_shell_details_app_remove_button_cb),
			  self);
	g_signal_connect (self->button_cancel, "clicked",
			  G_CALLBACK (gs_shell_details_app_cancel_button_cb),
			  self);
	g_signal_connect (self->button_more_reviews, "clicked",
			  G_CALLBACK (gs_shell_details_more_reviews_button_cb),
			  self);
	g_signal_connect (self->label_details_updated_value, "activate-link",
			  G_CALLBACK (gs_shell_details_history_cb),
			  self);
	g_signal_connect (self->button_details_launch, "clicked",
			  G_CALLBACK (gs_shell_details_app_launch_button_cb),
			  self);
	g_signal_connect (self->button_details_add_shortcut, "clicked",
			  G_CALLBACK (gs_shell_details_app_add_shortcut_button_cb),
			  self);
	g_signal_connect (self->button_details_remove_shortcut, "clicked",
			  G_CALLBACK (gs_shell_details_app_remove_shortcut_button_cb),
			  self);
	g_signal_connect (self->button_details_website, "clicked",
			  G_CALLBACK (gs_shell_details_website_cb),
			  self);
	g_signal_connect (self->button_details_license_free, "clicked",
			  G_CALLBACK (gs_shell_details_license_free_cb),
			  self);
	g_signal_connect (self->button_details_license_nonfree, "clicked",
			  G_CALLBACK (gs_shell_details_license_nonfree_cb),
			  self);
	g_signal_connect (self->button_details_license_unknown, "clicked",
			  G_CALLBACK (gs_shell_details_license_unknown_cb),
			  self);
	g_signal_connect (self->label_license_nonfree_details, "activate-link",
			  G_CALLBACK (gs_shell_details_activate_link_cb),
			  self);

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_details));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (self->box_details), adj);

	/* chain up */
	gs_page_setup (GS_PAGE (self),
	               shell,
	               plugin_loader,
	               cancellable);
}

static void
gs_shell_details_dispose (GObject *object)
{
	GsShellDetails *self = GS_SHELL_DETAILS (object);

	if (self->app != NULL) {
		g_signal_handlers_disconnect_by_func (self->app, gs_shell_details_notify_state_changed_cb, self);
		g_signal_handlers_disconnect_by_func (self->app, gs_shell_details_progress_changed_cb, self);
		g_clear_object (&self->app);
	}
	g_clear_object (&self->builder);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);
	g_clear_object (&self->session);

	G_OBJECT_CLASS (gs_shell_details_parent_class)->dispose (object);
}

static void
gs_shell_details_class_init (GsShellDetailsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_details_dispose;
	page_class->app_installed = gs_shell_details_app_installed;
	page_class->app_removed = gs_shell_details_app_removed;
	page_class->switch_to = gs_shell_details_switch_to;
	page_class->reload = gs_shell_details_reload;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-details.ui");

	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, application_details_icon);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, application_details_summary);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, application_details_title);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_addons);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_details);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_details_description);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_progress);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, star);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_review_count);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_details_screenshot);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_details_screenshot_main);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_details_screenshot_thumbnails);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_details_license_list);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_details_launch);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_details_add_shortcut);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_details_remove_shortcut);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_details_website);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_install);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_remove);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_cancel);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_more_reviews);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, infobar_details_app_norepo);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, infobar_details_app_repo);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, infobar_details_package_baseos);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, infobar_details_repo);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_addons_uninstalled_app);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_progress_percentage);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_progress_status);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_category_title);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_category_value);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_developer_title);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_developer_value);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_details_license_free);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_details_license_nonfree);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_details_license_unknown);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_origin_title);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_origin_value);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_size_download_title);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_size_download_value);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_size_installed_title);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_size_installed_value);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_updated_value);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_version_value);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_failed);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_pending);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, list_box_addons);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_reviews);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, box_details_screenshot_fallback);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, histogram);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, button_review);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, list_box_reviews);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, scrolledwindow_details);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, spinner_details);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, spinner_remove);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, stack_details);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, grid_details_kudo);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, image_details_kudo_docs);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, image_details_kudo_sandboxed);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, image_details_kudo_integration);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, image_details_kudo_translated);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, image_details_kudo_updated);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_kudo_docs);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_kudo_sandboxed);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_kudo_integration);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_kudo_translated);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_details_kudo_updated);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, progressbar_top);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, popover_license_free);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, popover_license_nonfree);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, popover_license_unknown);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_license_nonfree_details);
	gtk_widget_class_bind_template_child (widget_class, GsShellDetails, label_licenses_intro);
}

static void
gs_shell_details_init (GsShellDetails *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	/* setup networking */
	self->session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, gs_user_agent (),
	                                               NULL);
	self->settings = g_settings_new ("org.gnome.software");

	gtk_list_box_set_header_func (GTK_LIST_BOX (self->list_box_addons),
				      list_header_func,
				      self, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->list_box_addons),
				    list_sort_func,
				    self, NULL);
}

GsShellDetails *
gs_shell_details_new (void)
{
	GsShellDetails *self;
	self = g_object_new (GS_TYPE_SHELL_DETAILS, NULL);
	return GS_SHELL_DETAILS (self);
}

/* vim: set noexpandtab: */
