/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#include "gs-app-list-private.h"
#include "gs-common.h"
#include "gs-summary-tile.h"
#include "gs-category-page.h"
#include "gs-utils.h"

struct _GsCategoryPage
{
	GsPage		 parent_instance;

	GsPluginLoader	*plugin_loader;
	GCancellable	*cancellable;
	GsShell		*shell;
	GsCategory	*category;
	GsCategory	*subcategory;

	GtkWidget	*category_detail_box;
	GtkWidget	*scrolledwindow_category;
	GtkWidget	*featured_flow_box;
};

G_DEFINE_TYPE (GsCategoryPage, gs_category_page, GS_TYPE_PAGE)

typedef enum {
	PROP_TITLE = 1,
} GsCategoryPageProperty;

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
}

static gint
_max_results_sort_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	gint name_sort = gs_utils_sort_strcmp (gs_app_get_name (app1), gs_app_get_name (app2));

	if (name_sort != 0)
		return name_sort;

	return gs_app_get_rating (app1) - gs_app_get_rating (app2);
}

static void
gs_category_page_set_featured_placeholders (GsCategoryPage *self)
{
	gs_container_remove_all (GTK_CONTAINER (self->featured_flow_box));
	for (guint i = 0; i < 3; ++i) {
		GtkWidget *tile = gs_summary_tile_new (NULL);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (self->featured_flow_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}
	gtk_widget_show (self->featured_flow_box);
}

static void
gs_category_page_get_featured_apps_cb (GObject *source_object,
				       GAsyncResult *res,
				       gpointer user_data)
{
	GsApp *app;
	GtkWidget *tile;
	GsCategoryPage *self = GS_CATEGORY_PAGE (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	gs_container_remove_all (GTK_CONTAINER (self->featured_flow_box));
	gtk_widget_hide (self->featured_flow_box);

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get featured apps for category apps: %s", error->message);
		return;
	}
	if (gs_app_list_length (list) < 3) {
		g_debug ("not enough featured apps for category %s; not showing featured apps!",
			 gs_category_get_id (self->category));
		return;
	}

	/* randomize so we show different featured apps every time */
	gs_app_list_randomize (list);

	for (guint i = 0; i < gs_app_list_length (list); ++i) {
		app = gs_app_list_index (list, i);
		tile = gs_summary_tile_new (app);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (self->featured_flow_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	gtk_widget_show (self->featured_flow_box);
}

static void
gs_category_page_set_featured_apps (GsCategoryPage *self)
{
	GsCategory *featured_subcat = NULL;
	GPtrArray *children = gs_category_get_children (self->category);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	for (guint i = 0; i < children->len; ++i) {
		GsCategory *sub = GS_CATEGORY (g_ptr_array_index (children, i));
		if (g_strcmp0 (gs_category_get_id (sub), "featured") == 0) {
			featured_subcat = sub;
			break;
		}
	}

	if (featured_subcat == NULL)
		return;

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
					 "interactive", TRUE,
					 "category", featured_subcat,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
					 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
							 GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader,
					    plugin_job,
					    self->cancellable,
					    gs_category_page_get_featured_apps_cb,
					    self);
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

	g_cancellable_cancel (self->cancellable);
	g_clear_object (&self->cancellable);
	self->cancellable = g_cancellable_new ();

	g_debug ("search using %s/%s",
	         gs_category_get_id (self->category),
	         gs_category_get_id (self->subcategory));

	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));

	count = MIN(30, gs_category_get_size (self->subcategory));
	for (i = 0; i < count; i++) {
		tile = gs_summary_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (self->category_detail_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	gs_category_page_set_featured_apps (self);

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
					 "category", self->subcategory,
					 "filter-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
					 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
							 GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
					 NULL);
	gs_plugin_job_set_sort_func (plugin_job, _max_results_sort_cb, NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader,
					    plugin_job,
					    self->cancellable,
					    gs_category_page_get_apps_cb,
					    self);
}

static void
gs_category_page_create_filter (GsCategoryPage *self,
				GsCategory *category)
{
	GsCategory *s, *first_subcat = NULL;
	guint i;
	GPtrArray *children;
	gboolean featured_category_found = FALSE;

	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));

	children = gs_category_get_children (category);
	for (i = 0; i < children->len; i++) {
		s = GS_CATEGORY (g_ptr_array_index (children, i));
		/* don't include the featured subcategory (those will appear as banners) */
		if (g_strcmp0 (gs_category_get_id (s), "featured") == 0) {
			featured_category_found = TRUE;
			continue;
		}
		if (gs_category_get_size (s) < 1) {
			g_debug ("not showing %s/%s as no apps",
				 gs_category_get_id (category),
				 gs_category_get_id (s));
			continue;
		}

		if (first_subcat == NULL)
			first_subcat = s;
	}

	g_set_object (&self->subcategory, first_subcat);
	if (first_subcat != NULL)
		gs_category_page_reload (GS_PAGE (self));

	if (featured_category_found) {
		/* set up the placeholders as having the featured category is a good
		 * indicator that there will be featured apps */
		gs_category_page_set_featured_placeholders (self);
	} else {
		gs_container_remove_all (GTK_CONTAINER (self->featured_flow_box));
		gtk_widget_hide (self->featured_flow_box);
	}
}

void
gs_category_page_set_category (GsCategoryPage *self, GsCategory *category)
{
	GtkAdjustment *adj = NULL;

	/* this means we've come from the app-view -> back */
	if (self->category == category)
		return;

	/* save this */
	g_clear_object (&self->category);
	self->category = g_object_ref (category);

	/* find apps in this group */
	gs_category_page_create_filter (self, category);

	/* scroll the list of apps to the beginning, otherwise it will show
	 * with the previous scroll value */
	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_category));
	gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));

	/* notify of the updated title */
	g_object_notify (G_OBJECT (self), "title");
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
gs_category_page_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (object);

	switch ((GsCategoryPageProperty) prop_id) {
	case PROP_TITLE:
		if (self->category != NULL)
			g_value_set_string (value, gs_category_get_name (self->category));
		else
			g_value_set_string (value, NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_category_page_dispose (GObject *object)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (object);

	g_cancellable_cancel (self->cancellable);
	g_clear_object (&self->cancellable);

	g_clear_object (&self->category);
	g_clear_object (&self->subcategory);
	g_clear_object (&self->plugin_loader);

	G_OBJECT_CLASS (gs_category_page_parent_class)->dispose (object);
}

static gboolean
gs_category_page_setup (GsPage *page,
                        GsShell *shell,
                        GsPluginLoader *plugin_loader,
                        GCancellable *cancellable,
                        GError **error)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (page);

	self->plugin_loader = g_object_ref (plugin_loader);
	self->shell = shell;

	return TRUE;
}

static void
gs_category_page_class_init (GsCategoryPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_category_page_get_property;
	object_class->dispose = gs_category_page_dispose;

	page_class->reload = gs_category_page_reload;
	page_class->setup = gs_category_page_setup;

	g_object_class_override_property (object_class, PROP_TITLE, "title");

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-category-page.ui");

	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, category_detail_box);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, scrolledwindow_category);
	gtk_widget_class_bind_template_child (widget_class, GsCategoryPage, featured_flow_box);
}

GsCategoryPage *
gs_category_page_new (void)
{
	return g_object_new (GS_TYPE_CATEGORY_PAGE, NULL);
}
