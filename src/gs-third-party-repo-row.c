/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
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

#include "gs-third-party-repo-row.h"

typedef struct
{
	GtkWidget	*active_switch;
	GtkWidget	*name_label;
	GtkWidget	*comment_label;
} GsThirdPartyRepoRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsThirdPartyRepoRow, gs_third_party_repo_row, GTK_TYPE_LIST_BOX_ROW)

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

GtkWidget *
gs_third_party_repo_row_get_switch (GsThirdPartyRepoRow *row)
{
	GsThirdPartyRepoRowPrivate *priv = gs_third_party_repo_row_get_instance_private (row);
	return priv->active_switch;
}

static void
gs_third_party_repo_row_init (GsThirdPartyRepoRow *row)
{
	gtk_widget_init_template (GTK_WIDGET (row));
}

static void
gs_third_party_repo_row_class_init (GsThirdPartyRepoRowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-third-party-repo-row.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsThirdPartyRepoRow, active_switch);
	gtk_widget_class_bind_template_child_private (widget_class, GsThirdPartyRepoRow, name_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsThirdPartyRepoRow, comment_label);
}

GtkWidget *
gs_third_party_repo_row_new (void)
{
	return g_object_new (GS_TYPE_THIRD_PARTY_REPO_ROW, NULL);
}

/* vim: set noexpandtab: */
