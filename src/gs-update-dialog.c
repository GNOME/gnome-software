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

#include "gs-update-dialog.h"
#include "gs-markdown.h"
#include "gs-utils.h"

struct _GsUpdateDialogPrivate
{
	GsApp		*app;
	GtkWidget	*box_header;
	GtkWidget	*button_back;
	GtkWidget	*image_icon;
	GtkWidget	*label_details;
	GtkWidget	*label_name;
	GtkWidget	*label_summary;
	GtkWidget	*list_box;
	GtkWidget	*scrolledwindow;
	GtkWidget	*scrolledwindow_details;
	GtkWidget	*stack;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsUpdateDialog, gs_update_dialog, GTK_TYPE_DIALOG)

static void
set_updates_description_ui (GsUpdateDialog *dialog, GsApp *app)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);
	GsAppKind kind;
	GsMarkdown *markdown;
	gchar *tmp;
	gchar *update_desc;

	/* set window title */
	kind = gs_app_get_kind (app);
	if (kind == GS_APP_KIND_OS_UPDATE) {
		gtk_window_set_title (GTK_WINDOW (dialog), gs_app_get_name (app));
	} else {
		tmp = g_strdup_printf ("%s %s",
		                       gs_app_get_source_default (app),
		                       gs_app_get_update_version_ui (app));
		gtk_window_set_title (GTK_WINDOW (dialog), tmp);
		g_free (tmp);
	}

	/* get the update description */
	if (gs_app_get_update_details (app) == NULL) {
		/* TRANSLATORS: this is where the packager did not write a
		 * description for the update */
		update_desc = g_strdup ("No update description");
	} else {
		markdown = gs_markdown_new (GS_MARKDOWN_OUTPUT_PANGO);
		gs_markdown_set_smart_quoting (markdown, FALSE);
		gs_markdown_set_autocode (markdown, TRUE);
		update_desc = gs_markdown_parse (markdown, gs_app_get_update_details (app));
		g_object_unref (markdown);
	}

	/* set update header */
	gtk_widget_set_visible (priv->box_header, kind == GS_APP_KIND_NORMAL || kind == GS_APP_KIND_SYSTEM);
	gtk_label_set_markup (GTK_LABEL (priv->label_details), update_desc);
	gtk_image_set_from_pixbuf (GTK_IMAGE (priv->image_icon), gs_app_get_pixbuf (app));
	gtk_label_set_label (GTK_LABEL (priv->label_name), gs_app_get_name (app));
	gtk_label_set_label (GTK_LABEL (priv->label_summary), gs_app_get_summary (app));
	g_free (update_desc);
}

static void
row_activated_cb (GtkListBox *list_box,
                  GtkListBoxRow *row,
                  GsUpdateDialog *dialog)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);
	GsApp *app = NULL;

	app = GS_APP (g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), "app"));
	/* setup package view */
	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "package-details");
	set_updates_description_ui (dialog, app);
	gtk_widget_show (priv->button_back);
}

void
gs_update_dialog_set_app (GsUpdateDialog *dialog, GsApp *app)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);
	GsApp *app_related;
	GsAppKind kind;
	const gchar *sort;

	g_clear_object (&priv->app);
	priv->app = g_object_ref (app);

	kind = gs_app_get_kind (app);

	/* set update header */
	set_updates_description_ui (dialog, app);

	/* only OS updates can go back, and only on selection */
	gtk_widget_hide (priv->button_back);

	/* set update description */
	if (kind == GS_APP_KIND_OS_UPDATE) {
		GPtrArray *related;
		guint i;
		GtkWidget *row, *label;

		gs_container_remove_all (GTK_CONTAINER (priv->list_box));
		related = gs_app_get_related (app);
		for (i = 0; i < related->len; i++) {
			app_related = g_ptr_array_index (related, i);
			row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
			g_object_set_data_full (G_OBJECT (row),
			                        "app",
			                        g_object_ref (app_related),
			                        g_object_unref);
			sort = gs_app_get_source_default (app_related);
			g_object_set_data_full (G_OBJECT (row),
			                        "sort",
			                        g_strdup (sort),
			                        g_free);
			label = gtk_label_new (gs_app_get_source_default (app_related));
			g_object_set (label,
			              "margin-left", 20,
			              "margin-right", 20,
			              "margin-top", 6,
			              "margin-bottom", 6,
			              "xalign", 0.0,
			              NULL);
			gtk_widget_set_halign (label, GTK_ALIGN_START);
			gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
			gtk_box_pack_start (GTK_BOX (row), label, TRUE, TRUE, 0);
			label = gtk_label_new (gs_app_get_update_version_ui (app_related));
			g_object_set (label,
			              "margin-left", 20,
			              "margin-right", 20,
			              "margin-top", 6,
			              "margin-bottom", 6,
			              "xalign", 1.0,
			              NULL);
			gtk_widget_set_halign (label, GTK_ALIGN_END);
			gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
			gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
			gtk_widget_show_all (row);
			gtk_list_box_insert (GTK_LIST_BOX (priv->list_box), row, -1);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "os-update-list");
	} else {
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "package-details");
	}
}

static gint
os_updates_sort_func (GtkListBoxRow *a,
                      GtkListBoxRow *b,
                      gpointer user_data)
{
	GObject *o1 = G_OBJECT (gtk_bin_get_child (GTK_BIN (a)));
	GObject *o2 = G_OBJECT (gtk_bin_get_child (GTK_BIN (b)));
	const gchar *key1 = g_object_get_data (o1, "sort");
	const gchar *key2 = g_object_get_data (o2, "sort");
	return g_strcmp0 (key1, key2);
}


static void
button_back_cb (GtkWidget *widget, GsUpdateDialog *dialog)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);

	/* return to the list view */
	gtk_widget_hide (priv->button_back);
	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "os-update-list");

	gtk_window_set_title (GTK_WINDOW (dialog), gs_app_get_name (priv->app));
}

static void
scrollbar_mapped_cb (GtkWidget *sb, GtkScrolledWindow *swin)
{
	GtkWidget *frame;

	frame = gtk_bin_get_child (GTK_BIN (gtk_bin_get_child (GTK_BIN (swin))));

	if (gtk_widget_get_mapped (GTK_WIDGET (sb))) {
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_IN);
		if (GTK_IS_FRAME (frame))
			gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	} else {
		if (GTK_IS_FRAME (frame))
			gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_NONE);
	}
}

static void
gs_update_dialog_finalize (GObject *object)
{
	GsUpdateDialog *dialog = GS_UPDATE_DIALOG (object);
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);

	g_clear_object (&priv->app);

	G_OBJECT_CLASS (gs_update_dialog_parent_class)->finalize (object);
}

static void
gs_update_dialog_init (GsUpdateDialog *dialog)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);
	GtkWidget *scrollbar;

	gtk_widget_init_template (GTK_WIDGET (dialog));

	g_signal_connect (GTK_LIST_BOX (priv->list_box), "row-activated",
	                  G_CALLBACK (row_activated_cb), dialog);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->list_box),
	                            os_updates_sort_func,
	                            dialog, NULL);

	g_signal_connect (priv->button_back, "clicked",
	                  G_CALLBACK (button_back_cb),
	                  dialog);

	scrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (priv->scrolledwindow_details));
	g_signal_connect (scrollbar, "map", G_CALLBACK (scrollbar_mapped_cb), priv->scrolledwindow_details);
	g_signal_connect (scrollbar, "unmap", G_CALLBACK (scrollbar_mapped_cb), priv->scrolledwindow_details);

	scrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (priv->scrolledwindow));
	g_signal_connect (scrollbar, "map", G_CALLBACK (scrollbar_mapped_cb), priv->scrolledwindow);
	g_signal_connect (scrollbar, "unmap", G_CALLBACK (scrollbar_mapped_cb), priv->scrolledwindow);
}

static void
gs_update_dialog_class_init (GsUpdateDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = gs_update_dialog_finalize;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/software/gs-update-dialog.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, box_header);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, button_back);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, image_icon);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, label_details);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, label_name);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, label_summary);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, list_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, scrolledwindow);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, scrolledwindow_details);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, stack);
}

GtkWidget *
gs_update_dialog_new (void)
{
	return GTK_WIDGET (g_object_new (GS_TYPE_UPDATE_DIALOG,
	                                 "use-header-bar", TRUE,
	                                 NULL));
}

/* vim: set noexpandtab: */
