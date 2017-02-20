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
#include "gs-common.h"

typedef enum {
	GS_UPDATE_LIST_SECTION_OFFLINE_FIRMWARE,
	GS_UPDATE_LIST_SECTION_OFFLINE,
	GS_UPDATE_LIST_SECTION_ONLINE,
	GS_UPDATE_LIST_SECTION_ONLINE_FIRMWARE,
	GS_UPDATE_LIST_SECTION_LAST
} GsUpdateListSection;

typedef struct
{
	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	GtkSizeGroup		*sizegroup_button;
	GtkSizeGroup		*sizegroup_header;
	gboolean		 force_headers;
	GsUpdateListSection	 sections_cnt[GS_UPDATE_LIST_SECTION_LAST];
} GsUpdateListPrivate;

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (GsUpdateList, gs_update_list, GTK_TYPE_LIST_BOX)

static void
gs_update_list_button_clicked_cb (GsAppRow *app_row,
				  GsUpdateList *update_list)
{
	GsApp *app = gs_app_row_get_app (app_row);
	g_signal_emit (update_list, signals[SIGNAL_BUTTON_CLICKED], 0, app);
}

static GsUpdateListSection
gs_update_list_get_app_section (GsApp *app)
{
	if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE_LIVE) {
		if (gs_app_get_kind (app) == AS_APP_KIND_FIRMWARE)
			return GS_UPDATE_LIST_SECTION_ONLINE_FIRMWARE;
		return GS_UPDATE_LIST_SECTION_ONLINE;
	}
	if (gs_app_get_kind (app) == AS_APP_KIND_FIRMWARE)
		return GS_UPDATE_LIST_SECTION_OFFLINE_FIRMWARE;
	return GS_UPDATE_LIST_SECTION_OFFLINE;
}

void
gs_update_list_remove_all (GsUpdateList *update_list)
{
	GsUpdateListPrivate *priv = gs_update_list_get_instance_private (update_list);
	for (guint i = 0; i < GS_UPDATE_LIST_SECTION_LAST; i++)
		priv->sections_cnt[i] = 0;
	gs_container_remove_all (GTK_CONTAINER (update_list));
}

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
	GsUpdateListSection section;
	GtkWidget *app_row;

	/* keep track */
	section = gs_update_list_get_app_section (app);
	priv->sections_cnt[section]++;

	app_row = gs_app_row_new (app);
	gs_app_row_set_show_update (GS_APP_ROW (app_row), TRUE);
	gs_app_row_set_show_buttons (GS_APP_ROW (app_row), TRUE);
	g_signal_connect (app_row, "button-clicked",
			  G_CALLBACK (gs_update_list_button_clicked_cb),
			  update_list);
	gtk_container_add (GTK_CONTAINER (update_list), app_row);
	gs_app_row_set_size_groups (GS_APP_ROW (app_row),
				    priv->sizegroup_image,
				    priv->sizegroup_name,
				    priv->sizegroup_button);
	g_signal_connect (app, "notify::state",
			  G_CALLBACK (gs_update_list_app_state_notify_cb),
			  app_row);
	gtk_widget_show (app_row);
}

/* returns if the updates have section headers */
gboolean
gs_update_list_has_headers (GsUpdateList *update_list)
{
	GsUpdateListPrivate *priv = gs_update_list_get_instance_private (update_list);
	guint cnt = 0;

	/* forced on by the distro upgrade for example */
	if (priv->force_headers)
		return TRUE;

	/* more than one type of thing */
	for (guint i = 0; i < GS_UPDATE_LIST_SECTION_LAST; i++) {
		if (priv->sections_cnt[i] > 0)
			cnt++;
	}
	return cnt > 1;
}

/* forces the update list into having section headers even if it does not need
 * them, for instance if we're showing a big system upgrade banner at the top */
void
gs_update_list_set_force_headers (GsUpdateList *update_list, gboolean force_headers)
{
	GsUpdateListPrivate *priv = gs_update_list_get_instance_private (update_list);
	priv->force_headers = force_headers;
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

static void
gs_update_list_emit_clicked_for_section (GsUpdateList *update_list,
					 GsUpdateListSection section)
{
	g_autoptr(GList) children = NULL;
	children = gtk_container_get_children (GTK_CONTAINER (update_list));
	for (GList *l = children; l != NULL; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		GsApp *app = gs_app_row_get_app (app_row);
		if (gs_update_list_get_app_section (app) != section)
			continue;
		g_signal_emit (update_list, signals[SIGNAL_BUTTON_CLICKED], 0, app);
	}
}

static void
gs_update_list_update_offline_firmware_cb (GtkButton *button,
					   GsUpdateList *update_list)
{
	gs_update_list_emit_clicked_for_section (update_list,
						 GS_UPDATE_LIST_SECTION_OFFLINE_FIRMWARE);
}

static void
gs_update_list_update_offline_cb (GtkButton *button, GsUpdateList *update_list)
{
	gs_update_list_emit_clicked_for_section (update_list,
						 GS_UPDATE_LIST_SECTION_OFFLINE);
}

static void
gs_update_list_update_online_cb (GtkButton *button, GsUpdateList *update_list)
{
	gs_update_list_emit_clicked_for_section (update_list,
						 GS_UPDATE_LIST_SECTION_ONLINE);
}

static GtkWidget *
gs_update_list_get_section_header (GsUpdateList *update_list,
				   GsUpdateListSection section)
{
	GsUpdateListPrivate *priv = gs_update_list_get_instance_private (update_list);
	GtkStyleContext *context;
	GtkWidget *header;
	GtkWidget *label;
	GtkWidget *button = NULL;

	/* get labels and buttons for everything */
	if (section == GS_UPDATE_LIST_SECTION_OFFLINE_FIRMWARE) {
		/* TRANSLATORS: This is the header for system firmware that
		 * requires a reboot to apply */
		label = gtk_label_new (_("Integrated Firmware"));
		/* TRANSLATORS: This is the button for upgrading all
		 * system firmware */
		button = gtk_button_new_with_label (_("Restart & Update"));
		g_signal_connect (button, "clicked",
				  G_CALLBACK (gs_update_list_update_offline_firmware_cb),
				  update_list);
	} else if (section == GS_UPDATE_LIST_SECTION_OFFLINE) {
		/* TRANSLATORS: This is the header for offline OS and offline
		 * app updates that require a reboot to apply */
		label = gtk_label_new (_("Requires Restart"));
		/* TRANSLATORS: This is the button for upgrading all
		 * offline updates */
		button = gtk_button_new_with_label (_("Restart & Update"));
		g_signal_connect (button, "clicked",
				  G_CALLBACK (gs_update_list_update_offline_cb),
				  update_list);
	} else if (section == GS_UPDATE_LIST_SECTION_ONLINE) {
		/* TRANSLATORS: This is the header for online runtime and
		 * app updates, typically flatpaks or snaps */
		label = gtk_label_new (_("Application Updates"));
		/* TRANSLATORS: This is the button for upgrading all
		 * online-updatable applications */
		button = gtk_button_new_with_label (_("Update All"));
		g_signal_connect (button, "clicked",
				  G_CALLBACK (gs_update_list_update_online_cb),
				  update_list);
	} else if (section == GS_UPDATE_LIST_SECTION_ONLINE_FIRMWARE) {
		/* TRANSLATORS: This is the header for device firmware that can
		 * be installed online */
		label = gtk_label_new (_("Device Firmware"));
	} else {
		g_assert_not_reached ();
	}

	/* create header */
	header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_size_group_add_widget (priv->sizegroup_header, header);
	context = gtk_widget_get_style_context (header);
	gtk_style_context_add_class (context, "app-listbox-header");

	/* put label into the header */
	gtk_box_pack_start (GTK_BOX (header), label, TRUE, TRUE, 0);
	gtk_widget_set_visible (label, TRUE);
	gtk_widget_set_margin_start (label, 6);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	context = gtk_widget_get_style_context (label);
	gtk_style_context_add_class (context, "app-listbox-header-title");

	/* add button if one is specified */
	if (button != NULL) {
		gtk_box_pack_end (GTK_BOX (header), button, FALSE, FALSE, 0);
		gtk_widget_set_visible (button, TRUE);
		gtk_widget_set_margin_end (button, 6);
		gtk_size_group_add_widget (priv->sizegroup_button, button);
	}

	/* success */
	return header;
}

static void
list_header_func (GtkListBoxRow *row,
		  GtkListBoxRow *before,
		  gpointer user_data)
{
	GsApp *app = gs_app_row_get_app (GS_APP_ROW (row));
	GsUpdateListSection before_section = GS_UPDATE_LIST_SECTION_LAST;
	GsUpdateListSection section;
	GsUpdateList *update_list = GS_UPDATE_LIST (user_data);
	GtkWidget *header;

	/* first entry */
	gtk_list_box_row_set_header (row, NULL);
	if (before != NULL) {
		GsApp *before_app = gs_app_row_get_app (GS_APP_ROW (before));
		before_section = gs_update_list_get_app_section (before_app);
	}

	/* section changed or forced to have headers */
	section = gs_update_list_get_app_section (app);
	if (gs_update_list_has_headers (update_list) &&
	    before_section != section) {
		header = gs_update_list_get_section_header (update_list, section);
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

	/* Sections:
	 * 1. offline integrated firmware
	 * 2. offline os updates (OS-update, apps, runtimes, addons, other)
	 * 3. online apps (apps, runtimes, addons, other)
	 * 4. online device firmware */
	g_string_append_printf (key, "%u:", gs_update_list_get_app_section (app));

	/* sort apps by kind */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_OS_UPDATE:
		g_string_append (key, "1:");
		break;
	case AS_APP_KIND_DESKTOP:
		g_string_append (key, "2:");
		break;
	case AS_APP_KIND_WEB_APP:
		g_string_append (key, "3:");
		break;
	case AS_APP_KIND_RUNTIME:
		g_string_append (key, "4:");
		break;
	case AS_APP_KIND_ADDON:
		g_string_append (key, "5:");
		break;
	case AS_APP_KIND_CODEC:
		g_string_append (key, "6:");
		break;
	case AS_APP_KIND_FONT:
		g_string_append (key, "6:");
		break;
	case AS_APP_KIND_INPUT_METHOD:
		g_string_append (key, "7:");
		break;
	case AS_APP_KIND_SHELL_EXTENSION:
		g_string_append (key, "8:");
		break;
	default:
		g_string_append (key, "9:");
		break;
	}

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
	GsUpdateListPrivate *priv = gs_update_list_get_instance_private (update_list);

	g_clear_object (&priv->sizegroup_image);
	g_clear_object (&priv->sizegroup_name);
	g_clear_object (&priv->sizegroup_button);
	g_clear_object (&priv->sizegroup_header);

	G_OBJECT_CLASS (gs_update_list_parent_class)->dispose (object);
}

static void
gs_update_list_init (GsUpdateList *update_list)
{
	GsUpdateListPrivate *priv = gs_update_list_get_instance_private (update_list);
	priv->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->sizegroup_button = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->sizegroup_header = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);

	gtk_list_box_set_header_func (GTK_LIST_BOX (update_list),
				      list_header_func,
				      update_list, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (update_list),
				    list_sort_func,
				    update_list, NULL);

	/* set each section count to zero */
	for (guint i = 0; i < GS_UPDATE_LIST_SECTION_LAST; i++)
		priv->sections_cnt[i] = 0;
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
