/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "gs-packagekit-task.h"

typedef struct _GsPackageKitTaskPrivate {
	GWeakRef plugin_weakref; /* GsPlugin * */
	GsPluginAction action;
} GsPackageKitTaskPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsPackageKitTask, gs_packagekit_task, PK_TYPE_TASK)

static void
do_not_expand (GtkWidget *child,
	       gpointer data)
{
	gtk_container_child_set (GTK_CONTAINER (gtk_widget_get_parent (child)),
				 child, "expand", FALSE, "fill", FALSE, NULL);
}

static gboolean
unset_focus (GtkWidget *widget,
	     GdkEvent *event,
	     gpointer data)
{
	if (GTK_IS_WINDOW (widget))
		gtk_window_set_focus (GTK_WINDOW (widget), NULL);
	return FALSE;
}

static void
insert_details_widget (GtkMessageDialog *dialog,
		       const gchar *details)
{
	GtkWidget *message_area, *sw, *label;
	GtkWidget *box, *tv;
	GtkTextBuffer *buffer;
	GList *children;
	GtkStyleContext *style_context;
	g_autoptr(GError) error = NULL;
	g_autoptr(GtkCssProvider) css_provider = NULL;
	PangoAttrList *attr_list;

	g_assert (GTK_IS_MESSAGE_DIALOG (dialog));
	g_assert (details != NULL);

	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

	message_area = gtk_message_dialog_get_message_area (dialog);
	g_assert (GTK_IS_BOX (message_area));
	/* make the hbox expand */
	box = gtk_widget_get_parent (message_area);
	gtk_container_child_set (GTK_CONTAINER (gtk_widget_get_parent (box)), box,
	                         "expand", TRUE, "fill", TRUE, NULL);
	/* make the labels not expand */
	gtk_container_foreach (GTK_CONTAINER (message_area), do_not_expand, NULL);

	/* Find the secondary label and set its width_chars.   */
	/* Otherwise the label will tend to expand vertically. */
	children = gtk_container_get_children (GTK_CONTAINER (message_area));
	if (children && children->next && GTK_IS_LABEL (children->next->data)) {
		gtk_label_set_width_chars (GTK_LABEL (children->next->data), 40);
	}

	label = gtk_label_new (_("Details"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_visible (label, TRUE);
	attr_list = pango_attr_list_new ();
	pango_attr_list_insert (attr_list, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes (GTK_LABEL (label), attr_list);
	pango_attr_list_unref (attr_list);
	gtk_container_add (GTK_CONTAINER (message_area), label);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
	                                     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
	                                GTK_POLICY_NEVER,
	                                GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sw), 150);
	gtk_widget_set_visible (sw, TRUE);

	tv = gtk_text_view_new ();
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv));
	gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD);
	gtk_style_context_add_class (gtk_widget_get_style_context (tv),
	                             "gs-packagekit-question-details");
	style_context = gtk_widget_get_style_context (tv);
	css_provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (css_provider,
		".gs-packagekit-question-details {\n"
		"	font-family: Monospace;\n"
		"	font-size: smaller;\n"
		"	padding: 16px;\n"
		"}", -1, &error);
	if (error)
		g_warning ("GsPackageKitTask: Failed to parse CSS: %s", error->message);
	gtk_style_context_add_provider (style_context, GTK_STYLE_PROVIDER (css_provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	gtk_text_buffer_set_text (buffer, details, -1);
	gtk_widget_set_visible (tv, TRUE);

	gtk_container_add (GTK_CONTAINER (sw), tv);
	gtk_widget_set_vexpand (sw, TRUE);
	gtk_container_add (GTK_CONTAINER (message_area), sw);
	gtk_container_child_set (GTK_CONTAINER (message_area), sw, "pack-type", GTK_PACK_END, NULL);

	g_signal_connect (dialog, "map-event", G_CALLBACK (unset_focus), NULL);
}

static gboolean
gs_packagekit_task_user_accepted (const gchar *title,
				  const gchar *msg,
				  const gchar *details,
				  const gchar *ok_label)
{
	GtkWidget *dialog;
	GtkWindow *parent = NULL;
	GApplication *application = g_application_get_default ();
	gint response;

	if (application && GTK_IS_APPLICATION (application))
		parent = gtk_application_get_active_window (GTK_APPLICATION (application));

	dialog = gtk_message_dialog_new_with_markup (parent,
	                                             GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
	                                             GTK_MESSAGE_QUESTION,
	                                             GTK_BUTTONS_NONE,
	                                             "<big><b>%s</b></big>", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
	                                          "%s", msg);
	if (details != NULL)
		insert_details_widget (GTK_MESSAGE_DIALOG (dialog), details);
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (dialog), ok_label, GTK_RESPONSE_OK);

	response = gtk_dialog_run (GTK_DIALOG (dialog));

	gtk_widget_destroy (dialog);

	return response == GTK_RESPONSE_OK;
}

typedef struct _QuestionData {
	GWeakRef task_weakref;
	guint request;
	gchar *title;
	gchar *msg;
	gchar *details;
	gchar *ok_label;
} QuestionData;

static QuestionData *
question_data_new (GsPackageKitTask *task,
		   guint request,
		   const gchar *title,
		   const gchar *msg,
		   const gchar *details,
		   const gchar *ok_label)
{
	QuestionData *qd;

	qd = g_slice_new0 (QuestionData);
	g_weak_ref_init (&qd->task_weakref, task);
	qd->request = request;
	qd->title = g_strdup (title);
	qd->msg = g_strdup (msg);
	qd->details = g_strdup (details);
	qd->ok_label = g_strdup (ok_label);

	return qd;
}

static void
question_data_free (gpointer ptr)
{
	QuestionData *qd = ptr;

	if (qd) {
		g_weak_ref_clear (&qd->task_weakref);
		g_free (qd->title);
		g_free (qd->msg);
		g_free (qd->details);
		g_free (qd->ok_label);
		g_slice_free (QuestionData, qd);
	}
}

static gboolean
gs_packagekit_task_question_idle_cb (gpointer user_data)
{
	QuestionData *qd = user_data;
	PkTask *task;

	task = g_weak_ref_get (&qd->task_weakref);
	if (task) {
		if (gs_packagekit_task_user_accepted (qd->title, qd->msg, qd->details, qd->ok_label))
			pk_task_user_accepted (task, qd->request);
		else
			pk_task_user_declined (task, qd->request);
		g_object_unref (task);
	}

	return G_SOURCE_REMOVE;
}

static void
gs_packagekit_task_schedule_question (GsPackageKitTask *task,
				      guint request,
				      const gchar *title,
				      const gchar *msg,
				      const gchar *details,
				      const gchar *ok_label)
{
	QuestionData *qd;

	qd = question_data_new (task, request, title, msg, details, ok_label);
	g_idle_add_full (G_PRIORITY_HIGH_IDLE, gs_packagekit_task_question_idle_cb, qd, question_data_free);
}

static void
gs_packagekit_task_untrusted_question (PkTask *task,
				       guint request,
				       PkResults *results)
{
	GsPackageKitTask *gs_task = GS_PACKAGEKIT_TASK (task);
	GsPackageKitTaskPrivate *priv = gs_packagekit_task_get_instance_private (gs_task);
	g_autoptr(PkError) error = NULL;
	const gchar *title;
	const gchar *msg;
	const gchar *details;
	const gchar *ok_label;

	switch (priv->action) {
	case GS_PLUGIN_ACTION_INSTALL:
		title = _("Install Unsigned Software?");
		msg = _("Software that is to be installed is not signed. It will not be possible to verify the origin of updates to this software, or whether updates have been tampered with.");
		ok_label = _("_Install");
		break;
	case GS_PLUGIN_ACTION_DOWNLOAD:
	case GS_PLUGIN_ACTION_UPDATE:
		title = _("Update Unsigned Software?");
		msg = _("Unsigned updates are available. Without a signature, it is not possible to verify the origin of the update, or whether it has been tampered with. Software updates will be disabled until unsigned updates are either removed or updated.");
		ok_label = _("_Update");
		break;
	default:
		pk_task_user_declined (task, request);
		return;
	}

	error = pk_results_get_error_code (results);
	if (error)
		details = pk_error_get_details (error);
	else
		details = NULL;

	gs_packagekit_task_schedule_question (gs_task, request, title, msg, details, ok_label);
}

static void
gs_packagekit_task_finalize (GObject *object)
{
	GsPackageKitTask *task = GS_PACKAGEKIT_TASK (object);
	GsPackageKitTaskPrivate *priv = gs_packagekit_task_get_instance_private (task);

	g_weak_ref_clear (&priv->plugin_weakref);

	G_OBJECT_CLASS (gs_packagekit_task_parent_class)->finalize (object);
}

static void
gs_packagekit_task_class_init (GsPackageKitTaskClass *klass)
{
	GObjectClass *object_class;
	PkTaskClass *task_class;

	task_class = PK_TASK_CLASS (klass);
	task_class->untrusted_question = gs_packagekit_task_untrusted_question;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_packagekit_task_finalize;
}

static void
gs_packagekit_task_init (GsPackageKitTask *task)
{
	GsPackageKitTaskPrivate *priv = gs_packagekit_task_get_instance_private (task);

	g_weak_ref_init (&priv->plugin_weakref, NULL);
}

PkTask *
gs_packagekit_task_new (GsPlugin *plugin)
{
	GsPackageKitTask *task;
	GsPackageKitTaskPrivate *priv;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), NULL);

	task = g_object_new (GS_TYPE_PACKAGEKIT_TASK, NULL);
	priv = gs_packagekit_task_get_instance_private (task);

	g_weak_ref_set (&priv->plugin_weakref, plugin);

	return PK_TASK (task);
}

void
gs_packagekit_task_setup (GsPackageKitTask *task,
			  GsPluginAction action,
			  gboolean interactive)
{
	GsPackageKitTaskPrivate *priv = gs_packagekit_task_get_instance_private (task);

	g_return_if_fail (GS_IS_PACKAGEKIT_TASK (task));

	priv->action = action;
	pk_client_set_interactive (PK_CLIENT (task), interactive);
}

GsPluginAction
gs_packagekit_task_get_action (GsPackageKitTask *task)
{
	GsPackageKitTaskPrivate *priv = gs_packagekit_task_get_instance_private (task);

	g_return_val_if_fail (GS_IS_PACKAGEKIT_TASK (task), GS_PLUGIN_ACTION_UNKNOWN);

	return priv->action;
}
