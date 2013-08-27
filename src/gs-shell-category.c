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

#include "gs-shell-category.h"

static void	gs_shell_category_finalize	(GObject	*object);

#define GS_SHELL_CATEGORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_CATEGORY, GsShellCategoryPrivate))

struct GsShellCategoryPrivate
{
        GtkBuilder        *builder;
        GsShell           *shell;
	gchar             *category;
};

G_DEFINE_TYPE (GsShellCategory, gs_shell_category, G_TYPE_OBJECT)

void
gs_shell_category_refresh (GsShellCategory *shell)
{
	GsShellCategoryPrivate *priv = shell->priv;
        GtkWidget *widget;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
        gtk_widget_show (widget);
        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
        gtk_widget_show (widget);
        gtk_label_set_label (GTK_LABEL (widget), priv->category);
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
        GsShellCategory *shell = GS_SHELL_CATEGORY (data);
        GsApp *app;

        app = g_object_get_data (G_OBJECT (button), "app");
        gs_shell_show_details (shell->priv->shell, app);
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

static void
gs_shell_category_populate_filtered (GsShellCategory *shell, const gchar *category, const gchar *filter)
{
        GsShellCategoryPrivate *priv = shell->priv;
        gint i;
        GtkWidget *tile;
        GsApp *app;
        GtkWidget *grid;

        grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
        gtk_grid_remove_column (GTK_GRID (grid), 2);
        gtk_grid_remove_column (GTK_GRID (grid), 1);
        if (filter == NULL) {
                gtk_grid_remove_column (GTK_GRID (grid), 0);
        }

        /* FIXME load apps for this category and filter */
        app = gs_app_new ("gnome-boxes");
        gs_app_set_name (app, "Boxes");
        gs_app_set_summary (app, "View and use virtual machines");
        gs_app_set_url (app, "http://www.box.org");
        gs_app_set_kind (app, GS_APP_KIND_NORMAL);
        gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
        gs_app_set_pixbuf (app, gdk_pixbuf_new_from_file ("/usr/share/icons/hicolor/48x48/apps/gnome-boxes.png", NULL));

        for (i = 0; i < 30; i++) {
                tile = create_app_tile (shell, app);
                if (filter) {
                        gtk_grid_attach (GTK_GRID (grid), tile, 1 + (i % 2), i / 2, 1, 1);
                }
                else {
                        gtk_grid_attach (GTK_GRID (grid), tile, i % 3, i / 3, 1, 1);
                }
        }

        g_object_unref (app);
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
        const gchar *filter;
        const gchar *category;

        if (row == NULL)
                return;

        filter = gtk_label_get_label (GTK_LABEL (gtk_bin_get_child (GTK_BIN (row))));
        category = (const gchar*)g_object_get_data (G_OBJECT (filters), "category");
        gs_shell_category_populate_filtered (shell, category, filter);
}

static void
create_filter_list (GsShellCategory *shell, const gchar *category, const gchar *filters[])
{
        GsShellCategoryPrivate *priv = shell->priv;
        GtkWidget *grid;
        GtkWidget *list;
        GtkWidget *row;
        GtkWidget *frame;
        guint i;

        grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
        list = gtk_list_box_new ();
        g_object_set_data (G_OBJECT (list), "category", (gpointer)category);
        gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_BROWSE);
        g_signal_connect (list, "row-selected", G_CALLBACK (filter_selected), shell);
        gtk_list_box_set_header_func (GTK_LIST_BOX (list), add_separator, NULL, NULL);
        for (i = 0; filters[i]; i++) {
                row = gtk_label_new (filters[i]);
                g_object_set (row, "xalign", 0.0, "margin", 6, NULL);
                gtk_list_box_insert (GTK_LIST_BOX (list), row, i);
        }
        frame = gtk_frame_new (NULL);
        g_object_set (frame, "margin", 6, NULL);
        gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
        gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
        gtk_container_add (GTK_CONTAINER (frame), list);
        gtk_widget_show_all (frame);
        gtk_widget_set_valign (frame, GTK_ALIGN_START);
        gtk_grid_attach (GTK_GRID (grid), frame, 0, 0, 1, 5);
        gtk_list_box_select_row (GTK_LIST_BOX (list),
                                 gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), 0));
}

void
gs_shell_category_set_category (GsShellCategory *shell, const gchar *category)
{
	GsShellCategoryPrivate *priv = shell->priv;
        GtkWidget *grid;

	g_free (priv->category);
	priv->category = g_strdup (category);

        grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
        container_remove_all (GTK_CONTAINER (grid));

        /* FIXME: get actual filters */
        if (g_str_equal (category, "Games")) {
                const gchar *filters[] = {
                        "Popular", "Action", "Arcade", "Board",
                        "Blocks", "Card", "Kids", "Logic", "Role Playing",
                        "Shooter", "Simulation", "Sports", "Strategy",
                        NULL
                };
                create_filter_list (shell, category, filters);
        }
        else if (g_str_equal (category, "Add-ons")) {
                const gchar *filters[] = {
                        "Popular", "Codecs", "Fonts",
                        "Input Sources", "Language Packs",
                        NULL
                };
                create_filter_list (shell, category, filters);
        }
        else {
                gs_shell_category_populate_filtered (shell, category, NULL);
        }
}

static void
gs_shell_category_class_init (GsShellCategoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_category_finalize;

	g_type_class_add_private (klass, sizeof (GsShellCategoryPrivate));
}

static void
gs_shell_category_init (GsShellCategory *shell)
{
	shell->priv = GS_SHELL_CATEGORY_GET_PRIVATE (shell);
}

static void
gs_shell_category_finalize (GObject *object)
{
	GsShellCategory *shell = GS_SHELL_CATEGORY (object);
	GsShellCategoryPrivate *priv = shell->priv;

	g_object_unref (priv->builder);
        g_free (priv->category);

	G_OBJECT_CLASS (gs_shell_category_parent_class)->finalize (object);
}

void
gs_shell_category_setup (GsShellCategory *shell_category, GsShell *shell, GtkBuilder *builder)
{
	GsShellCategoryPrivate *priv = shell_category->priv;

        priv->builder = g_object_ref (builder);
        priv->shell = shell;
}

/**
 * gs_shell_category_new:
 **/
GsShellCategory *
gs_shell_category_new (void)
{
	GsShellCategory *shell;
	shell = g_object_new (GS_TYPE_SHELL_CATEGORY, NULL);
	return GS_SHELL_CATEGORY (shell);
}
