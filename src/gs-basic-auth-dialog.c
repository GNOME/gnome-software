/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include "gs-basic-auth-dialog.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

struct _GsBasicAuthDialog
{
	AdwDialog		 parent_instance;

	GsBasicAuthCallback	 callback;
	gpointer		 callback_data;

	/* template widgets */
	GtkButton		*login_button;
	AdwPreferencesPage      *page;
	AdwEntryRow		*user_entry;
	AdwEntryRow		*password_entry;
};

G_DEFINE_TYPE (GsBasicAuthDialog, gs_basic_auth_dialog, ADW_TYPE_DIALOG)

static void
cancel_button_clicked_cb (GsBasicAuthDialog *dialog)
{
	if (dialog->callback != NULL) {
		/* abort the basic auth request */
		dialog->callback (NULL, NULL, dialog->callback_data);
		dialog->callback = NULL;
	}

	adw_dialog_close (ADW_DIALOG (dialog));
}

static void
login_button_clicked_cb (GsBasicAuthDialog *dialog)
{
	const gchar *user;
	const gchar *password;

	user = gtk_editable_get_text (GTK_EDITABLE (dialog->user_entry));
	password = gtk_editable_get_text (GTK_EDITABLE (dialog->password_entry));

	if (dialog->callback != NULL) {
		/* submit the user/password to basic auth */
		dialog->callback (user, password, dialog->callback_data);
		dialog->callback = NULL;
	}

	adw_dialog_close (ADW_DIALOG (dialog));
}

static void
dialog_validate (GsBasicAuthDialog *dialog)
{
	const gchar *user;
	const gchar *password;
	gboolean valid_user;
	gboolean valid_password;

	/* require user */
	user = gtk_editable_get_text (GTK_EDITABLE (dialog->user_entry));
	valid_user = user != NULL && strlen (user) != 0;

	/* require password */
	password = gtk_editable_get_text (GTK_EDITABLE (dialog->password_entry));
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
	adw_preferences_page_set_description (dialog->page, description);
}

static gboolean
close_cb (GtkWidget *widget, GVariant *args, gpointer user_data)
{
  GsBasicAuthDialog *dialog = GS_BASIC_AUTH_DIALOG (widget);

  cancel_button_clicked_cb (dialog);

  return GDK_EVENT_STOP;
}

static void
gs_basic_auth_dialog_map (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (gs_basic_auth_dialog_parent_class)->map (widget);
	gtk_widget_grab_focus (GTK_WIDGET (GS_BASIC_AUTH_DIALOG (widget)->user_entry));
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

	widget_class->map = gs_basic_auth_dialog_map;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-basic-auth-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsBasicAuthDialog, login_button);
	gtk_widget_class_bind_template_child (widget_class, GsBasicAuthDialog, page);
	gtk_widget_class_bind_template_child (widget_class, GsBasicAuthDialog, user_entry);
	gtk_widget_class_bind_template_child (widget_class, GsBasicAuthDialog, password_entry);

	gtk_widget_class_bind_template_callback (widget_class, dialog_validate);
	gtk_widget_class_bind_template_callback (widget_class, cancel_button_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, login_button_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, close_cb);
}

GtkWidget *
gs_basic_auth_dialog_new (const gchar *remote,
                          const gchar *realm,
                          GsBasicAuthCallback callback,
                          gpointer callback_data)
{
	GsBasicAuthDialog *dialog;

	dialog = g_object_new (GS_TYPE_BASIC_AUTH_DIALOG,
	                       NULL);
	dialog->callback = callback;
	dialog->callback_data = callback_data;

	update_description (dialog, remote, realm);
	dialog_validate (dialog);

	return GTK_WIDGET (dialog);
}
