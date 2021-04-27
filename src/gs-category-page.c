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
	GsCategory	*category;
	GsCategory	*subcategory;

	GtkWidget	*category_detail_box;
	GtkWidget	*scrolledwindow_category;
	GtkWidget	*featured_flow_box;
};

G_DEFINE_TYPE (GsCategoryPage, gs_category_page, GS_TYPE_PAGE)

typedef enum {
	PROP_CATEGORY = 1,
	/* Override properties: */
	PROP_TITLE,
} GsCategoryPageProperty;

static GParamSpec *obj_props[PROP_CATEGORY + 1] = { NULL, };

typedef enum {
	SIGNAL_APP_CLICKED,
} GsCategoryPageSignal;

static guint obj_signals[SIGNAL_APP_CLICKED + 1] = { 0, };

static void
app_tile_clicked (GsAppTile *tile, gpointer data)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (data);
	GsApp *app;

	app = gs_app_tile_get_app (tile);
	g_signal_emit (self, obj_signals[SIGNAL_APP_CLICKED], 0, app);
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
gs_category_page_add_placeholders (GsCategoryPage *self,
                                   GtkFlowBox     *flow_box,
                                   guint           n_placeholders)
{
	gs_container_remove_all (GTK_CONTAINER (flow_box));

	for (guint i = 0; i < n_placeholders; ++i) {
		GtkWidget *tile = gs_summary_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (flow_box), tile);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	gtk_widget_show (GTK_WIDGET (flow_box));
}

typedef struct {
	GsCategoryPage *page;  /* (owned) */
	GHashTable *featured_app_ids;  /* (owned) (nullable) (element-type utf8 utf8) */
	gboolean get_featured_apps_finished;
	GsAppList *apps;  /* (owned) (nullable) */
	gboolean get_main_apps_finished;
} LoadCategoryData;

static void
load_category_data_free (LoadCategoryData *data)
{
	g_clear_object (&data->page);
	g_clear_pointer (&data->featured_app_ids, g_hash_table_unref);
	g_clear_object (&data->apps);
	g_free (data);
}

static void load_category_finish (LoadCategoryData *data);

static void
gs_category_page_get_featured_apps_cb (GObject *source_object,
				       GAsyncResult *res,
				       gpointer user_data)
{
	LoadCategoryData *data = user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GHashTable) featured_app_ids = NULL;

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    &local_error);
	if (list == NULL) {
		if (!g_error_matches (local_error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get featured apps for category apps: %s", local_error->message);
		data->get_featured_apps_finished = TRUE;
		load_category_finish (data);
		return;
	}

	featured_app_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		g_hash_table_add (featured_app_ids, g_strdup (gs_app_get_id (app)));
	}

	data->featured_app_ids = g_steal_pointer (&featured_app_ids);
	data->get_featured_apps_finished = TRUE;
	load_category_finish (data);
}

static void
gs_category_page_get_apps_cb (GObject *source_object,
                              GAsyncResult *res,
                              gpointer user_data)
{
	LoadCategoryData *data = user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GsAppList) list = NULL;

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    &local_error);
	if (list == NULL) {
		if (!g_error_matches (local_error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get apps for category apps: %s", local_error->message);
		data->get_main_apps_finished = TRUE;
		load_category_finish (data);
		return;
	}

	data->apps = g_steal_pointer (&list);
	data->get_main_apps_finished = TRUE;
	load_category_finish (data);
}

static void
load_category_finish (LoadCategoryData *data)
{
	GsCategoryPage *self = data->page;

	if (!data->get_featured_apps_finished ||
	    !data->get_main_apps_finished)
		return;

	/* Remove the loading tiles. */
	gs_container_remove_all (GTK_CONTAINER (self->featured_flow_box));
	gs_container_remove_all (GTK_CONTAINER (self->category_detail_box));

	for (guint i = 0; i < gs_app_list_length (data->apps); i++) {
		GsApp *app = gs_app_list_index (data->apps, i);
		gboolean is_featured;
		GtkWidget *tile;

		is_featured = (data->featured_app_ids != NULL &&
			       g_hash_table_contains (data->featured_app_ids, gs_app_get_id (app)));

		tile = gs_summary_tile_new (app);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), self);

		if (is_featured)
			gtk_container_add (GTK_CONTAINER (self->featured_flow_box), tile);
		else
			gtk_container_add (GTK_CONTAINER (self->category_detail_box), tile);

		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
	}

	/* Show each of the flow boxes if they have any children. */
	gtk_widget_set_visible (self->featured_flow_box, gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (self->featured_flow_box), 0) != NULL);
	gtk_widget_set_visible (self->category_detail_box, gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (self->category_detail_box), 0) != NULL);

	load_category_data_free (data);
}

static void
gs_category_page_load_category (GsCategoryPage *self)
{
	GsCategory *featured_subcat = NULL;
	GtkAdjustment *adj = NULL;
	g_autoptr(GsPluginJob) featured_plugin_job = NULL;
	g_autoptr(GsPluginJob) main_plugin_job = NULL;
	LoadCategoryData *load_data = NULL;

	g_assert (self->subcategory != NULL);

	featured_subcat = gs_category_find_child (self->category, "featured");

	g_cancellable_cancel (self->cancellable);
	g_clear_object (&self->cancellable);
	self->cancellable = g_cancellable_new ();

	g_debug ("search using %s/%s",
	         gs_category_get_id (self->category),
	         gs_category_get_id (self->subcategory));

	gs_category_page_add_placeholders (self, GTK_FLOW_BOX (self->category_detail_box),
					   MIN (30, gs_category_get_size (self->subcategory)));

	if (featured_subcat != NULL) {
		/* set up the placeholders as having the featured category is a good
		 * indicator that there will be featured apps */
		gs_category_page_add_placeholders (self, GTK_FLOW_BOX (self->featured_flow_box), 4);
	} else {
		gs_container_remove_all (GTK_CONTAINER (self->featured_flow_box));
		gtk_widget_hide (self->featured_flow_box);
	}

	/* Load the list of apps in the category, and also the list of all
	 * featured apps, in parallel.
	 *
	 * The list of featured apps has to be loaded separately (we can’t just
	 * query each app for its featured status) since it’s provided by a
	 * separate appstream file (org.gnome.Software.Featured.xml) and hence
	 * produces separate `GsApp` instances with stub data. In particular,
	 * they don’t have enough category data to match the main category
	 * query.
	 *
	 * Once both queries have returned, turn the list of featured apps into
	 * a filter, and split the main list in three:
	 *  - Featured apps
	 *  - Recently updated apps
	 *  - Everything else
	 * Then populate the UI.
	 *
	 * The `featured_subcat` can be `NULL` when loading the special ‘addons’
	 * category.
	 */
	load_data = g_new0 (LoadCategoryData, 1);
	load_data->page = g_object_ref (self);

	if (featured_subcat != NULL) {
		featured_plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
							  "interactive", TRUE,
							  "category", featured_subcat,
							  "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS,
							  NULL);
		gs_plugin_loader_job_process_async (self->plugin_loader,
						    featured_plugin_job,
						    self->cancellable,
						    gs_category_page_get_featured_apps_cb,
						    load_data);
	} else {
		/* Skip it */
		load_data->get_featured_apps_finished = TRUE;
	}

	main_plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
					      "interactive", TRUE,
					      "category", self->subcategory,
					      "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							      GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
							      GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS,
					      "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
							      GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
					      NULL);
	gs_plugin_job_set_sort_func (main_plugin_job, _max_results_sort_cb, NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader,
					    main_plugin_job,
					    self->cancellable,
					    gs_category_page_get_apps_cb,
					    load_data);

	/* scroll the list of apps to the beginning, otherwise it will show
	 * with the previous scroll value */
	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_category));
	gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
}

static void
gs_category_page_reload (GsPage *page)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (page);

	if (self->subcategory == NULL)
		return;

	gs_category_page_load_category (self);
}

void
gs_category_page_set_category (GsCategoryPage *self, GsCategory *category)
{
	GsCategory *all_subcat = NULL;

	/* this means we've come from the app-view -> back */
	if (self->category == category)
		return;

	/* set the category */
	all_subcat = gs_category_find_child (category, "all");

	g_set_object (&self->category, category);
	g_set_object (&self->subcategory, all_subcat);

	/* load the apps from it */
	if (all_subcat != NULL)
		gs_category_page_load_category (self);

	/* notify of the updates — the category’s title will have changed too */
	g_object_notify (G_OBJECT (self), "category");
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
	case PROP_CATEGORY:
		g_value_set_object (value, self->category);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_category_page_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	GsCategoryPage *self = GS_CATEGORY_PAGE (object);

	switch ((GsCategoryPageProperty) prop_id) {
	case PROP_TITLE:
		/* Read only */
		g_assert_not_reached ();
		break;
	case PROP_CATEGORY:
		gs_category_page_set_category (self, g_value_get_object (value));
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

	return TRUE;
}

static void
gs_category_page_class_init (GsCategoryPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_category_page_get_property;
	object_class->set_property = gs_category_page_set_property;
	object_class->dispose = gs_category_page_dispose;

	page_class->reload = gs_category_page_reload;
	page_class->setup = gs_category_page_setup;

	/**
	 * GsCategoryPage:category: (nullable)
	 *
	 * The category to display the apps from.
	 *
	 * This may be %NULL if no category is selected. If so, the behaviour
	 * of the widget will be safe, but undefined.
	 *
	 * Since: 41
	 */
	obj_props[PROP_CATEGORY] =
		g_param_spec_object ("category", NULL, NULL,
				     GS_TYPE_CATEGORY,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	g_object_class_override_property (object_class, PROP_TITLE, "title");

	/**
	 * GsCategoryPage::app-clicked:
	 * @app: the #GsApp which was clicked on
	 *
	 * Emitted when one of the app tiles is clicked. Typically the caller
	 * should display the details of the given app in the callback.
	 *
	 * Since: 41
	 */
	obj_signals[SIGNAL_APP_CLICKED] =
		g_signal_new ("app-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, GS_TYPE_APP);

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
