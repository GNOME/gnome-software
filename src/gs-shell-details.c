/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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
#include <gio/gdesktopappinfo.h>
#include <appstream-glib.h>

#include "gs-utils.h"

#include "gs-shell-details.h"
#include "gs-app-addon-row.h"
#include "gs-history-dialog.h"
#include "gs-screenshot-image.h"
#include "gs-star-widget.h"

static void	gs_shell_details_finalize	(GObject	*object);

typedef enum {
	GS_SHELL_DETAILS_STATE_LOADING,
	GS_SHELL_DETAILS_STATE_READY
} GsShellDetailsState;

struct GsShellDetailsPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
	GsApp			*app;
	GsShell			*shell;
	GtkWidget		*history_dialog;
	GtkWidget		*star;
	SoupSession		*session;

	GtkWidget		*application_details_icon;
	GtkWidget		*application_details_summary;
	GtkWidget		*application_details_title;
	GtkWidget		*box_addons;
	GtkWidget		*box_details;
	GtkWidget		*box_details_description;
	GtkWidget		*box_details_header;
	GtkWidget		*box_details_screenshot;
	GtkWidget		*box_details_screenshot_main;
	GtkWidget		*box_details_screenshot_thumbnails;
	GtkWidget		*button_details_launch;
	GtkWidget		*button_details_website;
	GtkWidget		*button_history;
	GtkWidget		*infobar_details_app_norepo;
	GtkWidget		*infobar_details_app_repo;
	GtkWidget		*infobar_details_package_baseos;
	GtkWidget		*infobar_details_repo;
	GtkWidget		*infobar_details_webapp;
	GtkWidget		*label_addons_uninstalled_app;
	GtkWidget		*label_details_category_value;
	GtkWidget		*label_details_developer_title;
	GtkWidget		*label_details_developer_value;
	GtkWidget		*label_details_licence_value;
	GtkWidget		*label_details_origin_title;
	GtkWidget		*label_details_origin_value;
	GtkWidget		*label_details_size_value;
	GtkWidget		*label_details_updated_value;
	GtkWidget		*label_details_version_value;
	GtkWidget		*list_box_addons;
	GtkWidget		*scrolledwindow_details;
	GtkWidget		*spinner_details;
	GtkWidget		*stack_details;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsShellDetails, gs_shell_details, GTK_TYPE_BIN)

/**
 * gs_shell_details_invalidate:
 **/
void
gs_shell_details_invalidate (GsShellDetails *shell_details)
{
	shell_details->priv->cache_valid = FALSE;
}

/**
 * gs_shell_details_set_state:
 **/
static void
gs_shell_details_set_state (GsShellDetails *shell_details,
			    GsShellDetailsState state)
{
	GsShellDetailsPrivate *priv = shell_details->priv;

	/* spinner */
	switch (state) {
	case GS_SHELL_DETAILS_STATE_LOADING:
		gs_start_spinner (GTK_SPINNER (priv->spinner_details));
		gtk_widget_show (priv->spinner_details);
		break;
	case GS_SHELL_DETAILS_STATE_READY:
		gs_stop_spinner (GTK_SPINNER (priv->spinner_details));
		gtk_widget_hide (priv->spinner_details);
		break;
	default:
		g_assert_not_reached ();
	}

	/* stack */
	switch (state) {
	case GS_SHELL_DETAILS_STATE_LOADING:
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_details), "spinner");
		break;
	case GS_SHELL_DETAILS_STATE_READY:
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_details), "ready");
		break;
	default:
		g_assert_not_reached ();
	}
}

/**
 * gs_shell_details_refresh:
 **/
void
gs_shell_details_refresh (GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GsAppKind kind;
	AsAppState state;
	GtkWidget *widget;
	GtkAdjustment *adj;

	if (gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_DETAILS)
		return;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
	gtk_widget_show (widget);

	kind = gs_app_get_kind (priv->app);
	state = gs_app_get_state (priv->app);

	/* label */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_label"));
	switch (state) {
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (widget, TRUE);
		gtk_label_set_label (GTK_LABEL (widget), _("Pending"));
		break;
	default:
		gtk_widget_set_visible (widget, FALSE);
		break;
	}

	/* install button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	switch (state) {
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
		gtk_widget_set_visible (widget, gs_app_get_kind (priv->app) != GS_APP_KIND_CORE);
		gtk_widget_set_sensitive (widget, TRUE);
		gtk_style_context_add_class (gtk_widget_get_style_context (widget), "suggested-action");
		/* TRANSLATORS: button text in the header when an application
		 * can be installed */
		gtk_button_set_label (GTK_BUTTON (widget), _("_Install"));
		break;
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (widget, FALSE);
		break;
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (widget, TRUE);
		gtk_widget_set_sensitive (widget, FALSE);
		gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "suggested-action");
		/* TRANSLATORS: button text in the header when an application
		 * is in the process of being installed */
		gtk_button_set_label (GTK_BUTTON (widget), _("_Installing"));
		break;
	case AS_APP_STATE_UNKNOWN:
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UNAVAILABLE:
		gtk_widget_set_visible (widget, FALSE);
		break;
	default:
		g_warning ("App unexpectedly in state %s",
			   as_app_state_to_string (state));
		g_assert_not_reached ();
	}

	/* remove button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	if (kind == GS_APP_KIND_SYSTEM) {
		gtk_widget_set_visible (widget, FALSE);
	} else {
		switch (state) {
		case AS_APP_STATE_INSTALLED:
		case AS_APP_STATE_UPDATABLE:
			gtk_widget_set_visible (widget, TRUE);
			gtk_widget_set_sensitive (widget, TRUE);
			gtk_style_context_add_class (gtk_widget_get_style_context (widget), "destructive-action");
			/* TRANSLATORS: button text in the header when an application can be erased */
			gtk_button_set_label (GTK_BUTTON (widget), _("_Remove"));
			break;
		case AS_APP_STATE_REMOVING:
			gtk_widget_set_visible (widget, TRUE);
			gtk_widget_set_sensitive (widget, FALSE);
			gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "destructive-action");
			/* TRANSLATORS: button text in the header when an application can be installed */
			gtk_button_set_label (GTK_BUTTON (widget), _("_Removing"));
			break;
		case AS_APP_STATE_QUEUED_FOR_INSTALL:
			gtk_widget_set_visible (widget, TRUE);
			gtk_widget_set_sensitive (widget, TRUE);
			gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "destructive-action");
			gtk_button_set_label (GTK_BUTTON (widget), _("_Cancel"));
			break;
		case AS_APP_STATE_AVAILABLE_LOCAL:
		case AS_APP_STATE_AVAILABLE:
		case AS_APP_STATE_INSTALLING:
		case AS_APP_STATE_UNAVAILABLE:
		case AS_APP_STATE_UNKNOWN:
			gtk_widget_set_visible (widget, FALSE);
			break;
		default:
			g_warning ("App unexpectedly in state %s",
				   as_app_state_to_string (state));
			g_assert_not_reached ();
		}
	}

	/* spinner */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_spinner_end"));
	if (kind == GS_APP_KIND_SYSTEM) {
		gtk_widget_set_visible (widget, FALSE);
		gtk_spinner_stop (GTK_SPINNER (widget));
	} else {
		switch (state) {
		case AS_APP_STATE_UNKNOWN:
		case AS_APP_STATE_INSTALLED:
		case AS_APP_STATE_AVAILABLE:
		case AS_APP_STATE_QUEUED_FOR_INSTALL:
		case AS_APP_STATE_UPDATABLE:
		case AS_APP_STATE_UNAVAILABLE:
		case AS_APP_STATE_AVAILABLE_LOCAL:
			gtk_widget_set_visible (widget, FALSE);
			gtk_spinner_stop (GTK_SPINNER (widget));
			break;
		case AS_APP_STATE_INSTALLING:
		case AS_APP_STATE_REMOVING:
			gtk_spinner_start (GTK_SPINNER (widget));
			gtk_widget_set_visible (widget, TRUE);
			break;
		default:
			g_warning ("App unexpectedly in state %s",
				   as_app_state_to_string (state));
			g_assert_not_reached ();
		}
	}

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_details));
	gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));

	gs_grab_focus_when_mapped (priv->scrolledwindow_details);
}

/**
 * gs_shell_details_notify_state_changed_cb:
 **/
static void
gs_shell_details_notify_state_changed_cb (GsApp *app,
					  GParamSpec *pspec,
					  GsShellDetails *shell_details)
{
	gs_shell_details_refresh (shell_details);
}

static void
gs_shell_details_screenshot_selected_cb (GtkListBox *list,
					 GtkListBoxRow *row,
					 GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GsScreenshotImage *ssmain;
	GsScreenshotImage *ssthumb;
	AsScreenshot *ss;
	GList *children;

	if (row == NULL)
		return;

	children = gtk_container_get_children (GTK_CONTAINER (priv->box_details_screenshot_main));
	ssmain = GS_SCREENSHOT_IMAGE (children->data);
	g_list_free (children);

	ssthumb = GS_SCREENSHOT_IMAGE (gtk_bin_get_child (GTK_BIN (row)));
	ss = gs_screenshot_image_get_screenshot (ssthumb);
	gs_screenshot_image_set_screenshot (ssmain, ss);
	gs_screenshot_image_load_async (ssmain, NULL);
}

/**
 * gs_shell_details_refresh_screenshots:
 **/
static void
gs_shell_details_refresh_screenshots (GsShellDetails *shell_details)
{
	GPtrArray *screenshots;
	AsScreenshot *ss;
	GsShellDetailsPrivate *priv = shell_details->priv;
	GtkWidget *label;
	GtkWidget *list;
	GtkWidget *ssimg;
	guint i;

	/* treat screenshots differently */
	if (gs_app_get_id_kind (priv->app) == AS_ID_KIND_FONT) {
		gs_container_remove_all (GTK_CONTAINER (priv->box_details_screenshot_thumbnails));
		gs_container_remove_all (GTK_CONTAINER (priv->box_details_screenshot_main));
		screenshots = gs_app_get_screenshots (priv->app);
		for (i = 0; i < screenshots->len; i++) {
			ss = g_ptr_array_index (screenshots, i);

			/* set caption */
			label = gtk_label_new (as_screenshot_get_caption (ss, NULL));
			g_object_set (label,
				      "xalign", 0.0,
				      NULL);
			gtk_box_pack_start (GTK_BOX (priv->box_details_screenshot_main), label, FALSE, FALSE, 0);
			gtk_widget_set_visible (label, TRUE);

			/* set images */
			ssimg = gs_screenshot_image_new (priv->session);
			gs_screenshot_image_set_cachedir (GS_SCREENSHOT_IMAGE (ssimg),
							  g_get_user_cache_dir ());
			gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), ss);
			gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg),
						      G_MAXUINT,
						      G_MAXUINT);
			gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg), NULL);
			gtk_box_pack_start (GTK_BOX (priv->box_details_screenshot_main), ssimg, FALSE, FALSE, 0);
			gtk_widget_set_visible (ssimg, TRUE);
		}
		return;
	}

	/* set screenshots */
	gs_container_remove_all (GTK_CONTAINER (priv->box_details_screenshot_main));
	screenshots = gs_app_get_screenshots (priv->app);
	gtk_widget_set_visible (priv->box_details_screenshot, screenshots->len > 0);
	if (screenshots->len == 0) {
		gs_container_remove_all (GTK_CONTAINER (priv->box_details_screenshot_thumbnails));
		return;
	}

	/* set the default image */
	ss = g_ptr_array_index (screenshots, 0);
	ssimg = gs_screenshot_image_new (priv->session);
	gtk_widget_set_can_focus (gtk_bin_get_child (GTK_BIN (ssimg)), FALSE);
	gs_screenshot_image_set_cachedir (GS_SCREENSHOT_IMAGE (ssimg),
					  g_get_user_cache_dir ());
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
	gtk_box_pack_start (GTK_BOX (priv->box_details_screenshot_main), ssimg, FALSE, FALSE, 0);
	gtk_widget_set_visible (ssimg, TRUE);

	/* set all the thumbnails */
	gs_container_remove_all (GTK_CONTAINER (priv->box_details_screenshot_thumbnails));
	if (screenshots->len < 2)
		return;

	list = gtk_list_box_new ();
	gtk_style_context_add_class (gtk_widget_get_style_context (list), "image-list");
	gtk_widget_show (list);
	gtk_box_pack_start (GTK_BOX (priv->box_details_screenshot_thumbnails), list, FALSE, FALSE, 0);
	for (i = 0; i < screenshots->len; i++) {
		ss = g_ptr_array_index (screenshots, i);
		ssimg = gs_screenshot_image_new (priv->session);
		gs_screenshot_image_set_cachedir (GS_SCREENSHOT_IMAGE (ssimg),
						  g_get_user_cache_dir ());
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
			  shell_details);
}

/**
 * gs_shell_details_website_cb:
 **/
static void
gs_shell_details_website_cb (GtkWidget *widget, GsShellDetails *shell_details)
{
	GError *error = NULL;
	GsShellDetailsPrivate *priv = shell_details->priv;
	const gchar *url;
	gboolean ret;

	url = gs_app_get_url (priv->app, AS_URL_KIND_HOMEPAGE);
	ret = gtk_show_uri (NULL, url, GDK_CURRENT_TIME, &error);
	if (!ret) {
		g_warning ("spawn of '%s' failed", url);
		g_error_free (error);
	}
}

/**
 * gs_shell_details_set_description:
 **/
static void
gs_shell_details_set_description (GsShellDetails *shell_details, const gchar *tmp)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GtkStyleContext *style_context;
	GtkWidget *para;
	gchar **split = NULL;
	guint i;

	/* does the description exist? */
	gtk_widget_set_visible (priv->box_details_description, tmp != NULL);
	if (tmp == NULL)
		goto out;

	/* add each paragraph as a new GtkLabel which lets us get the 24px
	 * paragraph spacing */
	gs_container_remove_all (GTK_CONTAINER (priv->box_details_description));
	split = g_strsplit (tmp, "\n\n", -1);
	for (i = 0; split[i] != NULL; i++) {
		para = gtk_label_new (split[i]);
		gtk_label_set_line_wrap (GTK_LABEL (para), TRUE);
		gtk_label_set_max_width_chars (GTK_LABEL (para), 80);
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

		gtk_box_pack_start (GTK_BOX (priv->box_details_description), para, FALSE, FALSE, 0);
	}
out:
	g_strfreev (split);
}

/**
 * gs_shell_details_is_addon_id_kind
 **/
static gboolean
gs_shell_details_is_addon_id_kind (GsApp *app)
{
        AsIdKind id_kind;
        id_kind = gs_app_get_id_kind (app);
        if (id_kind == AS_ID_KIND_DESKTOP)
                return FALSE;
        if (id_kind == AS_ID_KIND_WEB_APP)
                return FALSE;
        return TRUE;
}

/**
 * gs_shell_details_refresh_all:
 **/
static void
gs_shell_details_refresh_all (GsShellDetails *shell_details)
{
	GError *error = NULL;
	GPtrArray *history;
	GdkPixbuf *pixbuf = NULL;
	GList *addons, *l;
	GsShellDetailsPrivate *priv = shell_details->priv;
	GtkWidget *widget2;
	const gchar *tmp;
	gchar *size;
	guint64 updated;

	/* change widgets */
	tmp = gs_app_get_name (priv->app);
	widget2 = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_label_set_label (GTK_LABEL (priv->application_details_title), tmp);
		gtk_label_set_label (GTK_LABEL (widget2), tmp);
		gtk_widget_set_visible (priv->application_details_title, TRUE);
	} else {
		gtk_widget_set_visible (priv->application_details_title, FALSE);
		gtk_label_set_label (GTK_LABEL (widget2), "");
	}
	tmp = gs_app_get_summary (priv->app);
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_label_set_label (GTK_LABEL (priv->application_details_summary), tmp);
		gtk_widget_set_visible (priv->application_details_summary, TRUE);
	} else {
		gtk_widget_set_visible (priv->application_details_summary, FALSE);
	}

	/* set the description */
	tmp = gs_app_get_description (priv->app);
	gs_shell_details_set_description (shell_details, tmp);

	/* set the icon */
	tmp = gs_app_get_metadata_item (priv->app, "DataDir::desktop-icon");
	if (tmp != NULL) {
		pixbuf = gs_pixbuf_load (tmp, NULL, 96, &error);
		if (pixbuf == NULL) {
			g_warning ("Failed to load desktop icon: %s",
				   error->message);
			g_clear_error (&error);
		}
	}
	if (pixbuf == NULL)
		pixbuf = gs_app_get_pixbuf (priv->app);
	if (pixbuf == NULL && gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL) {
		if (gs_app_get_kind (priv->app) == GS_APP_KIND_SOURCE)
			pixbuf = gs_pixbuf_load ("x-package-repository", NULL, 96, NULL);
		else if (gs_shell_details_is_addon_id_kind (priv->app))
			pixbuf = gs_pixbuf_load ("application-x-addon", NULL, 96, NULL);
		else
			pixbuf = gs_pixbuf_load ("application-x-executable", NULL, 96, NULL);
	}
	if (pixbuf != NULL) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (priv->application_details_icon), pixbuf);
		gtk_widget_set_visible (priv->application_details_icon, TRUE);
	} else {
		gtk_widget_set_visible (priv->application_details_icon, FALSE);
	}

	tmp = gs_app_get_url (priv->app, AS_URL_KIND_HOMEPAGE);
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_widget_set_visible (priv->button_details_website, TRUE);
	} else {
		gtk_widget_set_visible (priv->button_details_website, FALSE);
	}

	/* set the project group */
	tmp = gs_app_get_project_group (priv->app);
	if (tmp == NULL) {
		gtk_widget_set_visible (priv->label_details_developer_title, FALSE);
		gtk_widget_set_visible (priv->label_details_developer_value, FALSE);
	} else {
		gtk_widget_set_visible (priv->label_details_developer_title, TRUE);
		gtk_label_set_label (GTK_LABEL (priv->label_details_developer_value), tmp);
		gtk_widget_set_visible (priv->label_details_developer_value, TRUE);
	}

	/* set the licence */
	tmp = gs_app_get_licence (priv->app);
	if (tmp == NULL) {
		/* TRANSLATORS: this is where the licence is not known */
		gtk_label_set_label (GTK_LABEL (priv->label_details_licence_value), C_("license", "Unknown"));
		gtk_widget_set_tooltip_text (priv->label_details_licence_value, NULL);
	} else {
		gtk_label_set_markup (GTK_LABEL (priv->label_details_licence_value), tmp);
		gtk_widget_set_tooltip_text (priv->label_details_licence_value, NULL);
	}

	/* set version */
	tmp = gs_app_get_version (priv->app);
	if (tmp != NULL){
		gtk_label_set_label (GTK_LABEL (priv->label_details_version_value), tmp);
	} else {
		/* TRANSLATORS: this is where the version is not known */
		gtk_label_set_label (GTK_LABEL (priv->label_details_version_value), C_("version", "Unknown"));
	}

	/* set the size */
	if (gs_app_get_size (priv->app) == GS_APP_SIZE_UNKNOWN) {
		/* TRANSLATORS: this is where the size is being worked out */
		gtk_label_set_label (GTK_LABEL (priv->label_details_size_value), C_("size", "Calculating…"));
	} else if (gs_app_get_size (priv->app) == GS_APP_SIZE_MISSING) {
		/* TRANSLATORS: this is where the size is not known */
		gtk_label_set_label (GTK_LABEL (priv->label_details_size_value), C_("size", "Unknown"));
	} else {
		size = g_format_size (gs_app_get_size (priv->app));
		gtk_label_set_label (GTK_LABEL (priv->label_details_size_value), size);
		g_free (size);
	}

	/* set the updated date */
	updated = gs_app_get_install_date (priv->app);
	if (updated == GS_APP_INSTALL_DATE_UNKNOWN ||
	    updated == GS_APP_INSTALL_DATE_UNSET) {
		/* TRANSLATORS: this is where the updated date is not known */
		gtk_label_set_label (GTK_LABEL (priv->label_details_updated_value), C_("updated", "Never"));
	} else {
		GDateTime *dt;
		dt = g_date_time_new_from_unix_utc (updated);
		size = g_date_time_format (dt, "%x");
		g_date_time_unref (dt);
		gtk_label_set_label (GTK_LABEL (priv->label_details_updated_value), size);
		g_free (size);
	}

	/* set the category */
	tmp = gs_app_get_menu_path (priv->app);
	if (tmp == NULL || tmp[0] == '\0') {
		/* TRANSLATORS: this is the application isn't in any
		 * defined menu category */
		gtk_label_set_label (GTK_LABEL (priv->label_details_category_value), C_("menu category", "None"));
	} else {
		gtk_label_set_label (GTK_LABEL (priv->label_details_category_value), tmp);
	}

	/* set the origin */
	tmp = gs_app_get_origin (priv->app);
	if (tmp == NULL || tmp[0] == '\0') {
		/* TRANSLATORS: this is where we don't know the origin of the
		 * application */
		gtk_label_set_label (GTK_LABEL (priv->label_details_origin_value), C_("origin", "Unknown"));
	} else {
		gtk_label_set_label (GTK_LABEL (priv->label_details_origin_value), tmp);
	}
	gtk_widget_set_visible (priv->label_details_origin_value,
				gs_app_get_state (priv->app) == AS_APP_STATE_INSTALLED ||
				gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL);
	gtk_widget_set_visible (priv->label_details_origin_title,
				gs_app_get_state (priv->app) == AS_APP_STATE_INSTALLED ||
				gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL);

	/* set the rating */
	switch (gs_app_get_id_kind (priv->app)) {
	case AS_ID_KIND_WEB_APP:
		gtk_widget_set_visible (priv->star, FALSE);
		break;
	default:
		gtk_widget_set_visible (priv->star, TRUE);
		if (gs_app_get_rating_kind (priv->app) == GS_APP_RATING_KIND_USER) {
			gs_star_widget_set_rating (GS_STAR_WIDGET (priv->star),
						   GS_APP_RATING_KIND_USER,
						   gs_app_get_rating (priv->app));
		} else {
			gs_star_widget_set_rating (GS_STAR_WIDGET (priv->star),
						   GS_APP_RATING_KIND_KUDOS,
						   gs_app_get_kudos_percentage (priv->app));
		}
		break;
	}

	/* don't show a missing rating on a local file */
	if (gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL &&
	    gs_app_get_rating (priv->app) < 0)
		gtk_widget_set_visible (priv->star, FALSE);

	/* only mark the stars as sensitive if the application is installed */
	gtk_widget_set_sensitive (priv->star,
				  gs_app_get_state (priv->app) == AS_APP_STATE_INSTALLED);

	/* only show launch button when the application is installed */
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
		if (gs_app_get_id_kind (priv->app) == AS_ID_KIND_DESKTOP ||
		    gs_app_get_id_kind (priv->app) == AS_ID_KIND_WEB_APP) {
			gtk_widget_set_visible (priv->button_details_launch, TRUE);
		} else {
			gtk_widget_set_visible (priv->button_details_launch, FALSE);
		}
		break;
	default:
		gtk_widget_set_visible (priv->button_details_launch, FALSE);
		break;
	}

	/* make history button insensitive if there is none */
	history = gs_app_get_history (priv->app);
	switch (gs_app_get_id_kind (priv->app)) {
	case AS_ID_KIND_WEB_APP:
		gtk_widget_set_visible (priv->button_history, FALSE);
		break;
	default:
		gtk_widget_set_sensitive (priv->button_history, history->len > 0);
		gtk_widget_set_visible (priv->button_history, TRUE);
		break;
	}

	/* don't show missing history on a local file */
	if (gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL &&
	    history->len == 0)
		gtk_widget_set_visible (priv->button_history, FALSE);

	/* are we trying to replace something in the baseos */
	switch (gs_app_get_kind (priv->app)) {
	case GS_APP_KIND_CORE:
		gtk_widget_set_visible (priv->infobar_details_package_baseos, TRUE);
		break;
	default:
		gtk_widget_set_visible (priv->infobar_details_package_baseos, FALSE);
		break;
	}

	/* is this a repo-release */
	switch (gs_app_get_kind (priv->app)) {
	case GS_APP_KIND_SOURCE:
		gtk_widget_set_visible (priv->infobar_details_repo, gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL);
		break;
	default:
		gtk_widget_set_visible (priv->infobar_details_repo, FALSE);
		break;
	}

	/* installing a app with a repo file */
	tmp = gs_app_get_metadata_item (priv->app, "PackageKit::has-source");
	switch (gs_app_get_kind (priv->app)) {
	case GS_APP_KIND_NORMAL:
	case GS_APP_KIND_SYSTEM:
		gtk_widget_set_visible (priv->infobar_details_app_repo, tmp != NULL && gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL);
		break;
	default:
		gtk_widget_set_visible (priv->infobar_details_app_repo, FALSE);
		break;
	}

	/* installing a app without a repo file */
	switch (gs_app_get_kind (priv->app)) {
	case GS_APP_KIND_NORMAL:
	case GS_APP_KIND_SYSTEM:
		gtk_widget_set_visible (priv->infobar_details_app_norepo, tmp == NULL && gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL);
		break;
	default:
		gtk_widget_set_visible (priv->infobar_details_app_norepo, FALSE);
		break;
	}

	/* installing a webapp */
	switch (gs_app_get_id_kind (priv->app)) {
	case AS_ID_KIND_WEB_APP:
		gtk_widget_set_visible (priv->infobar_details_webapp, TRUE);
		break;
	default:
		gtk_widget_set_visible (priv->infobar_details_webapp, FALSE);
		break;
	}

	/* only show the "select addons" string if the app isn't yet installed */
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
		gtk_widget_set_visible (priv->label_addons_uninstalled_app, FALSE);
		break;
	default:
		gtk_widget_set_visible (priv->label_addons_uninstalled_app, TRUE);
		break;
	}

	addons = gtk_container_get_children (GTK_CONTAINER (priv->list_box_addons));
	gtk_widget_set_visible (priv->box_addons, addons != NULL);
	for (l = addons; l; l = l->next) {
		/* show checkboxes in front of addons if the app isn't yet installed */
		switch (gs_app_get_state (priv->app)) {
		case AS_APP_STATE_INSTALLED:
		case AS_APP_STATE_UPDATABLE:
			break;
		default:
			break;
		}
	}
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

static void gs_shell_details_addon_selected_cb (GsAppAddonRow *row, GParamSpec *pspec, GsShellDetails *shell_details);

static void
gs_shell_details_refresh_addons (GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GPtrArray *addons;
	guint i;

	gs_container_remove_all (GTK_CONTAINER (priv->list_box_addons));

	addons = gs_app_get_addons (priv->app);
	for (i = 0; i < addons->len; i++) {
		GsApp *addon;
		GtkWidget *row;

	        addon = g_ptr_array_index (addons, i);
	        row = gs_app_addon_row_new ();

	        gs_app_addon_row_set_addon (GS_APP_ADDON_ROW (row), addon);
	        gtk_container_add (GTK_CONTAINER (priv->list_box_addons), row);
	        gtk_widget_show (row);

	        g_signal_connect (row, "notify::selected",
	                          G_CALLBACK (gs_shell_details_addon_selected_cb),
	                          shell_details);
	}
}

/**
 * gs_shell_details_app_refine_cb:
 **/
static void
gs_shell_details_app_refine_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellDetails *shell_details = GS_SHELL_DETAILS (user_data);
	GsShellDetailsPrivate *priv = shell_details->priv;
	gboolean ret;

	ret = gs_plugin_loader_app_refine_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to refine %s: %s",
			   gs_app_get_id (priv->app),
			   error->message);
		g_error_free (error);
		return;
	}
	gs_shell_details_refresh_addons (shell_details);
	gs_shell_details_refresh_all (shell_details);
	gs_shell_details_set_state (shell_details, GS_SHELL_DETAILS_STATE_READY);
}

/**
 * gs_shell_details_filename_to_app_cb:
 **/
static void
gs_shell_details_filename_to_app_cb (GObject *source,
				     GAsyncResult *res,
				     gpointer user_data)
{
	gchar *tmp;
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellDetails *shell_details = GS_SHELL_DETAILS (user_data);
	GsShellDetailsPrivate *priv = shell_details->priv;

	if (priv->app != NULL)
		g_object_unref (priv->app);
	priv->app = gs_plugin_loader_filename_to_app_finish(plugin_loader,
							    res,
							    &error);
	if (priv->app == NULL) {
		g_warning ("failed to convert to GsApp: %s", error->message);
		g_error_free (error);
		return;
	}

	/* save app */
	g_signal_connect_object (priv->app, "notify::state",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 shell_details, 0);
	g_signal_connect_object (priv->app, "notify::size",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 shell_details, 0);
	g_signal_connect_object (priv->app, "notify::licence",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 shell_details, 0);

	/* print what we've got */
	tmp = gs_app_to_string (priv->app);
	g_debug ("%s", tmp);
	g_free (tmp);

	/* change widgets */
	gs_shell_details_refresh (shell_details);
	gs_shell_details_refresh_screenshots (shell_details);
	gs_shell_details_refresh_addons (shell_details);
	gs_shell_details_refresh_all (shell_details);
	gs_shell_details_set_state (shell_details, GS_SHELL_DETAILS_STATE_READY);
}

/**
 * gs_shell_details_set_filename:
 **/
void
gs_shell_details_set_filename (GsShellDetails *shell_details, const gchar *filename)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	gs_shell_details_set_state (shell_details, GS_SHELL_DETAILS_STATE_LOADING);
	gs_plugin_loader_filename_to_app_async (priv->plugin_loader,
						filename,
						GS_PLUGIN_REFINE_FLAGS_DEFAULT |
						GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
						priv->cancellable,
						gs_shell_details_filename_to_app_cb,
						shell_details);
}

/**
 * gs_shell_details_set_app:
 **/
void
gs_shell_details_set_app (GsShellDetails *shell_details, GsApp *app)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	gchar *app_dump;

	/* show some debugging */
	app_dump = gs_app_to_string (app);
	g_debug ("%s", app_dump);
	g_free (app_dump);

	/* get extra details about the app */
	gs_shell_details_set_state (shell_details, GS_SHELL_DETAILS_STATE_LOADING);
	gs_plugin_loader_app_refine_async (priv->plugin_loader, app,
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENCE |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS,
					   priv->cancellable,
					   gs_shell_details_app_refine_cb,
					   shell_details);

	/* save app */
	if (priv->app != NULL)
		g_object_unref (priv->app);
	priv->app = g_object_ref (app);
	g_signal_connect_object (priv->app, "notify::state",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 shell_details, 0);
	g_signal_connect_object (priv->app, "notify::size",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 shell_details, 0);
	g_signal_connect_object (priv->app, "notify::licence",
				 G_CALLBACK (gs_shell_details_notify_state_changed_cb),
				 shell_details, 0);

	/* set screenshots */
	gs_shell_details_refresh_screenshots (shell_details);

	/* change widgets */
	gs_shell_details_refresh_all (shell_details);
}

GsApp *
gs_shell_details_get_app (GsShellDetails *shell_details)
{
	return shell_details->priv->app;
}

typedef struct {
	GsShellDetails	*shell_details;
	GsApp		*app;
} GsShellDetailsHelper;

/**
 * gs_shell_details_app_installed_cb:
 **/
static void
gs_shell_details_app_installed_cb (GObject *source,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellDetailsHelper *helper = (GsShellDetailsHelper *) user_data;
	gboolean ret;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to install %s: %s",
			   gs_app_get_id (helper->app),
			   error->message);
		gs_app_notify_failed_modal (helper->app,
					    gs_shell_get_window (helper->shell_details->priv->shell),
					    GS_PLUGIN_LOADER_ACTION_INSTALL,
					    error);
		g_error_free (error);
		return;
	}

	/* only show this if the window is not active */
	if (gs_app_get_state (helper->app) != AS_APP_STATE_QUEUED_FOR_INSTALL &&
	    !gs_shell_is_active (helper->shell_details->priv->shell))
		gs_app_notify_installed (helper->app);
	gs_shell_details_refresh_all (helper->shell_details);
	g_object_unref (helper->shell_details);
	g_object_unref (helper->app);
	g_free (helper);
}

/**
 * gs_shell_details_app_removed_cb:
 **/
static void
gs_shell_details_app_removed_cb (GObject *source,
				 GAsyncResult *res,
				 gpointer user_data)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellDetailsHelper *helper = (GsShellDetailsHelper *) user_data;
	gboolean ret;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to remove %s: %s",
			   gs_app_get_id (helper->app),
			   error->message);
		gs_app_notify_failed_modal (helper->app,
					    gs_shell_get_window (helper->shell_details->priv->shell),
					    GS_PLUGIN_LOADER_ACTION_REMOVE,
					    error);
		g_error_free (error);
		return;
	}

	gs_shell_details_refresh_all (helper->shell_details);
	g_object_unref (helper->shell_details);
	g_object_unref (helper->app);
	g_free (helper);
}

/**
 * gs_shell_details_app_remove
 **/
static void
gs_shell_details_app_remove (GsShellDetails *shell_details, GsApp *app)
{
	GsShellDetailsHelper *helper;
	GsShellDetailsPrivate *priv = shell_details->priv;
	GString *markup;
	GtkResponseType response;
	GtkWidget *dialog;

	markup = g_string_new ("");
	g_string_append_printf (markup,
				/* TRANSLATORS: this is a prompt message, and
				 * '%s' is an application summary, e.g. 'GNOME Clocks' */
				_("Are you sure you want to remove %s?"),
				gs_app_get_name (app));
	g_string_prepend (markup, "<b>");
	g_string_append (markup, "</b>");
	dialog = gtk_message_dialog_new (gs_shell_get_window (priv->shell),
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CANCEL,
					 NULL);
	gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), markup->str);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
						    /* TRANSLATORS: longer dialog text */
						    _("%s will be removed, and you will have to install it to use it again."),
						    gs_app_get_name (app));
	/* TRANSLATORS: this is button text to remove the application */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Remove"), GTK_RESPONSE_OK);
	if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
		response = gtk_dialog_run (GTK_DIALOG (dialog));
	else
		response = GTK_RESPONSE_OK; /* pending install */
	if (response == GTK_RESPONSE_OK) {
		g_debug ("remove %s", gs_app_get_id (app));
		helper = g_new0 (GsShellDetailsHelper, 1);
		helper->shell_details = g_object_ref (shell_details);
		helper->app = g_object_ref (app);
		gs_plugin_loader_app_action_async (priv->plugin_loader,
						   app,
						   GS_PLUGIN_LOADER_ACTION_REMOVE,
						   priv->cancellable,
						   gs_shell_details_app_removed_cb,
						   helper);
	}
	g_string_free (markup, TRUE);
	gtk_widget_destroy (dialog);
}

/**
 * gs_shell_details_app_remove_button_cb:
 **/
static void
gs_shell_details_app_remove_button_cb (GtkWidget *widget, GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;

	gs_shell_details_app_remove (shell_details, priv->app);
}

/**
 * gs_shell_details_app_install:
 **/
static void
gs_shell_details_app_install (GsShellDetails *shell_details, GsApp *app)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GsShellDetailsHelper *helper;

	helper = g_new0 (GsShellDetailsHelper, 1);
	helper->shell_details = g_object_ref (shell_details);
	helper->app = g_object_ref (app);
	gs_plugin_loader_app_action_async (priv->plugin_loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_INSTALL,
					   priv->cancellable,
					   gs_shell_details_app_installed_cb,
					   helper);
}

/**
 * gs_shell_details_app_install_button_cb:
 **/
static void
gs_shell_details_app_install_button_cb (GtkWidget *widget, GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GList *addons, *l;

	/* Mark ticked addons to be installed together with the app */
	addons = gtk_container_get_children (GTK_CONTAINER (priv->list_box_addons));
	for (l = addons; l; l = l->next) {
		if (gs_app_addon_row_get_selected (l->data)) {
			GsApp *addon = gs_app_addon_row_get_addon (l->data);

			if (gs_app_get_state (addon) == AS_APP_STATE_AVAILABLE)
				gs_app_set_to_be_installed (addon, TRUE);
		}
	}
	g_list_free (addons);

	gs_shell_details_app_install (shell_details, priv->app);
}

/**
 * gs_shell_details_addon_selected_cb:
 **/
static void
gs_shell_details_addon_selected_cb (GsAppAddonRow *row,
                                    GParamSpec *pspec,
                                    GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GsApp *addon;

	addon = gs_app_addon_row_get_addon (row);

	/* If the main app is already installed, ticking the addon checkbox
	 * triggers an immediate install. Otherwise we'll install the addon
	 * together with the main app. */
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
		if (gs_app_addon_row_get_selected (row))
			gs_shell_details_app_install (shell_details, addon);
		else
			gs_shell_details_app_remove (shell_details, addon);
		break;
	default:
		break;
	}
}

/**
 * gs_shell_details_app_launch_button_cb:
 **/
static void
gs_shell_details_app_launch_button_cb (GtkWidget *widget, GsShellDetails *shell_details)
{
	GAppInfo *appinfo;
	GAppLaunchContext *context;
	GError *error = NULL;
	GdkDisplay *display;
	const gchar *desktop_id;

	desktop_id = gs_app_get_id_full (shell_details->priv->app);
	display = gdk_display_get_default ();
	appinfo = G_APP_INFO (g_desktop_app_info_new (desktop_id));
	if (appinfo == NULL) {
		g_warning ("no such desktop file: %s", desktop_id);
		return;
	}
	context = G_APP_LAUNCH_CONTEXT (gdk_display_get_app_launch_context (display));
	if (!g_app_info_launch (appinfo, NULL, context, &error)) {
		g_warning ("launching %s failed: %s", desktop_id, error->message);
		g_error_free (error);
	}

	g_object_unref (appinfo);
	g_object_unref (context);
}

/**
 * gs_shell_details_app_history_button_cb:
 **/
static void
gs_shell_details_app_history_button_cb (GtkWidget *widget, GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;

	gs_history_dialog_set_app (GS_HISTORY_DIALOG (priv->history_dialog), priv->app);

	gtk_window_set_transient_for (GTK_WINDOW (priv->history_dialog), gs_shell_get_window (priv->shell));
	gtk_window_present (GTK_WINDOW (priv->history_dialog));
}

/**
 * gs_shell_details_app_set_ratings_cb:
 **/
static void
gs_shell_details_app_set_ratings_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellDetails *shell_details = GS_SHELL_DETAILS (user_data);
	GsShellDetailsPrivate *priv = shell_details->priv;
	gboolean ret;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to set rating %s: %s",
			   gs_app_get_id (priv->app),
			   error->message);
		g_error_free (error);
	}
}

/**
 * gs_shell_details_rating_changed_cb:
 **/
static void
gs_shell_details_rating_changed_cb (GsStarWidget *star,
				    guint rating,
				    GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;

	g_debug ("%s rating changed from %i%% to %i%%",
		 gs_app_get_id (priv->app),
		 gs_app_get_rating (priv->app),
		 rating);

	/* call into the plugins to set the new value */
	gs_app_set_rating (priv->app, rating);
	gs_app_set_rating_kind (priv->app, GS_APP_RATING_KIND_USER);
	gs_plugin_loader_app_action_async (priv->plugin_loader, priv->app,
					   GS_PLUGIN_LOADER_ACTION_SET_RATING,
					   priv->cancellable,
					   gs_shell_details_app_set_ratings_cb,
					   shell_details);
}

/**
 * gs_shell_details_setup:
 */
void
gs_shell_details_setup (GsShellDetails *shell_details,
			GsShell	*shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GtkWidget *widget;
	GtkAdjustment *adj;

	g_return_if_fail (GS_IS_SHELL_DETAILS (shell_details));

	priv->shell = shell;

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* set up star ratings */
	priv->star = gs_star_widget_new ();
	g_signal_connect (priv->star, "rating-changed",
			  G_CALLBACK (gs_shell_details_rating_changed_cb),
			  shell_details);
	gtk_widget_set_visible (priv->star, TRUE);
	gtk_widget_set_valign (priv->star, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (priv->box_details_header), priv->star, FALSE, FALSE, 0);

	/* setup details */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_details_app_install_button_cb),
			  shell_details);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_details_app_remove_button_cb),
			  shell_details);
	g_signal_connect (priv->button_history, "clicked",
			  G_CALLBACK (gs_shell_details_app_history_button_cb),
			  shell_details);
	g_signal_connect (priv->button_details_launch, "clicked",
			  G_CALLBACK (gs_shell_details_app_launch_button_cb),
			  shell_details);
	g_signal_connect (priv->button_details_website, "clicked",
			  G_CALLBACK (gs_shell_details_website_cb),
			  shell_details);

	/* setup history window */
	priv->history_dialog = gs_history_dialog_new ();
	g_signal_connect (priv->history_dialog, "delete-event",
			  G_CALLBACK (gtk_widget_hide_on_delete), shell_details);

        adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_details));
        gtk_container_set_focus_vadjustment (GTK_CONTAINER (priv->box_details), adj);
}

/**
 * gs_shell_details_class_init:
 **/
static void
gs_shell_details_class_init (GsShellDetailsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = gs_shell_details_finalize;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-details.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, application_details_icon);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, application_details_summary);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, application_details_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, box_addons);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, box_details);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, box_details_description);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, box_details_header);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, box_details_screenshot);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, box_details_screenshot_main);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, box_details_screenshot_thumbnails);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, button_details_launch);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, button_details_website);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, button_history);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, infobar_details_app_norepo);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, infobar_details_app_repo);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, infobar_details_package_baseos);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, infobar_details_repo);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, infobar_details_webapp);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_addons_uninstalled_app);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_details_category_value);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_details_developer_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_details_developer_value);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_details_licence_value);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_details_origin_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_details_origin_value);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_details_size_value);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_details_updated_value);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, label_details_version_value);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, list_box_addons);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, scrolledwindow_details);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, spinner_details);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellDetails, stack_details);
}

/**
 * gs_shell_details_init:
 **/
static void
gs_shell_details_init (GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv;

	gtk_widget_init_template (GTK_WIDGET (shell_details));

	shell_details->priv = gs_shell_details_get_instance_private (shell_details);
	priv = shell_details->priv;

	/* setup networking */
	priv->session = soup_session_sync_new_with_options (SOUP_SESSION_USER_AGENT,
							    "gnome-software",
							    SOUP_SESSION_TIMEOUT, 5000,
							    NULL);
	if (priv->session != NULL) {
		soup_session_add_feature_by_type (priv->session,
						  SOUP_TYPE_PROXY_RESOLVER_DEFAULT);
	}

	gtk_list_box_set_header_func (GTK_LIST_BOX (priv->list_box_addons),
	                              list_header_func,
	                              shell_details, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->list_box_addons),
	                            list_sort_func,
	                            shell_details, NULL);
}

/**
 * gs_shell_details_finalize:
 **/
static void
gs_shell_details_finalize (GObject *object)
{
	GsShellDetails *shell_details = GS_SHELL_DETAILS (object);
	GsShellDetailsPrivate *priv = shell_details->priv;

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);
	if (priv->app != NULL)
		g_object_unref (priv->app);
	if (priv->session != NULL)
		g_object_unref (priv->session);

	G_OBJECT_CLASS (gs_shell_details_parent_class)->finalize (object);
}

/**
 * gs_shell_details_new:
 **/
GsShellDetails *
gs_shell_details_new (void)
{
	GsShellDetails *shell_details;
	shell_details = g_object_new (GS_TYPE_SHELL_DETAILS, NULL);
	return GS_SHELL_DETAILS (shell_details);
}

/* vim: set noexpandtab: */
