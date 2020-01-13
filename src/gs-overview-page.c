/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <math.h>

#include "gs-shell.h"
#include "gs-overview-page.h"
#include "gs-app-list-private.h"
#include "gs-popular-tile.h"
#include "gs-feature-tile.h"
#include "gs-category-tile.h"
#include "gs-hiding-box.h"
#include "gs-common.h"

#define N_TILES					9
#define FEATURED_ROTATE_TIME			30 /* seconds */

typedef struct
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
	GsShell			*shell;
	gint			 action_cnt;
	gboolean		 loading_featured;
	gboolean		 loading_popular;
	gboolean		 loading_recent;
	gboolean		 loading_popular_rotating;
	gboolean		 loading_categories;
	gboolean		 empty;
	gchar			*category_of_day;
	GHashTable		*category_hash;		/* id : GsCategory */
	GSettings		*settings;
	GsApp			*third_party_repo;
	guint			 featured_rotate_timer_id;

	GtkWidget		*infobar_third_party;
	GtkWidget		*label_third_party;
	GtkWidget		*overlay;
	GtkWidget		*stack_featured;
	GtkWidget		*button_featured_back;
	GtkWidget		*button_featured_forwards;
	GtkWidget		*box_overview;
	GtkWidget		*box_popular;
	GtkWidget		*box_popular_rotating;
	GtkWidget		*box_recent;
	GtkWidget		*category_heading;
	GtkWidget		*flowbox_categories;
	GtkWidget		*popular_heading;
	GtkWidget		*recent_heading;
	GtkWidget		*scrolledwindow_overview;
	GtkWidget		*stack_overview;
} GsOverviewPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsOverviewPage, gs_overview_page, GS_TYPE_PAGE)

enum {
	SIGNAL_REFRESHED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

typedef struct {
        GsCategory	*category;
        GsOverviewPage	*self;
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

static void
gs_overview_page_invalidate (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);

	priv->cache_valid = FALSE;
}

static void
app_tile_clicked (GsAppTile *tile, gpointer data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (data);
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
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
gs_overview_page_decrement_action_cnt (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);

	/* every job increments this */
	if (priv->action_cnt == 0) {
		g_warning ("action_cnt already zero!");
		return;
	}
	if (--priv->action_cnt > 0)
		return;

	/* all done */
	priv->cache_valid = TRUE;
	g_signal_emit (self, signals[SIGNAL_REFRESHED], 0);
	priv->loading_categories = FALSE;
	priv->loading_featured = FALSE;
	priv->loading_popular = FALSE;
	priv->loading_recent = FALSE;
	priv->loading_popular_rotating = FALSE;
}

static void
gs_overview_page_get_popular_cb (GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	guint i;
	GsApp *app;
	GtkWidget *tile;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get popular apps */
	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get popular apps: %s", error->message);
		goto out;
	}

	/* not enough to show */
	if (gs_app_list_length (list) < N_TILES) {
		g_warning ("Only %u apps for popular list, hiding",
		           gs_app_list_length (list));
		gtk_widget_set_visible (priv->box_popular, FALSE);
		gtk_widget_set_visible (priv->popular_heading, FALSE);
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
	gtk_widget_set_visible (priv->box_popular, TRUE);
	gtk_widget_set_visible (priv->popular_heading, TRUE);

	priv->empty = FALSE;

out:
	gs_overview_page_decrement_action_cnt (self);
}

static void
gs_overview_page_get_recent_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	guint i;
	GsApp *app;
	GtkWidget *tile;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get recent apps */
	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get recent apps: %s", error->message);
		goto out;
	}

	/* not enough to show */
	if (gs_app_list_length (list) < N_TILES) {
		g_warning ("Only %u apps for recent list, hiding",
			   gs_app_list_length (list));
		gtk_widget_set_visible (priv->box_recent, FALSE);
		gtk_widget_set_visible (priv->recent_heading, FALSE);
		goto out;
	}

	/* Don't show apps from the category that's currently featured as the category of the day */
	gs_app_list_filter (list, filter_category, priv->category_of_day);
	gs_app_list_randomize (list);

	gs_container_remove_all (GTK_CONTAINER (priv->box_recent));

	for (i = 0; i < gs_app_list_length (list) && i < N_TILES; i++) {
		app = gs_app_list_index (list, i);
		tile = gs_popular_tile_new (app);
		g_signal_connect (tile, "clicked",
			  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (priv->box_recent), tile);
	}
	gtk_widget_set_visible (priv->box_recent, TRUE);
	gtk_widget_set_visible (priv->recent_heading, TRUE);

	priv->empty = FALSE;

out:
	gs_overview_page_decrement_action_cnt (self);
}

static void
gs_overview_page_category_more_cb (GtkButton *button, GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
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
gs_overview_page_get_category_apps_cb (GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data)
{
	LoadData *load_data = (LoadData *) user_data;
	GsOverviewPage *self = load_data->self;
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
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
	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			goto out;
		g_warning ("failed to get category %s featured applications: %s",
			   gs_category_get_id (load_data->category),
			   error->message);
		goto out;
	} else if (gs_app_list_length (list) < N_TILES) {
		g_warning ("hiding category %s featured applications: "
			   "found only %u to show, need at least %d",
			   gs_category_get_id (load_data->category),
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
	gtk_style_context_add_class (gtk_widget_get_style_context (button),
				     "overview-more-button");
	g_object_set_data_full (G_OBJECT (button), "GnomeSoftware::CategoryId",
				g_strdup (gs_category_get_id (load_data->category)),
				g_free);
	gtk_widget_set_visible (button, TRUE);
	gtk_widget_set_valign (button, GTK_ALIGN_END);
	gtk_widget_set_margin_bottom (button, 9);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (gs_overview_page_category_more_cb), self);
	gtk_container_add (GTK_CONTAINER (headerbox), button);
	gtk_container_add (GTK_CONTAINER (priv->box_popular_rotating), headerbox);

	/* add hiding box */
	box = gs_hiding_box_new ();
	gs_hiding_box_set_spacing (GS_HIDING_BOX (box), 14);
	gtk_widget_set_visible (box, TRUE);
	gtk_widget_set_valign (box, GTK_ALIGN_START);
	gtk_container_add (GTK_CONTAINER (priv->box_popular_rotating), box);

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
	gs_overview_page_decrement_action_cnt (self);
}

static void
_feature_banner_forward (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	GtkWidget *visible_child;
	GtkWidget *next_child = NULL;
	GList *banner_link;
	g_autoptr(GList) banners = NULL;

	visible_child = gtk_stack_get_visible_child (GTK_STACK (priv->stack_featured));
	banners = gtk_container_get_children (GTK_CONTAINER (priv->stack_featured));
	if (banners == NULL)
		return;

	/* find banner after the currently visible one */
	for (banner_link = banners; banner_link != NULL; banner_link = banner_link->next) {
		GtkWidget *child = banner_link->data;
		if (child == visible_child) {
			if (banner_link->next != NULL)
				next_child = banner_link->next->data;
			break;
		}
	}
	if (next_child == NULL)
		next_child = g_list_first(banners)->data;
	gtk_stack_set_visible_child (GTK_STACK (priv->stack_featured), next_child);
}

static void
_feature_banner_back (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	GtkWidget *visible_child;
	GtkWidget *next_child = NULL;
	GList *banner_link;
	g_autoptr(GList) banners = NULL;

	visible_child = gtk_stack_get_visible_child (GTK_STACK (priv->stack_featured));
	banners = gtk_container_get_children (GTK_CONTAINER (priv->stack_featured));
	if (banners == NULL)
		return;

	/* find banner before the currently visible one */
	for (banner_link = banners; banner_link != NULL; banner_link = banner_link->next) {
		GtkWidget *child = banner_link->data;
		if (child == visible_child) {
			if (banner_link->prev != NULL)
				next_child = banner_link->prev->data;
			break;
		}
	}
	if (next_child == NULL)
		next_child = g_list_last(banners)->data;
	gtk_stack_set_visible_child (GTK_STACK (priv->stack_featured), next_child);
}

static gboolean
gs_overview_page_featured_rotate_cb (gpointer user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);
	_feature_banner_forward (self);
	return G_SOURCE_CONTINUE;
}

static void
featured_reset_rotate_timer (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	if (priv->featured_rotate_timer_id != 0)
		g_source_remove (priv->featured_rotate_timer_id);
	priv->featured_rotate_timer_id = g_timeout_add_seconds (FEATURED_ROTATE_TIME,
								gs_overview_page_featured_rotate_cb,
								self);
}

static void
_featured_back_clicked_cb (GsCategoryTile *tile, gpointer data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (data);
	_feature_banner_back (self);
}

static void
_featured_forward_clicked_cb (GsCategoryTile *tile, gpointer data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (data);
	_feature_banner_forward (self);
}

static void
gs_overview_page_get_featured_cb (GObject *source_object,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
		goto out;

	if (priv->featured_rotate_timer_id != 0) {
		g_source_remove (priv->featured_rotate_timer_id);
		priv->featured_rotate_timer_id = 0;
	}

	gs_container_remove_all (GTK_CONTAINER (priv->stack_featured));
	gtk_widget_set_visible (priv->overlay, gs_app_list_length (list) > 0);
	gtk_widget_set_visible (priv->button_featured_back, gs_app_list_length (list) > 1);
	gtk_widget_set_visible (priv->button_featured_forwards, gs_app_list_length (list) > 1);
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

	if (g_getenv ("GNOME_SOFTWARE_FEATURED") == NULL) {
		/* Don't show apps from the category that's currently featured as the category of the day */
		gs_app_list_filter (list, filter_category, priv->category_of_day);
		gs_app_list_filter_duplicates (list, GS_APP_LIST_FILTER_FLAG_KEY_ID);
		gs_app_list_randomize (list);
	}
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GtkWidget *tile = gs_feature_tile_new (app);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (priv->stack_featured), tile);
	}

	priv->empty = FALSE;
	featured_reset_rotate_timer (self);

out:
	gs_overview_page_decrement_action_cnt (self);
}

static void
category_tile_clicked (GsCategoryTile *tile, gpointer data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (data);
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	GsCategory *category;

	category = gs_category_tile_get_category (tile);
	gs_shell_show_category (priv->shell, category);
}

static void
gs_overview_page_get_categories_cb (GObject *source_object,
                                    GAsyncResult *res,
                                    gpointer user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	guint i;
	GsCategory *cat;
	GtkFlowBox *flowbox;
	GtkWidget *tile;
	guint added_cnt = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) list = NULL;

	list = gs_plugin_loader_job_get_categories_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get categories: %s", error->message);
		goto out;
	}
	gs_container_remove_all (GTK_CONTAINER (priv->flowbox_categories));

	/* add categories to the correct flowboxes, the second being hidden */
	for (i = 0; i < list->len; i++) {
		cat = GS_CATEGORY (g_ptr_array_index (list, i));
		if (gs_category_get_size (cat) == 0)
			continue;
		tile = gs_category_tile_new (cat);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (category_tile_clicked), self);
		flowbox = GTK_FLOW_BOX (priv->flowbox_categories);
		gtk_flow_box_insert (flowbox, tile, -1);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
		added_cnt++;

		/* we save these for the 'More...' buttons */
		g_hash_table_insert (priv->category_hash,
				     g_strdup (gs_category_get_id (cat)),
				     g_object_ref (cat));
	}

out:
	if (added_cnt > 0)
		priv->empty = FALSE;
	gtk_widget_set_visible (priv->category_heading, added_cnt > 0);

	gs_overview_page_decrement_action_cnt (self);
}

static const gchar *
gs_overview_page_get_category_label (const gchar *id)
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
gs_overview_page_get_random_categories (void)
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
	rand = g_rand_new_with_seed ((guint32) g_date_time_get_day_of_year (date));
	cats = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; ids[i] != NULL; i++)
		g_ptr_array_add (cats, g_strdup (ids[i]));
	for (i = 0; i < powl (cats->len + 1, 2); i++) {
		gpointer tmp;
		guint rnd1 = (guint) g_rand_int_range (rand, 0, (gint32) cats->len);
		guint rnd2 = (guint) g_rand_int_range (rand, 0, (gint32) cats->len);
		if (rnd1 == rnd2)
			continue;
		tmp = cats->pdata[rnd1];
		cats->pdata[rnd1] = cats->pdata[rnd2];
		cats->pdata[rnd2] = tmp;
	}
	for (i = 0; i < cats->len; i++) {
		const gchar *tmp = g_ptr_array_index (cats, i);
		g_debug ("%u = %s", i + 1, tmp);
	}
	return cats;
}

static void
refresh_third_party_repo (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);

	/* only show if never prompted and third party repo is available */
	if (g_settings_get_boolean (priv->settings, "show-nonfree-prompt") &&
	    priv->third_party_repo != NULL &&
	    gs_app_get_state (priv->third_party_repo) == AS_APP_STATE_AVAILABLE) {
		gtk_widget_set_visible (priv->infobar_third_party, TRUE);
	} else {
		gtk_widget_set_visible (priv->infobar_third_party, FALSE);
	}
}

static void
resolve_third_party_repo_cb (GsPluginLoader *plugin_loader,
                             GAsyncResult *res,
                             GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get the results */
	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_CANCELLED)) {
			g_debug ("resolve third party repo cancelled");
			return;
		} else {
			g_warning ("failed to resolve third party repo: %s", error->message);
			return;
		}
	}

	/* save results for later */
	g_clear_object (&priv->third_party_repo);
	if (gs_app_list_length (list) > 0)
		priv->third_party_repo = g_object_ref (gs_app_list_index (list, 0));

	/* refresh widget */
	refresh_third_party_repo (self);
}

static gboolean
is_fedora (void)
{
	const gchar *id = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;

	os_release = gs_os_release_new (NULL);
	if (os_release == NULL)
		return FALSE;

	id = gs_os_release_get_id (os_release);
	if (g_strcmp0 (id, "fedora") == 0)
		return TRUE;

	return FALSE;
}

static void
reload_third_party_repo (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	const gchar *third_party_repo_package = "fedora-workstation-repositories";
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* only show if never prompted */
	if (!g_settings_get_boolean (priv->settings, "show-nonfree-prompt"))
		return;

	/* Fedora-specific functionality */
	if (!is_fedora ())
		return;

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH_PROVIDES,
	                                 "search", third_party_repo_package,
	                                 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
	                                                 GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES,
	                                 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
	                                    priv->cancellable,
	                                    (GAsyncReadyCallback) resolve_third_party_repo_cb,
	                                    self);
}

static void
gs_overview_page_load (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	guint i;

	priv->empty = TRUE;

	if (!priv->loading_featured) {
		g_autoptr(GsPluginJob) plugin_job = NULL;

		priv->loading_featured = TRUE;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_FEATURED,
						 "max-results", 5,
						 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
								 GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
						 NULL);
		gs_plugin_loader_job_process_async (priv->plugin_loader,
						    plugin_job,
						    priv->cancellable,
						    gs_overview_page_get_featured_cb,
						    self);
		priv->action_cnt++;
	}

	if (!priv->loading_popular) {
		g_autoptr(GsPluginJob) plugin_job = NULL;

		priv->loading_popular = TRUE;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_POPULAR,
						 "max-results", 20,
						 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
								 GS_PLUGIN_REFINE_FLAGS_REQUIRE_CATEGORIES |
								 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
								 GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
						 NULL);
		gs_plugin_loader_job_process_async (priv->plugin_loader,
						    plugin_job,
						    priv->cancellable,
						    gs_overview_page_get_popular_cb,
						    self);
		priv->action_cnt++;
	}

	if (!priv->loading_recent) {
		g_autoptr(GsPluginJob) plugin_job = NULL;

		priv->loading_recent = TRUE;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_RECENT,
						 "age", (guint64) (60 * 60 * 24 * 60),
						 "max-results", 20,
						 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
								 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
								 GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
						 NULL);
		gs_plugin_loader_job_process_async (priv->plugin_loader,
						    plugin_job,
						    priv->cancellable,
						    gs_overview_page_get_recent_cb,
						    self);
		priv->action_cnt++;
	}

	if (!priv->loading_popular_rotating) {
		const guint MAX_CATS = 2;
		g_autoptr(GPtrArray) cats_random = NULL;
		cats_random = gs_overview_page_get_random_categories ();

		/* remove existing widgets, if any */
		gs_container_remove_all (GTK_CONTAINER (priv->box_popular_rotating));

		/* load all the categories */
		for (i = 0; i < cats_random->len && i < MAX_CATS; i++) {
			LoadData *load_data;
			const gchar *cat_id;
			g_autoptr(GsCategory) category = NULL;
			g_autoptr(GsCategory) featured_category = NULL;
			g_autoptr(GsPluginJob) plugin_job = NULL;

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
			load_data->title = gs_overview_page_get_category_label (cat_id);
			plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
							 "max-results", 20,
							 "category", featured_category,
							 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
									 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
							 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
									 GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
							 NULL);
			gs_plugin_loader_job_process_async (priv->plugin_loader,
							    plugin_job,
							    priv->cancellable,
							    gs_overview_page_get_category_apps_cb,
							    load_data);
			priv->action_cnt++;
		}
		priv->loading_popular_rotating = TRUE;
	}

	if (!priv->loading_categories) {
		g_autoptr(GsPluginJob) plugin_job = NULL;
		priv->loading_categories = TRUE;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORIES, NULL);
		gs_plugin_loader_job_get_categories_async (priv->plugin_loader, plugin_job,
							  priv->cancellable,
							  gs_overview_page_get_categories_cb,
							  self);
		priv->action_cnt++;
	}

	reload_third_party_repo (self);
}

static void
gs_overview_page_reload (GsPage *page)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (page);
	gs_overview_page_invalidate (self);
	gs_overview_page_load (self);
}

static void
gs_overview_page_switch_to (GsPage *page, gboolean scroll_up)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (page);
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	GtkWidget *widget;
	GtkAdjustment *adj;

	if (gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_OVERVIEW) {
		g_warning ("Called switch_to(overview) when in mode %s",
			   gs_shell_get_mode_string (priv->shell));
		return;
	}

	/* we hid the search bar */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_button"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "menu_button"));
	gtk_widget_show (widget);

	if (scroll_up) {
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_overview));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	gs_grab_focus_when_mapped (priv->scrolledwindow_overview);

	if (priv->cache_valid || priv->action_cnt > 0)
		return;
	gs_overview_page_load (self);
}

static void
third_party_response_cb (GtkInfoBar *info_bar,
                         gint response_id,
                         GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	g_settings_set_boolean (priv->settings, "show-nonfree-prompt", FALSE);
	if (response_id == GTK_RESPONSE_CLOSE) {
		gtk_widget_hide (priv->infobar_third_party);
		return;
	}
	if (response_id != GTK_RESPONSE_YES)
		return;

	if (gs_app_get_state (priv->third_party_repo) == AS_APP_STATE_AVAILABLE) {
		gs_page_install_app (GS_PAGE (self), priv->third_party_repo,
		                     GS_SHELL_INTERACTION_FULL,
		                     priv->cancellable);
	}

	refresh_third_party_repo (self);
}

static gboolean
gs_overview_page_setup (GsPage *page,
                        GsShell *shell,
                        GsPluginLoader *plugin_loader,
                        GtkBuilder *builder,
                        GCancellable *cancellable,
                        GError **error)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (page);
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	GtkAdjustment *adj;
	GtkWidget *tile;
	gint i;
	g_autoptr(GString) str = g_string_new (NULL);

	g_return_val_if_fail (GS_IS_OVERVIEW_PAGE (self), TRUE);

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);
	priv->category_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
						     g_free, (GDestroyNotify) g_object_unref);

	g_string_append (str,
	                 /* TRANSLATORS: this is the third party repositories info bar. */
	                 _("Access additional software from selected third party sources."));
	g_string_append (str, " ");
	g_string_append (str,
			 /* TRANSLATORS: this is the third party repositories info bar. */
	                 _("Some of this software is proprietary and therefore has restrictions on use, sharing, and access to source code."));
	g_string_append_printf (str, " <a href=\"%s\">%s</a>",
	                        "https://fedoraproject.org/wiki/Workstation/Third_Party_Software_Repositories",
	                        /* TRANSLATORS: this is the clickable
	                         * link on the third party repositories info bar */
	                        _("Find out more…"));
	gtk_label_set_markup (GTK_LABEL (priv->label_third_party), str->str);

	/* create info bar if not already dismissed in initial-setup */
	refresh_third_party_repo (self);
	reload_third_party_repo (self);
	gtk_info_bar_add_button (GTK_INFO_BAR (priv->infobar_third_party),
				 /* TRANSLATORS: button to turn on third party software repositories */
				 _("Enable"), GTK_RESPONSE_YES);
	g_signal_connect (priv->infobar_third_party, "response",
			  G_CALLBACK (third_party_response_cb), self);

	/* avoid a ref cycle */
	priv->shell = shell;

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_overview));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (priv->box_overview), adj);

	tile = gs_feature_tile_new (NULL);
	gtk_container_add (GTK_CONTAINER (priv->stack_featured), tile);

	for (i = 0; i < N_TILES; i++) {
		tile = gs_popular_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (priv->box_popular), tile);
	}

	for (i = 0; i < N_TILES; i++) {
		tile = gs_popular_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (priv->box_recent), tile);
	}

	return TRUE;
}

static void
gs_overview_page_init (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);
	gtk_widget_init_template (GTK_WIDGET (self));

	g_signal_connect (priv->button_featured_back, "clicked",
			  G_CALLBACK (_featured_back_clicked_cb), self);
	g_signal_connect (priv->button_featured_forwards, "clicked",
			  G_CALLBACK (_featured_forward_clicked_cb), self);

	priv->settings = g_settings_new ("org.gnome.software");
}

static void
gs_overview_page_dispose (GObject *object)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (object);
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);

	g_clear_object (&priv->builder);
	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->cancellable);
	g_clear_object (&priv->settings);
	g_clear_object (&priv->third_party_repo);
	g_clear_pointer (&priv->category_of_day, g_free);
	g_clear_pointer (&priv->category_hash, g_hash_table_unref);

	if (priv->featured_rotate_timer_id != 0) {
		g_source_remove (priv->featured_rotate_timer_id);
		priv->featured_rotate_timer_id = 0;
	}

	G_OBJECT_CLASS (gs_overview_page_parent_class)->dispose (object);
}

static void
gs_overview_page_refreshed (GsOverviewPage *self)
{
	GsOverviewPagePrivate *priv = gs_overview_page_get_instance_private (self);

	if (priv->empty) {
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_overview), "no-results");
	} else {
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_overview), "overview");
	}
}

static void
gs_overview_page_class_init (GsOverviewPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_overview_page_dispose;
	page_class->switch_to = gs_overview_page_switch_to;
	page_class->reload = gs_overview_page_reload;
	page_class->setup = gs_overview_page_setup;
	klass->refreshed = gs_overview_page_refreshed;

	signals [SIGNAL_REFRESHED] =
		g_signal_new ("refreshed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsOverviewPageClass, refreshed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-overview-page.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, infobar_third_party);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, label_third_party);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, overlay);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, stack_featured);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, button_featured_back);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, button_featured_forwards);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, box_overview);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, box_popular);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, box_popular_rotating);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, box_recent);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, category_heading);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, flowbox_categories);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, popular_heading);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, recent_heading);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, scrolledwindow_overview);
	gtk_widget_class_bind_template_child_private (widget_class, GsOverviewPage, stack_overview);
}

GsOverviewPage *
gs_overview_page_new (void)
{
	return GS_OVERVIEW_PAGE (g_object_new (GS_TYPE_OVERVIEW_PAGE, NULL));
}
