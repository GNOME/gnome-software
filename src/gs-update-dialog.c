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

struct _GsUpdateDialog
{
	GtkDialog	 parent_instance;

	GQueue		*back_entry_stack;
	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
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
	GtkWidget	*spinner;
	GtkWidget	*stack;
};

G_DEFINE_TYPE (GsUpdateDialog, gs_update_dialog, GTK_TYPE_DIALOG)

static void
save_back_entry (GsUpdateDialog *dialog)
{
	BackEntry *entry;

	entry = g_slice_new0 (BackEntry);
	entry->stack_page = g_strdup (gtk_stack_get_visible_child_name (GTK_STACK (dialog->stack)));
	entry->title = g_strdup (gtk_window_get_title (GTK_WINDOW (dialog)));

	entry->focus = gtk_window_get_focus (GTK_WINDOW (dialog));
	if (entry->focus != NULL)
		g_object_add_weak_pointer (G_OBJECT (entry->focus),
		                           (gpointer *) &entry->focus);

	g_queue_push_head (dialog->back_entry_stack, entry);
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
	GsAppKind kind;
	const GdkPixbuf *pixbuf;
	const gchar *update_details;
	g_autofree gchar *update_desc = NULL;

	/* set window title */
	kind = gs_app_get_kind (app);
	if (kind == GS_APP_KIND_OS_UPDATE) {
		gtk_window_set_title (GTK_WINDOW (dialog), gs_app_get_name (app));
	} else if (gs_app_get_source_default (app) != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = g_strdup_printf ("%s %s",
				       gs_app_get_source_default (app),
				       gs_app_get_update_version (app));
		gtk_window_set_title (GTK_WINDOW (dialog), tmp);
	} else {
		gtk_window_set_title (GTK_WINDOW (dialog),
				      gs_app_get_update_version (app));
	}

	/* this is set unconditionally just in case the output of the
	 * markdown->PangoMarkup parser is invalid */
	gtk_label_set_markup (GTK_LABEL (dialog->label_details),
			      /* TRANSLATORS: this is where the
			       * packager did not write a
			       * description for the update */
			      _("No update description available."));

	/* get the update description */
	update_details = gs_app_get_update_details (app);
	if (update_details != NULL) {
		g_autoptr(GsMarkdown) markdown = NULL;
		markdown = gs_markdown_new (GS_MARKDOWN_OUTPUT_PANGO);
		gs_markdown_set_smart_quoting (markdown, FALSE);
		gs_markdown_set_autocode (markdown, TRUE);
		update_desc = gs_markdown_parse (markdown, update_details);
	}

	/* set update header */
	gtk_widget_set_visible (dialog->box_header, kind == GS_APP_KIND_NORMAL || kind == GS_APP_KIND_SYSTEM);
	gtk_label_set_markup (GTK_LABEL (dialog->label_details), update_desc);
	gtk_label_set_label (GTK_LABEL (dialog->label_name), gs_app_get_name (app));
	gtk_label_set_label (GTK_LABEL (dialog->label_summary), gs_app_get_summary (app));

	pixbuf = gs_app_get_pixbuf (app);
	if (pixbuf != NULL)
		gs_image_set_from_pixbuf (GTK_IMAGE (dialog->image_icon), pixbuf);

	/* show the back button if needed */
	gtk_widget_set_visible (dialog->button_back, !g_queue_is_empty (dialog->back_entry_stack));
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

static void
get_installed_updates_cb (GsPluginLoader *plugin_loader,
                          GAsyncResult *res,
                          GsUpdateDialog *dialog)
{
	GList *l;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) error = NULL;

	gs_stop_spinner (GTK_SPINNER (dialog->spinner));

	/* get the results */
	list = gs_plugin_loader_get_updates_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_NO_RESULTS)) {
			g_debug ("no installed updates to show");
			gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "empty");
			return;
		} else if (g_error_matches (error,
					    G_IO_ERROR,
					    G_IO_ERROR_CANCELLED)) {
			/* This should only ever happen while the dialog is being closed */
			g_debug ("get installed updates cancelled");
			return;
		}

		g_warning ("failed to get installed updates: %s", error->message);
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "empty");
		return;
	}

	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "installed-updates-list");

	gs_container_remove_all (GTK_CONTAINER (dialog->list_box_installed_updates));
	for (l = list; l != NULL; l = l->next) {
		gs_update_list_add_app (GS_UPDATE_LIST (dialog->list_box_installed_updates),
					GS_APP (l->data));
	}
}

void
gs_update_dialog_show_installed_updates (GsUpdateDialog *dialog)
{
	guint64 refine_flags;
	guint64 time_updates_installed;

	/* TRANSLATORS: this is the title of the installed updates dialog window */
	gtk_window_set_title (GTK_WINDOW (dialog), _("Installed Updates"));

	time_updates_installed = pk_offline_get_results_mtime (NULL);
	if (time_updates_installed > 0) {
		GtkWidget *header;
		g_autoptr(GDateTime) date = NULL;
		g_autofree gchar *date_str = NULL;
		g_autofree gchar *subtitle = NULL;

		date = g_date_time_new_from_unix_utc (time_updates_installed);
		date_str = g_date_time_format (date, "%x");

		/* TRANSLATORS: this is the subtitle of the installed updates dialog window.
		   %s will be replaced by the date when the updates were installed.
		   The date format is defined by the locale's preferred date representation
		   ("%x" in strftime.) */
		subtitle = g_strdup_printf (_("Installed on %s"), date_str);
		header = gtk_dialog_get_header_bar (GTK_DIALOG (dialog));
		gtk_header_bar_set_subtitle (GTK_HEADER_BAR (header), subtitle);
	}

	gtk_widget_set_visible (dialog->button_back, !g_queue_is_empty (dialog->back_entry_stack));
	gs_start_spinner (GTK_SPINNER (dialog->spinner));
	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "spinner");

	refine_flags = GS_PLUGIN_REFINE_FLAGS_DEFAULT |
	               GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS |
	               GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
	               GS_PLUGIN_REFINE_FLAGS_USE_HISTORY;

	gs_plugin_loader_get_updates_async (dialog->plugin_loader,
	                                    refine_flags,
	                                    dialog->cancellable,
	                                    (GAsyncReadyCallback) get_installed_updates_cb,
	                                    dialog);
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

void
gs_update_dialog_show_update_details (GsUpdateDialog *dialog, GsApp *app)
{
	GsApp *app_related;
	GsAppKind kind;
	const gchar *sort;

	kind = gs_app_get_kind (app);

	/* set update header */
	set_updates_description_ui (dialog, app);

	/* workaround a gtk+ issue where the dialog comes up with a label selected,
	 * https://bugzilla.gnome.org/show_bug.cgi?id=734033 */
	unset_focus (GTK_WIDGET (dialog));

	/* set update description */
	if (kind == GS_APP_KIND_OS_UPDATE) {
		GPtrArray *related;
		guint i;
		GtkWidget *row, *label;

		gs_container_remove_all (GTK_CONTAINER (dialog->list_box));
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
				      "margin-end", 0,
				      "margin-top", 6,
				      "margin-bottom", 6,
				      "xalign", 0.0,
				      "ellipsize", PANGO_ELLIPSIZE_END,
				      NULL);
			gtk_widget_set_halign (label, GTK_ALIGN_START);
			gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
			gtk_box_pack_start (GTK_BOX (row), label, TRUE, TRUE, 0);
			label = gtk_label_new (gs_app_get_update_version (app_related));
			g_object_set (label,
				      "margin-start", 0,
				      "margin-end", 20,
				      "margin-top", 6,
				      "margin-bottom", 6,
				      "xalign", 1.0,
				      "ellipsize", PANGO_ELLIPSIZE_END,
				      NULL);
			gtk_widget_set_halign (label, GTK_ALIGN_END);
			gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
			gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
			gtk_widget_show_all (row);
			gtk_list_box_insert (GTK_LIST_BOX (dialog->list_box), row, -1);
		}
		gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "os-update-list");
		gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_NONE);
	} else {
		gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "package-details");
		gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_NONE);
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
	BackEntry *entry;

	/* return to the previous view */
	entry = g_queue_pop_head (dialog->back_entry_stack);

	gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT);
	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), entry->stack_page);
	gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_NONE);

	gtk_window_set_title (GTK_WINDOW (dialog), entry->title);
	if (entry->focus)
		gtk_widget_grab_focus (entry->focus);
	back_entry_free (entry);

	gtk_widget_set_visible (dialog->button_back, !g_queue_is_empty (dialog->back_entry_stack));
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

static gboolean
key_press_event (GsUpdateDialog *dialog, GdkEventKey *event)
{
	GdkKeymap *keymap;
	GdkModifierType state;
	gboolean is_rtl;

	if (!gtk_widget_is_visible (dialog->button_back) || !gtk_widget_is_sensitive (dialog->button_back))
		return GDK_EVENT_PROPAGATE;

	state = event->state;
	keymap = gdk_keymap_get_default ();
	gdk_keymap_add_virtual_modifiers (keymap, &state);
	state = state & gtk_accelerator_get_default_mod_mask ();
	is_rtl = gtk_widget_get_direction (dialog->button_back) == GTK_TEXT_DIR_RTL;

	if ((!is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Left) ||
	    (is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Right) ||
	    event->keyval == GDK_KEY_Back) {
		gtk_widget_activate (dialog->button_back);
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static gboolean
button_press_event (GsUpdateDialog *dialog, GdkEventButton *event)
{
	/* Mouse hardware back button is 8 */
	if (event->button != 8)
		return GDK_EVENT_PROPAGATE;

	if (!gtk_widget_is_visible (dialog->button_back) || !gtk_widget_is_sensitive (dialog->button_back))
		return GDK_EVENT_PROPAGATE;

	gtk_widget_activate (dialog->button_back);
	return GDK_EVENT_STOP;
}

static void
set_plugin_loader (GsUpdateDialog *dialog, GsPluginLoader *plugin_loader)
{
	dialog->plugin_loader = g_object_ref (plugin_loader);
}

static void
gs_update_dialog_dispose (GObject *object)
{
	GsUpdateDialog *dialog = GS_UPDATE_DIALOG (object);

	if (dialog->back_entry_stack != NULL) {
		g_queue_free_full (dialog->back_entry_stack, (GDestroyNotify) back_entry_free);
		dialog->back_entry_stack = NULL;
	}

	if (dialog->cancellable != NULL) {
		g_cancellable_cancel (dialog->cancellable);
		g_clear_object (&dialog->cancellable);
	}

	g_clear_object (&dialog->plugin_loader);

	G_OBJECT_CLASS (gs_update_dialog_parent_class)->dispose (object);
}

static void
gs_update_dialog_init (GsUpdateDialog *dialog)
{
	GtkWidget *scrollbar;

	gtk_widget_init_template (GTK_WIDGET (dialog));

	dialog->back_entry_stack = g_queue_new ();
	dialog->cancellable = g_cancellable_new ();

	g_signal_connect (GTK_LIST_BOX (dialog->list_box), "row-activated",
			  G_CALLBACK (row_activated_cb), dialog);
	gtk_list_box_set_header_func (GTK_LIST_BOX (dialog->list_box),
				      list_header_func,
				      dialog, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (dialog->list_box),
				    os_updates_sort_func,
				    dialog, NULL);

	g_signal_connect (GTK_LIST_BOX (dialog->list_box_installed_updates), "row-activated",
			  G_CALLBACK (installed_updates_row_activated_cb), dialog);

	g_signal_connect (dialog->button_back, "clicked",
			  G_CALLBACK (button_back_cb),
			  dialog);

	g_signal_connect_after (dialog, "show", G_CALLBACK (unset_focus), NULL);

	scrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (dialog->scrolledwindow_details));
	g_signal_connect (scrollbar, "map", G_CALLBACK (scrollbar_mapped_cb), dialog->scrolledwindow_details);
	g_signal_connect (scrollbar, "unmap", G_CALLBACK (scrollbar_mapped_cb), dialog->scrolledwindow_details);

	/* global keynav and mouse back button */
	g_signal_connect (dialog, "key-press-event",
			  G_CALLBACK (key_press_event), NULL);
	g_signal_connect (dialog, "button-press-event",
			  G_CALLBACK (button_press_event), NULL);
}

static void
gs_update_dialog_class_init (GsUpdateDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_update_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-update-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, box_header);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, button_back);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, image_icon);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, label_details);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, label_name);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, label_summary);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, list_box);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, list_box_installed_updates);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, scrolledwindow);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, scrolledwindow_details);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, spinner);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, stack);
}

GtkWidget *
gs_update_dialog_new (GsPluginLoader *plugin_loader)
{
	GsUpdateDialog *dialog;

	dialog = g_object_new (GS_TYPE_UPDATE_DIALOG,
	                       "use-header-bar", TRUE,
	                       NULL);
	set_plugin_loader (dialog, plugin_loader);

	return GTK_WIDGET (dialog);
}

/* vim: set noexpandtab: */
