/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <adwaita.h>
#include <glib/gi18n.h>

#include "gs-common.h"
#include "gs-dkms-private.h"

#include "gs-dkms-dialog.h"

#define PASSWORD_LEN 4

struct _GsDkmsDialog
{
	AdwDialog parent_instance;

	AdwNavigationView *navigation_view;
	GtkLabel *password_label;
	GtkWidget *apply_button;

	GsApp *app;
	GCancellable *cancellable;

	gchar password[PASSWORD_LEN + 1];
};

typedef enum {
	PROP_APP = 1,
} GsDkmsDialogProperty;

static GParamSpec *obj_props[PROP_APP + 1] = { NULL, };

G_DEFINE_TYPE (GsDkmsDialog, gs_dkms_dialog, ADW_TYPE_DIALOG)

static void
gs_dkms_dialog_cancel_button_clicked_cb (GtkWidget *button,
					 GsDkmsDialog *self)
{
	if (self->cancellable != NULL)
		g_cancellable_cancel (self->cancellable);

	adw_dialog_force_close (ADW_DIALOG (self));
}

static void
gs_dkms_dialog_prepare_reboot_cb (GObject *source_object,
				  GAsyncResult *result,
				  gpointer user_data)
{
	g_autoptr(GError) local_error = NULL;

	if (!g_task_propagate_boolean (G_TASK (result), &local_error)) {
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			return;
		g_debug ("dkms-dialog: Failed to prepare reboot: %s", local_error->message);
		/* The code 126 is returned when the admin/root password prompt is dismissed */
		if (!g_error_matches (local_error, G_SPAWN_EXIT_ERROR, 126)) {
			gs_utils_show_error_dialog (GTK_WIDGET (source_object),
						    _("Failed to prepare reboot"),
						    "",
						    local_error->message);
		}
	} else {
		gs_utils_invoke_reboot_async (NULL, NULL, NULL);
		adw_dialog_force_close (ADW_DIALOG (source_object));
	}
}

static void
gs_dkms_dialog_enrolled_cb (GObject *source_object,
			    GAsyncResult *result,
			    gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(GError) local_error = NULL;
	GsDkmsState state;

	state = gs_dkms_enroll_finish (result, &local_error);

	if (local_error == NULL) {
		/* needs an explicit GError, no tell the user what failed and to not continue with the reboot */
		switch (state) {
		default:
		case GS_DKMS_STATE_ERROR:
			g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED, _("Unknown error"));
			break;
		case GS_DKMS_STATE_ENROLLED:
		case GS_DKMS_STATE_PENDING:
			break;
		case GS_DKMS_STATE_NOT_FOUND:
			g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED, _("The key was not found"));
			break;
		case GS_DKMS_STATE_NOT_ENROLLED:
			g_set_error_literal (&local_error, G_IO_ERROR, G_IO_ERROR_FAILED, _("The key is not enrolled"));
			break;
		}
	}

	if (local_error != NULL)
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_boolean (task, TRUE);
}

static void
gs_dkms_dialog_apply_button_clicked_cb (GtkWidget *button,
					GsDkmsDialog *self)
{
	g_autoptr(GTask) task = NULL;
	GsDkmsKeyKind key_kind;

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_clear_object (&self->cancellable);
	}

	self->cancellable = g_cancellable_new ();

	task = g_task_new (self, self->cancellable, gs_dkms_dialog_prepare_reboot_cb, NULL);
	g_task_set_source_tag (task, gs_dkms_dialog_apply_button_clicked_cb);
	if (g_strcmp0 (gs_app_get_metadata_item (self->app, "GnomeSoftware::requires-dkms-key"), "True") == 0)
		key_kind = GS_DKMS_KEY_KIND_DKMS;
	else if (g_strcmp0 (gs_app_get_metadata_item (self->app, "GnomeSoftware::requires-akmods-key"), "True") == 0)
		key_kind = GS_DKMS_KEY_KIND_AKMODS;
	else
		g_assert_not_reached ();

	gs_dkms_enroll_async (key_kind, self->password, self->cancellable, gs_dkms_dialog_enrolled_cb, g_steal_pointer (&task));
}

static void
gs_dkms_dialog_next_button_clicked_cb (GtkWidget *button,
				       GsDkmsDialog *self)
{
	adw_navigation_view_push_by_tag (self->navigation_view, "final-page");
}

static void
gs_dkms_dialog_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	GsDkmsDialog *self = GS_DKMS_DIALOG (object);

	switch ((GsDkmsDialogProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, self->app);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_dkms_dialog_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
	GsDkmsDialog *self = GS_DKMS_DIALOG (object);

	switch ((GsDkmsDialogProperty) prop_id) {
	case PROP_APP:
		g_assert (self->app == NULL);
		self->app = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_dkms_dialog_constructed (GObject *object)
{
	GsDkmsDialog *self = GS_DKMS_DIALOG (object);

	G_OBJECT_CLASS (gs_dkms_dialog_parent_class)->constructed (object);

	gtk_label_set_label (self->password_label, self->password);
}

static void
gs_dkms_dialog_dispose (GObject *object)
{
	GsDkmsDialog *self = GS_DKMS_DIALOG (object);

	if (self->cancellable) {
		g_cancellable_cancel (self->cancellable);
		g_clear_object (&self->cancellable);
	}

	g_clear_object (&self->app);

	G_OBJECT_CLASS (gs_dkms_dialog_parent_class)->dispose (object);
}

static void
gs_dkms_dialog_class_init (GsDkmsDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_dkms_dialog_get_property;
	object_class->set_property = gs_dkms_dialog_set_property;
	object_class->constructed = gs_dkms_dialog_constructed;
	object_class->dispose = gs_dkms_dialog_dispose;

	/*
	 * GsDkmsDialog:app: (nullable)
	 *
	 * The app to display the dialog for.
	 *
	 * This may be %NULL; if so, the content of the widget will be
	 * undefined.
	 *
	 * Since: 47
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-dkms-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsDkmsDialog, navigation_view);
	gtk_widget_class_bind_template_child (widget_class, GsDkmsDialog, password_label);
	gtk_widget_class_bind_template_child (widget_class, GsDkmsDialog, apply_button);
	gtk_widget_class_bind_template_callback (widget_class, gs_dkms_dialog_cancel_button_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_dkms_dialog_apply_button_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_dkms_dialog_next_button_clicked_cb);
}

static void
gs_dkms_dialog_init (GsDkmsDialog *self)
{
	g_autoptr(GRand) rand = NULL;

	gtk_widget_init_template (GTK_WIDGET (self));

	/* hide leftover notification, if any */
	g_application_withdraw_notification (g_application_get_default (), "dkms-key-pending");

	rand = g_rand_new_with_seed ((guint32) g_get_real_time ());
	for (guint i = 0; i < PASSWORD_LEN; i++) {
		self->password[i] = '0' + g_rand_int_range (rand, 1, 10);
	}

	self->password[PASSWORD_LEN] = '\0';
}

void
gs_dkms_dialog_run (GtkWidget *parent,
		    GsApp *app)
{
	GsDkmsDialog *self;

	self = g_object_new (GS_TYPE_DKMS_DIALOG,
			     "app", app,
			     NULL);
	adw_dialog_present (ADW_DIALOG (self), parent);
}
