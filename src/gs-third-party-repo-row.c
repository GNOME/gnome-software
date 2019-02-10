/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-third-party-repo-row.h"

#include "gs-progress-button.h"
#include <glib/gi18n.h>

typedef struct
{
	GsApp		*app;

	GtkWidget	*button;
	GtkWidget	*comment_label;
	GtkWidget	*name_label;
	guint		 refresh_idle_id;
} GsThirdPartyRepoRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsThirdPartyRepoRow, gs_third_party_repo_row, GTK_TYPE_LIST_BOX_ROW)

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

void
gs_third_party_repo_row_set_name (GsThirdPartyRepoRow *row, const gchar *name)
{
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);
	gtk_label_set_text (GTK_LABEL (priv->name_label), name);
}

void
gs_third_party_repo_row_set_comment (GsThirdPartyRepoRow *row, const gchar *comment)
{
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);
	gtk_label_set_markup (GTK_LABEL (priv->comment_label), comment);
}

static void
refresh_ui (GsThirdPartyRepoRow *row)
{
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);
	GtkStyleContext *context;

	if (priv->app == NULL)
		return;

	/* do a fill bar for the current progress */
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_INSTALLING:
		gs_progress_button_set_progress (GS_PROGRESS_BUTTON (priv->button),
		                                 gs_app_get_progress (priv->app));
		gs_progress_button_set_show_progress (GS_PROGRESS_BUTTON (priv->button), TRUE);
		break;
	default:
		gs_progress_button_set_show_progress (GS_PROGRESS_BUTTON (priv->button), FALSE);
		break;
	}

	/* set button text */
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_UNAVAILABLE:
		/* TRANSLATORS: this is a button in the software repositories
		   dialog for installing a repo.
		   The ellipsis indicates that further steps are required */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("_Install…"));
		/* enable button */
		gtk_widget_set_sensitive (priv->button, TRUE);
		break;
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
		/* TRANSLATORS: this is a button in the software repositories
		   dialog for installing a repo */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("_Install"));
		/* enable button */
		gtk_widget_set_sensitive (priv->button, TRUE);
		break;
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		/* TRANSLATORS: this is a button in the software repositories
		   dialog for removing multiple repos */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("_Remove All"));
		/* enable button */
		gtk_widget_set_sensitive (priv->button, TRUE);
		break;
	case AS_APP_STATE_INSTALLING:
		/* TRANSLATORS: this is a button in the software repositories dialog
		   that shows the status of a repo being installed */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Installing"));
		/* disable button */
		gtk_widget_set_sensitive (priv->button, FALSE);
		break;
	case AS_APP_STATE_REMOVING:
		/* TRANSLATORS: this is a button in the software repositories dialog
		   that shows the status of a repo being removed */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Removing"));
		/* disable button */
		gtk_widget_set_sensitive (priv->button, FALSE);
		break;
	default:
		break;
	}

	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (priv->button, FALSE);
		break;
	default:
		gtk_widget_set_visible (priv->button, TRUE);
		break;
	}

	context = gtk_widget_get_style_context (priv->button);
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		gtk_style_context_add_class (context, "destructive-action");
		break;
	default:
		gtk_style_context_remove_class (context, "destructive-action");
		break;
	}
}

static gboolean
refresh_idle (gpointer user_data)
{
	g_autoptr(GsThirdPartyRepoRow) row = (GsThirdPartyRepoRow *) user_data;
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);

	refresh_ui (row);

	priv->refresh_idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void
app_state_changed_cb (GsApp *repo, GParamSpec *pspec, GsThirdPartyRepoRow *row)
{
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);

	if (priv->refresh_idle_id > 0)
		return;
	priv->refresh_idle_id = g_idle_add (refresh_idle, g_object_ref (row));
}

void
gs_third_party_repo_row_set_app (GsThirdPartyRepoRow *row, GsApp *app)
{
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);

	if (priv->app != NULL)
		g_signal_handlers_disconnect_by_func (priv->app, app_state_changed_cb, row);

	g_set_object (&priv->app, app);
	if (priv->app != NULL) {
		g_signal_connect_object (priv->app, "notify::state",
		                         G_CALLBACK (app_state_changed_cb),
		                         row, 0);
		g_signal_connect_object (priv->app, "notify::progress",
		                         G_CALLBACK (app_state_changed_cb),
		                         row, 0);
		refresh_ui (row);
	}
}

GsApp *
gs_third_party_repo_row_get_app (GsThirdPartyRepoRow *row)
{
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);
	return priv->app;
}

static void
button_clicked_cb (GtkWidget *widget, GsThirdPartyRepoRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0);
}

static void
gs_third_party_repo_row_destroy (GtkWidget *object)
{
	GsThirdPartyRepoRow *row = GS_THIRD_PARTY_REPO_ROW (object);
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);

	if (priv->app != NULL) {
		g_signal_handlers_disconnect_by_func (priv->app, app_state_changed_cb, row);
		g_clear_object (&priv->app);
	}

	if (priv->refresh_idle_id != 0) {
		g_source_remove (priv->refresh_idle_id);
		priv->refresh_idle_id = 0;
	}

	GTK_WIDGET_CLASS (gs_third_party_repo_row_parent_class)->destroy (object);
}

static void
gs_third_party_repo_row_init (GsThirdPartyRepoRow *row)
{
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);

	gtk_widget_init_template (GTK_WIDGET (row));
	g_signal_connect (priv->button, "clicked",
	                  G_CALLBACK (button_clicked_cb), row);
}

static void
gs_third_party_repo_row_class_init (GsThirdPartyRepoRowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	widget_class->destroy = gs_third_party_repo_row_destroy;

	signals [SIGNAL_BUTTON_CLICKED] =
		g_signal_new ("button-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GsThirdPartyRepoRowClass, button_clicked),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-third-party-repo-row.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsThirdPartyRepoRow, button);
	gtk_widget_class_bind_template_child_private (widget_class, GsThirdPartyRepoRow, comment_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsThirdPartyRepoRow, name_label);
}

GtkWidget *
gs_third_party_repo_row_new (void)
{
	return g_object_new (GS_TYPE_THIRD_PARTY_REPO_ROW, NULL);
}
