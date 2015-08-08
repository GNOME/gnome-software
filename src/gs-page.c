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

#include "gs-app.h"
#include "gs-app-row.h"
#include "gs-cleanup.h"
#include "gs-page.h"
#include "gs-shell.h"
#include "gs-utils.h"

struct _GsPagePrivate
{
	GsPluginLoader		*plugin_loader;
	GCancellable		*cancellable;
	GsShell			*shell;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsPage, gs_page, GTK_TYPE_BIN)

typedef struct {
	GsApp		*app;
	GsPage		*page;
} InstallRemoveData;

static void
install_remove_data_free (InstallRemoveData *data)
{
	if (data->app != NULL)
		g_object_unref (data->app);
	if (data->page != NULL)
		g_object_unref (data->page);
	g_slice_free (InstallRemoveData, data);
}

static void
gs_page_app_installed_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	InstallRemoveData *data = (InstallRemoveData *) user_data;
	GsPage *page = data->page;
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	gboolean ret;
	_cleanup_error_free_ GError *error = NULL;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
	                                          res,
	                                          &error);
	if (!ret) {
		g_warning ("failed to install %s: %s",
		           gs_app_get_id (data->app),
		           error->message);
		gs_app_notify_failed_modal (data->app,
		                            gs_shell_get_window (priv->shell),
		                            GS_PLUGIN_LOADER_ACTION_INSTALL,
		                            error);
		goto out;
	}

	/* only show this if the window is not active */
	if (gs_app_get_state (data->app) != AS_APP_STATE_QUEUED_FOR_INSTALL &&
	    !gs_shell_is_active (priv->shell))
		gs_app_notify_installed (data->app);

	if (GS_PAGE_GET_CLASS (page)->app_installed != NULL)
		GS_PAGE_GET_CLASS (page)->app_installed (page, data->app);

out:
	install_remove_data_free (data);
}

static void
gs_page_app_removed_cb (GObject *source,
                        GAsyncResult *res,
                        gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	InstallRemoveData *data = (InstallRemoveData *) user_data;
	GsPage *page = data->page;
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	gboolean ret;
	_cleanup_error_free_ GError *error = NULL;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
	                                          res,
	                                          &error);
	if (!ret) {
		g_warning ("failed to remove: %s", error->message);
		gs_app_notify_failed_modal (data->app,
		                            gs_shell_get_window (priv->shell),
		                            GS_PLUGIN_LOADER_ACTION_REMOVE,
		                            error);
		goto out;
	}

	if (GS_PAGE_GET_CLASS (page)->app_removed != NULL)
		GS_PAGE_GET_CLASS (page)->app_removed (page, data->app);

out:
	install_remove_data_free (data);
}

void
gs_page_install_app (GsPage *page, GsApp *app)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	InstallRemoveData *data;
	GtkResponseType response;

	/* probably non-free */
	if (gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		response = gs_app_notify_unavailable (app, gs_shell_get_window (priv->shell));
		if (response != GTK_RESPONSE_OK)
			return;
	}

	data = g_slice_new0 (InstallRemoveData);
	data->app = g_object_ref (app);
	data->page = g_object_ref (page);
	gs_plugin_loader_app_action_async (priv->plugin_loader,
	                                   app,
	                                   GS_PLUGIN_LOADER_ACTION_INSTALL,
	                                   priv->cancellable,
	                                   gs_page_app_installed_cb,
	                                   data);
}

void
gs_page_remove_app (GsPage *page, GsApp *app)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GtkResponseType response;
	GtkWidget *dialog;
	_cleanup_string_free_ GString *markup = NULL;

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
	if (gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL)
		response = GTK_RESPONSE_OK; /* pending install */
	else
		response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_OK) {
		InstallRemoveData *data;
		g_debug ("remove %s", gs_app_get_id (app));
		data = g_slice_new0 (InstallRemoveData);
		data->app = g_object_ref (app);
		data->page = g_object_ref (page);
		gs_plugin_loader_app_action_async (priv->plugin_loader,
		                                   app,
		                                   GS_PLUGIN_LOADER_ACTION_REMOVE,
		                                   priv->cancellable,
		                                   gs_page_app_removed_cb,
		                                   data);
	}
	gtk_widget_destroy (dialog);
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
