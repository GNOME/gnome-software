/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "gs-app-private.h"
#include "gs-page.h"
#include "gs-shell.h"
#include "gs-common.h"

typedef struct
{
	GsPluginLoader		*plugin_loader;
	GCancellable		*cancellable;
	GsShell			*shell;
	GtkWidget		*header_start_widget;
	GtkWidget		*header_end_widget;
} GsPagePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsPage, gs_page, GTK_TYPE_BIN)

typedef struct {
	GsApp		*app;
	GsPage		*page;
} GsPageHelper;

static void
gs_page_helper_free (GsPageHelper *helper)
{
	if (helper->app != NULL)
		g_object_unref (helper->app);
	if (helper->page != NULL)
		g_object_unref (helper->page);
	g_slice_free (GsPageHelper, helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsPageHelper, gs_page_helper_free);

static void
gs_page_app_installed_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GError *last_error;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsPage *page = helper->page;
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
	                                          res,
	                                          &error);
	if (!ret) {
		g_warning ("failed to install %s: %s",
		           gs_app_get_id (helper->app),
		           error->message);
		gs_app_notify_failed_modal (helper->app,
		                            gs_shell_get_window (priv->shell),
		                            GS_PLUGIN_LOADER_ACTION_INSTALL,
		                            error);
		return;
	}

	/* non-fatal error */
	last_error = gs_app_get_last_error (helper->app);
	if (last_error != NULL) {
		g_warning ("failed to install %s: %s",
		           gs_app_get_id (helper->app),
		           last_error->message);
		gs_app_notify_failed_modal (helper->app,
					    gs_shell_get_window (priv->shell),
					    GS_PLUGIN_LOADER_ACTION_INSTALL,
					    last_error);
		return;
	}

	/* only show this if the window is not active */
	if (gs_app_get_state (helper->app) != AS_APP_STATE_QUEUED_FOR_INSTALL &&
	    !gs_shell_is_active (priv->shell))
		gs_app_notify_installed (helper->app);

	if (GS_PAGE_GET_CLASS (page)->app_installed != NULL)
		GS_PAGE_GET_CLASS (page)->app_installed (page, helper->app);
}

static void
gs_page_app_removed_cb (GObject *source,
                        GAsyncResult *res,
                        gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GError *last_error;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsPage *page = helper->page;
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
	                                          res,
	                                          &error);
	if (!ret) {
		g_warning ("failed to remove: %s", error->message);
		gs_app_notify_failed_modal (helper->app,
		                            gs_shell_get_window (priv->shell),
		                            GS_PLUGIN_LOADER_ACTION_REMOVE,
		                            error);
		return;
	}

	/* non-fatal error */
	last_error = gs_app_get_last_error (helper->app);
	if (last_error != NULL) {
		g_warning ("failed to remove %s: %s",
		           gs_app_get_id (helper->app),
		           last_error->message);
		gs_app_notify_failed_modal (helper->app,
					    gs_shell_get_window (priv->shell),
					    GS_PLUGIN_LOADER_ACTION_REMOVE,
					    last_error);
		return;
	}

	if (GS_PAGE_GET_CLASS (page)->app_removed != NULL)
		GS_PAGE_GET_CLASS (page)->app_removed (page, helper->app);
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

void
gs_page_install_app (GsPage *page, GsApp *app)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GsPageHelper *helper;
	GtkResponseType response;

	/* probably non-free */
	if (gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		response = gs_app_notify_unavailable (app, gs_shell_get_window (priv->shell));
		if (response != GTK_RESPONSE_OK)
			return;
	}

	helper = g_slice_new0 (GsPageHelper);
	helper->app = g_object_ref (app);
	helper->page = g_object_ref (page);
	gs_plugin_loader_app_action_async (priv->plugin_loader,
	                                   app,
	                                   GS_PLUGIN_LOADER_ACTION_INSTALL,
	                                   priv->cancellable,
	                                   gs_page_app_installed_cb,
	                                   helper);
}

static void
gs_page_update_app_response_cb (GtkDialog *dialog,
				gint response,
				GsPageHelper *helper)
{
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);

	/* not agreed */
	if (response != GTK_RESPONSE_OK) {
		gs_page_helper_free (helper);
		return;
	}
	g_debug ("update %s", gs_app_get_id (helper->app));
	gs_plugin_loader_app_action_async (priv->plugin_loader,
					   helper->app,
					   GS_PLUGIN_LOADER_ACTION_UPDATE,
					   priv->cancellable,
					   gs_page_app_installed_cb,
					   helper);
}

void
gs_page_update_app (GsPage *page, GsApp *app)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GsPageHelper *helper;
	GtkWidget *dialog;
	AsScreenshot *ss;
	g_autofree gchar *escaped = NULL;

	/* non-firmware applications do not have to be prepared */
	helper = g_slice_new0 (GsPageHelper);
	helper->app = g_object_ref (app);
	helper->page = g_object_ref (page);
	if (gs_app_get_kind (app) != AS_APP_KIND_FIRMWARE ||
	    gs_app_get_screenshots (app)->len == 0) {
		gs_plugin_loader_app_action_async (priv->plugin_loader,
						   helper->app,
						   GS_PLUGIN_LOADER_ACTION_UPDATE,
						   priv->cancellable,
						   gs_page_app_installed_cb,
						   helper);
		return;
	}

	/* tell the user what they have to do */
	ss = g_ptr_array_index (gs_app_get_screenshots (app), 0);
	if (as_screenshot_get_caption (ss, NULL) == NULL) {
		gs_plugin_loader_app_action_async (priv->plugin_loader,
						   helper->app,
						   GS_PLUGIN_LOADER_ACTION_UPDATE,
						   priv->cancellable,
						   gs_page_app_installed_cb,
						   helper);
		return;
	}

	/* show user caption */
	dialog = gtk_message_dialog_new (gs_shell_get_window (priv->shell),
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO,
					 GTK_BUTTONS_CANCEL,
					 /* TRANSLATORS: this is a prompt message, and
					  * '%s' is an application summary, e.g. 'GNOME Clocks' */
					 _("Prepare %s"),
					 gs_app_get_name (app));
	escaped = g_markup_escape_text (as_screenshot_get_caption (ss, NULL), -1);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
						    "%s", escaped);
	/* TRANSLATORS: this is button text to update the firware */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Install"), GTK_RESPONSE_OK);

	/* handle this async */
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gs_page_update_app_response_cb), helper);
	gs_shell_modal_dialog_present (priv->shell, GTK_DIALOG (dialog));
}

static void
gs_page_remove_app_response_cb (GtkDialog *dialog,
				gint response,
				GsPageHelper *helper)
{
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);

	/* not agreed */
	if (response != GTK_RESPONSE_OK) {
		gs_page_helper_free (helper);
		return;
	}
	g_debug ("remove %s", gs_app_get_id (helper->app));
	gs_plugin_loader_app_action_async (priv->plugin_loader,
					   helper->app,
					   GS_PLUGIN_LOADER_ACTION_REMOVE,
					   priv->cancellable,
					   gs_page_app_removed_cb,
					   helper);
}

void
gs_page_remove_app (GsPage *page, GsApp *app)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GsPageHelper *helper;
	GtkWidget *dialog;
	g_autofree gchar *escaped = NULL;

	/* pending install */
	helper = g_slice_new0 (GsPageHelper);
	helper->app = g_object_ref (app);
	helper->page = g_object_ref (page);
	if (gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL) {
		g_debug ("remove %s", gs_app_get_id (app));
		gs_plugin_loader_app_action_async (priv->plugin_loader,
		                                   app,
		                                   GS_PLUGIN_LOADER_ACTION_REMOVE,
		                                   priv->cancellable,
		                                   gs_page_app_removed_cb,
		                                   helper);
		return;
	}

	/* ask for confirmation */
	dialog = gtk_message_dialog_new (gs_shell_get_window (priv->shell),
	                                 GTK_DIALOG_MODAL,
	                                 GTK_MESSAGE_QUESTION,
	                                 GTK_BUTTONS_CANCEL,
	                                 /* TRANSLATORS: this is a prompt message, and
	                                  * '%s' is an application summary, e.g. 'GNOME Clocks' */
	                                 _("Are you sure you want to remove %s?"),
	                                 gs_app_get_name (app));
	escaped = g_markup_escape_text (gs_app_get_name (app), -1);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
	                                            /* TRANSLATORS: longer dialog text */
                                                    _("%s will be removed, and you will have to install it to use it again."),
                                                    escaped);

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
	if (!gs_plugin_loader_app_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to launch GsApp: %s", error->message);
		return;
	}
}

void
gs_page_launch_app (GsPage *page, GsApp *app)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	gs_plugin_loader_app_action_async (priv->plugin_loader,
	                                   app,
	                                   GS_PLUGIN_LOADER_ACTION_LAUNCH,
	                                   priv->cancellable,
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
	if (!gs_plugin_loader_app_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to add a shortcut to GsApp: %s", error->message);
		return;
	}
}

void
gs_page_shortcut_add (GsPage *page, GsApp *app)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	gs_plugin_loader_app_action_async (priv->plugin_loader,
	                                   app,
	                                   GS_PLUGIN_LOADER_ACTION_ADD_SHORTCUT,
	                                   priv->cancellable,
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
	if (!gs_plugin_loader_app_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to remove the shortcut to GsApp: %s", error->message);
		return;
	}
}

void
gs_page_shortcut_remove (GsPage *page, GsApp *app)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	gs_plugin_loader_app_action_async (priv->plugin_loader,
	                                   app,
	                                   GS_PLUGIN_LOADER_ACTION_REMOVE_SHORTCUT,
	                                   priv->cancellable,
	                                   gs_page_app_shortcut_removed_cb,
	                                   NULL);
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
	GsPageClass *klass;

	g_return_if_fail (GS_IS_PAGE (page));

	klass = GS_PAGE_GET_CLASS (page);
	g_assert (klass->switch_to != NULL);

	klass->switch_to (page, scroll_up);
}

void
gs_page_setup (GsPage *page,
               GsShell *shell,
               GsPluginLoader *plugin_loader,
               GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	g_return_if_fail (GS_IS_PAGE (page));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->cancellable = g_object_ref (cancellable);
	priv->shell = shell;
}

static void
gs_page_dispose (GObject *object)
{
	GsPage *page = GS_PAGE (object);
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->cancellable);
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
