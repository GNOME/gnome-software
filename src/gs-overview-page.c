/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <handy.h>
#include <math.h>

#include "gs-shell.h"
#include "gs-overview-page.h"
#include "gs-app-list-private.h"
#include "gs-featured-carousel.h"
#include "gs-category-tile.h"
#include "gs-common.h"
#include "gs-summary-tile.h"

#define N_TILES					9

struct _GsOverviewPage
{
	GsPage			 parent_instance;

	GsPluginLoader		*plugin_loader;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
	GsShell			*shell;
	gint			 action_cnt;
	gboolean		 loading_featured;
	gboolean		 loading_popular;
	gboolean		 loading_recent;
	gboolean		 loading_categories;
	gboolean		 empty;
	gchar			*category_of_day;
	GHashTable		*category_hash;		/* id : GsCategory */
	GSettings		*settings;
	GsApp			*third_party_repo;

	GtkWidget		*infobar_third_party;
	GtkWidget		*label_third_party;
	GtkWidget		*featured_carousel;
	GtkWidget		*box_overview;
	GtkWidget		*box_popular;
	GtkWidget		*box_recent;
	GtkWidget		*category_heading;
	GtkWidget		*flowbox_categories;
	GtkWidget		*popular_heading;
	GtkWidget		*recent_heading;
	GtkWidget		*scrolledwindow_overview;
	GtkWidget		*stack_overview;
};

G_DEFINE_TYPE (GsOverviewPage, gs_overview_page, GS_TYPE_PAGE)

typedef enum {
	PROP_VADJUSTMENT = 1,
	PROP_TITLE,
} GsOverviewPageProperty;

enum {
	SIGNAL_REFRESHED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

static void
gs_overview_page_invalidate (GsOverviewPage *self)
{
	self->cache_valid = FALSE;
}

static void
app_tile_clicked (GsAppTile *tile, gpointer data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (data);
	GsApp *app;

	app = gs_app_tile_get_app (tile);
	gs_shell_show_app (self->shell, app);
}

static void
featured_carousel_app_clicked_cb (GsFeaturedCarousel *carousel,
                                  GsApp              *app,
                                  gpointer            user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);

	gs_shell_show_app (self->shell, app);
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
	/* every job increments this */
	if (self->action_cnt == 0) {
		g_warning ("action_cnt already zero!");
		return;
	}
	if (--self->action_cnt > 0)
		return;

	/* all done */
	self->cache_valid = TRUE;
	g_signal_emit (self, signals[SIGNAL_REFRESHED], 0);
	self->loading_categories = FALSE;
	self->loading_featured = FALSE;
	self->loading_popular = FALSE;
	self->loading_recent = FALSE;
}

static void
gs_overview_page_get_popular_cb (GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);
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
		gtk_widget_set_visible (self->box_popular, FALSE);
		gtk_widget_set_visible (self->popular_heading, FALSE);
		goto out;
	}

	/* Don't show apps from the category that's currently featured as the category of the day */
	gs_app_list_filter (list, filter_category, self->category_of_day);
	gs_app_list_randomize (list);

	gs_container_remove_all (GTK_CONTAINER (self->box_popular));

	for (i = 0; i < gs_app_list_length (list) && i < N_TILES; i++) {
		app = gs_app_list_index (list, i);
		tile = gs_summary_tile_new (app);
		g_signal_connect (tile, "clicked",
			  G_CALLBACK (app_tile_clicked), self);
		gtk_container_add (GTK_CONTAINER (self->box_popular), tile);
	}
	gtk_widget_set_visible (self->box_popular, TRUE);
	gtk_widget_set_visible (self->popular_heading, TRUE);

	self->empty = FALSE;

out:
	gs_overview_page_decrement_action_cnt (self);
}

static void
gs_overview_page_get_recent_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	guint i;
	GsApp *app;
	GtkWidget *tile;
	GtkWidget *child;
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
		gtk_widget_set_visible (self->box_recent, FALSE);
		gtk_widget_set_visible (self->recent_heading, FALSE);
		goto out;
	}

	/* Don't show apps from the category that's currently featured as the category of the day */
	gs_app_list_filter (list, filter_category, self->category_of_day);
	gs_app_list_randomize (list);

	gs_container_remove_all (GTK_CONTAINER (self->box_recent));

	for (i = 0; i < gs_app_list_length (list) && i < N_TILES; i++) {
		app = gs_app_list_index (list, i);
		tile = gs_summary_tile_new (app);
		g_signal_connect (tile, "clicked",
			  G_CALLBACK (app_tile_clicked), self);
		child = gtk_flow_box_child_new ();
		/* Manually creating the child is needed to avoid having it be
		 * focusable but non activatable, and then have the child
		 * focusable and activatable, which is annoying and confusing.
		 */
		gtk_widget_set_can_focus (child, FALSE);
		gtk_widget_show (child);
		gtk_container_add (GTK_CONTAINER (child), tile);
		gtk_container_add (GTK_CONTAINER (self->box_recent), child);
	}
	gtk_widget_set_visible (self->box_recent, TRUE);
	gtk_widget_set_visible (self->recent_heading, TRUE);

	self->empty = FALSE;

out:
	gs_overview_page_decrement_action_cnt (self);
}

static gboolean
filter_hi_res_icon (GsApp *app, gpointer user_data)
{
	g_autoptr(GIcon) icon = NULL;
	GtkWidget *overview_page = GTK_WIDGET (user_data);

	/* This is the minimum icon size needed by `GsFeatureTile`. */
	icon = gs_app_get_icon_for_size (app,
					 128,
					 gtk_widget_get_scale_factor (overview_page),
					 NULL);

	/* Returning TRUE means to keep the app in the list */
	return (icon != NULL);
}

static void
gs_overview_page_get_featured_cb (GObject *source_object,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
		goto out;

	if (list == NULL || gs_app_list_length (list) == 0) {
		g_warning ("failed to get featured apps: %s",
			   (error != NULL) ? error->message : "no apps to show");
		gtk_widget_set_visible (self->featured_carousel, FALSE);
		goto out;
	}

	if (g_getenv ("GNOME_SOFTWARE_FEATURED") == NULL) {
		/* Don't show apps from the category that's currently featured as the category of the day */
		gs_app_list_filter (list, filter_category, self->category_of_day);
		gs_app_list_filter_duplicates (list, GS_APP_LIST_FILTER_FLAG_KEY_ID);
		gs_app_list_randomize (list);
	}

	/* Filter out apps which don’t have a suitable hi-res icon. */
	gs_app_list_filter (list, filter_hi_res_icon, self);

	gtk_widget_set_visible (self->featured_carousel, gs_app_list_length (list) > 0);
	gs_featured_carousel_set_apps (GS_FEATURED_CAROUSEL (self->featured_carousel), list);

	self->empty = self->empty && (gs_app_list_length (list) == 0);

out:
	gs_overview_page_decrement_action_cnt (self);
}

static void
category_tile_clicked (GsCategoryTile *tile, gpointer data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (data);
	GsCategory *category;

	category = gs_category_tile_get_category (tile);
	gs_shell_show_category (self->shell, category);
}

static void
gs_overview_page_get_categories_cb (GObject *source_object,
                                    GAsyncResult *res,
                                    gpointer user_data)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (user_data);
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
	gs_container_remove_all (GTK_CONTAINER (self->flowbox_categories));

	/* add categories to the correct flowboxes, the second being hidden */
	for (i = 0; i < list->len; i++) {
		cat = GS_CATEGORY (g_ptr_array_index (list, i));
		if (gs_category_get_size (cat) == 0)
			continue;
		tile = gs_category_tile_new (cat);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (category_tile_clicked), self);
		flowbox = GTK_FLOW_BOX (self->flowbox_categories);
		gtk_flow_box_insert (flowbox, tile, -1);
		gtk_widget_set_can_focus (gtk_widget_get_parent (tile), FALSE);
		added_cnt++;

		/* we save these for the 'More...' buttons */
		g_hash_table_insert (self->category_hash,
				     g_strdup (gs_category_get_id (cat)),
				     g_object_ref (cat));
	}

out:
	if (added_cnt > 0)
		self->empty = FALSE;
	gtk_widget_set_visible (self->category_heading, added_cnt > 0);

	gs_overview_page_decrement_action_cnt (self);
}

static void
refresh_third_party_repo (GsOverviewPage *self)
{
	/* only show if never prompted and third party repo is available */
	if (g_settings_get_boolean (self->settings, "show-nonfree-prompt") &&
	    self->third_party_repo != NULL &&
	    gs_app_get_state (self->third_party_repo) == GS_APP_STATE_AVAILABLE) {
		gtk_widget_set_visible (self->infobar_third_party, TRUE);
	} else {
		gtk_widget_set_visible (self->infobar_third_party, FALSE);
	}
}

static void
resolve_third_party_repo_cb (GsPluginLoader *plugin_loader,
                             GAsyncResult *res,
                             GsOverviewPage *self)
{
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
	g_clear_object (&self->third_party_repo);
	if (gs_app_list_length (list) > 0)
		self->third_party_repo = g_object_ref (gs_app_list_index (list, 0));

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
	const gchar *third_party_repo_package = "fedora-workstation-repositories";
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* only show if never prompted */
	if (!g_settings_get_boolean (self->settings, "show-nonfree-prompt"))
		return;

	/* Fedora-specific functionality */
	if (!is_fedora ())
		return;

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH_PROVIDES,
	                                 "search", third_party_repo_package,
	                                 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
	                                                 GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES,
	                                 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
	                                    self->cancellable,
	                                    (GAsyncReadyCallback) resolve_third_party_repo_cb,
	                                    self);
}

static void
gs_overview_page_load (GsOverviewPage *self)
{
	self->empty = TRUE;

	if (!self->loading_featured) {
		g_autoptr(GsPluginJob) plugin_job = NULL;

		self->loading_featured = TRUE;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_FEATURED,
						 "max-results", 5,
						 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
								 GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
						 NULL);
		gs_plugin_loader_job_process_async (self->plugin_loader,
						    plugin_job,
						    self->cancellable,
						    gs_overview_page_get_featured_cb,
						    self);
		self->action_cnt++;
	}

	if (!self->loading_popular) {
		g_autoptr(GsPluginJob) plugin_job = NULL;

		self->loading_popular = TRUE;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_POPULAR,
						 "max-results", 20,
						 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
								 GS_PLUGIN_REFINE_FLAGS_REQUIRE_CATEGORIES |
								 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
								 GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
						 NULL);
		gs_plugin_loader_job_process_async (self->plugin_loader,
						    plugin_job,
						    self->cancellable,
						    gs_overview_page_get_popular_cb,
						    self);
		self->action_cnt++;
	}

	if (!self->loading_recent) {
		g_autoptr(GsPluginJob) plugin_job = NULL;

		self->loading_recent = TRUE;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_RECENT,
						 "age", (guint64) (60 * 60 * 24 * 60),
						 "max-results", 20,
						 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
								 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED |
								 GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES,
						 NULL);
		gs_plugin_loader_job_process_async (self->plugin_loader,
						    plugin_job,
						    self->cancellable,
						    gs_overview_page_get_recent_cb,
						    self);
		self->action_cnt++;
	}

	if (!self->loading_categories) {
		g_autoptr(GsPluginJob) plugin_job = NULL;
		self->loading_categories = TRUE;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_CATEGORIES, NULL);
		gs_plugin_loader_job_get_categories_async (self->plugin_loader, plugin_job,
							  self->cancellable,
							  gs_overview_page_get_categories_cb,
							  self);
		self->action_cnt++;
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
gs_overview_page_switch_to (GsPage *page)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (page);

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_OVERVIEW) {
		g_warning ("Called switch_to(overview) when in mode %s",
			   gs_shell_get_mode_string (self->shell));
		return;
	}

	gs_grab_focus_when_mapped (self->scrolledwindow_overview);

	if (self->cache_valid || self->action_cnt > 0)
		return;
	gs_overview_page_load (self);
}

static void
third_party_response_cb (GtkInfoBar *info_bar,
                         gint response_id,
                         GsOverviewPage *self)
{
	g_settings_set_boolean (self->settings, "show-nonfree-prompt", FALSE);
	if (response_id == GTK_RESPONSE_CLOSE) {
		gtk_widget_hide (self->infobar_third_party);
		return;
	}
	if (response_id != GTK_RESPONSE_YES)
		return;

	if (gs_app_get_state (self->third_party_repo) == GS_APP_STATE_AVAILABLE) {
		gs_page_install_app (GS_PAGE (self), self->third_party_repo,
		                     GS_SHELL_INTERACTION_FULL,
		                     self->cancellable);
	}

	refresh_third_party_repo (self);
}

static gboolean
gs_overview_page_setup (GsPage *page,
                        GsShell *shell,
                        GsPluginLoader *plugin_loader,
                        GCancellable *cancellable,
                        GError **error)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (page);
	GtkAdjustment *adj;
	GtkWidget *tile;
	gint i;
	g_autoptr(GString) str = g_string_new (NULL);

	g_return_val_if_fail (GS_IS_OVERVIEW_PAGE (self), TRUE);

	self->plugin_loader = g_object_ref (plugin_loader);
	self->cancellable = g_object_ref (cancellable);
	self->category_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
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
	gtk_label_set_markup (GTK_LABEL (self->label_third_party), str->str);

	/* create info bar if not already dismissed in initial-setup */
	refresh_third_party_repo (self);
	reload_third_party_repo (self);
	gtk_info_bar_add_button (GTK_INFO_BAR (self->infobar_third_party),
				 /* TRANSLATORS: button to turn on third party software repositories */
				 _("Enable"), GTK_RESPONSE_YES);
	g_signal_connect (self->infobar_third_party, "response",
			  G_CALLBACK (third_party_response_cb), self);

	/* avoid a ref cycle */
	self->shell = shell;

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_overview));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (self->box_overview), adj);

	for (i = 0; i < N_TILES; i++) {
		tile = gs_summary_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (self->box_popular), tile);
	}

	for (i = 0; i < N_TILES; i++) {
		tile = gs_summary_tile_new (NULL);
		gtk_container_add (GTK_CONTAINER (self->box_recent), tile);
	}

	return TRUE;
}

static void
refreshed_cb (GsOverviewPage *self, gpointer user_data)
{
	if (self->empty) {
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_overview), "no-results");
	} else {
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_overview), "overview");
	}
}

static void
gs_overview_page_init (GsOverviewPage *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	g_signal_connect (self, "refreshed", G_CALLBACK (refreshed_cb), self);

	self->settings = g_settings_new ("org.gnome.software");
}

static void
gs_overview_page_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (object);

	switch ((GsOverviewPageProperty) prop_id) {
	case PROP_VADJUSTMENT:
		g_value_set_object (value, gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_overview)));
		break;
	case PROP_TITLE:
		/* Translators: This is the title of the main page of the UI. */
		g_value_set_string (value, _("Explore"));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_overview_page_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	switch ((GsOverviewPageProperty) prop_id) {
	case PROP_VADJUSTMENT:
	case PROP_TITLE:
		/* Read only. */
		g_assert_not_reached ();
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_overview_page_dispose (GObject *object)
{
	GsOverviewPage *self = GS_OVERVIEW_PAGE (object);

	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);
	g_clear_object (&self->settings);
	g_clear_object (&self->third_party_repo);
	g_clear_pointer (&self->category_of_day, g_free);
	g_clear_pointer (&self->category_hash, g_hash_table_unref);

	G_OBJECT_CLASS (gs_overview_page_parent_class)->dispose (object);
}

static void
gs_overview_page_class_init (GsOverviewPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_overview_page_get_property;
	object_class->set_property = gs_overview_page_set_property;
	object_class->dispose = gs_overview_page_dispose;

	page_class->switch_to = gs_overview_page_switch_to;
	page_class->reload = gs_overview_page_reload;
	page_class->setup = gs_overview_page_setup;

	g_object_class_override_property (object_class, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property (object_class, PROP_TITLE, "title");

	signals [SIGNAL_REFRESHED] =
		g_signal_new ("refreshed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-overview-page.ui");

	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, infobar_third_party);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, label_third_party);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, featured_carousel);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, box_overview);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, box_popular);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, box_recent);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, category_heading);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, flowbox_categories);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, popular_heading);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, recent_heading);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, scrolledwindow_overview);
	gtk_widget_class_bind_template_child (widget_class, GsOverviewPage, stack_overview);
	gtk_widget_class_bind_template_callback (widget_class, featured_carousel_app_clicked_cb);
}

GsOverviewPage *
gs_overview_page_new (void)
{
	return GS_OVERVIEW_PAGE (g_object_new (GS_TYPE_OVERVIEW_PAGE, NULL));
}
