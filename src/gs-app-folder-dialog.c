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
	GtkWidget	 *description_label;
	GtkWidget	 *app_folder_list;
	GtkWidget	 *new_folder_button;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsAppFolderDialog, gs_app_folder_dialog, GTK_TYPE_WINDOW)

#define PRIVATE(o) (gs_app_folder_dialog_get_instance_private (o))

static void
gs_app_folder_dialog_destroy (GtkWidget *widget)
{
	GsAppFolderDialog *dialog = GS_APP_FOLDER_DIALOG (widget);
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);

	g_list_free (priv->apps);
	priv->apps = NULL;

	g_clear_object (&priv->folders);

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
	GtkListBoxRow *row;
	const gchar *folder;
	GList *l;
	
	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (priv->app_folder_list));
	if (row == NULL)
		folder = NULL;
	else
		folder = (const gchar *)g_object_get_data (G_OBJECT (row), "folder");

	for (l = priv->apps; l; l = l->next) {
		GsApp *app = l->data;
		gs_folders_set_app_folder (priv->folders, gs_app_get_id (app), folder);
	}

	gs_folders_save (priv->folders);
}

static void
done_cb (GsAppFolderDialog *dialog)
{
	apply_changes (dialog);	
	gtk_window_close (GTK_WINDOW (dialog));
}

static void
delete_row (GtkWidget *child, GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	GtkWidget *row;
	const gchar *folder;

	row = gtk_widget_get_ancestor (child, GTK_TYPE_LIST_BOX_ROW);

	folder = (const gchar *)g_object_get_data (G_OBJECT (row), "folder");
	gs_folders_remove_folder (priv->folders, folder);

	gtk_container_remove (GTK_CONTAINER (priv->app_folder_list), row);
}

static void
done_editing (GtkEntry *entry, GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	GtkWidget *row;
	gchar *folder;
	GtkWidget *label;
	GtkWidget *parent;

	row = gtk_widget_get_ancestor (GTK_WIDGET (entry), GTK_TYPE_LIST_BOX_ROW);
	if (gtk_entry_get_text_length (entry) == 0) {
		delete_row (GTK_WIDGET (entry), dialog);
		return;
	}
	folder = g_strdup (gtk_entry_get_text (entry));
	parent = gtk_widget_get_parent (GTK_WIDGET (entry));
	gtk_container_remove (GTK_CONTAINER (parent), GTK_WIDGET (entry));

	label = gtk_label_new (folder);
	gtk_widget_show (label);
	gtk_widget_set_margin_left (label, 10);
	gtk_widget_set_margin_right (label, 10);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

	gtk_box_pack_start (GTK_BOX (parent), label, TRUE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (parent), label, 0);

	g_object_set_data_full (G_OBJECT (row), "folder", folder, g_free);
	gs_folders_add_folder (priv->folders, folder);
}

static void
new_folder_cb (GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *entry;
	GtkWidget *button;

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	entry = gtk_entry_new ();
	gtk_widget_set_margin_left (entry, 10);
	gtk_widget_set_margin_right (entry, 10);
	gtk_widget_set_halign (entry, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), entry, TRUE, TRUE, 0);

	button = gtk_button_new_from_icon_name ("edit-delete-symbolic", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
	
	row = gtk_list_box_row_new ();
	gtk_container_add (GTK_CONTAINER (row), box);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (delete_row), dialog);
	g_signal_connect (entry, "activate",
			  G_CALLBACK (done_editing), dialog);

	gtk_widget_show_all (row);

	gtk_list_box_insert (GTK_LIST_BOX (priv->app_folder_list), row, -1);

	gtk_widget_grab_focus (entry);
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
	g_signal_connect_swapped (priv->new_folder_button, "clicked",
				  G_CALLBACK (new_folder_cb), dialog);

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
	gtk_widget_class_bind_template_child_private (widget_class, GsAppFolderDialog, description_label);
        gtk_widget_class_bind_template_child_private (widget_class, GsAppFolderDialog, app_folder_list);
        gtk_widget_class_bind_template_child_private (widget_class, GsAppFolderDialog, new_folder_button);
}

static GtkWidget *
create_row (GsAppFolderDialog *dialog, const gchar *folder)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *label;
	GtkWidget *button;
	GString *s;
	GList *l;

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new (gs_folders_get_folder_name (priv->folders, folder));
	gtk_widget_set_margin_left (label, 10);
	gtk_widget_set_margin_right (label, 10);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

	s = g_string_new ("");
	for (l = priv->apps; l; l = l->next) {
		GsApp *app = l->data;
		const gchar *app_folder;

		app_folder = gs_folders_get_app_folder (priv->folders, gs_app_get_id (app));
		if (g_strcmp0 (folder, app_folder) == 0) {
			if (s->len > 0)
				g_string_append (s, ", ");
			g_string_append (s, gs_app_get_name (app));
		}
	}
	if (s->len > 0) {
		label = gtk_label_new (s->str);
		gtk_widget_set_margin_left (label, 10);
		gtk_widget_set_margin_right (label, 10);
		gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars (GTK_LABEL (label), 30);
		gtk_widget_set_halign (label, GTK_ALIGN_END);
		gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
		gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 0);
	}
	g_string_free (s, TRUE);

	button = gtk_button_new_from_icon_name ("edit-delete-symbolic", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
	
	row = gtk_list_box_row_new ();
	gtk_container_add (GTK_CONTAINER (row), box);

	gtk_widget_show_all (row);

	g_object_set_data_full (G_OBJECT (row), "folder", g_strdup (folder), g_free);

	g_signal_connect (button, "clicked",
			  G_CALLBACK (delete_row), dialog);

	return row;	
}

static void
populate_list (GsAppFolderDialog *dialog)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	gchar **folders;
	guint i;

	folders = gs_folders_get_folders (priv->folders);
	for (i = 0; folders[i]; i++) {
		gtk_list_box_insert (GTK_LIST_BOX (priv->app_folder_list), create_row (dialog, folders[i]), -1);
	}
	g_free (folders);
}

static void
gs_app_folder_dialog_set_apps (GsAppFolderDialog *dialog,
			       GList *apps)
{
	GsAppFolderDialogPrivate *priv = PRIVATE (dialog);
	gchar *label;
	const gchar *app1, *app2, *app3;

	priv->apps = g_list_copy (apps);

	switch (g_list_length (priv->apps)) {
	case 0:
		label = g_strdup (_("Add or remove folders. Your application folders can be found in the Activities Overview."));
		break;
	case 1:
		app1 = gs_app_get_name (GS_APP (priv->apps->data));
		label = g_strdup_printf (_("Choose a folder for %s. Your application folders can be found in the Activities Overview."), app1);
		break;
	case 2:
		app1 = gs_app_get_name (GS_APP (priv->apps->data));
		app2 = gs_app_get_name (GS_APP (priv->apps->next->data));
		label = g_strdup_printf (_("Choose a folder for %s and %s. Your application folders can be found in the Activities Overview."), app1, app2);
		break;
	case 3:
		app1 = gs_app_get_name (GS_APP (priv->apps->data));
		app2 = gs_app_get_name (GS_APP (priv->apps->next->data));
		app3 = gs_app_get_name (GS_APP (priv->apps->next->next->data));
		label = g_strdup_printf (_("Choose a folder for %s, %s and %s. Your application folders can be found in the Activities Overview."), app1, app2, app3);
		break;
	default:
		label = g_strdup (_("Choose a folder for the selected applications. Your application folders can be found in the Activities Overview."));
		break;
	}
	gtk_label_set_label (GTK_LABEL (priv->description_label), label);
	g_free (label);
}

GtkWidget *
gs_app_folder_dialog_new (GtkWindow *parent, GList *apps)
{
	GsAppFolderDialog *dialog;

	dialog = g_object_new (GS_TYPE_APP_FOLDER_DIALOG,
			       "transient-for", parent,
			       NULL);
	gs_app_folder_dialog_set_apps (dialog, apps);
	populate_list (dialog);

	return GTK_WIDGET (dialog);
}

/* vim: set noexpandtab: */
