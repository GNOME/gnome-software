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

#include "gs-shell.h"
#include "gs-shell-overview.h"
#include "gs-app.h"
#include "gs-app-widget.h"

static void	gs_shell_overview_finalize	(GObject	*object);

#define GS_SHELL_OVERVIEW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_OVERVIEW, GsShellOverviewPrivate))

struct GsShellOverviewPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	gboolean		 cache_valid;
};

enum {
	SIGNAL_SET_OVERVIEW_MODE,
	SIGNAL_LAST
};

G_DEFINE_TYPE (GsShellOverview, gs_shell_overview, G_TYPE_OBJECT)

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
gs_shell_overview_set_overview_mode (GsShellOverview *shell_overview, GsShellMode mode, GsApp *app, const gchar *category)
{
	g_signal_emit (shell_overview, signals[SIGNAL_SET_OVERVIEW_MODE], 0, mode, app, category);
}

static void
app_tile_clicked (GtkButton *button, gpointer data)
{
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (data);
	GsApp *app;

	app = g_object_get_data (G_OBJECT (button), "app");
	gs_shell_overview_set_overview_mode (shell_overview, GS_SHELL_MODE_DETAILS, app, NULL);
}

static GtkWidget *
create_popular_tile (GsShellOverview *shell_overview, GsApp *app)
{
	GtkWidget *button, *frame, *box, *image, *label;
	GtkWidget *f;

	f = gtk_aspect_frame_new (NULL, 0.5, 0, 1, FALSE);
	gtk_widget_set_valign (f, GTK_ALIGN_START);
	gtk_frame_set_shadow_type (GTK_FRAME (f), GTK_SHADOW_NONE);
        gtk_widget_set_size_request (f, -1, 200);
	button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	frame = gtk_aspect_frame_new (NULL, 0.5, 1, 1, FALSE);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
	gtk_widget_set_halign (frame, GTK_ALIGN_FILL);
	gtk_widget_set_valign (frame, GTK_ALIGN_FILL);
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add (GTK_CONTAINER (frame), box);
	gtk_widget_set_valign (box, GTK_ALIGN_FILL);
	image = gtk_image_new_from_pixbuf (gs_app_get_pixbuf (app));
	gtk_widget_set_valign (image, GTK_ALIGN_CENTER);
	g_object_set (box, "margin", 12, NULL);
	gtk_box_pack_start (GTK_BOX (box), image, TRUE, TRUE, 0);
	label = gtk_label_new (gs_app_get_name (app));
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
	for (l = list, i = 0; l != NULL; l = l->next, i++) {
		app = GS_APP (l->data);
		tile = create_popular_tile (shell_overview, app);
                gtk_box_pack_start (GTK_BOX (grid), tile, TRUE, TRUE, 0);
	}
out:
	return;
}

static void
category_tile_clicked (GtkButton *button, gpointer data)
{
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (data);
//	GsShellOverviewPrivate *priv = shell_overview->priv;
	const gchar *category;

	category = g_object_get_data (G_OBJECT (button), "category");
	gs_shell_overview_set_overview_mode (shell_overview, GS_SHELL_MODE_CATEGORY, NULL, category);
}

static GtkWidget *
create_category_tile (GsShellOverview *shell_overview, const gchar *category)
{
	GtkWidget *button, *frame, *label;

	button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	frame = gtk_frame_new (NULL);
	gtk_container_add (GTK_CONTAINER (button), frame);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
	label = gtk_label_new (category);
	g_object_set (label, "margin", 12, "xalign", 0, NULL);
	gtk_container_add (GTK_CONTAINER (frame), label);
	gtk_widget_show_all (button);
	g_object_set_data (G_OBJECT (button), "category", (gpointer)category);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (category_tile_clicked), shell_overview);

	return button;
}

static GtkWidget *
create_app_tile (GsShellOverview *shell_overview, GsApp *app)
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
			  G_CALLBACK (app_tile_clicked), shell_overview);

	return button;
}

static void
gs_shell_overview_populate_filtered_category (GsShellOverview *shell_overview,
				    const gchar   *category,
				    const gchar   *filter)
{
	gint i;
	GtkWidget *tile;
	GsApp *app;
	GtkWidget *grid;
	GsShellOverviewPrivate *priv = shell_overview->priv;

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
		tile = create_app_tile (shell_overview, app);
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
add_separator (GtkListBoxRow *row,
	       GtkListBoxRow *before,
	       gpointer       data)
{
	if (!before) {
		return;
	}

	gtk_list_box_row_set_header (row, gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
}

static void
filter_selected (GtkListBox    *filters,
		 GtkListBoxRow *row,
		 gpointer       data)
{
	GsShellOverview *shell_overview = GS_SHELL_OVERVIEW (data);
	const gchar *filter;
	const gchar *category;

	if (row == NULL)
		return;

	filter = gtk_label_get_label (GTK_LABEL (gtk_bin_get_child (GTK_BIN (row))));
	category = (const gchar*)g_object_get_data (G_OBJECT (filters), "category");
	gs_shell_overview_populate_filtered_category (shell_overview, category, filter);
}

static void
create_filter_list (GsShellOverview *shell_overview, const gchar *category, const gchar *filters[])
{
	GtkWidget *grid;
	GtkWidget *list;
	GtkWidget *row;
	GtkWidget *frame;
	guint i;
	GsShellOverviewPrivate *priv = shell_overview->priv;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
	list = gtk_list_box_new ();
	g_object_set_data (G_OBJECT (list), "category", (gpointer)category);
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_BROWSE);
	g_signal_connect (list, "row-selected", G_CALLBACK (filter_selected), shell_overview);
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
gs_shell_overview_set_category (GsShellOverview *shell_overview, const gchar *category)
{
	GtkWidget *grid;
	GsShellOverviewPrivate *priv = shell_overview->priv;

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
		create_filter_list (shell_overview, category, filters);
	}
	else if (g_str_equal (category, "Add-ons")) {
		const gchar *filters[] = {
			"Popular", "Codecs", "Fonts",
			"Input Sources", "Language Packs",
			NULL
		};
		create_filter_list (shell_overview, category, filters);
	}
	else {
		gs_shell_overview_populate_filtered_category (shell_overview, category, NULL);
	}
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

#ifdef SEARCH
	/* focus back to the text extry */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	gtk_widget_grab_focus (widget);
#endif
out:
	g_list_free (list);
	return;
}

/**
 * gs_shell_overview_refresh:
 **/
void
gs_shell_overview_refresh (GsShellOverview *shell_overview, GCancellable *cancellable)
{
	GsShellOverviewPrivate *priv = shell_overview->priv;
	/* FIXME get real categories */
	GtkWidget *grid;
	const gchar *categories[] = {
	  "Add-ons", "Books", "Business & Finance",
	  "Entertainment", "Education", "Games",
	  "Lifestyle", "Music", "Navigation",
	  "Overviews", "Photo & Video", "Productivity",
	  "Social Networking", "Utility", "Weather",
	};
	guint i;
	GtkWidget *tile;

	/* no need to refresh */
	if (priv->cache_valid)
		return;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_categories"));
	container_remove_all (GTK_CONTAINER (grid));

	for (i = 0; i < G_N_ELEMENTS (categories); i++) {
		tile = create_category_tile (shell_overview, categories[i]);
		gtk_grid_attach (GTK_GRID (grid), tile, i % 3, i / 3, 1, 1);
	}

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_popular"));
	container_remove_all (GTK_CONTAINER (grid));

	/* get popular apps */
	gs_plugin_loader_get_popular_async (priv->plugin_loader,
					    cancellable,
					    gs_shell_overview_get_popular_cb,
					    shell_overview);

	/* get featured apps */
	gs_plugin_loader_get_featured_async (priv->plugin_loader,
					     cancellable,
					     gs_shell_overview_get_featured_cb,
					     shell_overview);
	priv->cache_valid = TRUE;
}

/**
 * gs_shell_overview_setup:
 */
void
gs_shell_overview_setup (GsShellOverview *shell_overview,
		    GsPluginLoader *plugin_loader,
		    GtkBuilder *builder)
{
	GsShellOverviewPrivate *priv = shell_overview->priv;

	g_return_if_fail (GS_IS_SHELL_OVERVIEW (shell_overview));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
}

/**
 * gs_shell_overview_class_init:
 **/
static void
gs_shell_overview_class_init (GsShellOverviewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_overview_finalize;

	signals [SIGNAL_SET_OVERVIEW_MODE] =
		g_signal_new ("set-overview-mode",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsShellOverviewClass, set_overview_mode),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_POINTER, G_TYPE_STRING);

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
