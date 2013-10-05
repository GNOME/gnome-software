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

struct GsShellOverviewPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
	GsShell			*shell;
	gint			 refresh_count;
	gboolean		 empty;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsShellOverview, gs_shell_overview, G_TYPE_OBJECT)

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
	GtkWidget *grid;

	/* get popular apps */
	list = gs_plugin_loader_get_popular_finish (plugin_loader, res, &error);
	gtk_widget_set_visible (GTK_WIDGET (gtk_builder_get_object (priv->builder, "popular_heading")), list != NULL);
	if (list == NULL) {
		g_warning ("failed to get popular apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_popular"));
	gs_container_remove_all (GTK_CONTAINER (grid));

	for (l = list, i = 0; l != NULL && i < 6; l = l->next, i++) {
		app = GS_APP (l->data);
		tile = gs_popular_tile_new (app);
		g_signal_connect (tile, "clicked",
			  G_CALLBACK (popular_tile_clicked), shell);
		gtk_box_pack_start (GTK_BOX (grid), tile, TRUE, TRUE, 0);
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
	GtkWidget *box;
	GList *list;
	GError *error = NULL;
	GsApp *app;

	list = gs_plugin_loader_get_featured_finish (plugin_loader, res, &error);
	gtk_widget_set_visible (GTK_WIDGET (gtk_builder_get_object (priv->builder, "featured_heading")), list != NULL);
	if (list == NULL) {
		g_warning ("failed to get featured apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	box = GTK_WIDGET (gtk_builder_get_object (priv->builder, "bin_featured"));
	gs_container_remove_all (GTK_CONTAINER (box));

	/* at the moment, we only care about the first app */
	app = GS_APP (list->data);
	tile = gs_feature_tile_new (app);
	g_signal_connect (tile, "clicked",
			  G_CALLBACK (feature_tile_clicked), shell);

	gtk_container_add (GTK_CONTAINER (box), tile);

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
	GtkWidget *grid;
	GtkWidget *tile;
	gboolean has_category = FALSE;

	list = gs_plugin_loader_get_categories_finish (plugin_loader, res, &error);
	if (list == NULL) {
		g_warning ("failed to get categories: %s", error->message);
		g_error_free (error);
		goto out;
	}
	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_categories"));
	gs_container_remove_all (GTK_CONTAINER (grid));

	for (l = list, i = 0; l; l = l->next) {
		cat = GS_CATEGORY (l->data);
		if (gs_category_get_size (cat) == 0)
			continue;
		tile = gs_category_tile_new (cat);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (category_tile_clicked), shell);
		gtk_grid_attach (GTK_GRID (grid), tile, i % 3, i / 3, 1, 1);
		i++;
		has_category = TRUE;
	}
out:
	gs_plugin_list_free (list);
	if (has_category) {
		priv->empty = FALSE;
	}
	gtk_widget_set_visible (GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_heading")), has_category);

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
	GsShellOverviewPrivate *priv = shell->priv;
	GtkWidget *widget;
	GtkAdjustment *adj;

	if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_OVERVIEW) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_entry_set_text (GTK_ENTRY (widget), "");
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_overview"));
	if (scroll_up) {
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (widget));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_OVERVIEW) {
		gs_grab_focus_when_mapped (widget);
	}

	if (priv->cache_valid || priv->refresh_count > 0)
		return;

	priv->empty = TRUE;
	priv->refresh_count = 3;

	gs_plugin_loader_get_featured_async (priv->plugin_loader,
					     GS_PLUGIN_LOADER_FLAGS_NONE,
					     priv->cancellable,
					     gs_shell_overview_get_featured_cb,
					     shell);

	gs_plugin_loader_get_popular_async (priv->plugin_loader,
					    GS_PLUGIN_LOADER_FLAGS_NONE,
					    priv->cancellable,
					    gs_shell_overview_get_popular_cb,
					    shell);

	gs_plugin_loader_get_categories_async (priv->plugin_loader,
					       GS_PLUGIN_LOADER_FLAGS_NONE,
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
	GtkWidget *sw, *widget;
	GtkAdjustment *adj;
	GtkWidget *grid, *tile;
	gint i;

	g_return_if_fail (GS_IS_SHELL_OVERVIEW (shell_overview));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* avoid a ref cycle */
	priv->shell = shell;

	sw = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow_overview"));
	adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (sw));
 	widget = GTK_WIDGET (gtk_builder_get_object (builder, "box_overview"));
	gtk_container_set_focus_vadjustment (GTK_CONTAINER (widget), adj);

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "bin_featured"));
	tile = gs_feature_tile_new (NULL);
	gtk_container_add (GTK_CONTAINER (grid), tile);

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_popular"));
	for (i = 0; i < 6; i++) {
		tile = gs_popular_tile_new (NULL);
		gtk_box_pack_start (GTK_BOX (grid), tile, TRUE, TRUE, 0);
	}
}

static void
gs_shell_overview_init (GsShellOverview *shell)
{
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
	GtkWidget *stack;

	stack = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_overview"));
	if (priv->empty) {
		gtk_stack_set_visible_child_name (GTK_STACK (stack), "no-results");
	} else {
		gtk_stack_set_visible_child_name (GTK_STACK (stack), "overview");
	}
}

/**
 * gs_shell_overview_class_init:
 **/
static void
gs_shell_overview_class_init (GsShellOverviewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_overview_finalize;
	klass->refreshed = gs_shell_overview_refreshed;

	signals [SIGNAL_REFRESHED] =
		g_signal_new ("refreshed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsShellOverviewClass, refreshed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
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
