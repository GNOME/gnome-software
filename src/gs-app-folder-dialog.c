/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-folders.h"
#include "gs-app-folder-dialog.h"

struct _GsAppFolderDialog
{
	GtkDialog	  parent_instance;

	GList		 *apps;
	GsFolders	 *folders;
	GtkWidget	 *cancel_button;
	GtkWidget 	 *done_button;
	GtkWidget	 *app_folder_list;
	GtkSizeGroup	 *rows;
	GtkListBoxRow	 *new_folder_button;
	GtkWidget	 *new_folder_popover;
	GtkWidget	 *new_folder_entry;
	GtkListBoxRow	 *selected_row;
};

G_DEFINE_TYPE (GsAppFolderDialog, gs_app_folder_dialog, GTK_TYPE_DIALOG)

static void
gs_app_folder_dialog_destroy (GtkWidget *widget)
{
	GsAppFolderDialog *dialog = GS_APP_FOLDER_DIALOG (widget);

	g_list_free (dialog->apps);
	dialog->apps = NULL;

	g_clear_object (&dialog->folders);
	g_clear_object (&dialog->rows);

	GTK_WIDGET_CLASS (gs_app_folder_dialog_parent_class)->destroy (widget);
}

static void
apply_changes (GsAppFolderDialog *dialog)
{
	const gchar *folder;
	
	if (dialog->selected_row)
		folder = (const gchar *)g_object_get_data (G_OBJECT (dialog->selected_row), "folder");
	else
		folder = NULL;

	for (GList *l = dialog->apps; l; l = l->next) {
		GsApp *app = l->data;
		gs_folders_set_app_folder (dialog->folders,
					   gs_app_get_id (app),
					   gs_app_get_categories (app),
					   folder);
	}

	gs_folders_save (dialog->folders);
}

static void
response_cb (GtkDialog *dialog,
             GtkResponseType response_type,
             gpointer user_data)
{
	switch (response_type) {
	case GTK_RESPONSE_APPLY:
		apply_changes (GS_APP_FOLDER_DIALOG (dialog));
		gtk_window_close (GTK_WINDOW (dialog));
		break;
	case GTK_RESPONSE_CANCEL:
		gtk_window_close (GTK_WINDOW (dialog));
		break;
	case GTK_RESPONSE_DELETE_EVENT:
		break;
	default:
		g_assert_not_reached ();
	}
}

static GtkWidget *create_row (GsAppFolderDialog *dialog, const gchar *folder);

static void
new_folder_cb (GsAppFolderDialog *dialog)
{
	gtk_entry_set_text (GTK_ENTRY (dialog->new_folder_entry), "");
	gtk_widget_show (dialog->new_folder_popover);
	gtk_widget_grab_focus (dialog->new_folder_entry);
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
	dialog->folders = gs_folders_get ();
	gtk_widget_init_template (GTK_WIDGET (dialog));

	g_signal_connect (dialog, "response",
	                  G_CALLBACK (response_cb),
	                  NULL);
	dialog->rows = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);

	gtk_list_box_set_header_func (GTK_LIST_BOX (dialog->app_folder_list),
				      update_header_func, NULL, NULL);
}

static void
gs_app_folder_dialog_class_init (GsAppFolderDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_app_folder_dialog_destroy;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-folder-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAppFolderDialog, cancel_button);
	gtk_widget_class_bind_template_child (widget_class, GsAppFolderDialog, done_button);
	gtk_widget_class_bind_template_child (widget_class, GsAppFolderDialog, app_folder_list);
}

static GtkWidget *
create_row (GsAppFolderDialog *dialog, const gchar *folder)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *label;
	GtkWidget *image;

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new (gs_folders_get_folder_name (dialog->folders, folder));
	g_object_set (label,
		      "margin-start", 20,
		      "margin-end", 20,
		      "margin-top", 10,
		      "margin-bottom", 10,
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      NULL);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_container_add (GTK_CONTAINER (box), label);
	image = gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU);
	gtk_widget_set_no_show_all (image, TRUE);
	gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
	gtk_widget_set_halign (image, GTK_ALIGN_END);
	gtk_widget_set_margin_start (image, 20);
	gtk_widget_set_margin_end (image, 20);
	gtk_container_add (GTK_CONTAINER (box), image);

	row = gtk_list_box_row_new ();
	gtk_container_add (GTK_CONTAINER (row), box);

	gtk_widget_show_all (row);

	g_object_set_data (G_OBJECT (row), "image", image);
	g_object_set_data_full (G_OBJECT (row), "folder", g_strdup (folder), g_free);

	gtk_size_group_add_widget (dialog->rows, row);

	return row;	
}

static void
set_apps (GsAppFolderDialog *dialog, GList *apps)
{
       dialog->apps = g_list_copy (apps);
}

static void
populate_list (GsAppFolderDialog *dialog)
{
	guint i;
	g_autofree gchar **folders = NULL;

	folders = gs_folders_get_nonempty_folders (dialog->folders);
	for (i = 0; folders[i]; i++) {
		gtk_list_box_insert (GTK_LIST_BOX (dialog->app_folder_list),
				     create_row (dialog, folders[i]), -1);
	}
}

static void
select_row (GtkListBox *list_box, GtkListBoxRow *row, GsAppFolderDialog *dialog)
{
	GtkWidget *image;

	if (dialog->selected_row) {
		image = g_object_get_data (G_OBJECT (dialog->selected_row), "image");
		gtk_widget_hide (image);
		gtk_widget_set_sensitive (dialog->done_button, FALSE);
	}

	dialog->selected_row = row;

	if (dialog->selected_row) {
		image = g_object_get_data (G_OBJECT (dialog->selected_row), "image");
		gtk_widget_show (image);
		gtk_widget_set_sensitive (dialog->done_button, TRUE);
	}
}

static void
row_activated (GtkListBox *list_box, GtkListBoxRow *row, GsAppFolderDialog *dialog)
{
	if (row == dialog->new_folder_button) {
		new_folder_cb (dialog);
	} else {
		select_row (list_box, row, dialog);
	}
}

static void
update_sensitive (GObject *entry, GParamSpec *pspec, GtkWidget *button)
{
	guint16 len;

	len = gtk_entry_get_text_length (GTK_ENTRY (entry));
	gtk_widget_set_sensitive (button, len > 0);
}

static void
activate_entry (GtkEntry *entry, GtkWidget *button)
{
	gtk_widget_activate (button);
}

static void
add_folder_add (GtkButton *button, GsAppFolderDialog *dialog)
{
	const gchar *folder;

	gtk_widget_hide (dialog->new_folder_popover);

	folder = gtk_entry_get_text (GTK_ENTRY (dialog->new_folder_entry));
	if (folder[0] != '\0') {
		const gchar *id;
		GtkWidget *row;
		id = gs_folders_add_folder (dialog->folders, folder);
		row = create_row (dialog, id);
		gtk_list_box_insert (GTK_LIST_BOX (dialog->app_folder_list),
				     row,
				     gtk_list_box_row_get_index (dialog->new_folder_button));
		select_row (GTK_LIST_BOX (dialog->app_folder_list),
			    GTK_LIST_BOX_ROW (row),
			    dialog);
	}
}

static void
add_folder_cancel (GtkButton *button, GsAppFolderDialog *dialog)
{
	gtk_widget_hide (dialog->new_folder_popover);
}

static void
create_folder_name_popover (GsAppFolderDialog *dialog)
{
	GtkWidget *grid, *label, *button;
	g_autofree gchar *title = NULL;

	dialog->new_folder_popover = gtk_popover_new (GTK_WIDGET (dialog->new_folder_button));
	gtk_popover_set_position (GTK_POPOVER (dialog->new_folder_popover), GTK_POS_TOP);

	grid = gtk_grid_new ();
	gtk_grid_set_column_homogeneous (GTK_GRID (grid), TRUE);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 12);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
	g_object_set (grid, "margin", 12, NULL);
	gtk_container_add (GTK_CONTAINER (dialog->new_folder_popover), grid);

	title = g_strdup_printf ("<b>%s</b>", _("Folder Name"));
	label = gtk_label_new (title);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 2, 1);

	dialog->new_folder_entry = gtk_entry_new ();
	gtk_entry_set_width_chars (GTK_ENTRY (dialog->new_folder_entry), 25);
	gtk_widget_set_halign (dialog->new_folder_entry, GTK_ALIGN_FILL);
	gtk_grid_attach (GTK_GRID (grid), dialog->new_folder_entry, 0, 1, 2, 1);

	button = gtk_button_new_with_mnemonic (_("_Cancel"));
	g_signal_connect (button, "clicked", G_CALLBACK (add_folder_cancel), dialog);
	gtk_widget_set_halign (button, GTK_ALIGN_FILL);
	gtk_grid_attach (GTK_GRID (grid), button, 0, 2, 1, 1);
	
	button = gtk_button_new_with_mnemonic (_("_Add"));
	g_signal_connect (button, "clicked", G_CALLBACK (add_folder_add), dialog);
	gtk_widget_set_halign (button, GTK_ALIGN_FILL);
	gtk_grid_attach (GTK_GRID (grid), button, 1, 2, 1, 1);
	gtk_style_context_add_class (gtk_widget_get_style_context (button), GTK_STYLE_CLASS_SUGGESTED_ACTION);

	gtk_widget_set_sensitive (button, FALSE);
	g_signal_connect (dialog->new_folder_entry, "notify::text", G_CALLBACK (update_sensitive), button);
	g_signal_connect (dialog->new_folder_entry, "activate", G_CALLBACK (activate_entry), button);

	gtk_widget_show_all (grid);
}

static void
add_new_folder_row (GsAppFolderDialog *dialog)
{
	GtkWidget *row, *button;

	button = gtk_image_new_from_icon_name ("list-add-symbolic", GTK_ICON_SIZE_MENU);
	gtk_widget_set_halign (button, GTK_ALIGN_FILL);
	gtk_widget_set_valign (button, GTK_ALIGN_FILL);
	row = gtk_list_box_row_new ();
	dialog->new_folder_button = GTK_LIST_BOX_ROW (row);
	gtk_container_add (GTK_CONTAINER (row), button);
	gtk_list_box_insert (GTK_LIST_BOX (dialog->app_folder_list), row, -1);
	gtk_size_group_add_widget (dialog->rows, row);

	g_signal_connect (dialog->app_folder_list, "row-activated",
			  G_CALLBACK (row_activated), dialog);

	gtk_widget_show_all (row);

	create_folder_name_popover (dialog);
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
