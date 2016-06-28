/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <appstream-glib.h>

#include "gs-update-list.h"

#include "gs-app.h"
#include "gs-app-row.h"

typedef struct
{
	GtkSizeGroup	*sizegroup_image;
	GtkSizeGroup	*sizegroup_name;
} GsUpdateListPrivate;

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (GsUpdateList, gs_update_list, GTK_TYPE_LIST_BOX)

#define GET_PRIV(o)	gs_update_list_get_instance_private(o)

static void
gs_update_list_button_clicked_cb (GsAppRow *app_row,
				  GsUpdateList *update_list)
{
	GsApp *app = gs_app_row_get_app (app_row);
	g_signal_emit (update_list, signals[SIGNAL_BUTTON_CLICKED], 0, app);
}

void
gs_update_list_add_app (GsUpdateList *update_list,
			GsApp	*app)
{
	GsUpdateListPrivate *priv = GET_PRIV (update_list);
	GtkWidget *app_row;

	app_row = gs_app_row_new (app);
	gs_app_row_set_show_update (GS_APP_ROW (app_row), TRUE);
	g_signal_connect (app_row, "button-clicked",
			  G_CALLBACK (gs_update_list_button_clicked_cb),
			  update_list);
	gtk_container_add (GTK_CONTAINER (update_list), app_row);
	gs_app_row_set_size_groups (GS_APP_ROW (app_row),
				    priv->sizegroup_image,
				    priv->sizegroup_name);
	gtk_widget_show (app_row);
}

GsAppList *
gs_update_list_get_apps (GsUpdateList *update_list)
{
	GsAppList *apps;
	GList *l;
	g_autoptr(GList) children = NULL;

	apps = gs_app_list_new ();
	children = gtk_container_get_children (GTK_CONTAINER (update_list));
	for (l = children; l != NULL; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		gs_app_list_add (apps, gs_app_row_get_app (app_row));
	}
	return apps;
}

static gboolean
is_addon_id_kind (GsApp *app)
{
	AsAppKind id_kind;
	id_kind = gs_app_get_kind (app);
	if (id_kind == AS_APP_KIND_DESKTOP)
		return FALSE;
	if (id_kind == AS_APP_KIND_WEB_APP)
		return FALSE;
	if (id_kind == AS_APP_KIND_FIRMWARE)
		return FALSE;
	if (id_kind == AS_APP_KIND_RUNTIME)
		return FALSE;
	return TRUE;
}

static void
list_header_func (GtkListBoxRow *row,
		  GtkListBoxRow *before,
		  gpointer user_data)
{
	GtkStyleContext *context;
	GtkWidget *header;

	/* first entry */
	gtk_list_box_row_set_header (row, NULL);
	if (before == NULL)
		return;

	/* desktop -> addons */
	if (!is_addon_id_kind (gs_app_row_get_app (GS_APP_ROW (before))) &&
	    is_addon_id_kind (gs_app_row_get_app (GS_APP_ROW (row)))) {
		/* TRANSLATORS: This is the header dividing the normal
		 * applications and the addons */
		header = gtk_label_new (_("Add-ons"));
		g_object_set (header,
			      "xalign", 0.0,
			      NULL);
		context = gtk_widget_get_style_context (header);
		gtk_style_context_add_class (context, "header-label");
	} else {
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	}
	gtk_list_box_row_set_header (row, header);
}

static gchar *
get_app_sort_key (GsApp *app)
{
	GString *key;

	key = g_string_sized_new (64);

	/* sort by kind */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_OS_UPDATE:
		g_string_append (key, "1:");
		break;
	default:
		g_string_append (key, "2:");
		break;
	}

	/* sort desktop files, then addons */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_FIRMWARE:
		g_string_append (key, "1:");
		break;
	case AS_APP_KIND_DESKTOP:
		g_string_append (key, "2:");
		break;
	default:
		g_string_append (key, "3:");
		break;
	}

	/* sort by install date */
	g_string_append_printf (key, "%09" G_GUINT64_FORMAT ":",
				G_MAXUINT64 - gs_app_get_install_date (app));

	/* finally, sort by short name */
	g_string_append (key, gs_app_get_name (app));
	return g_string_free (key, FALSE);
}

static gint
list_sort_func (GtkListBoxRow *a,
		GtkListBoxRow *b,
		gpointer user_data)
{
	GsApp *a1 = gs_app_row_get_app (GS_APP_ROW (a));
	GsApp *a2 = gs_app_row_get_app (GS_APP_ROW (b));
	g_autofree gchar *key1 = get_app_sort_key (a1);
	g_autofree gchar *key2 = get_app_sort_key (a2);

	/* compare the keys according to the algorithm above */
	return g_strcmp0 (key1, key2);
}

static void
gs_update_list_dispose (GObject *object)
{
	GsUpdateList *update_list = GS_UPDATE_LIST (object);
	GsUpdateListPrivate *priv = GET_PRIV (update_list);

	g_clear_object (&priv->sizegroup_image);
	g_clear_object (&priv->sizegroup_name);

	G_OBJECT_CLASS (gs_update_list_parent_class)->dispose (object);
}

static void
gs_update_list_init (GsUpdateList *update_list)
{
	GsUpdateListPrivate *priv = GET_PRIV (update_list);
	priv->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

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

	signals [SIGNAL_BUTTON_CLICKED] =
		g_signal_new ("button-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsUpdateListClass, button_clicked),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, GS_TYPE_APP);

	object_class->dispose = gs_update_list_dispose;
}

GtkWidget *
gs_update_list_new (void)
{
	GsUpdateList *update_list;

	update_list = g_object_new (GS_TYPE_UPDATE_LIST, NULL);

	return GTK_WIDGET (update_list);
}

/* vim: set noexpandtab: */
