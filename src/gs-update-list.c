/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-update-list.h"

#include "gs-app-row.h"
#include "gs-common.h"

typedef struct
{
	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	GtkSizeGroup		*sizegroup_desc;
} GsUpdateListPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsUpdateList, gs_update_list, GTK_TYPE_LIST_BOX)

static void
gs_update_list_app_state_notify_cb (GsApp *app, GParamSpec *pspec, gpointer user_data)
{
	if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED) {
		GsAppRow *app_row = GS_APP_ROW (user_data);
		gs_app_row_unreveal (app_row);
	}
}

void
gs_update_list_add_app (GsUpdateList *update_list, GsApp *app)
{
	GsUpdateListPrivate *priv = gs_update_list_get_instance_private (update_list);
	GtkWidget *app_row;

	app_row = gs_app_row_new (app);
	gs_app_row_set_show_update (GS_APP_ROW (app_row), FALSE);
	gs_app_row_set_show_buttons (GS_APP_ROW (app_row), FALSE);
	gtk_container_add (GTK_CONTAINER (update_list), app_row);
	gs_app_row_set_size_groups (GS_APP_ROW (app_row),
				    priv->sizegroup_image,
				    priv->sizegroup_name,
				    priv->sizegroup_desc,
				    NULL);
	g_signal_connect_object (app, "notify::state",
	                         G_CALLBACK (gs_update_list_app_state_notify_cb),
	                         app_row, 0);
	gtk_widget_show (app_row);
}

static void
list_header_func (GtkListBoxRow *row,
		  GtkListBoxRow *before,
		  gpointer user_data)
{
	GtkWidget *header;
	header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

static gint
list_sort_func (GtkListBoxRow *a,
		GtkListBoxRow *b,
		gpointer user_data)
{
	GsApp *a1 = gs_app_row_get_app (GS_APP_ROW (a));
	GsApp *b1 = gs_app_row_get_app (GS_APP_ROW (b));
	return g_strcmp0 (gs_app_get_name (a1), gs_app_get_name (b1));
}

static void
gs_update_list_dispose (GObject *object)
{
	GsUpdateList *update_list = GS_UPDATE_LIST (object);
	GsUpdateListPrivate *priv = gs_update_list_get_instance_private (update_list);

	g_clear_object (&priv->sizegroup_image);
	g_clear_object (&priv->sizegroup_name);
	g_clear_object (&priv->sizegroup_desc);

	G_OBJECT_CLASS (gs_update_list_parent_class)->dispose (object);
}

static void
gs_update_list_init (GsUpdateList *update_list)
{
	GsUpdateListPrivate *priv = gs_update_list_get_instance_private (update_list);
	priv->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->sizegroup_desc = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_list_box_set_header_func (GTK_LIST_BOX (update_list),
				      list_header_func,
				      update_list, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (update_list),
				    list_sort_func,
				    update_list, NULL);
}

static void
gs_update_list_class_init (GsUpdateListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_update_list_dispose;
}

GtkWidget *
gs_update_list_new (void)
{
	GsUpdateList *update_list;
	update_list = g_object_new (GS_TYPE_UPDATE_LIST, NULL);
	return GTK_WIDGET (update_list);
}
