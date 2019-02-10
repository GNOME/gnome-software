/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2018 Canonical Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>

#include "gs-auth-dialog.h"
#include "gs-common.h"

struct _GsAuthDialog
{
	GtkDialog parent_instance;

	GoaClient *goa_client;
	GtkListStore *liststore_account;

	GtkWidget *label_header;
	GtkWidget *combobox_account;
	GtkWidget *label_account;
	GtkWidget *button_add_another;
	GtkWidget *button_cancel;
	GtkWidget *button_continue;

	gboolean dispose_on_new_account;

	GCancellable *cancellable;
	GsPluginLoader *plugin_loader;
	GsApp *app;
	GsAuth *auth;
};

static void gs_auth_dialog_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (GsAuthDialog, gs_auth_dialog, GTK_TYPE_DIALOG,
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, gs_auth_dialog_initable_iface_init))

enum {
	COLUMN_ID,
	COLUMN_EMAIL,
	COLUMN_ACCOUNT,
	N_COLUMNS
};

static gboolean
gs_auth_dialog_ignore_account (GsAuthDialog *self, GoaAccount *account)
{
	return g_strcmp0 (goa_account_get_provider_type (account),
			  gs_auth_get_provider_type (self->auth)) != 0;
}

static void
gs_auth_dialog_set_header (GsAuthDialog *self,
			   const gchar *text)
{
	g_autofree gchar *markup = NULL;
	markup = g_strdup_printf ("<span size='larger' weight='bold'>%s</span>", text);
	gtk_label_set_markup (GTK_LABEL (self->label_header), markup);
}

static gint
gs_auth_dialog_get_naccounts (GsAuthDialog *self)
{
	return gtk_tree_model_iter_n_children (GTK_TREE_MODEL (self->liststore_account), NULL);
}

static gboolean
gs_auth_dialog_get_nth_account_data (GsAuthDialog *self,
				     gint n,
				     ...)
{
	GtkTreeIter iter;
	va_list var_args;

	if (!gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (self->liststore_account), &iter, NULL, n))
		return FALSE;

	va_start (var_args, n);
	gtk_tree_model_get_valist (GTK_TREE_MODEL (self->liststore_account), &iter, var_args);
	va_end (var_args);

	return TRUE;
}

static gboolean
gs_auth_dialog_get_account_iter (GsAuthDialog *self,
				 GoaAccount *account,
				 GtkTreeIter *iter)
{
	gboolean valid;

	valid = gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (self->liststore_account), iter, NULL, 0);

	while (valid) {
		g_autofree gchar *id;
		gtk_tree_model_get (GTK_TREE_MODEL (self->liststore_account), iter, COLUMN_ID, &id, -1);
		if (g_strcmp0 (id, goa_account_get_id (account)) == 0)
			return TRUE;
		else
			valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (self->liststore_account), iter);
	}

	return FALSE;
}


static void
gs_auth_dialog_check_ui (GsAuthDialog *self,
			 gboolean select)
{
	gint naccounts = gs_auth_dialog_get_naccounts (self);

	gs_auth_dialog_set_header (self, gs_auth_get_header (self->auth, naccounts));

	if (naccounts == 0) {
		gtk_widget_set_visible (self->combobox_account, FALSE);
		gtk_widget_set_visible (self->label_account, FALSE);
		gtk_widget_set_visible (self->button_add_another, FALSE);
		gtk_button_set_label (GTK_BUTTON (self->button_continue), _("Sign In / Register…"));
	} else if (naccounts == 1) {
		g_autofree gchar *email = NULL;

		gtk_widget_set_visible (self->combobox_account, FALSE);
		gtk_widget_set_visible (self->label_account, TRUE);
		gtk_widget_set_visible (self->button_add_another, TRUE);
		gtk_button_set_label (GTK_BUTTON (self->button_continue), _("Continue"));
		gs_auth_dialog_get_nth_account_data (self, 0, COLUMN_EMAIL, &email, -1);
		gtk_label_set_text (GTK_LABEL (self->label_account), email);
	} else {
		gtk_widget_set_visible (self->combobox_account, TRUE);
		gtk_widget_set_visible (self->label_account, FALSE);
		gtk_widget_set_visible (self->button_add_another, TRUE);
		gtk_button_set_label (GTK_BUTTON (self->button_continue), _("Use"));

		if (select)
			gtk_combo_box_set_active (GTK_COMBO_BOX (self->combobox_account), naccounts - 1);
		else if (gtk_combo_box_get_active (GTK_COMBO_BOX (self->combobox_account)) == -1)
			gtk_combo_box_set_active (GTK_COMBO_BOX (self->combobox_account), 0);
	}
}

static void
gs_auth_dialog_add_account (GsAuthDialog *self,
			    GoaAccount *account,
			    gboolean select)
{
	GtkTreeIter iter;

	if (gs_auth_dialog_ignore_account (self, account) ||
	    gs_auth_dialog_get_account_iter (self, account, &iter))
		return;

	gtk_list_store_append (self->liststore_account, &iter);
	gtk_list_store_set (self->liststore_account, &iter,
			    COLUMN_ID, goa_account_get_id (account),
			    COLUMN_EMAIL, goa_account_get_presentation_identity (account),
			    COLUMN_ACCOUNT, account,
			    -1);

	gs_auth_dialog_check_ui (self, select);
}

static void
gs_auth_dialog_remove_account (GsAuthDialog *self,
			       GoaAccount *account)
{
	GtkTreeIter iter;

	if (gs_auth_dialog_ignore_account (self, account) ||
	    !gs_auth_dialog_get_account_iter (self, account, &iter))
		return;


	gtk_list_store_remove (self->liststore_account, &iter);
	gs_auth_dialog_check_ui (self, FALSE);
}

static void
gs_auth_dialog_setup_model (GsAuthDialog *self)
{
	g_autoptr(GList) accounts = goa_client_get_accounts (self->goa_client);

	for (GList *l = accounts; l != NULL; l = l->next) {
		gs_auth_dialog_add_account (self,  goa_object_peek_account (l->data), FALSE);
		g_object_unref (l->data);
	}
}

static GVariant*
gs_auth_dialog_build_dbus_parameters (const gchar *action,
				      const gchar *arg)
{
	GVariantBuilder builder;
	GVariant *array[1], *params2[3];

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("av"));

	if (!action && !arg) {
		g_variant_builder_add (&builder, "v", g_variant_new_string (""));
	} else {
		if (action)
			g_variant_builder_add (&builder, "v", g_variant_new_string (action));

		if (arg)
			g_variant_builder_add (&builder, "v", g_variant_new_string (arg));
	}

	array[0] = g_variant_new ("v", g_variant_new ("(sav)", "online-accounts", &builder));

	params2[0] = g_variant_new_string ("launch-panel");
	params2[1] = g_variant_new_array (G_VARIANT_TYPE ("v"), array, 1);
	params2[2] = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);

	return g_variant_new_tuple (params2, 3);
}

static void
gs_auth_dialog_spawn_goa_with_args (const gchar *action,
				    const gchar *arg)
{
	g_autoptr(GDBusProxy) proxy = NULL;

	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "org.gnome.ControlCenter",
					       "/org/gnome/ControlCenter",
					       "org.gtk.Actions",
					       NULL,
					       NULL);

	if (!proxy)
	{
		g_warning ("Couldn't open Online Accounts panel");
		return;
	}

	g_dbus_proxy_call_sync (proxy,
				"Activate",
				gs_auth_dialog_build_dbus_parameters (action, arg),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				NULL);
}

static void
gs_auth_dialog_ensure_crendentials_cb (GObject *source_object,
				       GAsyncResult *res,
				       gpointer user_data)
{
	GsAuthDialog *self = (GsAuthDialog*) user_data;
	GoaAccount *account = GOA_ACCOUNT (source_object);
	g_autoptr(GError) error = NULL;

	if (!goa_account_call_ensure_credentials_finish (account, NULL, res, &error)) {
		gs_auth_dialog_spawn_goa_with_args (goa_account_get_id (account), NULL);
	} else {
		GoaObject *goa_object;

		goa_object = goa_client_lookup_by_id (self->goa_client,
						      goa_account_get_id (account));

		gs_auth_set_goa_object (self->auth, goa_object);
		gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
	}
}

static void
gs_auth_dialog_response_if_valid (GsAuthDialog *self,
				  GoaAccount *account)
{
	goa_account_call_ensure_credentials (account,
					     self->cancellable,
					     gs_auth_dialog_ensure_crendentials_cb,
					     self);
}

static void
gs_auth_dialog_account_added_cb (GoaClient *client,
				 GoaObject *object,
				 GsAuthDialog *self)
{
	GoaAccount *account = goa_object_peek_account (object);

	if (gs_auth_dialog_ignore_account (self, account))
		return;

	if (!self->dispose_on_new_account)
		gs_auth_dialog_add_account (self, account, FALSE);
	else
		gs_auth_dialog_response_if_valid (self, account);
}

static void
gs_auth_dialog_account_removed_cb (GoaClient *client,
				   GoaObject *object,
				   GsAuthDialog *self)
{
	GoaAccount *account = goa_object_peek_account (object);
	gs_auth_dialog_remove_account (self, account);
}

static void
gs_auth_dialog_button_add_another_cb (GtkButton *button,
				      GsAuthDialog *self)
{
	gs_auth_dialog_spawn_goa_with_args ("add", gs_auth_get_provider_type (self->auth));
	self->dispose_on_new_account = TRUE;
}

static void
gs_auth_dialog_button_cancel_cb (GtkButton *button,
				 GsAuthDialog *self)
{
	gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_CANCEL);
}

static void
gs_auth_dialog_button_continue_cb (GtkButton *button,
				   GsAuthDialog *self)
{
	GoaAccount *account = NULL;
	gint naccounts = gs_auth_dialog_get_naccounts (self);

	if (naccounts == 1) {
		gs_auth_dialog_get_nth_account_data (self, 0, COLUMN_ACCOUNT, &account, -1);
	} else {
		gint active = gtk_combo_box_get_active (GTK_COMBO_BOX (self->combobox_account));
		gs_auth_dialog_get_nth_account_data (self, active, COLUMN_ACCOUNT, &account, -1);
	}

	if (account == NULL)
		gs_auth_dialog_button_add_another_cb (GTK_BUTTON (self->button_add_another), self);
	else
		gs_auth_dialog_response_if_valid (self, account);

	g_clear_object (&account);
}

/* GObject */

static void
gs_auth_dialog_dispose (GObject *object)
{
	GsAuthDialog *self = GS_AUTH_DIALOG (object);

	g_clear_object (&self->goa_client);

	g_cancellable_cancel (self->cancellable);
	g_clear_object (&self->cancellable);

	G_OBJECT_CLASS (gs_auth_dialog_parent_class)->dispose (object);
}

static void
gs_auth_dialog_class_init (GsAuthDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_auth_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-auth-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, label_header);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, combobox_account);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, label_account);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, button_add_another);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, button_cancel);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, button_continue);
	gtk_widget_class_bind_template_child (widget_class, GsAuthDialog, liststore_account);


	gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), gs_auth_dialog_button_add_another_cb);
	gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), gs_auth_dialog_button_cancel_cb);
	gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), gs_auth_dialog_button_continue_cb);
}

static void
gs_auth_dialog_init (GsAuthDialog *self)
{
	self->cancellable = g_cancellable_new ();

	gtk_widget_init_template (GTK_WIDGET (self));
	gtk_widget_grab_focus (self->button_continue);
}

/* GInitable */

static gboolean
gs_auth_dialog_initable_init (GInitable *initable,
			      GCancellable *cancellable,
			      GError  **error)
{
	GsAuthDialog *self;

	g_return_val_if_fail (GS_IS_AUTH_DIALOG (initable), FALSE);

	self = GS_AUTH_DIALOG (initable);

	self->goa_client = goa_client_new_sync (NULL, error);
	if (!self->goa_client)
		return FALSE;

	/* Be ready to other accounts */
	g_signal_connect (self->goa_client, "account-added", G_CALLBACK (gs_auth_dialog_account_added_cb), self);
	g_signal_connect (self->goa_client, "account-removed", G_CALLBACK (gs_auth_dialog_account_removed_cb), self);

	return TRUE;
}

static void
gs_auth_dialog_initable_iface_init (GInitableIface *iface)
{
	iface->init = gs_auth_dialog_initable_init;
}

/* Public API */

GtkWidget *
gs_auth_dialog_new (GsPluginLoader *plugin_loader,
		    GsApp *app,
		    const gchar *auth_id,
		    GError **error)
{
	GsAuthDialog *dialog;
	GsAuth *auth;

	/* get the authentication provider */
	if (auth_id == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "no auth-provider given for %s",
			     app != NULL ? gs_app_get_id (app) : NULL);
		return NULL;
	}
	auth = gs_plugin_loader_get_auth_by_id (plugin_loader, auth_id);
	if (auth == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no auth-provider %s for %s",
			     auth_id,
			     app != NULL ? gs_app_get_id (app) : NULL);
		return NULL;
	}

	/* create dialog */
	dialog = g_initable_new (GS_TYPE_AUTH_DIALOG,
				 NULL, error,
				 "use-header-bar", FALSE,
				 NULL);

	if (dialog == NULL)
		return NULL;

	dialog->plugin_loader = g_object_ref (plugin_loader);
	dialog->app = app != NULL ? g_object_ref (app) : NULL;
	dialog->auth = g_object_ref (auth);

	gs_auth_dialog_setup_model (dialog);
	gs_auth_dialog_check_ui (dialog, FALSE);

	return GTK_WIDGET (dialog);
}
