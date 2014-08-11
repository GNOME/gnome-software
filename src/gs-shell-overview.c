/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "gs-shell.h"
#include "gs-shell-overview.h"
#include "gs-app.h"
#include "gs-category.h"
#include "gs-popular-tile.h"
#include "gs-feature-tile.h"
#include "gs-category-tile.h"
#include "gs-utils.h"

#define N_TILES 6

struct GsShellOverviewPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
	GsShell			*shell;
	gint			 refresh_count;
	gboolean		 empty;

	GtkWidget		*bin_featured;
	GtkWidget		*box_overview;
	GtkWidget		*box_popular;
	GtkWidget		*box_popular_rotating;
	GtkWidget		*category_heading;
	GtkWidget		*featured_heading;
	GtkWidget		*grid_categories;
	GtkWidget		*popular_heading;
	GtkWidget		*popular_rotating_heading;
	GtkWidget		*scrolledwindow_overview;
	GtkWidget		*stack_overview;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsShellOverview, gs_shell_overview, GTK_TYPE_BIN)

enum {
	SIGNAL_REFRESHED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_shell_overview_invalidate:
 **/
void
gs_shell_overview_invalidate (GsShellOverview *shell_overview)
{
	shell_overview->priv->cache_valid = FALSE;
}

static void
popular_tile_clicked (GsPopularTile *tile, gpointer data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (data);
	GsApp *app;

	app = gs_popular_tile_get_app (tile);
	gs_shell_show_app (shell->priv->shell, app);
}

/**
 * gs_shell_overview_get_popular_cb:
 **/
static void
gs_shell_overview_get_popular_cb (GObject *source_object,
				  GAsyncResult *res,
				  gpointer user_data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	gint i;
	GtkWidget *tile;

	/* get popular apps */
	list = gs_plugin_loader_get_popular_finish (plugin_loader, res, &error);
	gtk_widget_set_visible (priv->popular_heading, list != NULL);
	if (list == NULL) {
		g_warning ("failed to get popular apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	gs_container_remove_all (GTK_CONTAINER (priv->box_popular));

	for (l = list, i = 0; l != NULL && i < N_TILES; l = l->next, i++) {
		app = GS_APP (l->data);
		tile = gs_popular_tile_new (app);
		g_signal_connect (tile, "clicked",
			  G_CALLBACK (popular_tile_clicked), shell);
		gtk_box_pack_start (GTK_BOX (priv->box_popular), tile, TRUE, TRUE, 0);
	}

	priv->empty = FALSE;

out:
	gs_plugin_list_free (list);
	priv->refresh_count--;
	if (priv->refresh_count == 0)
		g_signal_emit (shell, signals[SIGNAL_REFRESHED], 0);
}

static void
gs_shell_overview_get_popular_rotating_cb (GObject *source_object,
                                           GAsyncResult *res,
                                           gpointer user_data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	gint i;
	GtkWidget *tile;

	/* get popular apps */
	list = gs_plugin_loader_get_popular_finish (plugin_loader, res, &error);
	if (list == NULL) {
		g_warning ("failed to get recommended applications: %s", error->message);
		g_error_free (error);
		gtk_widget_hide (priv->popular_rotating_heading);
		gtk_widget_hide (priv->box_popular_rotating);
		goto out;
	} else if (g_list_length (list) < N_TILES) {
		g_warning ("hiding recommended applications: found only %d to show, need at least %d", g_list_length (list), N_TILES);
		gtk_widget_hide (priv->popular_rotating_heading);
		gtk_widget_hide (priv->box_popular_rotating);
		goto out;
	}

	gtk_widget_show (priv->popular_rotating_heading);
	gtk_widget_show (priv->box_popular_rotating);

	gs_container_remove_all (GTK_CONTAINER (priv->box_popular_rotating));

	for (l = list, i = 0; l != NULL && i < N_TILES; l = l->next, i++) {
		app = GS_APP (l->data);
		tile = gs_popular_tile_new (app);
		g_signal_connect (tile, "clicked",
			  G_CALLBACK (popular_tile_clicked), shell);
		gtk_box_pack_start (GTK_BOX (priv->box_popular_rotating), tile, TRUE, TRUE, 0);
	}

	priv->empty = FALSE;

out:
	gs_plugin_list_free (list);
	priv->refresh_count--;
	if (priv->refresh_count == 0)
		g_signal_emit (shell, signals[SIGNAL_REFRESHED], 0);
}

static void
feature_tile_clicked (GsFeatureTile *tile, gpointer data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (data);
	GsApp *app;

	app = gs_feature_tile_get_app (tile);
	gs_shell_show_app (shell->priv->shell, app);
}

static void
gs_shell_overview_get_featured_cb (GObject *source_object,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *tile;
	GList *list;
	GError *error = NULL;
	GsApp *app;

	gs_container_remove_all (GTK_CONTAINER (priv->bin_featured));

	list = gs_plugin_loader_get_featured_finish (plugin_loader, res, &error);
	gtk_widget_set_visible (priv->featured_heading, list != NULL);
	if (list == NULL) {
		g_warning ("failed to get featured apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* at the moment, we only care about the first app */
	app = GS_APP (list->data);
	tile = gs_feature_tile_new (app);
	g_signal_connect (tile, "clicked",
			  G_CALLBACK (feature_tile_clicked), shell);

	gtk_container_add (GTK_CONTAINER (priv->bin_featured), tile);

	priv->empty = FALSE;

out:
	gs_plugin_list_free (list);
	priv->refresh_count--;
	if (priv->refresh_count == 0)
		g_signal_emit (shell, signals[SIGNAL_REFRESHED], 0);
}

static void
category_tile_clicked (GsCategoryTile *tile, gpointer data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (data);
	GsCategory *category;

	category = gs_category_tile_get_category (tile);
	gs_shell_show_category (shell->priv->shell, category);
}

/**
 * gs_shell_overview_get_categories_cb:
 **/
static void
gs_shell_overview_get_categories_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GError *error = NULL;
	gint i;
	GList *l;
	GList *list;
	GsCategory *cat;
	GtkWidget *tile;
	gboolean has_category = FALSE;

	list = gs_plugin_loader_get_categories_finish (plugin_loader, res, &error);
	if (list == NULL) {
		g_warning ("failed to get categories: %s", error->message);
		g_error_free (error);
		goto out;
	}
	gs_container_remove_all (GTK_CONTAINER (priv->grid_categories));

	for (l = list, i = 0; l; l = l->next) {
		cat = GS_CATEGORY (l->data);
		if (gs_category_get_size (cat) == 0)
			continue;
		tile = gs_category_tile_new (cat);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (category_tile_clicked), shell);
		gtk_grid_attach (GTK_GRID (priv->grid_categories), tile, i % 4, i / 4, 1, 1);
		i++;
		has_category = TRUE;
	}
out:
	gs_plugin_list_free (list);
	if (has_category) {
		priv->empty = FALSE;
	}
	gtk_widget_set_visible (priv->category_heading, has_category);

	priv->cache_valid = TRUE;
	priv->refresh_count--;
	if (priv->refresh_count == 0)
		g_signal_emit (shell, signals[SIGNAL_REFRESHED], 0);
}

/**
 * gs_shell_overview_refresh:
 **/
void
gs_shell_overview_refresh (GsShellOverview *shell, gboolean scroll_up)
{
	GDateTime *date;
	GsShellOverviewPrivate *priv = shell->priv;
	GtkWidget *widget;
	GtkAdjustment *adj;
	const gchar *category_of_day;

	if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_OVERVIEW) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_entry_set_text (GTK_ENTRY (widget), "");
	}

	if (scroll_up) {
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_overview));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_OVERVIEW) {
		gs_grab_focus_when_mapped (priv->scrolledwindow_overview);
	}

	if (priv->cache_valid || priv->refresh_count > 0)
		return;

	priv->empty = TRUE;
	priv->refresh_count = 4;

	date = g_date_time_new_now_utc ();
	switch (g_date_time_get_day_of_year (date) % 4) {
	case 0:
		category_of_day = "Audio";
		/* TRANSLATORS: this is a heading for audio applications which have been featured ('recommended') by the distribution */
		gtk_label_set_label (GTK_LABEL (priv->popular_rotating_heading), _("Recommended Audio Applications"));
		break;
	case 1:
		category_of_day = "Game";
		/* TRANSLATORS: this is a heading for games which have been featured ('recommended') by the distribution */
		gtk_label_set_label (GTK_LABEL (priv->popular_rotating_heading), _("Recommended Games"));
		break;
	case 2:
		category_of_day = "Graphics";
		/* TRANSLATORS: this is a heading for graphics applications which have been featured ('recommended') by the distribution */
		gtk_label_set_label (GTK_LABEL (priv->popular_rotating_heading), _("Recommended Graphics Applications"));
		break;
	case 3:
		category_of_day = "Office";
		/* TRANSLATORS: this is a heading for office applications which have been featured ('recommended') by the distribution */
		gtk_label_set_label (GTK_LABEL (priv->popular_rotating_heading), _("Recommended Office Applications"));
		break;
	}
	g_date_time_unref (date);

	gs_plugin_loader_get_featured_async (priv->plugin_loader,
					     GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					     priv->cancellable,
					     gs_shell_overview_get_featured_cb,
					     shell);

	gs_plugin_loader_get_popular_async (priv->plugin_loader,
					    GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					    NULL,
					    priv->cancellable,
					    gs_shell_overview_get_popular_cb,
					    shell);

	gs_plugin_loader_get_popular_async (priv->plugin_loader,
					    GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					    category_of_day,
					    priv->cancellable,
					    gs_shell_overview_get_popular_rotating_cb,
					    shell);

	gs_plugin_loader_get_categories_async (priv->plugin_loader,
					       GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					       priv->cancellable,
					       gs_shell_overview_get_categories_cb,
					       shell);
}

void
gs_shell_overview_setup (GsShellOverview *shell_overview,
			 GsShell *shell,
			 GsPluginLoader *plugin_loader,
			 GtkBuilder *builder,
			 GCancellable *cancellable)
{
	GsShellOverviewPrivate *priv = shell_overview->priv;
	GtkAdjustment *adj;
	GtkWidget *tile;
	gint i;

	g_return_if_fail (GS_IS_SHELL_OVERVIEW (shell_overview));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* avoid a ref cycle */
	priv->shell = shell;

	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_overview));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (priv->box_overview), adj);

	tile = gs_feature_tile_new (NULL);
	gtk_container_add (GTK_CONTAINER (priv->bin_featured), tile);

	for (i = 0; i < N_TILES; i++) {
		tile = gs_popular_tile_new (NULL);
		gtk_box_pack_start (GTK_BOX (priv->box_popular), tile, TRUE, TRUE, 0);

		tile = gs_popular_tile_new (NULL);
		gtk_box_pack_start (GTK_BOX (priv->box_popular_rotating), tile, TRUE, TRUE, 0);
	}
}

static void
gs_shell_overview_init (GsShellOverview *shell)
{
	gtk_widget_init_template (GTK_WIDGET (shell));

	shell->priv = gs_shell_overview_get_instance_private (shell);
}

static void
gs_shell_overview_finalize (GObject *object)
{
	GsShellOverview *shell = GS_SHELL_OVERVIEW (object);
	GsShellOverviewPrivate *priv = shell->priv;

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);

	G_OBJECT_CLASS (gs_shell_overview_parent_class)->finalize (object);
}

static void
gs_shell_overview_refreshed (GsShellOverview *shell)
{
	GsShellOverviewPrivate *priv = shell->priv;

	if (priv->empty) {
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_overview), "no-results");
	} else {
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_overview), "overview");
	}
}

/**
 * gs_shell_overview_class_init:
 **/
static void
gs_shell_overview_class_init (GsShellOverviewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = gs_shell_overview_finalize;
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
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, box_popular_rotating);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, category_heading);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, featured_heading);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, grid_categories);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, popular_heading);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, popular_rotating_heading);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, scrolledwindow_overview);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellOverview, stack_overview);
}

/**
 * gs_shell_overview_new:
 **/
GsShellOverview *
gs_shell_overview_new (void)
{
	return GS_SHELL_OVERVIEW (g_object_new (GS_TYPE_SHELL_OVERVIEW, NULL));
}

/* vim: set noexpandtab: */
