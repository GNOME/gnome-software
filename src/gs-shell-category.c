/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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
 * GNU General Public License for more category.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#include "gs-utils.h"
#include "gs-app-tile.h"
#include "gs-shell-category.h"

struct GsShellCategoryPrivate {
	GsPluginLoader	*plugin_loader;
	GtkBuilder	*builder;
	GCancellable	*cancellable;
	GsShell		*shell;
	GsCategory	*category;
	GtkWidget	*col0_placeholder;
	GtkWidget	*col1_placeholder;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsShellCategory, gs_shell_category, G_TYPE_OBJECT)

void
gs_shell_category_refresh (GsShellCategory *shell)
{
	GsShellCategoryPrivate *priv = shell->priv;
	GtkWidget *widget;
	GsCategory *category;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
	gtk_widget_show (widget);
	category = priv->category;
	if (gs_category_get_parent (category))
		category = gs_category_get_parent (category);
	gtk_label_set_label (GTK_LABEL (widget), gs_category_get_name (category));
}

static void
app_tile_clicked (GsAppTile *tile, gpointer data)
{
	GsShellCategory *shell = GS_SHELL_CATEGORY (data);
	GsApp *app;

	app = gs_app_tile_get_app (tile);
	gs_shell_show_app (shell->priv->shell, app);
}

/**
 * gs_shell_category_get_apps_cb:
 **/
static void
gs_shell_category_get_apps_cb (GObject *source_object,
			       GAsyncResult *res,
			       gpointer user_data)
{
	GError *error = NULL;
	gint i = 0;
	GList *l;
	GList *list;
	GsApp *app;
	GtkWidget *grid;
	GtkWidget *tile;
	GsShellCategory *shell = GS_SHELL_CATEGORY (user_data);
	GsShellCategoryPrivate *priv = shell->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);

	list = gs_plugin_loader_get_category_apps_finish (plugin_loader,
							  res,
							  &error);
	if (list == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_warning ("failed to get apps for category apps: %s", error->message);
		}
		g_error_free (error);
		goto out;
	}
	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
	gtk_grid_remove_column (GTK_GRID (grid), 1);
	gtk_grid_remove_column (GTK_GRID (grid), 0);

	for (l = list, i = 0; l != NULL; l = l->next, i++) {
		app = GS_APP (l->data);
		tile = gs_app_tile_new (app);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), shell);
		gtk_grid_attach (GTK_GRID (grid), tile, (i % 2), i / 2, 1, 1);
	}

	if (i == 1)
		gtk_grid_attach (GTK_GRID (grid), priv->col1_placeholder, 1, 0, 1, 1);

out:
	gs_plugin_list_free (list);

}

static void
gs_shell_category_populate_filtered (GsShellCategory *shell)
{
	GsShellCategoryPrivate *priv = shell->priv;
	GtkWidget *grid;
	GsCategory *parent;
	GtkWidget *tile;
	guint i;

	g_cancellable_cancel (priv->cancellable);
	g_cancellable_reset (priv->cancellable);

	parent = gs_category_get_parent (priv->category);
	if (parent == NULL) {
		g_debug ("search using %s",
			 gs_category_get_id (priv->category));
	} else {
		g_debug ("search using %s/%s",
			 gs_category_get_id (parent),
			 gs_category_get_id (priv->category));
	}

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
	gtk_grid_remove_column (GTK_GRID (grid), 1);
	gtk_grid_remove_column (GTK_GRID (grid), 0);

	for (i = 0; i < MIN (30, gs_category_get_size (priv->category)); i++) {
		tile = gs_app_tile_new (NULL);
		gtk_grid_attach (GTK_GRID (grid), tile, (i % 2), i / 2, 1, 1);
	}

	gtk_grid_attach (GTK_GRID (grid), priv->col0_placeholder, 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), priv->col1_placeholder, 1, 0, 1, 1);

	gs_plugin_loader_get_category_apps_async (priv->plugin_loader,
						  priv->category,
						  GS_PLUGIN_REFINE_FLAGS_DEFAULT |
						  GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
						  priv->cancellable,
						  gs_shell_category_get_apps_cb,
						  shell);
}

static void
add_separator (GtkListBoxRow *row, GtkListBoxRow *before, gpointer data)
{
	if (!before) {
		return;
	}

	gtk_list_box_row_set_header (row, gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
}

static void
filter_selected (GtkListBox *filters, GtkListBoxRow *row, gpointer data)
{
	GsShellCategory *shell = GS_SHELL_CATEGORY (data);
	GsCategory *category;

	if (row == NULL)
		return;

	category = g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), "category");
	g_clear_object (&shell->priv->category);
	shell->priv->category = g_object_ref (category);
	gs_shell_category_populate_filtered (shell);
}

static void
gs_shell_category_create_filter_list (GsShellCategory *shell, GsCategory *category, GsCategory *subcategory)
{
	GsShellCategoryPrivate *priv = shell->priv;
	GtkWidget *grid;
	GtkWidget *list_box;
	GtkWidget *row;
	GList *list, *l;
	GsCategory *s;
	GtkWidget *frame, *swin;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
	gs_container_remove_all (GTK_CONTAINER (grid));

	frame = GTK_WIDGET (gtk_builder_get_object (priv->builder, "frame_filter"));
	swin = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_filter"));
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (swin), GTK_SHADOW_NONE);

	list = gs_category_get_subcategories (category);
	if (!list)
		return;

	gtk_grid_attach (GTK_GRID (grid), priv->col0_placeholder, 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), priv->col1_placeholder, 1, 0, 1, 1);

	list_box = GTK_WIDGET (gtk_builder_get_object (priv->builder, "listbox_filter"));
	gs_container_remove_all (GTK_CONTAINER (list_box));

	for  (l = list; l; l = l->next) {
		s = l->data;
		if (gs_category_get_size (s) < 1)
			continue;
		row = gtk_label_new (gs_category_get_name (s));
		g_object_set_data_full (G_OBJECT (row), "category", g_object_ref (s), g_object_unref);
		g_object_set (row, "xalign", 0.0, "margin", 6, NULL);
		gtk_widget_show (row);
		gtk_list_box_insert (GTK_LIST_BOX (list_box), row, -1);
		if (subcategory == s)
			gtk_list_box_select_row (GTK_LIST_BOX (list_box), GTK_LIST_BOX_ROW (gtk_widget_get_parent (row)));
	}
	g_list_free (list);
}

void
gs_shell_category_set_category (GsShellCategory *shell, GsCategory *category)
{
	GsShellCategoryPrivate *priv = shell->priv;
	GsCategory *sub;
	GsCategory *selected = NULL;
	GList *list;
	GList *l;

	/* this means we've come from the app-view -> back */
	if (gs_category_get_parent (category) != NULL)
		return;

	/* select favourites by default */
	list = gs_category_get_subcategories (category);
	for (l = list; l != NULL; l = l->next) {
		sub = GS_CATEGORY (l->data);
		if (g_strcmp0 (gs_category_get_id (sub), "favourites") == 0) {
			selected = sub;
			break;
		}
	}

	/* okay, no favourites, so just select the first entry */
	if (selected == NULL && list != NULL)
		selected = GS_CATEGORY (list->data);

	/* save this */
	g_clear_object (&priv->category);
	priv->category = g_object_ref (selected);

	/* find apps in this group */
	gs_shell_category_create_filter_list (shell, category, selected);
	g_list_free (list);
}

GsCategory *
gs_shell_category_get_category (GsShellCategory *shell)
{
	return shell->priv->category;
}

static void
gs_shell_category_init (GsShellCategory *shell)
{
	GsShellCategoryPrivate *priv;

	priv = gs_shell_category_get_instance_private (shell);
	shell->priv = priv;

	priv->col0_placeholder = g_object_ref_sink (gtk_label_new (""));
	priv->col1_placeholder = g_object_ref_sink (gtk_label_new (""));

	gtk_widget_show (priv->col0_placeholder);
	gtk_widget_show (priv->col1_placeholder);
}

static void
gs_shell_category_finalize (GObject *object)
{
	GsShellCategory *shell = GS_SHELL_CATEGORY (object);
	GsShellCategoryPrivate *priv = shell->priv;

	g_clear_object (&priv->builder);
	g_clear_object (&priv->category);
	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->cancellable);
	g_clear_object (&priv->col0_placeholder);
	g_clear_object (&priv->col1_placeholder);

	G_OBJECT_CLASS (gs_shell_category_parent_class)->finalize (object);
}

static void
gs_shell_category_class_init (GsShellCategoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gs_shell_category_finalize;
}

static void
scrollbar_mapped_cb (GtkWidget *sb, GtkScrolledWindow *swin)
{
	GtkWidget *frame;

	frame = gtk_bin_get_child (GTK_BIN (gtk_bin_get_child (GTK_BIN (swin))));
	if (gtk_widget_get_mapped (GTK_WIDGET (sb))) {
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_IN);
		gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	}
	else {
		gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_NONE);
	}
}

static gboolean
key_event (GtkWidget *listbox, GdkEvent *event, GsShellCategory *shell)
{
	guint keyval;
	GtkWidget *sw;
	GtkWidget *grid;
	gboolean handled;

	if (!gdk_event_get_keyval (event, &keyval))
		return FALSE;

	sw = GTK_WIDGET (gtk_builder_get_object (shell->priv->builder, "scrolledwindow_category"));
	grid = GTK_WIDGET (gtk_builder_get_object (shell->priv->builder, "category_detail_grid"));
	if (keyval == GDK_KEY_Page_Up ||
	    keyval == GDK_KEY_KP_Page_Up)
		g_signal_emit_by_name (sw, "scroll-child",
				       GTK_SCROLL_PAGE_UP, FALSE, &handled);
	else if (keyval == GDK_KEY_Page_Down ||
	    	 keyval == GDK_KEY_KP_Page_Down)
		g_signal_emit_by_name (sw, "scroll-child",
				       GTK_SCROLL_PAGE_DOWN, FALSE, &handled);
	else if (keyval == GDK_KEY_Tab ||
		 keyval == GDK_KEY_KP_Tab)
		gtk_widget_child_focus (grid, GTK_DIR_TAB_FORWARD);
	else
		return FALSE;

	return TRUE;
}

void
gs_shell_category_setup (GsShellCategory *shell_category,
			 GsShell *shell,
			 GsPluginLoader *plugin_loader,
			 GtkBuilder *builder,
			 GCancellable *cancellable)
{
	GsShellCategoryPrivate *priv = shell_category->priv;
	GtkWidget *widget;
	GtkWidget *sw;
	GtkAdjustment *adj;

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_cancellable_new ();
	priv->shell = shell;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "listbox_filter"));
	g_signal_connect (widget, "row-selected", G_CALLBACK (filter_selected), shell_category);
	gtk_list_box_set_header_func (GTK_LIST_BOX (widget), add_separator, NULL, NULL);

	sw = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_filter"));
	widget = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (sw));
	g_signal_connect (widget, "map", G_CALLBACK (scrollbar_mapped_cb), sw);
	g_signal_connect (widget, "unmap", G_CALLBACK (scrollbar_mapped_cb), sw);

	sw = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_category"));
	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (sw));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (widget), adj);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "listbox_filter"));
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (key_event), shell_category);
}

GsShellCategory *
gs_shell_category_new (void)
{
	GsShellCategory *shell;

	shell = g_object_new (GS_TYPE_SHELL_CATEGORY, NULL);

	return shell;
}

/* vim: set noexpandtab: */
