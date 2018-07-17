/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2016 Kalev Lember <klember@redhat.com>
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

#include "gs-page.h"
#include "gs-common.h"
#include "gs-auth-dialog.h"
#include "gs-screenshot-image.h"

typedef struct
{
	GsPluginLoader		*plugin_loader;
	GsShell			*shell;
	GtkWidget		*header_start_widget;
	GtkWidget		*header_end_widget;
	gboolean		 is_active;
} GsPagePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsPage, gs_page, GTK_TYPE_BIN)

GsShell *
gs_page_get_shell (GsPage *page)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	return priv->shell;
}

typedef struct {
	GsApp		*app;
	GsPage		*page;
	GCancellable	*cancellable;
	SoupSession	*soup_session;
	gulong		 notify_quirk_id;
	GtkWidget	*button_install;
	GsPluginAction	 action;
	GsShellInteraction interaction;
	GsPrice		*price;
	GsPageAuthCallback callback;
	gpointer	 callback_data;
} GsPageHelper;

static void
gs_page_helper_free (GsPageHelper *helper)
{
	if (helper->notify_quirk_id > 0)
		g_signal_handler_disconnect (helper->app, helper->notify_quirk_id);
	if (helper->app != NULL)
		g_object_unref (helper->app);
	if (helper->page != NULL)
		g_object_unref (helper->page);
	if (helper->cancellable != NULL)
		g_object_unref (helper->cancellable);
	if (helper->soup_session != NULL)
		g_object_unref (helper->soup_session);
	if (helper->price != NULL)
		g_object_unref (helper->price);
	g_slice_free (GsPageHelper, helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsPageHelper, gs_page_helper_free);

static void
gs_page_authenticate_cb (GtkDialog *dialog,
		 GtkResponseType response_type,
		 gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = user_data;

	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (helper->callback != NULL)
		helper->callback (helper->page, response_type == GTK_RESPONSE_OK, helper->callback_data);
}

void
gs_page_authenticate (GsPage *page,
		      GsApp *app,
		      const gchar *provider_id,
		      GCancellable *cancellable,
                      GsPageAuthCallback callback,
                      gpointer user_data)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_autoptr(GsPageHelper) helper = NULL;
	GtkWidget *dialog;
	g_autoptr(GError) error = NULL;

	helper = g_slice_new0 (GsPageHelper);
	helper->app = app != NULL ? g_object_ref (app) : NULL;
	helper->page = g_object_ref (page);
	helper->callback = callback;
	helper->callback_data = user_data;

	dialog = gs_auth_dialog_new (priv->plugin_loader,
				     app,
				     provider_id,
				     &error);
	if (dialog == NULL) {
		g_warning ("%s", error->message);
		return;
	}
	gs_shell_modal_dialog_present (priv->shell, GTK_DIALOG (dialog));
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gs_page_authenticate_cb),
			  helper);
	g_steal_pointer (&helper);
}

static void
gs_page_app_installed_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data);

static void
gs_page_install_authenticate_cb (GsPage *page,
				 gboolean authenticated,
				 gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (!authenticated)
		return;

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "interactive", TRUE,
					 "app", helper->app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    helper->cancellable,
					    gs_page_app_installed_cb,
					    helper);
	g_steal_pointer (&helper);
}

static void
gs_page_app_removed_cb (GObject *source,
                        GAsyncResult *res,
                        gpointer user_data);

static void
gs_page_remove_authenticate_cb (GsPage *page,
				gboolean authenticated,
				gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (!authenticated)
		return;

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
					 "interactive", TRUE,
					 "app", helper->app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    helper->cancellable,
					    gs_page_app_removed_cb,
					    helper);
	g_steal_pointer (&helper);
}

static void
gs_page_app_installed_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsPage *page = helper->page;
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = gs_plugin_loader_job_action_finish (plugin_loader,
						   res,
						   &error);
	if (g_error_matches (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_CANCELLED)) {
		g_debug ("%s", error->message);
		return;
	}
	if (!ret) {
		/* try to authenticate then retry */
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_AUTH_REQUIRED)) {
			gs_page_authenticate (page,
					      helper->app,
					      gs_utils_get_error_value (error),
					      helper->cancellable,
					      gs_page_install_authenticate_cb,
					      helper);
			g_steal_pointer (&helper);
			return;
		}

		g_warning ("failed to install %s: %s",
		           gs_app_get_id (helper->app),
		           error->message);
		return;
	}

	/* only show this if the window is not active */
	if (gs_app_is_installed (helper->app) &&
	    helper->action == GS_PLUGIN_ACTION_INSTALL &&
	    !gs_shell_is_active (priv->shell) &&
	    ((helper->interaction) & GS_SHELL_INTERACTION_NOTIFY) != 0)
		gs_app_notify_installed (helper->app);

	if (gs_app_is_installed (helper->app) &&
	    GS_PAGE_GET_CLASS (page)->app_installed != NULL) {
		GS_PAGE_GET_CLASS (page)->app_installed (page, helper->app);
	}
}

static void
gs_page_app_removed_cb (GObject *source,
                        GAsyncResult *res,
                        gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsPage *page = helper->page;
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = gs_plugin_loader_job_action_finish (plugin_loader,
						   res,
						   &error);
	if (g_error_matches (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_CANCELLED)) {
		g_debug ("%s", error->message);
		return;
	}
	if (!ret) {
		/* try to authenticate then retry */
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_AUTH_REQUIRED)) {
			gs_page_authenticate (page,
					      helper->app,
					      gs_utils_get_error_value (error),
					      helper->cancellable,
					      gs_page_remove_authenticate_cb,
					      helper);
			g_steal_pointer (&helper);
			return;
		}

		g_warning ("failed to remove: %s", error->message);
		return;
	}

	if (!gs_app_is_installed (helper->app) &&
	    GS_PAGE_GET_CLASS (page)->app_removed != NULL) {
		GS_PAGE_GET_CLASS (page)->app_removed (page, helper->app);
	}
}

GtkWidget *
gs_page_get_header_start_widget (GsPage *page)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	return priv->header_start_widget;
}

void
gs_page_set_header_start_widget (GsPage *page, GtkWidget *widget)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	g_set_object (&priv->header_start_widget, widget);
}

GtkWidget *
gs_page_get_header_end_widget (GsPage *page)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	return priv->header_end_widget;
}

void
gs_page_set_header_end_widget (GsPage *page, GtkWidget *widget)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	g_set_object (&priv->header_end_widget, widget);
}

static void
gs_page_app_purchased_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data);

static void
gs_page_purchase_authenticate_cb (GsPage *page,
				  gboolean authenticated,
				  gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (!authenticated)
		return;

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_PURCHASE,
					 "interactive", TRUE,
					 "app", helper->app,
					 "price", gs_app_get_price (helper->app),
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    helper->cancellable,
					    gs_page_app_purchased_cb,
					    helper);
	g_steal_pointer (&helper);
}

static void
gs_page_app_purchased_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsPage *page = helper->page;
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	gboolean ret;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GError) error = NULL;

	ret = gs_plugin_loader_job_action_finish (plugin_loader,
						  res,
						  &error);
	if (g_error_matches (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_CANCELLED)) {
		g_debug ("%s", error->message);
		return;
	}
	if (!ret) {
		/* try to authenticate then retry */
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_AUTH_REQUIRED)) {
			gs_page_authenticate (page,
					      helper->app,
					      gs_utils_get_error_value (error),
					      helper->cancellable,
					      gs_page_purchase_authenticate_cb,
					      helper);
			g_steal_pointer (&helper);
			return;
		} else if (g_error_matches (error,
		                            GS_PLUGIN_ERROR,
		                            GS_PLUGIN_ERROR_PURCHASE_NOT_SETUP)) {
			const gchar *url;

			/* have we been given a link */
			url = gs_utils_get_error_value (error);
			if (url != NULL) {
				g_autoptr(GError) error_local = NULL;
				g_debug ("showing link in: %s", error->message);
				if (!gtk_show_uri_on_window (GTK_WINDOW (gs_shell_get_window (priv->shell)),
				                             url,
				                             GDK_CURRENT_TIME,
				                             &error_local)) {
					g_warning ("failed to show URI %s: %s",
					           url, error_local->message);
				}
				return;
			}
		}

		g_warning ("failed to purchase %s: %s",
		           gs_app_get_id (helper->app),
		           error->message);
		return;
	}

	if (gs_app_get_state (helper->app) != AS_APP_STATE_AVAILABLE) {
		g_warning ("no plugin purchased %s",
		           gs_app_get_id (helper->app));
		return;
	}

	/* now install */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_INSTALL,
					 "interactive", TRUE,
					 "app", helper->app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader,
					    plugin_job,
					    helper->cancellable,
					    gs_page_app_installed_cb,
					    helper);
	g_steal_pointer (&helper);
}

static void
gs_page_install_purchase_response_cb (GtkDialog *dialog,
				      gint response,
				      gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (dialog));

	/* not agreed */
	if (response != GTK_RESPONSE_OK)
		return;

	g_debug ("purchase %s", gs_app_get_id (helper->app));

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_PURCHASE,
					 "interactive", TRUE,
					 "app", helper->app,
					 "price", gs_app_get_price (helper->app),
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader,
					    plugin_job,
					    helper->cancellable,
					    gs_page_app_purchased_cb,
					    helper);
	g_steal_pointer (&helper);
}

void
gs_page_install_app (GsPage *page,
		     GsApp *app,
		     GsShellInteraction interaction,
		     GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GsPageHelper *helper;

	/* probably non-free */
	if (gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		GtkResponseType response;

		response = gs_app_notify_unavailable (app, gs_shell_get_window (priv->shell));
		if (response != GTK_RESPONSE_OK)
			return;
	}

	helper = g_slice_new0 (GsPageHelper);
	helper->action = GS_PLUGIN_ACTION_INSTALL;
	helper->app = g_object_ref (app);
	helper->page = g_object_ref (page);
	helper->cancellable = g_object_ref (cancellable);
	helper->interaction = interaction;

	/* need to purchase first */
	if (gs_app_get_state (app) == AS_APP_STATE_PURCHASABLE) {
		GtkWidget *dialog;
		g_autofree gchar *title = NULL;
		g_autofree gchar *message = NULL;
		g_autofree gchar *price_text = NULL;

		/* TRANSLATORS: this is a prompt message, and '%s' is an
		 * application summary, e.g. 'GNOME Clocks' */
		title = g_strdup_printf (_("Are you sure you want to purchase %s?"),
					 gs_app_get_name (app));
		price_text = gs_price_to_string (gs_app_get_price (app));
		/* TRANSLATORS: longer dialog text */
		message = g_strdup_printf (_("%s will be installed, and you will "
					     "be charged %s."),
					   gs_app_get_name (app), price_text);

		dialog = gtk_message_dialog_new (gs_shell_get_window (priv->shell),
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_QUESTION,
						 GTK_BUTTONS_CANCEL,
						 "%s", title);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  "%s", message);

		/* TRANSLATORS: this is button text to purchase the application */
		gtk_dialog_add_button (GTK_DIALOG (dialog), _("Purchase"), GTK_RESPONSE_OK);

		/* handle this async */
		g_signal_connect (dialog, "response",
				  G_CALLBACK (gs_page_install_purchase_response_cb), helper);
		gs_shell_modal_dialog_present (priv->shell, GTK_DIALOG (dialog));
	} else {
		g_autoptr(GsPluginJob) plugin_job = NULL;

		plugin_job = gs_plugin_job_newv (helper->action,
						 "interactive", TRUE,
						 "app", helper->app,
						 NULL);
		gs_plugin_loader_job_process_async (priv->plugin_loader,
						    plugin_job,
						    helper->cancellable,
						    gs_page_app_installed_cb,
						    helper);
	}
}

static void
gs_page_update_app_response_cb (GtkDialog *dialog,
				gint response,
				gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (dialog));

	/* not agreed */
	if (response != GTK_RESPONSE_OK)
		return;

	g_debug ("update %s", gs_app_get_id (helper->app));
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPDATE,
					 "interactive", TRUE,
					 "app", helper->app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader,
					    plugin_job,
					    helper->cancellable,
					    gs_page_app_installed_cb,
					    helper);
	g_steal_pointer (&helper);
}

static void
gs_page_notify_quirk_cb (GsApp *app, GParamSpec *pspec, GsPageHelper *helper)
{
	gtk_widget_set_sensitive (helper->button_install,
				  !gs_app_has_quirk (helper->app,
						     AS_APP_QUIRK_NEEDS_USER_ACTION));
}

static void
gs_page_needs_user_action (GsPageHelper *helper, AsScreenshot *ss)
{
	GtkWidget *content_area;
	GtkWidget *dialog;
	GtkWidget *ssimg;
	g_autofree gchar *escaped = NULL;
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);

	dialog = gtk_message_dialog_new (gs_shell_get_window (priv->shell),
					 GTK_DIALOG_MODAL |
					 GTK_DIALOG_USE_HEADER_BAR,
					 GTK_MESSAGE_INFO,
					 GTK_BUTTONS_CANCEL,
					 /* TRANSLATORS: this is a prompt message, and
					  * '%s' is an application summary, e.g. 'GNOME Clocks' */
					 _("Prepare %s"),
					 gs_app_get_name (helper->app));
	escaped = g_markup_escape_text (as_screenshot_get_caption (ss, NULL), -1);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
						    "%s", escaped);

	/* this will be enabled when the device is in the right mode */
	helper->button_install = gtk_dialog_add_button (GTK_DIALOG (dialog),
							/* TRANSLATORS: update the fw */
							_("Install"),
							GTK_RESPONSE_OK);
	helper->notify_quirk_id =
		g_signal_connect (helper->app, "notify::quirk",
				  G_CALLBACK (gs_page_notify_quirk_cb),
				  helper);
	gtk_widget_set_sensitive (helper->button_install, FALSE);

	/* load screenshot */
	helper->soup_session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT,
							      gs_user_agent (), NULL);
	ssimg = gs_screenshot_image_new (helper->soup_session);
	gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), ss);
	gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg), 400, 225);
	gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg),
					helper->cancellable);
	gtk_widget_set_margin_start (ssimg, 24);
	gtk_widget_set_margin_end (ssimg, 24);
	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_box_pack_end (GTK_BOX (content_area), ssimg, FALSE, FALSE, 0);

	/* handle this async */
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gs_page_update_app_response_cb), helper);
	gs_shell_modal_dialog_present (priv->shell, GTK_DIALOG (dialog));
}

void
gs_page_update_app (GsPage *page, GsApp *app, GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GsPageHelper *helper;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* non-firmware applications do not have to be prepared */
	helper = g_slice_new0 (GsPageHelper);
	helper->action = GS_PLUGIN_ACTION_UPDATE;
	helper->app = g_object_ref (app);
	helper->page = g_object_ref (page);
	helper->cancellable = g_object_ref (cancellable);

	/* tell the user what they have to do */
	if (gs_app_get_kind (app) == AS_APP_KIND_FIRMWARE &&
	    gs_app_has_quirk (app, AS_APP_QUIRK_NEEDS_USER_ACTION)) {
		GPtrArray *screenshots = gs_app_get_screenshots (app);
		if (screenshots->len > 0) {
			AsScreenshot *ss = g_ptr_array_index (screenshots, 0);
			if (as_screenshot_get_caption (ss, NULL) != NULL) {
				gs_page_needs_user_action (helper, ss);
				return;
			}
		}
	}

	/* generic fallback */
	plugin_job = gs_plugin_job_newv (helper->action,
					 "interactive", TRUE,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    helper->cancellable,
					    gs_page_app_installed_cb,
					    helper);
}

static void
gs_page_remove_app_response_cb (GtkDialog *dialog,
				gint response,
				gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (dialog));

	/* not agreed */
	if (response != GTK_RESPONSE_OK)
		return;

	g_debug ("remove %s", gs_app_get_id (helper->app));
	plugin_job = gs_plugin_job_newv (helper->action,
					 "interactive", TRUE,
					 "app", helper->app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    helper->cancellable,
					    gs_page_app_removed_cb,
					    helper);
	g_steal_pointer (&helper);
}

void
gs_page_remove_app (GsPage *page, GsApp *app, GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GsPageHelper *helper;
	GtkWidget *dialog;
	g_autofree gchar *message = NULL;
	g_autofree gchar *title = NULL;

	/* pending install */
	helper = g_slice_new0 (GsPageHelper);
	helper->action = GS_PLUGIN_ACTION_REMOVE;
	helper->app = g_object_ref (app);
	helper->page = g_object_ref (page);
	helper->cancellable = g_object_ref (cancellable);
	if (gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL) {
		g_autoptr(GsPluginJob) plugin_job = NULL;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
						 "interactive", TRUE,
						 "app", app,
							 NULL);
		g_debug ("remove %s", gs_app_get_id (app));
		gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
						    helper->cancellable,
						    gs_page_app_removed_cb,
						    helper);
		return;
	}

	/* use different name and summary */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_SOURCE:
		/* TRANSLATORS: this is a prompt message, and '%s' is an
		 * source name, e.g. 'GNOME Nightly' */
		title = g_strdup_printf (_("Are you sure you want to remove "
					   "the %s source?"),
					 gs_app_get_name (app));
		/* TRANSLATORS: longer dialog text */
		message = g_strdup_printf (_("All applications from %s will be "
					     "removed, and you will have to "
					     "re-install the source to use them again."),
					   gs_app_get_name (app));
		break;
	default:
		/* TRANSLATORS: this is a prompt message, and '%s' is an
		 * application summary, e.g. 'GNOME Clocks' */
		title = g_strdup_printf (_("Are you sure you want to remove %s?"),
					 gs_app_get_name (app));
		/* TRANSLATORS: longer dialog text */
		message = g_strdup_printf (_("%s will be removed, and you will "
					     "have to install it to use it again."),
					   gs_app_get_name (app));
		break;
	}

	/* ask for confirmation */
	dialog = gtk_message_dialog_new (gs_shell_get_window (priv->shell),
	                                 GTK_DIALOG_MODAL,
	                                 GTK_MESSAGE_QUESTION,
	                                 GTK_BUTTONS_CANCEL,
	                                 "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", message);

	/* TRANSLATORS: this is button text to remove the application */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Remove"), GTK_RESPONSE_OK);

	/* handle this async */
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gs_page_remove_app_response_cb), helper);
	gs_shell_modal_dialog_present (priv->shell, GTK_DIALOG (dialog));
}

static void
gs_page_app_launched_cb (GObject *source,
			 GAsyncResult *res,
			 gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to launch GsApp: %s", error->message);
		return;
	}
}

void
gs_page_launch_app (GsPage *page, GsApp *app, GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_autoptr(GsPluginJob) plugin_job = NULL;
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_LAUNCH,
					 "interactive", TRUE,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    cancellable,
					    gs_page_app_launched_cb,
					    NULL);
}

static void
gs_page_app_shortcut_added_cb (GObject *source,
			       GAsyncResult *res,
			       gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to add a shortcut to GsApp: %s", error->message);
		return;
	}
}

void
gs_page_shortcut_add (GsPage *page, GsApp *app, GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_autoptr(GsPluginJob) plugin_job = NULL;
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_ADD_SHORTCUT,
					 "interactive", TRUE,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    cancellable,
					    gs_page_app_shortcut_added_cb,
					    NULL);
}

static void
gs_page_app_shortcut_removed_cb (GObject *source,
				 GAsyncResult *res,
				 gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to remove the shortcut to GsApp: %s", error->message);
		return;
	}
}

void
gs_page_shortcut_remove (GsPage *page, GsApp *app, GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_autoptr(GsPluginJob) plugin_job = NULL;
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE_SHORTCUT,
					 "interactive", TRUE,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    cancellable,
					    gs_page_app_shortcut_removed_cb,
					    NULL);
}

gboolean
gs_page_is_active (GsPage *page)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_return_val_if_fail (GS_IS_PAGE (page), FALSE);
	return priv->is_active;
}

/**
 * gs_page_switch_to:
 *
 * Pure virtual method that subclasses have to override to show page specific
 * widgets.
 */
void
gs_page_switch_to (GsPage *page,
                   gboolean scroll_up)
{
	GsPageClass *klass = GS_PAGE_GET_CLASS (page);
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	priv->is_active = TRUE;
	if (klass->switch_to != NULL)
		klass->switch_to (page, scroll_up);
}

/**
 * gs_page_switch_from:
 *
 * Pure virtual method that subclasses have to override to show page specific
 * widgets.
 */
void
gs_page_switch_from (GsPage *page)
{
	GsPageClass *klass = GS_PAGE_GET_CLASS (page);
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	priv->is_active = FALSE;
	if (klass->switch_from != NULL)
		klass->switch_from (page);
}

void
gs_page_reload (GsPage *page)
{
	GsPageClass *klass;
	g_return_if_fail (GS_IS_PAGE (page));
	klass = GS_PAGE_GET_CLASS (page);
	if (klass->reload != NULL)
		klass->reload (page);
}

gboolean
gs_page_setup (GsPage *page,
               GsShell *shell,
               GsPluginLoader *plugin_loader,
               GtkBuilder *builder,
               GCancellable *cancellable,
               GError **error)
{
	GsPageClass *klass;
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	g_return_val_if_fail (GS_IS_PAGE (page), FALSE);

	klass = GS_PAGE_GET_CLASS (page);
	g_assert (klass->setup != NULL);

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->shell = shell;

	return klass->setup (page, shell, plugin_loader, builder, cancellable, error);
}

static void
gs_page_dispose (GObject *object)
{
	GsPage *page = GS_PAGE (object);
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->header_start_widget);
	g_clear_object (&priv->header_end_widget);

	G_OBJECT_CLASS (gs_page_parent_class)->dispose (object);
}

static void
gs_page_init (GsPage *page)
{
}

static void
gs_page_class_init (GsPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_page_dispose;
}

GsPage *
gs_page_new (void)
{
	GsPage *page;
	page = g_object_new (GS_TYPE_PAGE, NULL);
	return GS_PAGE (page);
}

/* vim: set noexpandtab: */
