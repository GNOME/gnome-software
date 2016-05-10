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

#include "gs-common.h"
#include "gs-app-tile.h"
#include "gs-shell-category.h"

struct _GsShellCategory
{
	GsPage		 parent_instance;

	GsPluginLoader	*plugin_loader;
	GtkBuilder	*builder;
	GCancellable	*cancellable;
	GsShell		*shell;
	GsCategory	*category;

	GtkWidget	*infobar_category_shell_extensions;
	GtkWidget	*button_category_shell_extensions;
	GtkWidget	*category_detail_box;
	GtkWidget	*listbox_filter;
	GtkWidget	*scrolledwindow_category;
	GtkWidget	*scrolledwindow_filter;
};

G_DEFINE_TYPE (GsShellCategory, gs_shell_category, GS_TYPE_PAGE)

/**
 * gs_shell_category_reload:
 */
void
gs_shell_category_reload (GsShellCategory *self)
{
}

static void
gs_shell_category_switch_to (GsPage *page, gboolean scroll_up)
{
	GsShellCategory *self = GS_SHELL_CATEGORY (page);
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "application_details_header"));
	gtk_widget_show (widget);
	gtk_label_set_label (GTK_LABEL (widget), gs_category_get_name (self->category));
}

static void
app_tile_clicked (GsAppTile *tile, gpointer data)
{
	GsShellCategory *self = GS_SHELL_CATEGORY (data);
	GsApp *app;

	app = gs_app_tile_get_app (tile);
	gs_shell_show_app (self->shell, app);
}

/**
 * gs_shell_category_get_apps_cb:
 **/
static void
gs_shell_category_get_apps_cb (GObject *source_object,
			       GAsyncResult *res,
			       gpointer user_data)
{
	gint i = 0;
	GList *l;
	GsApp *app;
	GtkWidget *tile;
	GsShellCategory *self = GS_SHELL_CATEGORY (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* show an empty space for no results */
	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));

	list = gs_plugin_loader_get_category_apps_finish (plugin_loader,
							  res,
							  &error);
	if (list == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to get apps for category apps: %s", error->message);
		return;
	}

	for (l = list, i = 0; l != NULL; l = l->next, i++) {
		app = GS_APP (l->data);
		tile = gs_app_tile_new (app);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (self->category_detail_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}
}

static void
gs_shell_category_populate_filtered (GsShellCategory *self, GsCategory *subcategory)
{
	GtkWidget *tile;
	guint i, count;

	g_assert (subcategory != NULL);

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_object_unref (self->cancellable);
	}
	self->cancellable = g_cancellable_new ();

	g_debug ("search using %s/%s",
	         gs_category_get_id (self->category),
	         gs_category_get_id (subcategory));

	/* show the shell extensions header */
	if (g_strcmp0 (gs_category_get_id (self->category), "Addons") == 0 &&
	    g_strcmp0 (gs_category_get_id (subcategory), "ShellExtensions") == 0) {
		gtk_widget_set_visible (self->infobar_category_shell_extensions, TRUE);
	} else {
		gtk_widget_set_visible (self->infobar_category_shell_extensions, FALSE);
	}

	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));
	count = MIN(30, gs_category_get_size (subcategory));
	for (i = 0; i < count; i++) {
		tile = gs_app_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (self->category_detail_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	gs_plugin_loader_get_category_apps_async (self->plugin_loader,
						  subcategory,
						  GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
						  GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
						  GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
						  self->cancellable,
						  gs_shell_category_get_apps_cb,
						  self);
}

static void
filter_selected (GtkListBox *filters, GtkListBoxRow *row, gpointer data)
{
	GsShellCategory *self = GS_SHELL_CATEGORY (data);
	GsCategory *category;

	if (row == NULL)
		return;

	category = g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), "category");
	gs_shell_category_populate_filtered (self, category);
}

static void
gs_shell_category_create_filter_list (GsShellCategory *self,
				      GsCategory *category,
				      GsCategory *subcategory)
{
	GtkWidget *row;
	GList *l;
	GsCategory *s;
	g_autoptr(GList) list = NULL;

	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));

	list = gs_category_get_subcategories (category);
	if (!list)
		return;

	gs_container_remove_all (GTK_CONTAINER (self->listbox_filter));

	for  (l = list; l; l = l->next) {
		s = l->data;
		if (gs_category_get_size (s) < 1)
			continue;
		row = gtk_label_new (gs_category_get_name (s));
		g_object_set_data_full (G_OBJECT (row), "category", g_object_ref (s), g_object_unref);
		g_object_set (row, "xalign", 0.0, "margin", 10, NULL);
		gtk_widget_show (row);
		gtk_list_box_insert (GTK_LIST_BOX (self->listbox_filter), row, -1);
		if (subcategory == s)
			gtk_list_box_select_row (GTK_LIST_BOX (self->listbox_filter), GTK_LIST_BOX_ROW (gtk_widget_get_parent (row)));
	}
}

void
gs_shell_category_set_category (GsShellCategory *self, GsCategory *category)
{
	GsCategory *sub;
	GsCategory *selected = NULL;
	GList *l;
	g_autoptr(GList) list = NULL;

	/* this means we've come from the app-view -> back */
	if (self->category == category)
		return;

	/* save this */
	g_clear_object (&self->category);
	self->category = g_object_ref (category);

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

	/* find apps in this group */
	gs_shell_category_create_filter_list (self, category, selected);
}

GsCategory *
gs_shell_category_get_category (GsShellCategory *self)
{
	return self->category;
}

static void
gs_shell_category_init (GsShellCategory *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_shell_category_dispose (GObject *object)
{
	GsShellCategory *self = GS_SHELL_CATEGORY (object);

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_clear_object (&self->cancellable);
	}

	g_clear_object (&self->builder);
	g_clear_object (&self->category);
	g_clear_object (&self->plugin_loader);

	G_OBJECT_CLASS (gs_shell_category_parent_class)->dispose (object);
}

static void
gs_shell_category_class_init (GsShellCategoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_category_dispose;
	page_class->switch_to = gs_shell_category_switch_to;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-category.ui");

	gtk_widget_class_bind_template_child (widget_class, GsShellCategory, category_detail_box);
	gtk_widget_class_bind_template_child (widget_class, GsShellCategory, infobar_category_shell_extensions);
	gtk_widget_class_bind_template_child (widget_class, GsShellCategory, button_category_shell_extensions);
	gtk_widget_class_bind_template_child (widget_class, GsShellCategory, listbox_filter);
	gtk_widget_class_bind_template_child (widget_class, GsShellCategory, scrolledwindow_category);
	gtk_widget_class_bind_template_child (widget_class, GsShellCategory, scrolledwindow_filter);
}

static gboolean
key_event (GtkWidget *listbox, GdkEvent *event, GsShellCategory *self)
{
	guint keyval;
	gboolean handled;

	if (!gdk_event_get_keyval (event, &keyval))
		return FALSE;

	if (keyval == GDK_KEY_Page_Up ||
	    keyval == GDK_KEY_KP_Page_Up)
		g_signal_emit_by_name (self->scrolledwindow_category, "scroll-child",
				       GTK_SCROLL_PAGE_UP, FALSE, &handled);
	else if (keyval == GDK_KEY_Page_Down ||
	    	 keyval == GDK_KEY_KP_Page_Down)
		g_signal_emit_by_name (self->scrolledwindow_category, "scroll-child",
				       GTK_SCROLL_PAGE_DOWN, FALSE, &handled);
	else if (keyval == GDK_KEY_Tab ||
		 keyval == GDK_KEY_KP_Tab)
		gtk_widget_child_focus (self->category_detail_box, GTK_DIR_TAB_FORWARD);
	else
		return FALSE;

	return TRUE;
}

static void
button_shell_extensions_cb (GtkButton *button, GsShellCategory *self)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	const gchar *argv[] = { "gnome-shell-extension-prefs", NULL };
	ret = g_spawn_async (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
			     NULL, NULL, NULL, &error);
	if (!ret)
		g_warning ("failed to exec %s: %s", argv[0], error->message);
}

void
gs_shell_category_setup (GsShellCategory *self,
			 GsShell *shell,
			 GsPluginLoader *plugin_loader,
			 GtkBuilder *builder,
			 GCancellable *cancellable)
{
	GtkAdjustment *adj;

	self->plugin_loader = g_object_ref (plugin_loader);
	self->builder = g_object_ref (builder);
	self->shell = shell;

	g_signal_connect (self->listbox_filter, "row-selected", G_CALLBACK (filter_selected), self);

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_category));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (self->category_detail_box), adj);

	g_signal_connect (self->listbox_filter, "key-press-event",
			  G_CALLBACK (key_event), self);

	g_signal_connect (self->button_category_shell_extensions, "clicked",
			  G_CALLBACK (button_shell_extensions_cb), self);

	/* chain up */
	gs_page_setup (GS_PAGE (self),
	               shell,
	               plugin_loader,
	               cancellable);
}

GsShellCategory *
gs_shell_category_new (void)
{
	GsShellCategory *self;
	self = g_object_new (GS_TYPE_SHELL_CATEGORY, NULL);
	return self;
}

/* vim: set noexpandtab: */
