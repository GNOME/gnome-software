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
#include "gs-app-widget.h"
#include "gs-utils.h"

static void	gs_shell_overview_finalize	(GObject	*object);

#define GS_SHELL_OVERVIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_OVERVIEW, GsShellOverviewPrivate))

struct GsShellOverviewPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	gboolean		 cache_valid;
        GsShell                 *shell;
        gint                     refresh_count;
};

G_DEFINE_TYPE (GsShellOverview, gs_shell_overview, G_TYPE_OBJECT)

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
container_remove_all (GtkContainer *container)
{
	GList *children, *l;
	children = gtk_container_get_children (container);
	for (l = children; l; l = l->next)
		gtk_container_remove (container, GTK_WIDGET (l->data));
	g_list_free (children);
}

static void
app_tile_clicked (GtkButton *button, gpointer data)
{
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (data);
	GsApp *app;

	app = g_object_get_data (G_OBJECT (button), "app");
        gs_shell_show_app (shell_overview->priv->shell, app);
}

static gboolean
transform_state_func (GBinding *binding,
                      const GValue *source,
                      GValue *target,
                      gpointer user_data)
{
        GsAppState state;
        gboolean installed;

        state = g_value_get_uint (source);

        switch (state) {
        case GS_APP_STATE_INSTALLED:
        case GS_APP_STATE_INSTALLING:
        case GS_APP_STATE_REMOVING:
        case GS_APP_STATE_UPDATABLE:
                installed = TRUE;
                break;
        case GS_APP_STATE_AVAILABLE:
        default:
                installed = FALSE;
                break;
        }
        g_value_set_boolean (target, installed);

        return TRUE;
}

static GtkWidget *
create_popular_tile (GsShellOverview *shell_overview, GsApp *app)
{
	GtkWidget *button, *frame, *box, *image, *label;
	GtkWidget *f, *o, *e;

	f = gtk_aspect_frame_new (NULL, 0.5, 0, 1, FALSE);
	gtk_widget_set_valign (f, GTK_ALIGN_START);
	gtk_frame_set_shadow_type (GTK_FRAME (f), GTK_SHADOW_NONE);
        gtk_widget_set_size_request (f, -1, 180);
	button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	frame = gtk_aspect_frame_new (NULL, 0.5, 1, 1, FALSE);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
	gtk_widget_set_halign (frame, GTK_ALIGN_FILL);
	gtk_widget_set_valign (frame, GTK_ALIGN_FILL);
        o = gtk_overlay_new ();
	gtk_widget_set_halign (o, GTK_ALIGN_FILL);
	gtk_widget_set_valign (o, GTK_ALIGN_FILL);
        e = gtk_event_box_new ();
        gtk_widget_set_no_show_all (e, TRUE);
        g_object_bind_property_full (app, "state",
                                     e, "visible",
                                     G_BINDING_SYNC_CREATE,
                                     transform_state_func,
                                     NULL, NULL, NULL);
        gtk_overlay_add_overlay (GTK_OVERLAY (o), e);
        gtk_event_box_set_visible_window (GTK_EVENT_BOX (e), TRUE);
        gtk_style_context_add_class (gtk_widget_get_style_context (e), "installed-overlay-box");
        gtk_widget_set_halign (e, GTK_ALIGN_END);
        gtk_widget_set_valign (e, GTK_ALIGN_END);
        gtk_widget_set_margin_bottom (e, 35);

        label = gtk_label_new (_("Installed"));
        gtk_widget_show (label);
        gtk_container_add (GTK_CONTAINER (e), label);
        gtk_style_context_add_class (gtk_widget_get_style_context (label), "installed-overlay-label");
        gtk_widget_set_margin_left (label, 16);
        gtk_widget_set_margin_right (label, 16);
        gtk_widget_set_margin_top (label, 4);
        gtk_widget_set_margin_bottom (label, 4);

	gtk_container_add (GTK_CONTAINER (frame), o);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add (GTK_CONTAINER (o), box);
	gtk_widget_set_valign (box, GTK_ALIGN_FILL);
	image = gtk_image_new_from_pixbuf (gs_app_get_pixbuf (app));
	gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
	g_object_set (box, "margin", 12, NULL);
	gtk_box_pack_start (GTK_BOX (box), image, TRUE, TRUE, 0);
	label = gtk_label_new (gs_app_get_name (app));
        gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
        gtk_label_set_width_chars (GTK_LABEL (label), 10);
        gtk_label_set_max_width_chars (GTK_LABEL (label), 15);
	gtk_widget_set_valign (label, GTK_ALIGN_END);
	g_object_set (label, "margin", 6, NULL);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (button), frame);
	gtk_container_add (GTK_CONTAINER (f), button);
	gtk_widget_show_all (f);
	g_object_set_data_full (G_OBJECT (button), "app", g_object_ref (app), g_object_unref);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (app_tile_clicked), shell_overview);

	return f;
}

/**
 * gs_shell_overview_get_popular_cb:
 **/
static void
gs_shell_overview_get_popular_cb (GObject *source_object,
			GAsyncResult *res,
			gpointer user_data)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	gint i;
	GtkWidget *tile;
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell_overview->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *grid;

	/* get popular apps */
	list = gs_plugin_loader_get_popular_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		g_warning ("failed to get popular apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_popular"));
	for (l = list, i = 0; l != NULL && i < 6; l = l->next, i++) {
		app = GS_APP (l->data);
		tile = create_popular_tile (shell_overview, app);
                gtk_box_pack_start (GTK_BOX (grid), tile, TRUE, TRUE, 0);
	}
out:
        priv->refresh_count--;
        if (priv->refresh_count == 0)
                g_signal_emit (shell_overview, signals[SIGNAL_REFRESHED], 0);
}

static void
category_tile_clicked (GtkButton *button, gpointer data)
{
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (data);
        GsCategory *category;

	category = GS_CATEGORY (g_object_get_data (G_OBJECT (button), "category"));
        gs_shell_show_category (shell_overview->priv->shell, category);
}

static GtkWidget *
create_category_tile (GsShellOverview *shell_overview, GsCategory *category)
{
	GtkWidget *button, *frame, *label;

	button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	frame = gtk_frame_new (NULL);
	gtk_container_add (GTK_CONTAINER (button), frame);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
	label = gtk_label_new (gs_category_get_name (category));
	g_object_set (label, "margin", 12, "xalign", 0, NULL);
	gtk_container_add (GTK_CONTAINER (frame), label);
	gtk_widget_show_all (button);
	g_object_set_data_full (G_OBJECT (button), "category", g_object_ref (category), g_object_unref);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (category_tile_clicked), shell_overview);

	return button;
}

/**
 * gs_shell_overview_get_featured_cb:
 **/
static void
gs_shell_overview_get_featured_cb (GObject *source_object,
               	 		   GAsyncResult *res,
	         		   gpointer user_data)
{
	GdkPixbuf *pixbuf;
	GError *error = NULL;
	GList *list;
	GsApp *app;
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell_overview->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkImage *image;
	GtkWidget *button;

	list = gs_plugin_loader_get_featured_finish (plugin_loader,
						     res,
						     &error);
	if (list == NULL) {
		g_warning ("failed to get featured apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* at the moment, we only care about the first app */
	app = GS_APP (list->data);
	image = GTK_IMAGE (gtk_builder_get_object (priv->builder, "featured_image"));
	pixbuf = gs_app_get_featured_pixbuf (app);
	gtk_image_set_from_pixbuf (image, pixbuf);
	button = GTK_WIDGET (gtk_builder_get_object (priv->builder, "featured_button"));
	g_object_set_data_full (G_OBJECT (button), "app", app, g_object_unref);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (app_tile_clicked), shell_overview);

out:
	g_list_free (list);

        priv->refresh_count--;
        if (priv->refresh_count == 0)
                g_signal_emit (shell_overview, signals[SIGNAL_REFRESHED], 0);
}

/**
 * gs_shell_overview_get_categories_cb:
 **/
static void
gs_shell_overview_get_categories_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GError *error = NULL;
	gint i;
	GList *l;
	GList *list;
	GsCategory *cat;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (user_data);
	GsShellOverviewPrivate *priv = shell_overview->priv;
	GtkWidget *grid;
	GtkWidget *tile;

	list = gs_plugin_loader_get_categories_finish (plugin_loader,
						       res,
						       &error);
	if (list == NULL) {
		g_warning ("failed to get categories: %s", error->message);
		g_error_free (error);
		goto out;
	}
	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_categories"));
	for (l = list, i = 0; l; l = l->next, i++) {
		cat = GS_CATEGORY (l->data);
		tile = create_category_tile (shell_overview, cat);
		gtk_grid_attach (GTK_GRID (grid), tile, i % 3, i / 3, 1, 1);
	}
        g_list_free_full (list, g_object_unref);
out:
	priv->cache_valid = TRUE;
}

/**
 * gs_shell_overview_refresh:
 **/
void
gs_shell_overview_refresh (GsShellOverview *shell_overview, gboolean scroll_up)
{
	GsShellOverviewPrivate *priv = shell_overview->priv;
	GtkWidget *widget;
	GtkWidget *grid;
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

	/* no need to refresh */
	if (priv->cache_valid)
		return;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_popular"));
	container_remove_all (GTK_CONTAINER (grid));

        priv->refresh_count = 2;

	/* get featured apps */
	gs_plugin_loader_get_featured_async (priv->plugin_loader,
					     priv->cancellable,
					     gs_shell_overview_get_featured_cb,
					     shell_overview);

	/* get popular apps */
	gs_plugin_loader_get_popular_async (priv->plugin_loader,
					    priv->cancellable,
					    gs_shell_overview_get_popular_cb,
					    shell_overview);

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_categories"));
	container_remove_all (GTK_CONTAINER (grid));

	/* get categories */
	gs_plugin_loader_get_categories_async (priv->plugin_loader,
					       priv->cancellable,
					       gs_shell_overview_get_categories_cb,
					       shell_overview);
}

void
gs_shell_overview_setup (GsShellOverview *shell_overview,
                         GsShell *shell,
			 GsPluginLoader *plugin_loader,
			 GtkBuilder *builder,
			 GCancellable *cancellable)
{
	GsShellOverviewPrivate *priv = shell_overview->priv;

	g_return_if_fail (GS_IS_SHELL_OVERVIEW (shell_overview));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

        /* avoid a ref cycle */
        priv->shell = shell;
}

/**
 * gs_shell_overview_class_init:
 **/
static void
gs_shell_overview_class_init (GsShellOverviewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_overview_finalize;

        signals [SIGNAL_REFRESHED] =
                g_signal_new ("refreshed",
                              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsShellOverviewClass, refreshed),
                              NULL, NULL, g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (GsShellOverviewPrivate));
}

/**
 * gs_shell_overview_init:
 **/
static void
gs_shell_overview_init (GsShellOverview *shell_overview)
{
	shell_overview->priv = GS_SHELL_OVERVIEW_GET_PRIVATE (shell_overview);
}

/**
 * gs_shell_overview_finalize:
 **/
static void
gs_shell_overview_finalize (GObject *object)
{
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (object);
	GsShellOverviewPrivate *priv = shell_overview->priv;

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);

	G_OBJECT_CLASS (gs_shell_overview_parent_class)->finalize (object);
}

/**
 * gs_shell_overview_new:
 **/
GsShellOverview *
gs_shell_overview_new (void)
{
	GsShellOverview *shell_overview;
	shell_overview = g_object_new (GS_TYPE_SHELL_OVERVIEW, NULL);
	return GS_SHELL_OVERVIEW (shell_overview);
}
