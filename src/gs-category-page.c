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

#include "gs-common.h"
#include "gs-summary-tile.h"
#include "gs-category-page.h"

struct _GsCategoryPage
{
	GsPage		 parent_instance;

	GsPluginLoader	*plugin_loader;
	GtkBuilder	*builder;
	GCancellable	*cancellable;
	GsShell		*shell;
	GsCategory	*category;
	GsCategory	*subcategory;

	GtkWidget	*infobar_category_shell_extensions;
	GtkWidget	*button_category_shell_extensions;
	GtkWidget	*category_detail_box;
	GtkWidget	*listbox_filter;
	GtkWidget	*scrolledwindow_category;
	GtkWidget	*scrolledwindow_filter;
};

G_DEFINE_TYPE (GsCategoryPage, gs_category_page, GS_TYPE_PAGE)

static void
gs_category_page_switch_to (GsPage *page, gboolean scroll_up)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (page);
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "application_details_header"));
	gtk_widget_show (widget);
	gtk_label_set_label (GTK_LABEL (widget), gs_category_get_name (self->category));
}

static void
app_tile_clicked (GsAppTile *tile, gpointer data)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (data);
	GsApp *app;

	app = gs_app_tile_get_app (tile);
	gs_shell_show_app (self->shell, app);
}

static void
gs_category_page_get_apps_cb (GObject *source_object,
                              GAsyncResult *res,
                              gpointer user_data)
{
	guint i;
	GsApp *app;
	GtkWidget *tile;
	GsCategoryPage *self = GS_CATEGORY_PAGE (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* show an empty space for no results */
	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get apps for category apps: %s", error->message);
		return;
	}

	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		tile = gs_summary_tile_new (app);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (self->category_detail_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	/* seems a good place */
	gs_shell_profile_dump (self->shell);
}

static void
gs_category_page_reload (GsPage *page)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (page);
	GtkWidget *tile;
	guint i, count;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (self->subcategory == NULL)
		return;

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_object_unref (self->cancellable);
	}
	self->cancellable = g_cancellable_new ();

	g_debug ("search using %s/%s",
	         gs_category_get_id (self->category),
	         gs_category_get_id (self->subcategory));

	/* show the shell extensions header */
	if (g_strcmp0 (gs_category_get_id (self->category), "addons") == 0 &&
	    g_strcmp0 (gs_category_get_id (self->subcategory), "shell-extensions") == 0) {
		gtk_widget_set_visible (self->infobar_category_shell_extensions, TRUE);
	} else {
		gtk_widget_set_visible (self->infobar_category_shell_extensions, FALSE);
	}

	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));
	count = MIN(30, gs_category_get_size (self->subcategory));
	for (i = 0; i < count; i++) {
		tile = gs_summary_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (self->category_detail_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
					 "category", self->subcategory,
					 "failure-flags", GS_PLUGIN_FAILURE_FLAGS_USE_EVENTS,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader,
					    plugin_job,
					    self->cancellable,
					    gs_category_page_get_apps_cb,
					    self);
}

static void
gs_category_page_populate_filtered (GsCategoryPage *self, GsCategory *subcategory)
{
	g_assert (subcategory != NULL);
	g_set_object (&self->subcategory, subcategory);
	gs_category_page_reload (GS_PAGE (self));
}

static void
filter_selected (GtkListBox *filters, GtkListBoxRow *row, gpointer data)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (data);
	GsCategory *category;

	if (row == NULL)
		return;

	category = g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), "category");
	gs_category_page_populate_filtered (self, category);
}

static void
gs_category_page_create_filter_list (GsCategoryPage *self,
                                     GsCategory *category)
{
	GtkWidget *row;
	GsCategory *s;
	guint i;
	GPtrArray *children;
	GtkWidget *first_subcat = NULL;

	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));
	gs_container_remove_all (GTK_CONTAINER (self->listbox_filter));

	children = gs_category_get_children (category);
	for (i = 0; i < children->len; i++) {
		s = GS_CATEGORY (g_ptr_array_index (children, i));
		if (gs_category_get_size (s) < 1) {
			g_debug ("not showing %s/%s as no apps",
				 gs_category_get_id (category),
				 gs_category_get_id (s));
			continue;
		}
		row = gtk_label_new (gs_category_get_name (s));
		g_object_set_data_full (G_OBJECT (row), "category", g_object_ref (s), g_object_unref);
		g_object_set (row, "xalign", 0.0, "margin", 10, NULL);
		gtk_widget_show (row);
		gtk_list_box_insert (GTK_LIST_BOX (self->listbox_filter), row, -1);

		/* make sure the first subcategory gets selected */
		if (first_subcat == NULL)
		        first_subcat = row;
	}
	if (first_subcat != NULL)
		gtk_list_box_select_row (GTK_LIST_BOX (self->listbox_filter),
					 GTK_LIST_BOX_ROW (gtk_widget_get_parent (first_subcat)));
}

void
gs_category_page_set_category (GsCategoryPage *self, GsCategory *category)
{
	/* this means we've come from the app-view -> back */
	if (self->category == category)
		return;

	/* save this */
	g_clear_object (&self->category);
	self->category = g_object_ref (category);

	/* find apps in this group */
	gs_category_page_create_filter_list (self, category);
}

GsCategory *
gs_category_page_get_category (GsCategoryPage *self)
{
	return self->category;
}

static void
gs_category_page_init (GsCategoryPage *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_category_page_dispose (GObject *object)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (object);

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_clear_object (&self->cancellable);
	}

	g_clear_object (&self->builder);
	g_clear_object (&self->category);
	g_clear_object (&self->subcategory);
	g_clear_object (&self->plugin_loader);

	G_OBJECT_CLASS (gs_category_page_parent_class)->dispose (object);
}

static gboolean
key_event (GtkWidget *listbox, GdkEvent *event, GsCategoryPage *self)
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
button_shell_extensions_cb (GtkButton *button, GsCategoryPage *self)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	const gchar *argv[] = { "gnome-shell-extension-prefs", NULL };
	ret = g_spawn_async (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
			     NULL, NULL, NULL, &error);
	if (!ret)
		g_warning ("failed to exec %s: %s", argv[0], error->message);
}

static gboolean
gs_category_page_setup (GsPage *page,
                        GsShell *shell,
                        GsPluginLoader *plugin_loader,
                        GtkBuilder *builder,
                        GCancellable *cancellable,
                        GError **error)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (page);
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
	return TRUE;
}

static void
gs_category_page_class_init (GsCategoryPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_category_page_dispose;
	page_class->switch_to = gs_category_page_switch_to;
	page_class->reload = gs_category_page_reload;
	page_class->setup = gs_category_page_setup;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-category-page.ui");

	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, category_detail_box);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, infobar_category_shell_extensions);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, button_category_shell_extensions);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, listbox_filter);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, scrolledwindow_category);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, scrolledwindow_filter);
}

GsCategoryPage *
gs_category_page_new (void)
{
	GsCategoryPage *self;
	self = g_object_new (GS_TYPE_CATEGORY_PAGE, NULL);
	return self;
}

/* vim: set noexpandtab: */
