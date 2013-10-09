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

#include "gs-shell-details.h"
#include "gs-screenshot-image.h"

static void	gs_shell_details_finalize	(GObject	*object);

#define GS_SHELL_DETAILS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_DETAILS, GsShellDetailsPrivate))

struct GsShellDetailsPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
	GsApp			*app;
	GsShell			*shell;
	GtkSizeGroup		*history_sizegroup_state;
	GtkSizeGroup		*history_sizegroup_timestamp;
	GtkSizeGroup		*history_sizegroup_version;
	SoupSession		*session;
};

G_DEFINE_TYPE (GsShellDetails, gs_shell_details, G_TYPE_OBJECT)

/**
 * gs_shell_details_invalidate:
 **/
void
gs_shell_details_invalidate (GsShellDetails *shell_details)
{
	shell_details->priv->cache_valid = FALSE;
}

/**
 * gs_shell_details_refresh:
 **/
void
gs_shell_details_refresh (GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GsAppKind kind;
	GsAppState state;
	GtkWidget *widget;

	if (gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_DETAILS)
		return;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	gtk_widget_show (widget);

	kind = gs_app_get_kind (priv->app);
	state = gs_app_get_state (priv->app);

	/* install button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	switch (state) {
	case GS_APP_STATE_AVAILABLE:
		gtk_widget_set_visible (widget, TRUE);
		gtk_widget_set_sensitive (widget, TRUE);
		gtk_style_context_add_class (gtk_widget_get_style_context (widget), "suggested-action");
		/* TRANSLATORS: button text in the header when an application
		 * can be installed */
		gtk_button_set_label (GTK_BUTTON (widget), _("Install"));
		break;
	case GS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (widget, TRUE);
		gtk_widget_set_sensitive (widget, FALSE);
		gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "suggested-action");
		/* TRANSLATORS: button text in the header when an application
		 * is in the process of being installed */
		gtk_button_set_label (GTK_BUTTON (widget), _("Installing"));
		break;
	case GS_APP_STATE_INSTALLED:
	case GS_APP_STATE_REMOVING:
	case GS_APP_STATE_UPDATABLE:
		gtk_widget_set_visible (widget, FALSE);
		break;
	default:
		g_warning ("App unexpectedly in state %s",
			   gs_app_state_to_string (state));
		g_assert_not_reached ();
	}

	/* remove button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	if (kind == GS_APP_KIND_SYSTEM) {
		gtk_widget_set_visible (widget, FALSE);
	} else {
		switch (state) {
		case GS_APP_STATE_INSTALLED:
		case GS_APP_STATE_UPDATABLE:
			gtk_widget_set_visible (widget, TRUE);
			gtk_widget_set_sensitive (widget, TRUE);
			gtk_style_context_add_class (gtk_widget_get_style_context (widget), "destructive-action");
			/* TRANSLATORS: button text in the header when an application can be erased */
			gtk_button_set_label (GTK_BUTTON (widget), _("Remove"));
			break;
		case GS_APP_STATE_REMOVING:
			gtk_widget_set_visible (widget, TRUE);
			gtk_widget_set_sensitive (widget, FALSE);
			gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "destructive-action");
			/* TRANSLATORS: button text in the header when an application can be installed */
			gtk_button_set_label (GTK_BUTTON (widget), _("Removing"));
			break;
		case GS_APP_STATE_AVAILABLE:
		case GS_APP_STATE_INSTALLING:
			gtk_widget_set_visible (widget, FALSE);
			break;
		default:
			g_warning ("App unexpectedly in state %s",
				   gs_app_state_to_string (state));
			g_assert_not_reached ();
		}
	}

	/* spinner */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_spinner"));
	if (kind == GS_APP_KIND_SYSTEM) {
		gtk_widget_set_visible (widget, FALSE);
		gtk_spinner_stop (GTK_SPINNER (widget));
	} else {
		switch (state) {
		case GS_APP_STATE_INSTALLED:
		case GS_APP_STATE_AVAILABLE:
		case GS_APP_STATE_UPDATABLE:
			gtk_widget_set_visible (widget, FALSE);
			gtk_spinner_stop (GTK_SPINNER (widget));
			break;
		case GS_APP_STATE_INSTALLING:
		case GS_APP_STATE_REMOVING:
			gtk_spinner_start (GTK_SPINNER (widget));
			gtk_widget_set_visible (widget, TRUE);
			break;
		default:
			g_warning ("App unexpectedly in state %s",
				   gs_app_state_to_string (state));
			g_assert_not_reached ();
		}
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_details"));
	gs_grab_focus_when_mapped (widget);
}

/**
 * gs_shell_details_app_state_changed_cb:
 **/
static void
gs_shell_details_app_state_changed_cb (GsApp *app, GsShellDetails *shell_details)
{
	gs_shell_details_refresh (shell_details);
}

static void
gs_shell_details_screenshot_selected_cb (GtkListBox *list,
					 GtkListBoxRow *row,
					 GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GtkWidget *widget;
	GsScreenshotImage *ssmain;
	GsScreenshotImage *ssthumb;
	GsScreenshot *ss;
	GList *children;

	if (row == NULL)
		return;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
						     "box_details_screenshot_main"));
	children = gtk_container_get_children (GTK_CONTAINER (widget));
	ssmain = GS_SCREENSHOT_IMAGE (children->data);
	g_list_free (children);

	ssthumb = GS_SCREENSHOT_IMAGE (gtk_bin_get_child (GTK_BIN (row)));
	ss = gs_screenshot_image_get_screenshot (ssthumb);
	gs_screenshot_image_set_screenshot (ssmain,
					    ss,
					    GS_SCREENSHOT_SIZE_LARGE_WIDTH,
					    GS_SCREENSHOT_SIZE_LARGE_HEIGHT);
}

/**
 * gs_shell_details_set_app:
 **/
void
gs_shell_details_set_app (GsShellDetails *shell_details, GsApp *app)
{
	GPtrArray *history;
	GPtrArray *screenshots;
	GdkPixbuf *pixbuf;
	GsScreenshot *ss;
	GsShellDetailsPrivate *priv = shell_details->priv;
	GtkWidget *ssimg;
	GtkWidget *widget2;
	GtkWidget *widget;
	const gchar *tmp;
	gchar *app_dump;
	guint i;

	/* show some debugging */
	app_dump = gs_app_to_string (app);
	g_debug ("%s", app_dump);
	g_free (app_dump);

	/* save app */
	if (priv->app != NULL)
		g_object_unref (priv->app);
	priv->app = g_object_ref (app);
	g_signal_connect (priv->app, "state-changed",
			  G_CALLBACK (gs_shell_details_app_state_changed_cb),
			  shell_details);

	/* change widgets */
	tmp = gs_app_get_name (app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_title"));
	widget2 = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_label_set_label (GTK_LABEL (widget), tmp);
		gtk_label_set_label (GTK_LABEL (widget2), tmp);
		gtk_widget_set_visible (widget, TRUE);
	} else {
		gtk_widget_set_visible (widget, FALSE);
		gtk_label_set_label (GTK_LABEL (widget2), "");
	}
	tmp = gs_app_get_summary (app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_summary"));
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_label_set_label (GTK_LABEL (widget), tmp);
		gtk_widget_set_visible (widget, TRUE);
	} else {
		gtk_widget_set_visible (widget, FALSE);
	}
	tmp = gs_app_get_description (app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_description"));
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_label_set_label (GTK_LABEL (widget), tmp);
		gtk_widget_set_visible (widget, TRUE);
	} else {
		gtk_widget_set_visible (widget, FALSE);
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_description_header"));
	gtk_widget_set_visible (widget, tmp != NULL);

	pixbuf = gs_app_get_pixbuf (app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_icon"));
	if (pixbuf != NULL) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (widget), pixbuf);
		gtk_widget_set_visible (widget, TRUE);
	} else {
		gtk_widget_set_visible (widget, FALSE);
	}

	tmp = gs_app_get_url (app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_url"));
	if (tmp != NULL && tmp[0] != '\0') {
		gtk_link_button_set_uri (GTK_LINK_BUTTON (widget), tmp);
		gtk_widget_set_visible (widget, TRUE);
	} else {
		gtk_widget_set_visible (widget, FALSE);
	}

	/* set screenshots */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_details_screenshot_main"));
	gs_container_remove_all (GTK_CONTAINER (widget));
	screenshots = gs_app_get_screenshots (app);
	if (screenshots->len > 0) {
		ss = g_ptr_array_index (screenshots, 0);
		ssimg = gs_screenshot_image_new (priv->session);
		gtk_widget_set_can_focus (gtk_bin_get_child (GTK_BIN (ssimg)), FALSE);
		gs_screenshot_image_set_cachedir (GS_SCREENSHOT_IMAGE (ssimg), g_get_user_cache_dir ());
		gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg),
						    ss,
						    GS_SCREENSHOT_SIZE_LARGE_WIDTH,
						    GS_SCREENSHOT_SIZE_LARGE_HEIGHT);
		gtk_box_pack_start (GTK_BOX (widget), ssimg, FALSE, FALSE, 0);
		gtk_widget_set_visible (ssimg, TRUE);
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_details_screenshot_thumbnails"));
	gs_container_remove_all (GTK_CONTAINER (widget));
	if (screenshots->len > 1) {
		GtkWidget *list;
		list = gtk_list_box_new ();
		gtk_style_context_add_class (gtk_widget_get_style_context (list), "image-list");
		gtk_widget_show (list);
		gtk_box_pack_start (GTK_BOX (widget), list, FALSE, FALSE, 0);
		for (i = 0; i < screenshots->len; i++) {
			ss = g_ptr_array_index (screenshots, i);
			ssimg = gs_screenshot_image_new (priv->session);
			gs_screenshot_image_set_cachedir (GS_SCREENSHOT_IMAGE (ssimg), g_get_user_cache_dir ());
			gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg),
							    ss,
							    GS_SCREENSHOT_SIZE_SMALL_WIDTH,
							    GS_SCREENSHOT_SIZE_SMALL_HEIGHT);
			gtk_list_box_insert (GTK_LIST_BOX (list), ssimg, -1);
			gtk_widget_set_visible (ssimg, TRUE);
		}

		gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_BROWSE);
		gtk_list_box_select_row (GTK_LIST_BOX (list),
					 gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), 0));
		g_signal_connect (list, "row-selected", 
				  G_CALLBACK (gs_shell_details_screenshot_selected_cb), shell_details);
	}

	/* set the project group */
	tmp = gs_app_get_project_group (app);
	if (tmp == NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details_developer_title"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details_developer_value"));
		gtk_widget_set_visible (widget, FALSE);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details_developer_title"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details_developer_value"));
		gtk_label_set_label (GTK_LABEL (widget), tmp);
		gtk_widget_set_visible (widget, TRUE);
	}

	/* set the licence */
	tmp = gs_app_get_licence (app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
						     "label_details_licence_value"));
	if (tmp == NULL) {
		/* TRANSLATORS: this is where the licence is not known */
		gtk_label_set_label (GTK_LABEL (widget), _("Unknown"));
	} else {
		gtk_label_set_label (GTK_LABEL (widget), tmp);
	}

	/* set version */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_details_version_value"));
	gtk_label_set_label (GTK_LABEL (widget), gs_app_get_version (app));

	/* FIXME: This isn't ready yet */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_details_title"));
	gtk_widget_set_visible (widget, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_details_details"));
	gtk_widget_set_visible (widget, FALSE);

	/* make history button insensitive if there is none */
	history = gs_app_get_history (app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_history"));
	gtk_widget_set_sensitive (widget, history->len > 0);
}

GsApp *
gs_shell_details_get_app (GsShellDetails *shell_details)
{
	return shell_details->priv->app;
}

static void
gs_shell_details_installed_func (GsPluginLoader *plugin_loader, GsApp *app, gpointer user_data)
{
	GsShellDetails *shell_details = GS_SHELL_DETAILS (user_data);
	gs_shell_details_refresh (shell_details);

	if (app) {
		gs_app_notify_installed (app);
	}
}

static void
gs_shell_details_removed_func (GsPluginLoader *plugin_loader, GsApp *app, gpointer user_data)
{
	GsShellDetails *shell_details = GS_SHELL_DETAILS (user_data);
	gs_shell_details_refresh (shell_details);
}

/**
 * gs_shell_details_app_remove_button_cb:
 **/
static void
gs_shell_details_app_remove_button_cb (GtkWidget *widget, GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GString *markup;
	GtkResponseType response;
	GtkWidget *dialog;
	GtkWindow *window;

	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	markup = g_string_new ("");
	g_string_append_printf (markup,
				/* TRANSLATORS: this is a prompt message, and
				 * '%s' is an application summary, e.g. 'GNOME Clocks' */
				_("Are you sure you want to remove %s?"),
				gs_app_get_name (priv->app));
	g_string_prepend (markup, "<b>");
	g_string_append (markup, "</b>");
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CANCEL,
					 NULL);
	gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), markup->str);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
						    /* TRANSLATORS: longer dialog text */
						    _("%s will be removed, and you will have to install it to use it again."),
						    gs_app_get_name (priv->app));
	/* TRANSLATORS: this is button text to remove the application */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Remove"), GTK_RESPONSE_OK);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_OK) {
		g_debug ("remove %s", gs_app_get_id (priv->app));
		gs_plugin_loader_app_remove (priv->plugin_loader,
					     priv->app,
					     GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					     priv->cancellable,
					     gs_shell_details_removed_func,
					     shell_details);
	}
	g_string_free (markup, TRUE);
	gtk_widget_destroy (dialog);
}

/**
 * gs_shell_details_app_install_button_cb:
 **/
static void
gs_shell_details_app_install_button_cb (GtkWidget *widget, GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	gs_plugin_loader_app_install (priv->plugin_loader,
				      priv->app,
				      GS_PLUGIN_REFINE_FLAGS_DEFAULT,
				      priv->cancellable,
				      gs_shell_details_installed_func,
				      shell_details);
}

/**
 * gs_shell_details_history_sort_cb:
 **/
static gint
gs_shell_details_history_sort_cb (gconstpointer a, gconstpointer b)
{
	gint64 timestamp_a = gs_app_get_install_date (*(GsApp **) a);
	gint64 timestamp_b = gs_app_get_install_date (*(GsApp **) b);
	if (timestamp_a < timestamp_b)
		return 1;
	if (timestamp_a > timestamp_b)
		return -1;
	return 0;
}

/**
 * gs_shell_details_app_history_button_cb:
 **/
static void
gs_shell_details_app_history_button_cb (GtkWidget *widget, GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	const gchar *tmp;
	gchar *date_str;
	GDateTime *datetime;
	GPtrArray *history;
	GsApp *app;
	GtkBox *box;
	GtkListBox *list_box;
	guint64 timestamp;
	guint i;

	/* add each history package to the dialog */
	list_box = GTK_LIST_BOX (gtk_builder_get_object (priv->builder, "list_box_history"));
	gs_container_remove_all (GTK_CONTAINER (list_box));
	history = gs_app_get_history (priv->app);
	g_ptr_array_sort (history, gs_shell_details_history_sort_cb);
	for (i = 0; i < history->len; i++) {
		app = g_ptr_array_index (history, i);
		box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0));

		/* add the action */
		switch (gs_app_get_state (app)) {
		case GS_APP_STATE_AVAILABLE:
		case GS_APP_STATE_REMOVING:
			/* TRANSLATORS: this is the status in the history UI,
			 * where we are showing the application was removed */
			tmp = _("Removed");
			break;
		case GS_APP_STATE_INSTALLED:
		case GS_APP_STATE_INSTALLING:
			/* TRANSLATORS: this is the status in the history UI,
			 * where we are showing the application was installed */
			tmp = _("Installed");
			break;
		case GS_APP_STATE_UPDATABLE:
			/* TRANSLATORS: this is the status in the history UI,
			 * where we are showing the application was updated */
			tmp = _("Updated");
			break;
		default:
			/* TRANSLATORS: this is the status in the history UI,
			 * where we are showing that something happened to the
			 * application but we don't know what */
			tmp = _("Unknown");
			break;
		}
		widget = gtk_label_new (tmp);
		g_object_set (widget,
			      "margin-left", 20,
			      "margin-right", 20,
			      "margin-top", 6,
			      "margin-bottom", 6,
			      "xalign", 0.0,
			      NULL);
		gtk_size_group_add_widget (priv->history_sizegroup_state, widget);
		gtk_box_pack_start (box, widget, TRUE, TRUE, 0);

		/* add the timestamp */
		timestamp = gs_app_get_install_date (app);
		datetime = g_date_time_new_from_unix_utc (timestamp);
		if (timestamp == GS_APP_INSTALL_DATE_UNKNOWN) {
			date_str = g_strdup ("");
		} else {
			date_str = g_date_time_format (datetime, "%e %B %Y");
		}
		widget = gtk_label_new (date_str);
		g_object_set (widget,
			      "margin-left", 20,
			      "margin-right", 20,
			      "margin-top", 6,
			      "margin-bottom", 6,
			      "xalign", 0.0,
			      NULL);
		gtk_size_group_add_widget (priv->history_sizegroup_timestamp, widget);
		gtk_box_pack_start (box, widget, TRUE, TRUE, 0);
		g_free (date_str);
		g_date_time_unref (datetime);

		/* add the version */
		widget = gtk_label_new (gs_app_get_version (app));
		g_object_set (widget,
			      "margin-left", 20,
			      "margin-right", 20,
			      "margin-top", 6,
			      "margin-bottom", 6,
			      "xalign", 1.0,
			      NULL);
		gtk_size_group_add_widget (priv->history_sizegroup_version, widget);
		gtk_box_pack_start (box, widget, TRUE, TRUE, 0);

		gtk_widget_show_all (GTK_WIDGET (box));
		gtk_list_box_insert (list_box, GTK_WIDGET (box), -1);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_history"));
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * gs_shell_details_button_close_cb:
 **/
static void
gs_shell_details_button_close_cb (GtkWidget *widget, GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_history"));
	gtk_widget_hide (widget);
}

/**
 * gs_shell_details_list_header_func
 **/
static void
gs_shell_details_list_header_func (GtkListBoxRow *row,
				   GtkListBoxRow *before,
				   gpointer user_data)
{
	GtkWidget *header;

	/* first entry */
	header = gtk_list_box_row_get_header (row);
	if (before == NULL) {
		gtk_list_box_row_set_header (row, NULL);
		return;
	}

	/* already set */
	if (header != NULL)
		return;

	/* set new */
	header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

static void
scrollbar_mapped_cb (GtkWidget *sb, GtkScrolledWindow *swin)
{
        GtkWidget *frame;

        frame = gtk_bin_get_child (GTK_BIN (gtk_bin_get_child (GTK_BIN (swin))));
        if (gtk_widget_get_mapped (GTK_WIDGET (sb))) {
                gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_IN);
                gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
        }
        else {
                gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
                gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_NONE);
        }
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
	GtkListBox *list_box;
	GtkWidget *sw;
	GtkAdjustment *adj;

	g_return_if_fail (GS_IS_SHELL_DETAILS (shell_details));

	priv->shell = shell;

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* setup history */
	list_box = GTK_LIST_BOX (gtk_builder_get_object (priv->builder, "list_box_history"));
	gtk_list_box_set_header_func (list_box,
				      gs_shell_details_list_header_func,
				      shell_details,
				      NULL);

	/* setup details */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_details_app_install_button_cb),
			  shell_details);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_details_app_remove_button_cb),
			  shell_details);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_history"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_details_app_history_button_cb),
			  shell_details);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_history_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_details_button_close_cb),
			  shell_details);

	/* setup history window */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_history"));
	g_signal_connect (widget, "delete-event",
			  G_CALLBACK (gtk_widget_hide_on_delete), shell_details);

        sw = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_history"));
        widget = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (sw));
        g_signal_connect (widget, "map", G_CALLBACK (scrollbar_mapped_cb), sw);
        g_signal_connect (widget, "unmap", G_CALLBACK (scrollbar_mapped_cb), sw);

	sw = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_details"));
        adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (sw));
        widget = GTK_WIDGET (gtk_builder_get_object (builder, "box_details"));
        gtk_container_set_focus_vadjustment (GTK_CONTAINER (widget), adj);

}

/**
 * gs_shell_details_class_init:
 **/
static void
gs_shell_details_class_init (GsShellDetailsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_details_finalize;

	g_type_class_add_private (klass, sizeof (GsShellDetailsPrivate));
}

/**
 * gs_shell_details_init:
 **/
static void
gs_shell_details_init (GsShellDetails *shell_details)
{
	GsShellDetailsPrivate *priv;

	shell_details->priv = GS_SHELL_DETAILS_GET_PRIVATE (shell_details);
	priv = shell_details->priv;

	priv->history_sizegroup_state = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->history_sizegroup_timestamp = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->history_sizegroup_version = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	/* setup networking */
	priv->session = soup_session_sync_new_with_options (SOUP_SESSION_USER_AGENT,
							    "gnome-software",
							    SOUP_SESSION_TIMEOUT, 5000,
							    NULL);
	if (priv->session != NULL) {
		soup_session_add_feature_by_type (priv->session,
						  SOUP_TYPE_PROXY_RESOLVER_DEFAULT);
	}
}

/**
 * gs_shell_details_finalize:
 **/
static void
gs_shell_details_finalize (GObject *object)
{
	GsShellDetails *shell_details = GS_SHELL_DETAILS (object);
	GsShellDetailsPrivate *priv = shell_details->priv;

	g_object_unref (priv->history_sizegroup_state);
	g_object_unref (priv->history_sizegroup_timestamp);
	g_object_unref (priv->history_sizegroup_version);

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
