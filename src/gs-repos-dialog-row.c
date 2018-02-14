/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
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

#include "gs-repos-dialog-row.h"

#include <glib/gi18n.h>

typedef struct
{
	GsApp		*repo;
	GtkWidget	*active_switch;
	GtkWidget	*button;
	GtkWidget	*name_label;
	GtkWidget	*comment_label;
	GtkWidget	*details_revealer;
	GtkWidget	*status_label;
	GtkWidget	*url_title_label;
	GtkWidget	*url_value_label;
	guint		 refresh_idle_id;
} GsReposDialogRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsReposDialogRow, gs_repos_dialog_row, GTK_TYPE_LIST_BOX_ROW)

enum {
	PROP_0,
	PROP_SWITCH_ACTIVE,
	PROP_LAST
};

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

void
gs_repos_dialog_row_set_switch_enabled (GsReposDialogRow *row,
                                        gboolean switch_enabled)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);
	gtk_widget_set_visible (priv->active_switch, switch_enabled);
}

void
gs_repos_dialog_row_set_switch_active (GsReposDialogRow *row,
                                       gboolean switch_active)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);
	gtk_switch_set_active (GTK_SWITCH (priv->active_switch), switch_active);
}

void
gs_repos_dialog_row_set_name (GsReposDialogRow *row, const gchar *name)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	gtk_label_set_text (GTK_LABEL (priv->name_label), name);
	gtk_widget_set_visible (priv->name_label, name != NULL);
}

void
gs_repos_dialog_row_set_comment (GsReposDialogRow *row, const gchar *comment)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	gtk_label_set_markup (GTK_LABEL (priv->comment_label), comment);
	gtk_widget_set_visible (priv->comment_label, comment != NULL);
}

void
gs_repos_dialog_row_set_url (GsReposDialogRow *row, const gchar *url)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	gtk_label_set_text (GTK_LABEL (priv->url_value_label), url);
	gtk_widget_set_visible (priv->url_value_label, url != NULL);
	gtk_widget_set_visible (priv->url_title_label, url != NULL);
}

static gboolean
repo_supports_removal (GsApp *repo)
{
	const gchar *management_plugin = gs_app_get_management_plugin (repo);

	/* can't remove a repo, only enable/disable existing ones */
	if (g_strcmp0 (management_plugin, "fwupd") == 0 ||
	    g_strcmp0 (management_plugin, "packagekit") == 0)
		return FALSE;

	return TRUE;
}

static void
refresh_ui (GsReposDialogRow *row)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

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
	g_autoptr(GsReposDialogRow) row = (GsReposDialogRow *) user_data;
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	refresh_ui (row);

	priv->refresh_idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void
repo_state_changed_cb (GsApp *repo, GParamSpec *pspec, GsReposDialogRow *row)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	if (priv->refresh_idle_id > 0)
		return;
	priv->refresh_idle_id = g_idle_add (refresh_idle, g_object_ref (row));
}

void
gs_repos_dialog_row_set_repo (GsReposDialogRow *row, GsApp *repo)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	g_assert (priv->repo == NULL);

	priv->repo = g_object_ref (repo);
	g_signal_connect_object (priv->repo, "notify::state",
	                         G_CALLBACK (repo_state_changed_cb),
	                         row, 0);
	refresh_ui (row);
}

GsApp *
gs_repos_dialog_row_get_repo (GsReposDialogRow *row)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);
	return priv->repo;
}

void
gs_repos_dialog_row_show_details (GsReposDialogRow *row)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
	gtk_revealer_set_reveal_child (GTK_REVEALER (priv->details_revealer), TRUE);
}

void
gs_repos_dialog_row_hide_details (GsReposDialogRow *row)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
	gtk_revealer_set_reveal_child (GTK_REVEALER (priv->details_revealer), FALSE);
}

void
gs_repos_dialog_row_show_status (GsReposDialogRow *row)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);
	gtk_widget_set_visible (priv->status_label, TRUE);
}

static void
button_clicked_cb (GtkWidget *widget, GsReposDialogRow *row)
{
	g_signal_emit (row, signals[SIGNAL_BUTTON_CLICKED], 0);
}

static void
gs_repos_dialog_switch_active_cb (GtkSwitch *active_switch,
                                  GParamSpec *pspec,
                                  GsReposDialogRow *row)
{
	g_object_notify (G_OBJECT (row), "switch-active");
}

gboolean
gs_repos_dialog_row_get_switch_active (GsReposDialogRow *row)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	return gtk_switch_get_active (GTK_SWITCH (priv->active_switch));
}

static void
gs_repos_dialog_row_get_property (GObject *object,
                                  guint prop_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
	GsReposDialogRow *row = GS_REPOS_DIALOG_ROW (object);
	switch (prop_id) {
	case PROP_SWITCH_ACTIVE:
		g_value_set_boolean (value,
				     gs_repos_dialog_row_get_switch_active (row));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_repos_dialog_row_destroy (GtkWidget *object)
{
	GsReposDialogRow *row = GS_REPOS_DIALOG_ROW (object);
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	if (priv->repo != NULL) {
		g_signal_handlers_disconnect_by_func (priv->repo, repo_state_changed_cb, row);
		g_clear_object (&priv->repo);
	}

	if (priv->refresh_idle_id != 0) {
		g_source_remove (priv->refresh_idle_id);
		priv->refresh_idle_id = 0;
	}

	GTK_WIDGET_CLASS (gs_repos_dialog_row_parent_class)->destroy (object);
}

static void
gs_repos_dialog_row_init (GsReposDialogRow *row)
{
	GsReposDialogRowPrivate *priv = gs_repos_dialog_row_get_instance_private (row);

	gtk_widget_init_template (GTK_WIDGET (row));
	g_signal_connect (priv->active_switch, "notify::active",
			  G_CALLBACK (gs_repos_dialog_switch_active_cb), row);
	g_signal_connect (priv->button, "clicked",
	                  G_CALLBACK (button_clicked_cb), row);
}

static void
gs_repos_dialog_row_class_init (GsReposDialogRowClass *klass)
{
	GParamSpec *pspec;
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gs_repos_dialog_row_get_property;
	widget_class->destroy = gs_repos_dialog_row_destroy;

	pspec = g_param_spec_string ("switch-active", NULL, NULL, FALSE,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_SWITCH_ACTIVE, pspec);

	signals [SIGNAL_BUTTON_CLICKED] =
		g_signal_new ("button-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GsReposDialogRowClass, button_clicked),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-repos-dialog-row.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsReposDialogRow, active_switch);
	gtk_widget_class_bind_template_child_private (widget_class, GsReposDialogRow, button);
	gtk_widget_class_bind_template_child_private (widget_class, GsReposDialogRow, name_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsReposDialogRow, comment_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsReposDialogRow, details_revealer);
	gtk_widget_class_bind_template_child_private (widget_class, GsReposDialogRow, status_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsReposDialogRow, url_title_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsReposDialogRow, url_value_label);
}

GtkWidget *
gs_repos_dialog_row_new (void)
{
	return g_object_new (GS_TYPE_REPOS_DIALOG_ROW, NULL);
}

/* vim: set noexpandtab: */
