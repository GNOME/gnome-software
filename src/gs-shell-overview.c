/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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
#include <math.h>

#include "gs-shell.h"
#include "gs-shell-overview.h"
#include "gs-app.h"
#include "gs-app-list-private.h"
#include "gs-category.h"
#include "gs-popular-tile.h"
#include "gs-feature-tile.h"
#include "gs-category-tile.h"
#include "gs-hiding-box.h"
#include "gs-common.h"

#define N_TILES 9

typedef struct
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
	GsShell			*shell;
	gint			 refresh_count;
	gboolean		 loading_featured;
	gboolean		 loading_popular;
	gboolean		 loading_popular_rotating;
	gboolean		 loading_categories;
	gboolean		 empty;
	gchar			*category_of_day;
	GtkWidget		*search_button;
	GHashTable		*category_hash;		/* id : GsCategory */

	GtkWidget		*bin_featured;
	GtkWidget		*box_overview;
	GtkWidget		*box_popular;
	GtkWidget		*category_heading;
	GtkWidget		*flowbox_categories;
	GtkWidget		*flowbox_categories2;
	GtkWidget		*popular_heading;
	GtkWidget		*scrolledwindow_overview;
	GtkWidget		*stack_overview;
	GtkWidget		*categories_expander_button;
	GtkWidget		*categories_expander;
	GtkWidget		*categories_more;
} GsShellOverviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsShellOverview, gs_shell_overview, GS_TYPE_PAGE)

enum {
	SIGNAL_REFRESHED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

typedef struct {
        GsCategory	*category;
        GsShellOverview	*self;
        const gchar	*title;
} LoadData;

static void
load_data_free (LoadData *data)
{
        if (data->category != NULL)
                g_object_unref (data->category);
        if (data->self != NULL)
                g_object_unref (data->self);
        g_slice_free (LoadData, data);
}

void
gs_shell_overview_invalidate (GsShellOverview *self)
{
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);

	priv->cache_valid = FALSE;
}

static void
app_tile_clicked (GsAppTile *tile, gpointer data)
{
	GsShellOverview *self = GS_SHELL_OVERVIEW (data);
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	GsApp *app;

	app = gs_app_tile_get_app (tile);
	gs_shell_show_app (priv->shell, app);
}

static gboolean
filter_category (GsApp *app, gpointer user_data)
{
	const gchar *category = (const gchar *) user_data;

	return !gs_app_has_category (app, category);
}

static void
gs_shell_overview_get_popular_cb (GObject *source_object,
				  GAsyncResult *res,
				  gpointer user_data)
{
	GsShellOverview *self = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	guint i;
	GsApp *app;
	GtkWidget *tile;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get popular apps */
	list = gs_plugin_loader_get_popular_finish (plugin_loader, res, &error);
	gtk_widget_set_visible (priv->box_popular, list != NULL);
	gtk_widget_set_visible (priv->popular_heading, list != NULL);
	if (list == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to get popular apps: %s", error->message);
		goto out;
	}
	/* Don't show apps from the category that's currently featured as the category of the day */
	gs_app_list_filter (list, filter_category, priv->category_of_day);
	gs_app_list_randomize (list);

	gs_container_remove_all (GTK_CONTAINER (priv->box_popular));

	for (i = 0; i < gs_app_list_length (list) && i < N_TILES; i++) {
		app = gs_app_list_index (list, i);
		tile = gs_popular_tile_new (app);
		g_signal_connect (tile, "clicked",
			  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (priv->box_popular), tile);
	}

	priv->empty = FALSE;

out:
	priv->loading_popular = FALSE;
	priv->refresh_count--;
	if (priv->refresh_count == 0) {
		priv->cache_valid = TRUE;
		g_signal_emit (self, signals[SIGNAL_REFRESHED], 0);
	}
}

static void
gs_shell_overview_category_more_cb (GtkButton *button, GsShellOverview *self)
{
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	GsCategory *cat;
	const gchar *id;

	id = g_object_get_data (G_OBJECT (button), "GnomeSoftware::CategoryId");
	if (id == NULL)
		return;
	cat = g_hash_table_lookup (priv->category_hash, id);
	if (cat == NULL)
		return;
	gs_shell_show_category (priv->shell, cat);
}

static void
gs_shell_overview_get_category_apps_cb (GObject *source_object,
					GAsyncResult *res,
					gpointer user_data)
{
	LoadData *load_data = (LoadData *) user_data;
	GsShellOverview *self = load_data->self;
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	guint i;
	GsApp *app;
	GtkWidget *box;
	GtkWidget *button;
	GtkWidget *headerbox;
	GtkWidget *label;
	GtkWidget *tile;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get popular apps */
	list = gs_plugin_loader_get_category_apps_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to get recommended applications: %s", error->message);
		goto out;
	} else if (gs_app_list_length (list) < N_TILES) {
		g_warning ("hiding recommended applications: "
			   "found only %d to show, need at least %d",
			   gs_app_list_length (list), N_TILES);
		goto out;
	}
	gs_app_list_randomize (list);

	/* add header */
	headerbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 9);
	gtk_widget_set_visible (headerbox, TRUE);

	/* add label */
	label = gtk_label_new (load_data->title);
	gtk_widget_set_visible (label, TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0.f);
	gtk_widget_set_margin_top (label, 24);
	gtk_widget_set_margin_bottom (label, 6);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_style_context_add_class (gtk_widget_get_style_context (label),
				     "index-title-alignment-software");
	gtk_container_add (GTK_CONTAINER (headerbox), label);

	/* add button */
	button = gtk_button_new_with_label (_("More…"));
	g_object_set_data_full (G_OBJECT (button), "GnomeSoftware::CategoryId",
				g_strdup (gs_category_get_id (load_data->category)),
				g_free);
	gtk_widget_set_visible (button, TRUE);
	gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (gs_shell_overview_category_more_cb), self);
	gtk_container_add (GTK_CONTAINER (headerbox), button);
	gtk_container_add (GTK_CONTAINER (priv->box_overview), headerbox);

	/* add hiding box */
	box = gs_hiding_box_new ();
	gs_hiding_box_set_spacing (GS_HIDING_BOX (box), 14);
	gtk_widget_set_visible (box, TRUE);
	gtk_widget_set_valign (box, GTK_ALIGN_START);
	gtk_container_add (GTK_CONTAINER (priv->box_overview), box);

	/* add all the apps */
	for (i = 0; i < gs_app_list_length (list) && i < N_TILES; i++) {
		app = gs_app_list_index (list, i);
		tile = gs_popular_tile_new (app);
		g_signal_connect (tile, "clicked",
			  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (box), tile);
	}

	priv->empty = FALSE;

out:
	load_data_free (load_data);
	priv->loading_popular_rotating = FALSE;
	priv->refresh_count--;
	if (priv->refresh_count == 0) {
		priv->cache_valid = TRUE;
		g_signal_emit (self, signals[SIGNAL_REFRESHED], 0);
	}
}

static void
gs_shell_overview_get_featured_cb (GObject *source_object,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GsShellOverview *self = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *tile;
	GsApp *app;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	list = gs_plugin_loader_get_featured_finish (plugin_loader, res, &error);
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		goto out;

	if (g_getenv ("GNOME_SOFTWARE_FEATURED") == NULL) {
		/* Don't show apps from the category that's currently featured as the category of the day */
		gs_app_list_filter (list, filter_category, priv->category_of_day);
		gs_app_list_randomize (list);
	}

	gs_container_remove_all (GTK_CONTAINER (priv->bin_featured));
	if (list == NULL) {
		g_warning ("failed to get featured apps: %s",
			   error->message);
		goto out;
	}
	if (gs_app_list_length (list) == 0) {
		g_warning ("failed to get featured apps: "
			   "no apps to show");
		goto out;
	}

	/* at the moment, we only care about the first app */
	app = gs_app_list_index (list, 0);
	tile = gs_feature_tile_new (app);
	g_signal_connect (tile, "clicked",
			  G_CALLBACK (app_tile_clicked), self);

	gtk_container_add (GTK_CONTAINER (priv->bin_featured), tile);

	priv->empty = FALSE;

out:
	priv->loading_featured = FALSE;
	priv->refresh_count--;
	if (priv->refresh_count == 0) {
		priv->cache_valid = TRUE;
		g_signal_emit (self, signals[SIGNAL_REFRESHED], 0);
	}
}

static void
category_tile_clicked (GsCategoryTile *tile, gpointer data)
{
	GsShellOverview *self = GS_SHELL_OVERVIEW (data);
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	GsCategory *category;

	category = gs_category_tile_get_category (tile);
	gs_shell_show_category (priv->shell, category);
}

static void
gs_shell_overview_get_categories_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GsShellOverview *self = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	guint i;
	guint important_cnt = 0;
	guint important_aim;
	GsCategory *cat;
	GtkFlowBox *flowbox;
	GtkWidget *tile;
	gboolean has_category = FALSE;
	gboolean use_expander = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) list = NULL;

	list = gs_plugin_loader_get_categories_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to get categories: %s", error->message);
		goto out;
	}
	gs_container_remove_all (GTK_CONTAINER (priv->flowbox_categories));
	gs_container_remove_all (GTK_CONTAINER (priv->flowbox_categories2));

	/* ensure there are no more than two rows of curated categories */
	flowbox = GTK_FLOW_BOX (priv->flowbox_categories);
	important_aim = 2 * gtk_flow_box_get_max_children_per_line (flowbox);
	for (i = 0; i < list->len; i++) {
		cat = GS_CATEGORY (g_ptr_array_index (list, i));
		if (gs_category_get_size (cat) == 0)
			continue;
		if (gs_category_get_important (cat)) {
			if (++important_cnt > important_aim) {
				g_debug ("overriding %s as unimportant",
					 gs_category_get_id (cat));
				gs_category_set_important (cat, FALSE);
			}
		}
	}

	/* ensure we show a full first flowbox if we don't have enough */
	for (i = 0; i < list->len && important_cnt < important_aim; i++) {
		cat = GS_CATEGORY (g_ptr_array_index (list, i));
		if (gs_category_get_size (cat) == 0)
			continue;
		if (!gs_category_get_important (cat)) {
			important_cnt++;
			g_debug ("overriding %s as important",
				 gs_category_get_id (cat));
			gs_category_set_important (cat, TRUE);
		}
	}

	/* add categories to the correct flowboxes, the second being hidden */
	for (i = 0; i < list->len; i++) {
		cat = GS_CATEGORY (g_ptr_array_index (list, i));
		if (gs_category_get_size (cat) == 0)
			continue;
		tile = gs_category_tile_new (cat);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (category_tile_clicked), self);
		if (gs_category_get_important (cat)) {
			flowbox = GTK_FLOW_BOX (priv->flowbox_categories);
		} else {
			flowbox = GTK_FLOW_BOX (priv->flowbox_categories2);
			use_expander = TRUE;
		}
		gtk_flow_box_insert (flowbox, tile, -1);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
		has_category = TRUE;

		/* we save these for the 'More...' buttons */
		g_hash_table_insert (priv->category_hash,
				     g_strdup (gs_category_get_id (cat)),
				     g_object_ref (cat));
	}

	/* show the expander if we have too many children */
	gtk_widget_set_visible (priv->categories_expander, use_expander);
out:
	if (has_category) {
		priv->empty = FALSE;
	}
	gtk_widget_set_visible (priv->category_heading, has_category);

	priv->loading_categories = FALSE;
	priv->refresh_count--;
	if (priv->refresh_count == 0) {
		priv->cache_valid = TRUE;
		g_signal_emit (self, signals[SIGNAL_REFRESHED], 0);
	}
}

static const gchar *
gs_shell_overview_get_category_label (const gchar *id)
{
	if (g_strcmp0 (id, "audio-video") == 0) {
		/* TRANSLATORS: this is a heading for audio applications which
		 * have been featured ('recommended') by the distribution */
		return _("Recommended Audio & Video Applications");
	}
	if (g_strcmp0 (id, "games") == 0) {
		/* TRANSLATORS: this is a heading for games which have been
		 * featured ('recommended') by the distribution */
		return _("Recommended Games");
	}
	if (g_strcmp0 (id, "graphics") == 0) {
		/* TRANSLATORS: this is a heading for graphics applications
		 * which have been featured ('recommended') by the distribution */
		return _("Recommended Graphics Applications");
	}
	if (g_strcmp0 (id, "productivity") == 0) {
		/* TRANSLATORS: this is a heading for office applications which
		 * have been featured ('recommended') by the distribution */
		return _("Recommended Productivity Applications");
	}
	return NULL;
}

static GPtrArray *
gs_shell_overview_get_random_categories (void)
{
	GPtrArray *cats;
	guint i;
	g_autoptr(GDateTime) date = NULL;
	g_autoptr(GRand) rand = NULL;
	const gchar *ids[] = { "audio-video",
			       "games",
			       "graphics",
			       "productivity",
			       NULL };

	date = g_date_time_new_now_utc ();
	rand = g_rand_new_with_seed (g_date_time_get_day_of_year (date));
	cats = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; ids[i] != NULL; i++)
		g_ptr_array_add (cats, g_strdup (ids[i]));
	for (i = 0; i < powl (cats->len + 1, 2); i++) {
		gpointer tmp;
		guint rnd1 = g_rand_int_range (rand, 0, cats->len);
		guint rnd2 = g_rand_int_range (rand, 0, cats->len);
		if (rnd1 == rnd2)
			continue;
		tmp = cats->pdata[rnd1];
		cats->pdata[rnd1] = cats->pdata[rnd2];
		cats->pdata[rnd2] = tmp;
	}
	for (i = 0; i < cats->len; i++) {
		const gchar *tmp = g_ptr_array_index (cats, i);
		g_debug ("%i = %s", i + 1, tmp);
	}
	return cats;
}

static void
gs_shell_overview_load (GsShellOverview *self)
{
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	guint i;

	priv->empty = TRUE;

	if (!priv->loading_featured) {
		priv->loading_featured = TRUE;
		gs_plugin_loader_get_featured_async (priv->plugin_loader,
						     GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						     priv->cancellable,
						     gs_shell_overview_get_featured_cb,
						     self);
		priv->refresh_count++;
	}

	if (!priv->loading_popular) {
		priv->loading_popular = TRUE;
		gs_plugin_loader_get_popular_async (priv->plugin_loader,
						    GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS |
						    GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						    priv->cancellable,
						    gs_shell_overview_get_popular_cb,
						    self);
		priv->refresh_count++;
	}

	if (!priv->loading_popular_rotating) {
		const guint MAX_CATS = 2;
		g_autoptr(GPtrArray) cats_random = NULL;
		cats_random = gs_shell_overview_get_random_categories ();

		/* load all the categories */
		for (i = 0; i < cats_random->len && i < MAX_CATS; i++) {
			LoadData *load_data;
			const gchar *cat_id = g_ptr_array_index (cats_random, 0);
			g_autoptr(GsCategory) category = NULL;
			g_autoptr(GsCategory) featured_category = NULL;

			cat_id = g_ptr_array_index (cats_random, i);
			if (i == 0) {
				g_free (priv->category_of_day);
				priv->category_of_day = g_strdup (cat_id);
			}
			category = gs_category_new (cat_id);
			featured_category = gs_category_new ("featured");
			gs_category_add_child (category, featured_category);

			load_data = g_slice_new0 (LoadData);
			load_data->category = g_object_ref (category);
			load_data->self = g_object_ref (self);
			load_data->title = gs_shell_overview_get_category_label (cat_id);
			gs_plugin_loader_get_category_apps_async (priv->plugin_loader,
								  featured_category,
								  GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS |
								  GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
								  priv->cancellable,
								  gs_shell_overview_get_category_apps_cb,
								  load_data);
			priv->refresh_count++;
		}
		priv->loading_popular_rotating = TRUE;
	}

	if (!priv->loading_categories) {
		priv->loading_categories = TRUE;
		gs_plugin_loader_get_categories_async (priv->plugin_loader,
						       GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						       priv->cancellable,
						       gs_shell_overview_get_categories_cb,
						       self);
		priv->refresh_count++;
	}
}

static void
gs_shell_overview_reload (GsPage *page)
{
	GsShellOverview *self = GS_SHELL_OVERVIEW (page);
	gs_shell_overview_invalidate (self);
	gs_shell_overview_load (self);
}

static void
gs_shell_overview_switch_to (GsPage *page, gboolean scroll_up)
{
	GsShellOverview *self = GS_SHELL_OVERVIEW (page);
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	GtkWidget *widget;
	GtkAdjustment *adj;

	if (gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_OVERVIEW) {
		g_warning ("Called switch_to(overview) when in mode %s",
			   gs_shell_get_mode_string (priv->shell));
		return;
	}

	/* we hid the search bar */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->search_button), FALSE);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
	gtk_widget_show (widget);

	/* hide the expander */
	gtk_revealer_set_transition_duration (GTK_REVEALER (priv->categories_expander), 0);
	gtk_revealer_set_transition_duration (GTK_REVEALER (priv->categories_more), 0);
	gtk_revealer_set_reveal_child (GTK_REVEALER (priv->categories_expander), TRUE);
	gtk_revealer_set_reveal_child (GTK_REVEALER (priv->categories_more), FALSE);

	if (scroll_up) {
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_overview));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	gs_grab_focus_when_mapped (priv->scrolledwindow_overview);

	if (priv->cache_valid || priv->refresh_count > 0)
		return;
	gs_shell_overview_load (self);
}

static void
gs_shell_overview_categories_expander_cb (GtkButton *button, GsShellOverview *self)
{
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	gtk_revealer_set_transition_duration (GTK_REVEALER (priv->categories_expander), 250);
	gtk_revealer_set_transition_duration (GTK_REVEALER (priv->categories_more), 250);
	gtk_revealer_set_reveal_child (GTK_REVEALER (priv->categories_expander), FALSE);
	gtk_revealer_set_reveal_child (GTK_REVEALER (priv->categories_more), TRUE);
}

void
gs_shell_overview_setup (GsShellOverview *self,
			 GsShell *shell,
			 GsPluginLoader *plugin_loader,
			 GtkBuilder *builder,
			 GCancellable *cancellable)
{
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);
	GtkSearchBar *search_bar;
	GtkAdjustment *adj;
	GtkWidget *tile;
	gint i;

	g_return_if_fail (GS_IS_SHELL_OVERVIEW (self));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);
	priv->category_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
						     g_free, (GDestroyNotify) g_object_unref);

	/* avoid a ref cycle */
	priv->shell = shell;

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_overview));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (priv->box_overview), adj);

	tile = gs_feature_tile_new (NULL);
	gtk_container_add (GTK_CONTAINER (priv->bin_featured), tile);

	for (i = 0; i < N_TILES; i++) {
		tile = gs_popular_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (priv->box_popular), tile);
	}

	/* handle category expander */
	g_signal_connect (priv->categories_expander_button, "clicked",
			  G_CALLBACK (gs_shell_overview_categories_expander_cb), self);

	/* search button */
	search_bar = GTK_SEARCH_BAR (gtk_builder_get_object (priv->builder,
							     "search_bar"));
	priv->search_button = gs_search_button_new (search_bar);
	gs_page_set_header_end_widget (GS_PAGE (self), priv->search_button);

	/* chain up */
	gs_page_setup (GS_PAGE (self),
	               shell,
	               plugin_loader,
	               cancellable);
}

static void
gs_shell_overview_init (GsShellOverview *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_shell_overview_dispose (GObject *object)
{
	GsShellOverview *self = GS_SHELL_OVERVIEW (object);
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);

	g_clear_object (&priv->builder);
	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->cancellable);
	g_clear_pointer (&priv->category_of_day, g_free);
	g_clear_pointer (&priv->category_hash, g_hash_table_unref);

	G_OBJECT_CLASS (gs_shell_overview_parent_class)->dispose (object);
}

static void
gs_shell_overview_refreshed (GsShellOverview *self)
{
	GsShellOverviewPrivate *priv = gs_shell_overview_get_instance_private (self);

	if (priv->empty) {
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_overview), "no-results");
	} else {
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_overview), "overview");
	}
}

static void
gs_shell_overview_class_init (GsShellOverviewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_overview_dispose;
	page_class->switch_to = gs_shell_overview_switch_to;
	page_class->reload = gs_shell_overview_reload;
	klass->refreshed = gs_shell_overview_refreshed;

	signals [SIGNAL_REFRESHED] =
		g_signal_new ("refreshed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsShellOverviewClass, refreshed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-overview.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, bin_featured);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, box_overview);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, box_popular);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, category_heading);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, flowbox_categories);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, flowbox_categories2);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, popular_heading);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, scrolledwindow_overview);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, stack_overview);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, categories_expander_button);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, categories_expander);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, categories_more);
}

GsShellOverview *
gs_shell_overview_new (void)
{
	return GS_SHELL_OVERVIEW (g_object_new (GS_TYPE_SHELL_OVERVIEW, NULL));
}

/* vim: set noexpandtab: */
