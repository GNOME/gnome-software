/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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
#include <gtk/gtk.h>

#include "gs-auth.h"
#include "gs-auth-dialog.h"
#include "gs-common.h"

struct _GsAuthDialog
{
	GtkDialog	 parent_instance;

	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
	GsApp		*app;
	GsAuth		*auth;
	GtkWidget	*box_error;
	GtkWidget	*button_cancel;
	GtkWidget	*button_continue;
	GtkWidget	*checkbutton_remember;
	GtkWidget	*entry_password;
	GtkWidget	*entry_pin;
	GtkWidget	*entry_username;
	GtkWidget	*image_vendor;
	GtkWidget	*label_error;
	GtkWidget	*label_title;
	GtkWidget	*radiobutton_already;
	GtkWidget	*radiobutton_lost_pwd;
	GtkWidget	*radiobutton_register;
	GtkWidget	*stack;
};

G_DEFINE_TYPE (GsAuthDialog, gs_auth_dialog, GTK_TYPE_DIALOG)

static void
gs_auth_dialog_check_ui (GsAuthDialog *dialog)
{
	g_autofree gchar *title = NULL;
	const gchar *tmp;
	const gchar *username = gtk_entry_get_text (GTK_ENTRY (dialog->entry_username));
	const gchar *password = gtk_entry_get_text (GTK_ENTRY (dialog->entry_password));

	/* set the header */
	tmp = gs_auth_get_provider_name (dialog->auth);
	if (tmp == NULL) {
		/* TRANSLATORS: this is when the service name is not known */
		title = g_strdup (_("To continue you need to sign in."));
		gtk_label_set_label (GTK_LABEL (dialog->label_title), title);
	} else {
		/* TRANSLATORS: the %s is a service name, e.g. "Ubuntu One" */
		title = g_strdup_printf (_("To continue you need to sign in to %s."), tmp);
		gtk_label_set_label (GTK_LABEL (dialog->label_title), title);
	}

	/* set the vendor image */
	tmp = gs_auth_get_provider_logo (dialog->auth);
	if (tmp == NULL) {
		gtk_widget_hide (dialog->image_vendor);
	} else {
		gtk_image_set_from_file (GTK_IMAGE (dialog->image_vendor), tmp);
		gtk_widget_show (dialog->image_vendor);
	}

	/* need username and password to continue for known account */
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->radiobutton_already))) {
		gtk_widget_set_sensitive (dialog->button_continue,
					  username[0] != '\0' && password[0] != '\0');
		gtk_widget_set_sensitive (dialog->checkbutton_remember, TRUE);
	} else {
		gtk_entry_set_text (GTK_ENTRY (dialog->entry_password), "");
		gtk_widget_set_sensitive (dialog->button_continue,
					  username[0] != '\0');
		gtk_widget_set_sensitive (dialog->checkbutton_remember, FALSE);
	}
}

static void
gs_auth_dialog_cancel_button_cb (GtkWidget *widget, GsAuthDialog *dialog)
{
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
}

static void
gs_auth_dialog_authenticate_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsAuthDialog *dialog = GS_AUTH_DIALOG (user_data);
	g_autoptr(GError) error = NULL;

	/* we failed */
	if (!gs_plugin_loader_app_action_finish (plugin_loader, res, &error)) {
		const gchar *url;

		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_PIN_REQUIRED)) {
			gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "2fa");
			return;
		}

		url = gs_utils_get_error_value (error);

		/* have we been given a link */
		url = gs_utils_get_error_value (error);
		if (url != NULL) {
			g_autoptr(GError) error_local = NULL;
			g_debug ("showing link in: %s", error->message);
			if (!gtk_show_uri (NULL, url, GDK_CURRENT_TIME, &error_local)) {
				g_warning ("failed to show URI %s: %s",
					   url, error_local->message);
			}
			return;
		}

		g_warning ("failed to authenticate: %s", error->message);
		gtk_label_set_label (GTK_LABEL (dialog->label_error), error->message);
		gtk_widget_set_visible (dialog->box_error, TRUE);
		return;
	}

	/* we didn't get authenticated */
	if (!gs_auth_has_flag (dialog->auth, GS_AUTH_FLAG_VALID)) {
		return;
	}

	/* success */
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

static void
gs_auth_dialog_continue_button_cb (GtkWidget *widget, GsAuthDialog *dialog)
{
	GsPluginLoaderAction action = GS_AUTH_ACTION_LOGIN;

	/* alternate actions */
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->radiobutton_lost_pwd)))
		action = GS_AUTH_ACTION_LOST_PASSWORD;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->radiobutton_register)))
		action = GS_AUTH_ACTION_REGISTER;
	gs_plugin_loader_auth_action_async (dialog->plugin_loader,
					    dialog->auth,
					    action,
					    dialog->cancellable,
					    gs_auth_dialog_authenticate_cb,
					    dialog);
}

static void
gs_auth_dialog_setup (GsAuthDialog *dialog)
{

	/* update widgets with known values */
	if (gs_auth_get_username (dialog->auth) != NULL) {
		gtk_entry_set_text (GTK_ENTRY (dialog->entry_username),
				    gs_auth_get_username (dialog->auth));
	}
	if (gs_auth_get_password (dialog->auth) != NULL) {
		gtk_entry_set_text (GTK_ENTRY (dialog->entry_password),
				    gs_auth_get_password (dialog->auth));
	}

	/* refresh UI */
	gs_auth_dialog_check_ui (dialog);
}

static void
gs_auth_dialog_notify_username_cb (GtkEntry *entry,
				   GParamSpec *pspec,
				   GsAuthDialog *dialog)
{
	gs_auth_set_username (dialog->auth, gtk_entry_get_text (entry));
	gs_auth_dialog_check_ui (dialog);
}

static void
gs_auth_dialog_notify_password_cb (GtkEntry *entry,
				   GParamSpec *pspec,
				   GsAuthDialog *dialog)
{
	gs_auth_set_password (dialog->auth, gtk_entry_get_text (entry));
	gs_auth_dialog_check_ui (dialog);
}

static void
gs_auth_dialog_notify_pin_cb (GtkEntry *entry,
			      GParamSpec *pspec,
			      GsAuthDialog *dialog)
{
	gs_auth_set_pin (dialog->auth, gtk_entry_get_text (entry));
	gs_auth_dialog_check_ui (dialog);
}

static void
gs_auth_dialog_toggled_cb (GtkToggleButton *togglebutton, GsAuthDialog *dialog)
{
	gs_auth_dialog_check_ui (dialog);
}

static void
gs_auth_dialog_remember_cb (GtkToggleButton *togglebutton, GsAuthDialog *dialog)
{
	if (gtk_toggle_button_get_active (togglebutton))
		gs_auth_add_flags (dialog->auth, GS_AUTH_FLAG_REMEMBER);
	gs_auth_dialog_check_ui (dialog);
}

static void
gs_auth_dialog_dispose (GObject *object)
{
	GsAuthDialog *dialog = GS_AUTH_DIALOG (object);

	g_clear_object (&dialog->plugin_loader);
	g_clear_object (&dialog->app);
	g_clear_object (&dialog->auth);

	if (dialog->cancellable != NULL) {
		g_cancellable_cancel (dialog->cancellable);
		g_clear_object (&dialog->cancellable);
	}

	G_OBJECT_CLASS (gs_auth_dialog_parent_class)->dispose (object);
}

static void
gs_auth_dialog_init (GsAuthDialog *dialog)
{
	gtk_widget_init_template (GTK_WIDGET (dialog));

	dialog->cancellable = g_cancellable_new ();

	g_signal_connect (dialog->entry_username, "notify::text",
			  G_CALLBACK (gs_auth_dialog_notify_username_cb), dialog);
	g_signal_connect (dialog->entry_password, "notify::text",
			  G_CALLBACK (gs_auth_dialog_notify_password_cb), dialog);
	g_signal_connect (dialog->entry_pin, "notify::text",
			  G_CALLBACK (gs_auth_dialog_notify_pin_cb), dialog);
	g_signal_connect (dialog->checkbutton_remember, "toggled",
			  G_CALLBACK (gs_auth_dialog_remember_cb), dialog);
	g_signal_connect (dialog->radiobutton_already, "toggled",
			  G_CALLBACK (gs_auth_dialog_toggled_cb), dialog);
	g_signal_connect (dialog->radiobutton_register, "toggled",
			  G_CALLBACK (gs_auth_dialog_toggled_cb), dialog);
	g_signal_connect (dialog->radiobutton_lost_pwd, "toggled",
			  G_CALLBACK (gs_auth_dialog_toggled_cb), dialog);
	g_signal_connect (dialog->button_cancel, "clicked",
			  G_CALLBACK (gs_auth_dialog_cancel_button_cb), dialog);
	g_signal_connect (dialog->button_continue, "clicked",
			  G_CALLBACK (gs_auth_dialog_continue_button_cb), dialog);
}

static void
gs_auth_dialog_class_init (GsAuthDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_auth_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-auth-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, box_error);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, button_cancel);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, button_continue);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, checkbutton_remember);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, entry_password);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, entry_pin);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, entry_username);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, image_vendor);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, label_error);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, label_title);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, radiobutton_already);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, radiobutton_lost_pwd);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, radiobutton_register);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, stack);
}

GtkWidget *
gs_auth_dialog_new (GsPluginLoader *plugin_loader,
		    GsApp *app,
		    const gchar *provider_id,
		    GError **error)
{
	GsAuthDialog *dialog;
	GsAuth *auth;

	/* get the authentication provider */
	if (provider_id == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "no auth-provider given for %s",
			     gs_app_get_id (app));
		return NULL;
	}
	auth = gs_plugin_loader_get_auth_by_id (plugin_loader, provider_id);
	if (auth == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no auth-provider %s for %s",
			     provider_id, gs_app_get_id (app));
		return NULL;
	}

	/* create dialog */
	dialog = g_object_new (GS_TYPE_AUTH_DIALOG,
			       "use-header-bar", TRUE,
			       NULL);
	dialog->plugin_loader = g_object_ref (plugin_loader);
	dialog->app = g_object_ref (app);
	dialog->auth = g_object_ref (auth);
	gs_auth_dialog_setup (dialog);

	return GTK_WIDGET (dialog);
}

/* vim: set noexpandtab: */
