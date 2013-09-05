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

#include "gs-utils.h"

#include "gs-shell-details.h"

static void	gs_shell_details_finalize	(GObject	*object);

#define GS_SHELL_DETAILS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_DETAILS, GsShellDetailsPrivate))

struct GsShellDetailsPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
	GsApp			*app;
        GsShell                 *shell;
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
		gtk_button_set_label (GTK_BUTTON (widget), _("Install"));
		break;
	case GS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (widget, TRUE);
		gtk_widget_set_sensitive (widget, FALSE);
                gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "suggested-action");
		gtk_button_set_label (GTK_BUTTON (widget), _("Installing"));
		break;
	case GS_APP_STATE_INSTALLED:
	case GS_APP_STATE_REMOVING:
		gtk_widget_set_visible (widget, FALSE);
		break;
	default:
		g_assert_not_reached ();
	}

	/* remove button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	if (kind == GS_APP_KIND_SYSTEM) {
		gtk_widget_set_visible (widget, FALSE);
	} else {
		switch (state) {
		case GS_APP_STATE_INSTALLED:
			gtk_widget_set_visible (widget, TRUE);
			gtk_widget_set_sensitive (widget, TRUE);
                        gtk_style_context_add_class (gtk_widget_get_style_context (widget), "destructive-action");
			gtk_button_set_label (GTK_BUTTON (widget), _("Remove"));
			break;
		case GS_APP_STATE_REMOVING:
			gtk_widget_set_visible (widget, TRUE);
			gtk_widget_set_sensitive (widget, FALSE);
                        gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "destructive-action");
			gtk_button_set_label (GTK_BUTTON (widget), _("Removing"));
			break;
		case GS_APP_STATE_AVAILABLE:
		case GS_APP_STATE_INSTALLING:
			gtk_widget_set_visible (widget, FALSE);
			break;
		default:
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
                        gtk_widget_set_visible (widget, FALSE);
                        gtk_spinner_stop (GTK_SPINNER (widget));
                        break;
                case GS_APP_STATE_INSTALLING:
                case GS_APP_STATE_REMOVING:
                        gtk_spinner_start (GTK_SPINNER (widget));
                        gtk_widget_set_visible (widget, TRUE);
                        break;
		default:
			g_assert_not_reached ();
                }
        }
}

/**
 * gs_shell_details_app_state_changed_cb:
 **/
static void
gs_shell_details_app_state_changed_cb (GsApp *app, GsShellDetails *shell_details)
{
	gs_shell_details_refresh (shell_details);
}

/**
 * gs_shell_details_set_app:
 **/
void
gs_shell_details_set_app (GsShellDetails *shell_details, GsApp *app)
{
	const gchar *tmp;
	GdkPixbuf *pixbuf;
	GtkWidget *widget;
	GtkWidget *widget2;
	GsShellDetailsPrivate *priv = shell_details->priv;

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
						    _("%s will be removed, and you will have to install it to use it again."),
						    gs_app_get_name (priv->app));
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Remove"), GTK_RESPONSE_OK);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_OK) {
		g_debug ("remove %s", gs_app_get_id (priv->app));
		gs_plugin_loader_app_remove (priv->plugin_loader,
					     priv->app,
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
				      priv->cancellable,
				      gs_shell_details_installed_func,
				      shell_details);
}

/**
 * gs_shell_details_setup:
 */
void
gs_shell_details_setup (GsShellDetails *shell_details,
                        GsShell        *shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	GsShellDetailsPrivate *priv = shell_details->priv;
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL_DETAILS (shell_details));

        priv->shell = shell;

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* setup details */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_details_app_install_button_cb),
			  shell_details);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_details_app_remove_button_cb),
			  shell_details);
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
	shell_details->priv = GS_SHELL_DETAILS_GET_PRIVATE (shell_details);
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
        if (priv->app)
                g_object_unref (priv->app);

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
