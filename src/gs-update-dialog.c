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
#include <packagekit-glib2/packagekit.h>

#include "gs-update-dialog.h"
#include "gs-app-row.h"
#include "gs-markdown.h"
#include "gs-offline-updates.h"
#include "gs-update-list.h"
#include "gs-utils.h"

typedef struct {
	gchar		*title;
	gchar		*stack_page;
	GtkWidget	*focus;
} BackEntry;

struct _GsUpdateDialogPrivate
{
	GQueue		*back_entry_stack;
	GtkWidget	*box_header;
	GtkWidget	*button_back;
	GtkWidget	*image_icon;
	GtkWidget	*label_details;
	GtkWidget	*label_name;
	GtkWidget	*label_summary;
	GtkWidget	*list_box;
	GtkWidget	*list_box_installed_updates;
	GtkWidget	*scrolledwindow;
	GtkWidget	*scrolledwindow_details;
	GtkWidget	*stack;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsUpdateDialog, gs_update_dialog, GTK_TYPE_DIALOG)

static void
save_back_entry (GsUpdateDialog *dialog)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);
	BackEntry *entry;

	entry = g_slice_new0 (BackEntry);
	entry->stack_page = g_strdup (gtk_stack_get_visible_child_name (GTK_STACK (priv->stack)));
	entry->title = g_strdup (gtk_window_get_title (GTK_WINDOW (dialog)));

	entry->focus = gtk_window_get_focus (GTK_WINDOW (dialog));
	if (entry->focus != NULL)
		g_object_add_weak_pointer (G_OBJECT (entry->focus),
		                           (gpointer *) &entry->focus);

	g_queue_push_head (priv->back_entry_stack, entry);
}

static void
back_entry_free (BackEntry *entry)
{
	if (entry->focus != NULL)
		g_object_remove_weak_pointer (G_OBJECT (entry->focus),
		                              (gpointer *) &entry->focus);
	g_free (entry->stack_page);
	g_free (entry->title);
	g_slice_free (BackEntry, entry);
}

static void
set_updates_description_ui (GsUpdateDialog *dialog, GsApp *app)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);
	GsAppKind kind;
	GsMarkdown *markdown;
	gchar *tmp;
	gchar *update_desc;
	const gchar *update_details;

	/* set window title */
	kind = gs_app_get_kind (app);
	if (kind == GS_APP_KIND_OS_UPDATE) {
		gtk_window_set_title (GTK_WINDOW (dialog), gs_app_get_name (app));
	} else {
		tmp = g_strdup_printf ("%s %s",
		                       gs_app_get_source_default (app),
		                       gs_app_get_update_version (app));
		gtk_window_set_title (GTK_WINDOW (dialog), tmp);
		g_free (tmp);
	}

	/* get the update description */
	update_details = gs_app_get_update_details (app);
	if (update_details == NULL || update_details[0] == '\0') {
		/* TRANSLATORS: this is where the packager did not write a
		 * description for the update */
		update_desc = g_strdup (_("No update description available."));
	} else {
		markdown = gs_markdown_new (GS_MARKDOWN_OUTPUT_PANGO);
		gs_markdown_set_smart_quoting (markdown, FALSE);
		gs_markdown_set_autocode (markdown, TRUE);
		update_desc = gs_markdown_parse (markdown, update_details);
		g_object_unref (markdown);
	}

	/* set update header */
	gtk_widget_set_visible (priv->box_header, kind == GS_APP_KIND_NORMAL || kind == GS_APP_KIND_SYSTEM);
	gtk_label_set_markup (GTK_LABEL (priv->label_details), update_desc);
	gs_image_set_from_pixbuf (GTK_IMAGE (priv->image_icon), gs_app_get_pixbuf (app));
	gtk_label_set_label (GTK_LABEL (priv->label_name), gs_app_get_name (app));
	gtk_label_set_label (GTK_LABEL (priv->label_summary), gs_app_get_summary (app));
	g_free (update_desc);

	/* show the back button if needed */
	gtk_widget_set_visible (priv->button_back, !g_queue_is_empty (priv->back_entry_stack));
}

static void
row_activated_cb (GtkListBox *list_box,
                  GtkListBoxRow *row,
                  GsUpdateDialog *dialog)
{
	GsApp *app;

	app = GS_APP (g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), "app"));

	/* save the current stack state for the back button */
	save_back_entry (dialog);

	/* setup package view */
	gs_update_dialog_show_update_details (dialog, app);
}

static void
installed_updates_row_activated_cb (GtkListBox *list_box,
                                    GtkListBoxRow *row,
                                    GsUpdateDialog *dialog)
{
	GsApp *app;

	app = gs_app_row_get_app (GS_APP_ROW (row));

	/* save the current stack state for the back button */
	save_back_entry (dialog);

	gs_update_dialog_show_update_details (dialog, app);
}

void
gs_update_dialog_show_installed_updates (GsUpdateDialog *dialog, GList *installed_updates)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);
	GList *l;
	guint64 time_updates_installed;

	/* TRANSLATORS: this is the title of the installed updates dialog window */
	gtk_window_set_title (GTK_WINDOW (dialog), _("Installed Updates"));

	time_updates_installed = pk_offline_get_results_mtime (NULL);
	if (time_updates_installed > 0) {
		GDateTime *date;
		GtkWidget *header;
		gchar *date_str;
		gchar *subtitle;

		date = g_date_time_new_from_unix_utc (time_updates_installed);
		date_str = g_date_time_format (date, "%x");
		g_date_time_unref (date);

		/* TRANSLATORS: this is the subtitle of the installed updates dialog window */
		subtitle = g_strdup_printf (_("Installed on %s"), date_str);
		header = gtk_dialog_get_header_bar (GTK_DIALOG (dialog));
		gtk_header_bar_set_subtitle (GTK_HEADER_BAR (header), subtitle);

		g_free (date_str);
	}

	gtk_widget_set_visible (priv->button_back, !g_queue_is_empty (priv->back_entry_stack));
	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "installed-updates-list");

	gs_container_remove_all (GTK_CONTAINER (priv->list_box_installed_updates));
	for (l = installed_updates; l != NULL; l = l->next) {
		gs_update_list_add_app (GS_UPDATE_LIST (priv->list_box_installed_updates),
		                        GS_APP (l->data));
	}
}

void
gs_update_dialog_show_update_details (GsUpdateDialog *dialog, GsApp *app)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);
	GsApp *app_related;
	GsAppKind kind;
	const gchar *sort;

	kind = gs_app_get_kind (app);

	/* set update header */
	set_updates_description_ui (dialog, app);

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
			              "margin-start", 20,
			              "margin-end", 20,
			              "margin-top", 6,
			              "margin-bottom", 6,
			              "xalign", 0.0,
			              NULL);
			gtk_widget_set_halign (label, GTK_ALIGN_START);
			gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
			gtk_box_pack_start (GTK_BOX (row), label, TRUE, TRUE, 0);
			label = gtk_label_new (gs_app_get_update_version (app_related));
			g_object_set (label,
			              "margin-start", 20,
			              "margin-end", 20,
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

static void
list_header_func (GtkListBoxRow *row,
                  GtkListBoxRow *before,
                  gpointer user_data)
{
	GtkWidget *header = NULL;
	if (before != NULL)
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
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
	BackEntry *entry;

	/* return to the previous view */
	entry = g_queue_pop_head (priv->back_entry_stack);
	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), entry->stack_page);
	gtk_window_set_title (GTK_WINDOW (dialog), entry->title);
	if (entry->focus)
		gtk_widget_grab_focus (entry->focus);
	back_entry_free (entry);

	gtk_widget_set_visible (priv->button_back, !g_queue_is_empty (priv->back_entry_stack));
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
unset_focus (GtkWidget *widget)
{
	GtkWidget *focus;

	focus = gtk_window_get_focus (GTK_WINDOW (widget));
	if (GTK_IS_LABEL (focus))
		gtk_label_select_region (GTK_LABEL (focus), 0, 0);
	gtk_window_set_focus (GTK_WINDOW (widget), NULL);
}

static void
gs_update_dialog_finalize (GObject *object)
{
	GsUpdateDialog *dialog = GS_UPDATE_DIALOG (object);
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);

	if (priv->back_entry_stack != NULL) {
		g_queue_free_full (priv->back_entry_stack, (GDestroyNotify) back_entry_free);
		priv->back_entry_stack = NULL;
	}

	G_OBJECT_CLASS (gs_update_dialog_parent_class)->finalize (object);
}

static void
gs_update_dialog_init (GsUpdateDialog *dialog)
{
	GsUpdateDialogPrivate *priv = gs_update_dialog_get_instance_private (dialog);
	GtkWidget *scrollbar;

	gtk_widget_init_template (GTK_WIDGET (dialog));

	priv->back_entry_stack = g_queue_new ();

	g_signal_connect (GTK_LIST_BOX (priv->list_box), "row-activated",
	                  G_CALLBACK (row_activated_cb), dialog);
	gtk_list_box_set_header_func (GTK_LIST_BOX (priv->list_box),
	                              list_header_func,
	                              dialog, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->list_box),
	                            os_updates_sort_func,
	                            dialog, NULL);

	g_signal_connect (GTK_LIST_BOX (priv->list_box_installed_updates), "row-activated",
			  G_CALLBACK (installed_updates_row_activated_cb), dialog);

	g_signal_connect (priv->button_back, "clicked",
	                  G_CALLBACK (button_back_cb),
	                  dialog);

	g_signal_connect_after (dialog, "show", G_CALLBACK (unset_focus), NULL);

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

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-update-dialog.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, box_header);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, button_back);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, image_icon);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, label_details);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, label_name);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, label_summary);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, list_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpdateDialog, list_box_installed_updates);
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
