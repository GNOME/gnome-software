/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-repo-row.h"

#include <glib/gi18n.h>

typedef struct
{
	GsApp		*repo;
	GtkWidget	*button;
	GtkWidget	*name_label;
	GtkWidget	*comment_label;
	GtkWidget	*details_revealer;
	GtkWidget	*status_label;
	GtkWidget	*url_box;
	GtkWidget	*url_label;
	guint		 refresh_idle_id;
} GsRepoRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsRepoRow, gs_repo_row, GTK_TYPE_LIST_BOX_ROW)

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

void
gs_repo_row_set_name (GsRepoRow *row, const gchar *name)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	gtk_label_set_text (GTK_LABEL (priv->name_label), name);
	gtk_widget_set_visible (priv->name_label, name != NULL);
}

void
gs_repo_row_set_comment (GsRepoRow *row, const gchar *comment)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	gtk_label_set_markup (GTK_LABEL (priv->comment_label), comment);
	gtk_widget_set_visible (priv->comment_label, comment != NULL);
}

void
gs_repo_row_set_url (GsRepoRow *row, const gchar *url)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	gtk_label_set_text (GTK_LABEL (priv->url_label), url);
	gtk_widget_set_visible (priv->url_box, url != NULL);
}

static gboolean
repo_supports_removal (GsApp *repo)
{
	const gchar *management_plugin = gs_app_get_management_plugin (repo);

	/* can't remove a repo, only enable/disable existing ones */
	if (g_strcmp0 (management_plugin, "fwupd") == 0 ||
	    g_strcmp0 (management_plugin, "packagekit") == 0 ||
	    g_strcmp0 (management_plugin, "rpm-ostree") == 0)
		return FALSE;

	return TRUE;
}

static void
refresh_ui (GsRepoRow *row)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	if (priv->repo == NULL) {
		gtk_widget_set_visible (priv->button, FALSE);
		return;
	}

	gtk_widget_set_visible (priv->button, TRUE);

	/* set button text */
	switch (gs_app_get_state (priv->repo)) {
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
		/* TRANSLATORS: this is a button in the software repositories
		   dialog for enabling a repo */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("_Enable"));
		/* enable button */
		gtk_widget_set_sensitive (priv->button, TRUE);
		break;
	case AS_APP_STATE_INSTALLED:
		if (repo_supports_removal (priv->repo)) {
			/* TRANSLATORS: this is a button in the software repositories dialog
			   for removing a repo. The ellipsis indicates that further
			   steps are required */
			gtk_button_set_label (GTK_BUTTON (priv->button), _("_Remove…"));
		} else {
			/* TRANSLATORS: this is a button in the software repositories dialog
			   for disabling a repo. The ellipsis indicates that further
			   steps are required */
			gtk_button_set_label (GTK_BUTTON (priv->button), _("_Disable…"));
		}
		/* enable button */
		gtk_widget_set_sensitive (priv->button, TRUE);
		break;
	case AS_APP_STATE_INSTALLING:
		/* TRANSLATORS: this is a button in the software repositories dialog
		   that shows the status of a repo being enabled */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Enabling"));
		/* disable button */
		gtk_widget_set_sensitive (priv->button, FALSE);
		break;
	case AS_APP_STATE_REMOVING:
		if (repo_supports_removal (priv->repo)) {
			/* TRANSLATORS: this is a button in the software repositories dialog
			   that shows the status of a repo being removed */
			gtk_button_set_label (GTK_BUTTON (priv->button), _("Removing"));
		} else {
			/* TRANSLATORS: this is a button in the software repositories dialog
			   that shows the status of a repo being disabled */
			gtk_button_set_label (GTK_BUTTON (priv->button), _("Disabling"));
		}
		/* disable button */
		gtk_widget_set_sensitive (priv->button, FALSE);
		break;
	default:
		break;
	}

	/* set enabled/disabled label */
	switch (gs_app_get_state (priv->repo)) {
	case AS_APP_STATE_INSTALLED:
		/* TRANSLATORS: this is a label in the software repositories
		   dialog that indicates that a repo is enabled. */
		gtk_label_set_text (GTK_LABEL (priv->status_label), _("Enabled"));
		break;
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
		/* TRANSLATORS: this is a label in the software repositories
		   dialog that indicates that a repo is disabled. */
		gtk_label_set_text (GTK_LABEL (priv->status_label), _("Disabled"));
		break;
	default:
		break;
	}
}

static gboolean
refresh_idle (gpointer user_data)
{
	g_autoptr(GsRepoRow) row = (GsRepoRow *) user_data;
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	refresh_ui (row);

	priv->refresh_idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void
repo_state_changed_cb (GsApp *repo, GParamSpec *pspec, GsRepoRow *row)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	if (priv->refresh_idle_id > 0)
		return;
	priv->refresh_idle_id = g_idle_add (refresh_idle, g_object_ref (row));
}

void
gs_repo_row_set_repo (GsRepoRow *row, GsApp *repo)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	g_assert (priv->repo == NULL);

	priv->repo = g_object_ref (repo);
	g_signal_connect_object (priv->repo, "notify::state",
	                         G_CALLBACK (repo_state_changed_cb),
	                         row, 0);
	refresh_ui (row);
}

GsApp *
gs_repo_row_get_repo (GsRepoRow *row)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);
	return priv->repo;
}

void
gs_repo_row_show_details (GsRepoRow *row)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
	gtk_revealer_set_reveal_child (GTK_REVEALER (priv->details_revealer), TRUE);
}

void
gs_repo_row_hide_details (GsRepoRow *row)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
	gtk_revealer_set_reveal_child (GTK_REVEALER (priv->details_revealer), FALSE);
}

void
gs_repo_row_show_status (GsRepoRow *row)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);
	gtk_widget_set_visible (priv->status_label, TRUE);
}

static void
button_clicked_cb (GtkWidget *widget, GsRepoRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0);
}

static void
gs_repo_row_destroy (GtkWidget *object)
{
	GsRepoRow *row = GS_REPO_ROW (object);
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	if (priv->repo != NULL) {
		g_signal_handlers_disconnect_by_func (priv->repo, repo_state_changed_cb, row);
		g_clear_object (&priv->repo);
	}

	if (priv->refresh_idle_id != 0) {
		g_source_remove (priv->refresh_idle_id);
		priv->refresh_idle_id = 0;
	}

	GTK_WIDGET_CLASS (gs_repo_row_parent_class)->destroy (object);
}

static void
gs_repo_row_init (GsRepoRow *row)
{
	GsRepoRowPrivate *priv = gs_repo_row_get_instance_private (row);

	gtk_widget_init_template (GTK_WIDGET (row));
	g_signal_connect (priv->button, "clicked",
	                  G_CALLBACK (button_clicked_cb), row);
}

static void
gs_repo_row_class_init (GsRepoRowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	widget_class->destroy = gs_repo_row_destroy;

	signals [SIGNAL_BUTTON_CLICKED] =
		g_signal_new ("button-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GsRepoRowClass, button_clicked),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-repo-row.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsRepoRow, button);
	gtk_widget_class_bind_template_child_private (widget_class, GsRepoRow, name_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsRepoRow, comment_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsRepoRow, details_revealer);
	gtk_widget_class_bind_template_child_private (widget_class, GsRepoRow, status_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsRepoRow, url_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsRepoRow, url_label);
}

GtkWidget *
gs_repo_row_new (void)
{
	return g_object_new (GS_TYPE_REPO_ROW, NULL);
}
