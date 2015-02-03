/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include "gs-history-dialog.h"
#include "gs-cleanup.h"
#include "gs-utils.h"

struct _GsHistoryDialogPrivate
{
	GsApp		*app;
	GtkSizeGroup	*sizegroup_state;
	GtkSizeGroup	*sizegroup_timestamp;
	GtkSizeGroup	*sizegroup_version;
	GtkWidget	*list_box;
	GtkWidget	*scrolledwindow;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsHistoryDialog, gs_history_dialog, GTK_TYPE_DIALOG)

static gint
history_sort_cb (gconstpointer a, gconstpointer b)
{
	gint64 timestamp_a = gs_app_get_install_date (*(GsApp **) a);
	gint64 timestamp_b = gs_app_get_install_date (*(GsApp **) b);
	if (timestamp_a < timestamp_b)
		return 1;
	if (timestamp_a > timestamp_b)
		return -1;
	return 0;
}

void
gs_history_dialog_set_app (GsHistoryDialog *dialog, GsApp *app)
{
	GsHistoryDialogPrivate *priv = gs_history_dialog_get_instance_private (dialog);
	const gchar *tmp;
	GDateTime *datetime;
	GPtrArray *history;
	GtkBox *box;
	GtkWidget *widget;
	guint64 timestamp;
	guint i;

	/* add each history package to the dialog */
	gs_container_remove_all (GTK_CONTAINER (priv->list_box));
	history = gs_app_get_history (app);
	g_ptr_array_sort (history, history_sort_cb);
	for (i = 0; i < history->len; i++) {
		_cleanup_free_ gchar *date_str = NULL;
		app = g_ptr_array_index (history, i);
		box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0));

		/* add the action */
		switch (gs_app_get_state (app)) {
		case AS_APP_STATE_AVAILABLE:
		case AS_APP_STATE_REMOVING:
			/* TRANSLATORS: this is the status in the history UI,
			 * where we are showing the application was removed */
			tmp = C_("app status", "Removed");
			break;
		case AS_APP_STATE_INSTALLED:
		case AS_APP_STATE_INSTALLING:
			/* TRANSLATORS: this is the status in the history UI,
			 * where we are showing the application was installed */
			tmp = C_("app status", "Installed");
			break;
		case AS_APP_STATE_UPDATABLE:
			/* TRANSLATORS: this is the status in the history UI,
			 * where we are showing the application was updated */
			tmp = C_("app status", "Updated");
			break;
		default:
			/* TRANSLATORS: this is the status in the history UI,
			 * where we are showing that something happened to the
			 * application but we don't know what */
			tmp = C_("app status", "Unknown");
			break;
		}
		widget = gtk_label_new (tmp);
		g_object_set (widget,
			      "margin-start", 20,
			      "margin-end", 20,
			      "margin-top", 6,
			      "margin-bottom", 6,
			      "xalign", 0.0,
			      NULL);
		gtk_size_group_add_widget (priv->sizegroup_state, widget);
		gtk_box_pack_start (box, widget, TRUE, TRUE, 0);

		/* add the timestamp */
		timestamp = gs_app_get_install_date (app);
		datetime = g_date_time_new_from_unix_utc (timestamp);
		if (timestamp == GS_APP_INSTALL_DATE_UNKNOWN) {
			date_str = g_strdup ("");
		} else {
			date_str = g_date_time_format (datetime, "%e %B %Y");
		}
		widget = gtk_label_new (date_str);
		g_object_set (widget,
			      "margin-start", 20,
			      "margin-end", 20,
			      "margin-top", 6,
			      "margin-bottom", 6,
			      "xalign", 0.0,
			      NULL);
		gtk_size_group_add_widget (priv->sizegroup_timestamp, widget);
		gtk_box_pack_start (box, widget, TRUE, TRUE, 0);
		g_date_time_unref (datetime);

		/* add the version */
		widget = gtk_label_new (gs_app_get_version (app));
		g_object_set (widget,
			      "margin-start", 20,
			      "margin-end", 20,
			      "margin-top", 6,
			      "margin-bottom", 6,
			      "xalign", 1.0,
			      NULL);
		gtk_size_group_add_widget (priv->sizegroup_version, widget);
		gtk_box_pack_start (box, widget, TRUE, TRUE, 0);

		gtk_widget_show_all (GTK_WIDGET (box));
		gtk_list_box_insert (GTK_LIST_BOX (priv->list_box), GTK_WIDGET (box), -1);
	}
}

static void
update_header_func (GtkListBoxRow *row,
		    GtkListBoxRow *before,
		    gpointer user_data)
{
	GtkWidget *header;

	/* first entry */
	header = gtk_list_box_row_get_header (row);
	if (before == NULL) {
		gtk_list_box_row_set_header (row, NULL);
		return;
	}

	/* already set */
	if (header != NULL)
		return;

	/* set new */
	header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

static void
scrollbar_mapped_cb (GtkWidget *sb, GtkScrolledWindow *swin)
{
	GtkWidget *frame;

	frame = gtk_bin_get_child (GTK_BIN (gtk_bin_get_child (GTK_BIN (swin))));
	if (gtk_widget_get_mapped (GTK_WIDGET (sb))) {
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_IN);
		gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	} else {
		gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_NONE);
	}
}

static void
gs_history_dialog_finalize (GObject *object)
{
	GsHistoryDialog *dialog = GS_HISTORY_DIALOG (object);
	GsHistoryDialogPrivate *priv = gs_history_dialog_get_instance_private (dialog);

	g_object_unref (priv->sizegroup_state);
	g_object_unref (priv->sizegroup_timestamp);
	g_object_unref (priv->sizegroup_version);

	if (priv->app != NULL)
		g_object_unref (priv->app);

	G_OBJECT_CLASS (gs_history_dialog_parent_class)->finalize (object);
}

static void
gs_history_dialog_init (GsHistoryDialog *dialog)
{
	GsHistoryDialogPrivate *priv = gs_history_dialog_get_instance_private (dialog);
	GtkWidget *scrollbar;

	gtk_widget_init_template (GTK_WIDGET (dialog));

	priv->sizegroup_state = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->sizegroup_timestamp = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->sizegroup_version = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_list_box_set_header_func (GTK_LIST_BOX (priv->list_box),
				      update_header_func,
				      dialog,
				      NULL);

	scrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (priv->scrolledwindow));
	g_signal_connect (scrollbar, "map", G_CALLBACK (scrollbar_mapped_cb), priv->scrolledwindow);
	g_signal_connect (scrollbar, "unmap", G_CALLBACK (scrollbar_mapped_cb), priv->scrolledwindow);
}

static void
gs_history_dialog_class_init (GsHistoryDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = gs_history_dialog_finalize;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-history-dialog.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsHistoryDialog, list_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsHistoryDialog, scrolledwindow);
}

GtkWidget *
gs_history_dialog_new (void)
{
	return GTK_WIDGET (g_object_new (GS_TYPE_HISTORY_DIALOG,
					 "use-header-bar", TRUE,
					 NULL));
}

/* vim: set noexpandtab: */
