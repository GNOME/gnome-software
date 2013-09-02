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
 * GNU General Public License for more category.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#include "gs-utils.h"
#include "gs-shell-category.h"

struct GsShellCategoryPrivate {
	GsPluginLoader	  *plugin_loader;
        GtkBuilder        *builder;
	GCancellable      *cancellable;
        GsShell           *shell;
        GsCategory        *category;
        GtkWidget         *col1_placeholder;
        GtkWidget         *col2_placeholder;
};

G_DEFINE_TYPE (GsShellCategory, gs_shell_category, G_TYPE_OBJECT)

void
gs_shell_category_refresh (GsShellCategory *shell)
{
	GsShellCategoryPrivate *priv = shell->priv;
        GtkWidget *widget;
        GsCategory *category;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
        gtk_widget_show (widget);
        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
        gtk_widget_show (widget);
        category = g_object_ref (priv->category);
        if (gs_category_get_parent (category))
                category = gs_category_get_parent (category);
        gtk_label_set_label (GTK_LABEL (widget), gs_category_get_name (category));
}

static void
app_tile_clicked (GtkButton *button, gpointer data)
{
        GsShellCategory *shell = GS_SHELL_CATEGORY (data);
        GsApp *app;

        app = g_object_get_data (G_OBJECT (button), "app");
        gs_shell_show_app (shell->priv->shell, app);
}

static GtkWidget *
create_app_tile (GsShellCategory *shell, GsApp *app)
{
        GtkWidget *button, *frame, *label;
        GtkWidget *image, *grid;
        const gchar *tmp;
        PangoAttrList *attrs;

        button = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
        frame = gtk_frame_new (NULL);
        gtk_container_add (GTK_CONTAINER (button), frame);
        gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
        gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
        grid = gtk_grid_new ();
        gtk_container_add (GTK_CONTAINER (frame), grid);
        g_object_set (grid, "margin", 12, "row-spacing", 6, "column-spacing", 6, NULL);
        image = gtk_image_new_from_pixbuf (gs_app_get_pixbuf (app));
        gtk_grid_attach (GTK_GRID (grid), image, 0, 0, 1, 2);
        label = gtk_label_new (gs_app_get_name (app));
        attrs = pango_attr_list_new ();
        pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes (GTK_LABEL (label), attrs);
        pango_attr_list_unref (attrs);
        g_object_set (label, "xalign", 0, NULL);
        gtk_grid_attach (GTK_GRID (grid), label, 1, 0, 1, 1);
        tmp = gs_app_get_summary (app);
        if (tmp != NULL && tmp[0] != '\0') {
                label = gtk_label_new (tmp);
                g_object_set (label, "xalign", 0, NULL);
                gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
                gtk_grid_attach (GTK_GRID (grid), label, 1, 1, 1, 1);
        }

        gtk_widget_show_all (button);
        g_object_set_data_full (G_OBJECT (button), "app", g_object_ref (app), g_object_unref);
        g_signal_connect (button, "clicked",
                          G_CALLBACK (app_tile_clicked), shell);

        return button;
}

/**
 * gs_shell_category_get_apps_cb:
 **/
static void
gs_shell_category_get_apps_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        GError *error = NULL;
        gint i = 0;
        GList *l;
        GList *list;
        GsApp *app;
        GtkWidget *grid;
        GtkWidget *tile;
        GsShellCategory *shell = GS_SHELL_CATEGORY (user_data);
        GsShellCategoryPrivate *priv = shell->priv;
        GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);

        list = gs_plugin_loader_get_category_apps_finish (plugin_loader,
                                                          res,
                                                          &error);
	if (list == NULL) {
		g_warning ("failed to get apps for category apps: %s", error->message);
		g_error_free (error);
		goto out;
	}
        grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
        gtk_grid_remove_column (GTK_GRID (grid), 2);
        gtk_grid_remove_column (GTK_GRID (grid), 1);

        for (l = list, i = 0; l != NULL; l = l->next, i++) {
                app = GS_APP (l->data);
                tile = create_app_tile (shell, app);
                if (gs_category_get_parent (priv->category) != NULL)
                        gtk_grid_attach (GTK_GRID (grid), tile, 1 + (i % 2), i / 2, 1, 1);
                else
                        gtk_grid_attach (GTK_GRID (grid), tile, i % 3, i / 3, 1, 1);
        }

        if (i == 1)
                gtk_grid_attach (GTK_GRID (grid), priv->col2_placeholder, 2, 0, 1, 1);

out:
	g_list_free (list);

}

static void
gs_shell_category_populate_filtered (GsShellCategory *shell)
{
        GsShellCategoryPrivate *priv = shell->priv;
        GtkWidget *grid;
        GsCategory *parent;

        parent = gs_category_get_parent (priv->category);
        if (parent == NULL) {
                g_debug ("search using %s",
                         gs_category_get_id (priv->category));
        } else {
                g_debug ("search using %s/%s",
                         gs_category_get_id (parent),
                         gs_category_get_id (priv->category));
        }

        /* Remove old content. Be careful not to remove the
         * subcategories and put placeholders there to keep
         * the subcategory list from growing
         */
        grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
        gtk_grid_remove_column (GTK_GRID (grid), 2);
        gtk_grid_remove_column (GTK_GRID (grid), 1);
        gtk_grid_attach (GTK_GRID (grid), priv->col1_placeholder, 1, 0, 1, 1);
        gtk_grid_attach (GTK_GRID (grid), priv->col2_placeholder, 2, 0, 1, 1);

        gs_plugin_loader_get_category_apps_async (priv->plugin_loader,
                                                  priv->category,
                                                  priv->cancellable,
                                                  gs_shell_category_get_apps_cb,
                                                  shell);
}

static void
add_separator (GtkListBoxRow *row, GtkListBoxRow *before, gpointer data)
{
        if (!before) {
                return;
        }

        gtk_list_box_row_set_header (row, gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
}

static void
filter_selected (GtkListBox *filters, GtkListBoxRow *row, gpointer data)
{
        GsShellCategory *shell = GS_SHELL_CATEGORY (data);
        GsCategory *category;

        if (row == NULL)
                return;

        category = g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), "category");
        g_clear_object (&shell->priv->category);
        shell->priv->category = g_object_ref (category);
        gs_shell_category_populate_filtered (shell);
}

static void
gs_shell_category_create_filter_list (GsShellCategory *shell, GsCategory *category, GsCategory *subcategory)
{
        GsShellCategoryPrivate *priv = shell->priv;
        GtkWidget *grid;
        GtkWidget *list_box;
        GtkWidget *row;
        GtkWidget *frame;
        guint i;
        GList *list, *l;
        GsCategory *s;

        grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
        gs_container_remove_all (GTK_CONTAINER (grid));

        list = gs_category_get_subcategories (category);
        if (!list)
                return;

        gtk_grid_attach (GTK_GRID (grid), priv->col1_placeholder, 1, 0, 1, 1);
        gtk_grid_attach (GTK_GRID (grid), priv->col2_placeholder, 2, 0, 1, 1);

        list_box = gtk_list_box_new ();
        gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_box), GTK_SELECTION_BROWSE);
        g_signal_connect (list_box, "row-selected", G_CALLBACK (filter_selected), shell);
        gtk_list_box_set_header_func (GTK_LIST_BOX (list_box), add_separator, NULL, NULL);
        for  (l = list, i = 0; l; l = l->next, i++) {
                s = l->data;
                row = gtk_label_new (gs_category_get_name (s));
                g_object_set_data_full (G_OBJECT (row), "category", g_object_ref (s), g_object_unref);
                g_object_set (row, "xalign", 0.0, "margin", 6, NULL);
                gtk_list_box_insert (GTK_LIST_BOX (list_box), row, i);
                if (subcategory == s)
                        gtk_list_box_select_row (GTK_LIST_BOX (list_box), GTK_LIST_BOX_ROW (gtk_widget_get_parent (row)));
        }
        g_list_free (list);

        frame = gtk_frame_new (NULL);
        g_object_set (frame, "margin", 6, NULL);
        gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
        gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
        gtk_container_add (GTK_CONTAINER (frame), list_box);
        gtk_widget_show_all (frame);
        gtk_widget_set_valign (frame, GTK_ALIGN_START);
        gtk_grid_attach (GTK_GRID (grid), frame, 0, 0, 1, 20);
}

void
gs_shell_category_set_category (GsShellCategory *shell, GsCategory *category)
{
        GsShellCategoryPrivate *priv = shell->priv;
        GsCategory *sub;
        GsCategory *selected = NULL;
        GList *list;
        GList *l;

        /* this means we've come from the app-view -> back */
        if (gs_category_get_parent (category) != NULL)
                return;

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

        /* save this */
        g_clear_object (&priv->category);
        priv->category = g_object_ref (selected);

        /* find apps in this group */
        gs_shell_category_create_filter_list (shell, category, selected);
        g_list_free (list);
}

GsCategory *
gs_shell_category_get_category (GsShellCategory *shell)
{
        return shell->priv->category;
}

static void
gs_shell_category_init (GsShellCategory *shell)
{
        GsShellCategoryPrivate *priv;

	priv = G_TYPE_INSTANCE_GET_PRIVATE (shell, GS_TYPE_SHELL_CATEGORY, GsShellCategoryPrivate);
        shell->priv = priv;

        priv->col1_placeholder = g_object_ref_sink (gtk_label_new (""));
        priv->col2_placeholder = g_object_ref_sink (gtk_label_new (""));

        gtk_widget_show (priv->col1_placeholder);
        gtk_widget_show (priv->col2_placeholder);
}

static void
gs_shell_category_finalize (GObject *object)
{
	GsShellCategory *shell = GS_SHELL_CATEGORY (object);
	GsShellCategoryPrivate *priv = shell->priv;

	g_clear_object (&priv->builder);
        g_clear_object (&priv->category);
	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->cancellable);
        g_clear_object (&priv->col1_placeholder);
        g_clear_object (&priv->col2_placeholder);

	G_OBJECT_CLASS (gs_shell_category_parent_class)->finalize (object);
}

static void
gs_shell_category_class_init (GsShellCategoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gs_shell_category_finalize;

	g_type_class_add_private (klass, sizeof (GsShellCategoryPrivate));
}

void
gs_shell_category_setup (GsShellCategory *shell_category,
                         GsShell *shell,
                         GsPluginLoader *plugin_loader,
                         GtkBuilder *builder,
                         GCancellable *cancellable)
{
	GsShellCategoryPrivate *priv = shell_category->priv;

	priv->plugin_loader = g_object_ref (plugin_loader);
        priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);
        priv->shell = shell;
}

GsShellCategory *
gs_shell_category_new (void)
{
	GsShellCategory *shell;

	shell = g_object_new (GS_TYPE_SHELL_CATEGORY, NULL);

	return shell;
}
