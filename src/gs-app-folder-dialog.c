/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#include "gs-folders.h"
#include "gs-app-folder-dialog.h"

typedef struct _GsAppFolderDialogPrivate        GsAppFolderDialogPrivate;
struct _GsAppFolderDialogPrivate
{
	GList		 *apps;
	GsFolders	 *folders;
	GtkWidget	 *header;
	GtkWidget	 *cancel_button;
	GtkWidget 	 *done_button;
	GtkWidget	 *app_folder_list;
	GtkSizeGroup     *rows;
	GtkSizeGroup     *labels;
	GtkListBoxRow	 *new_folder_button;
	GtkListBoxRow	 *selected_row;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsAppFolderDialog, gs_app_folder_dialog, GTK_TYPE_DIALOG)

#define PRIVATE(o) (gs_app_folder_dialog_get_instance_private (o))

static void
gs_app_folder_dialog_destroy (GtkWidget *widget)
{
	GsAppFolderDialog *dialog = GS_APP_FOLDER_DIALOG (widget);
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);

	g_list_free (priv->apps);
	priv->apps = NULL;

	g_clear_object (&priv->folders);
	g_clear_object (&priv->rows);
	g_clear_object (&priv->labels);

	GTK_WIDGET_CLASS (gs_app_folder_dialog_parent_class)->destroy (widget);
}

static void
cancel_cb (GsAppFolderDialog *dialog)
{
	gtk_window_close (GTK_WINDOW (dialog));
}

static void
apply_changes (GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	const gchar *folder;
	GList *l;
	
	if (priv->selected_row)
		folder = (const gchar *)g_object_get_data (G_OBJECT (priv->selected_row), "folder");
	else
		folder = NULL;

	for (l = priv->apps; l; l = l->next) {
		GsApp *app = l->data;
		gs_folders_set_app_folder (priv->folders,
					   gs_app_get_id_full (app),
					   gs_app_get_categories (app),
					   folder);
	}

	gs_folders_save (priv->folders);
}

static void
done_cb (GsAppFolderDialog *dialog)
{
	apply_changes (dialog);	
	gtk_window_close (GTK_WINDOW (dialog));
}

static GtkWidget *create_row (GsAppFolderDialog *dialog, const gchar *folder);

static void
new_folder_cb (GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	GtkWidget *entry;
	GtkWidget *subdialog;
	const gchar *folder;

	subdialog = gtk_dialog_new_with_buttons (_("Folder Name"),
					         GTK_WINDOW (dialog),
						 GTK_DIALOG_MODAL |
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
						 _("_Cancel"), GTK_RESPONSE_CANCEL,
                                                 _("_Add"), GTK_RESPONSE_OK,
                                                 NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (subdialog), GTK_RESPONSE_OK);

	entry = gtk_entry_new ();
	gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
        g_object_set (entry, "margin", 10, NULL);
	gtk_widget_set_halign (entry, GTK_ALIGN_FILL);
	gtk_widget_set_valign (entry, GTK_ALIGN_CENTER);
	gtk_widget_show (entry);

	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (subdialog))), entry);

	if (gtk_dialog_run (GTK_DIALOG (subdialog)) == GTK_RESPONSE_OK) {
		folder = gtk_entry_get_text (GTK_ENTRY (entry));
		if (folder[0] != '\0') {
			gs_folders_add_folder (priv->folders, folder);
			gtk_list_box_insert (GTK_LIST_BOX (priv->app_folder_list), 
                        	             create_row (dialog, folder),
					     gtk_list_box_row_get_index (priv->new_folder_button));
		}
	}

	gtk_widget_destroy (subdialog);
}

static void
update_header_func (GtkListBoxRow  *row,
                    GtkListBoxRow  *before,
                    gpointer    user_data)
{
  GtkWidget *current;

  if (before == NULL)
    return;

  current = gtk_list_box_row_get_header (row);
  if (current == NULL)
    {
      current = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gtk_widget_show (current);
      gtk_list_box_row_set_header (row, current);
    }
}

static void
gs_app_folder_dialog_init (GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);

	priv->folders = gs_folders_get ();
	gtk_widget_init_template (GTK_WIDGET (dialog));

	g_signal_connect_swapped (priv->cancel_button, "clicked",
				  G_CALLBACK (cancel_cb), dialog);
	g_signal_connect_swapped (priv->done_button, "clicked",
				  G_CALLBACK (done_cb), dialog);
	priv->rows = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
	priv->labels = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_list_box_set_header_func (GTK_LIST_BOX (priv->app_folder_list),
				      update_header_func, NULL, NULL);
}

static void
gs_app_folder_dialog_class_init (GsAppFolderDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_app_folder_dialog_destroy;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/software/app-folder-dialog.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsAppFolderDialog, header);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppFolderDialog, cancel_button);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppFolderDialog, done_button);
        gtk_widget_class_bind_template_child_private (widget_class, GsAppFolderDialog, app_folder_list);
}

static GtkWidget *
create_row (GsAppFolderDialog *dialog, const gchar *folder)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *label;
	GtkWidget *image;

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new (gs_folders_get_folder_name (priv->folders, folder));
	g_object_set (label,
                      "margin-start", 20,
                      "margin-end", 20,
                      "margin-top", 10,
                      "margin-bottom", 10,
                      NULL);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (label, GTK_ALIGN_START);
	gtk_container_add (GTK_CONTAINER (box), label);
	image = gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU);
	gtk_widget_set_no_show_all (image, TRUE);
	gtk_container_add (GTK_CONTAINER (box), image);

	row = gtk_list_box_row_new ();
	gtk_container_add (GTK_CONTAINER (row), box);

	gtk_widget_show_all (row);

	g_object_set_data (G_OBJECT (row), "image", image);
	g_object_set_data_full (G_OBJECT (row), "folder", g_strdup (folder), g_free);

	gtk_size_group_add_widget (priv->labels, label);
	gtk_size_group_add_widget (priv->rows, row);

	return row;	
}

static void
set_apps (GsAppFolderDialog *dialog, GList *apps)
{
       GsAppFolderDialogPrivate *priv = PRIVATE (dialog);

       priv->apps = g_list_copy (apps);
}

static void
populate_list (GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	gchar **folders;
	guint i;

	folders = gs_folders_get_nonempty_folders (priv->folders);
	for (i = 0; folders[i]; i++) {
		gtk_list_box_insert (GTK_LIST_BOX (priv->app_folder_list), 
                                     create_row (dialog, folders[i]), -1);
	}
	g_free (folders);
}

static void
row_activated (GtkListBox *list_box, GtkListBoxRow *row, GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	GtkWidget *image;

	if (row == priv->new_folder_button) {
		new_folder_cb (dialog);
		return;
	}

	if (priv->selected_row) {
		image = g_object_get_data (G_OBJECT (priv->selected_row), "image");
		gtk_widget_hide (image);
		gtk_widget_set_sensitive (priv->done_button, FALSE);
	}

	priv->selected_row = row;

	if (priv->selected_row) {
		image = g_object_get_data (G_OBJECT (priv->selected_row), "image");
		gtk_widget_show (image);
		gtk_widget_set_sensitive (priv->done_button, TRUE);
	}
}

static void
add_new_folder_row (GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	GtkWidget *row, *button;

	button = gtk_image_new_from_icon_name ("list-add-symbolic", GTK_ICON_SIZE_MENU);
	gtk_widget_set_halign (button, GTK_ALIGN_FILL);
	gtk_widget_set_valign (button, GTK_ALIGN_FILL);
	row = gtk_list_box_row_new ();
	priv->new_folder_button = GTK_LIST_BOX_ROW (row);
	gtk_container_add (GTK_CONTAINER (row), button);
	gtk_list_box_insert (GTK_LIST_BOX (priv->app_folder_list), row, -1);
	gtk_size_group_add_widget (priv->rows, row);

	g_signal_connect (priv->app_folder_list, "row-activated",
			  G_CALLBACK (row_activated), dialog);

	gtk_widget_show_all (row);
}

GtkWidget *
gs_app_folder_dialog_new (GtkWindow *parent, GList *apps)
{
	GsAppFolderDialog *dialog;

	dialog = g_object_new (GS_TYPE_APP_FOLDER_DIALOG,
			       "use-header-bar", TRUE,
			       "transient-for", parent,
			       "modal", TRUE,
			       NULL);
        set_apps (dialog, apps);
	populate_list (dialog);
 	add_new_folder_row (dialog);

	return GTK_WIDGET (dialog);
}

/* vim: set noexpandtab: */
