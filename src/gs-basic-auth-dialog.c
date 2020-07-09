/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-basic-auth-dialog.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

struct _GsBasicAuthDialog
{
	GtkDialog		 parent_instance;

	GsBasicAuthCallback	 callback;
	gpointer		 callback_data;

	/* template widgets */
	GtkButton		*login_button;
	GtkLabel		*description_label;
	GtkEntry		*user_entry;
	GtkEntry		*password_entry;
};

G_DEFINE_TYPE (GsBasicAuthDialog, gs_basic_auth_dialog, GTK_TYPE_DIALOG)

static void
cancel_button_clicked_cb (GsBasicAuthDialog *dialog)
{
	/* abort the basic auth request */
	dialog->callback (NULL, NULL, dialog->callback_data);

	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
}

static void
login_button_clicked_cb (GsBasicAuthDialog *dialog)
{
	const gchar *user;
	const gchar *password;

	user = gtk_entry_get_text (dialog->user_entry);
	password = gtk_entry_get_text (dialog->password_entry);

	/* submit the user/password to basic auth */
	dialog->callback (user, password, dialog->callback_data);

	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
}

static void
dialog_validate (GsBasicAuthDialog *dialog)
{
	const gchar *user;
	const gchar *password;
	gboolean valid_user;
	gboolean valid_password;

	/* require user */
	user = gtk_entry_get_text (dialog->user_entry);
	valid_user = user != NULL && strlen (user) != 0;

	/* require password */
	password = gtk_entry_get_text (dialog->password_entry);
	valid_password = password != NULL && strlen (password) != 0;

	gtk_widget_set_sensitive (GTK_WIDGET (dialog->login_button), valid_user && valid_password);
}

static void
update_description (GsBasicAuthDialog *dialog, const gchar *remote, const gchar *realm)
{
	g_autofree gchar *description = NULL;

	/* TRANSLATORS: This is a description for entering user/password */
	description = g_strdup_printf (_("Login required remote %s (realm %s)"),
				       remote, realm);
	gtk_label_set_text (dialog->description_label, description);
}

static void
gs_basic_auth_dialog_init (GsBasicAuthDialog *dialog)
{
	gtk_widget_init_template (GTK_WIDGET (dialog));
}

static void
gs_basic_auth_dialog_class_init (GsBasicAuthDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-basic-auth-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsBasicAuthDialog, login_button);
	gtk_widget_class_bind_template_child (widget_class, GsBasicAuthDialog, description_label);
	gtk_widget_class_bind_template_child (widget_class, GsBasicAuthDialog, user_entry);
	gtk_widget_class_bind_template_child (widget_class, GsBasicAuthDialog, password_entry);

	gtk_widget_class_bind_template_callback (widget_class, dialog_validate);
	gtk_widget_class_bind_template_callback (widget_class, cancel_button_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, login_button_clicked_cb);
}

GtkWidget *
gs_basic_auth_dialog_new (GtkWindow *parent,
                          const gchar *remote,
                          const gchar *realm,
                          GsBasicAuthCallback callback,
                          gpointer callback_data)
{
	GsBasicAuthDialog *dialog;

	dialog = g_object_new (GS_TYPE_BASIC_AUTH_DIALOG,
	                       "use-header-bar", TRUE,
	                       "transient-for", parent,
	                       "modal", TRUE,
	                       NULL);
	dialog->callback = callback;
	dialog->callback_data = callback_data;

	update_description (dialog, remote, realm);
	dialog_validate (dialog);

	return GTK_WIDGET (dialog);
}
