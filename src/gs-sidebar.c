/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation, Inc
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-sidebar
 * @short_description: A widget to list pages and categories at the side of a window
 *
 * #GsSidebar is a widget which lists the top-level pages and categories which
 * the user might want to navigate between in GNOME Software. It’s intended to
 * be used as the left-hand sidebar in the application’s main window.
 *
 * Its rows are populated from the pages of the provided #GsSidebar:stack, and
 * from the categories from the provided #GsSidebar:category-manager.
 *
 * Since: 41
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-category.h"
#include "gs-sidebar.h"

struct _GsSidebar
{
	GtkBox		 parent;
	GtkListBox	*list_box;
	gint		 first_category_row_index;

	GtkStack	*stack;
	GHashTable	*stack_rows;  /* mapping from stack child (GtkWidget) → list box row (GtkListBoxRow) */

	GsCategoryManager *category_manager;  /* (owned) (nullable) */
	GHashTable	*category_rows;  /* mapping from list box row (GtkListBoxRow) → GsCategory */
};

G_DEFINE_TYPE (GsSidebar, gs_sidebar, GTK_TYPE_BOX)

enum {
	SIGNAL_CATEGORY_SELECTED,
};

static guint signals[SIGNAL_CATEGORY_SELECTED + 1] = { 0 };

typedef enum {
	PROP_STACK = 1,
	PROP_CATEGORY_MANAGER,
} GsSidebarProperty;

static GParamSpec *obj_props[PROP_CATEGORY_MANAGER + 1] = { NULL, };

static void
gs_sidebar_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsSidebar *self = GS_SIDEBAR (object);

	switch ((GsSidebarProperty) prop_id) {
	case PROP_STACK:
		g_value_set_object (value, self->stack);
		break;
	case PROP_CATEGORY_MANAGER:
		g_value_set_object (value, self->category_manager);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_sidebar_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsSidebar *self = GS_SIDEBAR (object);

	switch ((GsSidebarProperty) prop_id) {
	case PROP_STACK:
		gs_sidebar_set_stack (self, g_value_get_object (value));
		break;
	case PROP_CATEGORY_MANAGER:
		gs_sidebar_set_category_manager (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_sidebar_dispose (GObject *object)
{
	GsSidebar *self = GS_SIDEBAR (object);

	gs_sidebar_set_category_manager (self, NULL);
	gs_sidebar_set_stack (self, NULL);

	G_OBJECT_CLASS (gs_sidebar_parent_class)->dispose (object);
}

static void
gs_sidebar_finalize (GObject *object)
{
	GsSidebar *self = GS_SIDEBAR (object);

	g_clear_pointer (&self->stack_rows, g_hash_table_unref);
	g_clear_pointer (&self->category_rows, g_hash_table_unref);

	G_OBJECT_CLASS (gs_sidebar_parent_class)->finalize (object);
}

static void
row_selected_cb (GtkListBox    *list_box,
                 GtkListBoxRow *row,
                 gpointer       user_data)
{
	GsSidebar *self = GS_SIDEBAR (user_data);
	GHashTableIter iter;
	gpointer key, value;
	GsCategory *category;

	/* Is one of the stack pages? */
	g_hash_table_iter_init (&iter, self->stack_rows);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GtkWidget *stack_child = key;
		GtkListBoxRow *possible_row = value;

		if (possible_row == row) {
			gtk_stack_set_visible_child (self->stack, stack_child);
			return;
		}
	}

	/* Otherwise, is it a category? */
	category = g_hash_table_lookup (self->category_rows, row);
	if (category != NULL) {
		g_signal_emit (self, signals[SIGNAL_CATEGORY_SELECTED], 0, category);
		return;
	}

	g_assert_not_reached ();
}

static void
gs_sidebar_class_init (GsSidebarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_sidebar_get_property;
	object_class->set_property = gs_sidebar_set_property;
	object_class->dispose = gs_sidebar_dispose;
	object_class->finalize = gs_sidebar_finalize;

	/**
	 * GsSidebar:stack: (nullable)
	 *
	 * A #GtkStack of pages which should be listed in the sidebar. Selecting
	 * one of these pages in the sidebar will result in it being set as the
	 * visible child in the stack.
	 *
	 * This may be %NULL if no pages are to be listed in the sidebar.
	 *
	 * Since: 41
	 */
	obj_props[PROP_STACK] =
		g_param_spec_object ("stack", NULL, NULL,
				     GTK_TYPE_STACK,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * GsSidebar:category-manager: (nullable)
	 *
	 * A category manager to provide a list of categories to be displayed in
	 * the sidebar. If a category is selected in the sidebar, the
	 * #GsSidebar:category-selected signal will be emitted.
	 *
	 * This may be %NULL if no categories are to be listed in the sidebar.
	 *
	 * Since: 41
	 */
	obj_props[PROP_CATEGORY_MANAGER] =
		g_param_spec_object ("category-manager", NULL, NULL,
				     GS_TYPE_CATEGORY_MANAGER,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	/**
	 * GsSidebar::category-selected:
	 * @category: (transfer none) (not nullable): the category which has been selected
	 *
	 * Emitted when a category is selected in the sidebar.
	 *
	 * Since: 41
	 */
	signals[SIGNAL_CATEGORY_SELECTED] =
		g_signal_new ("category-selected",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, GS_TYPE_CATEGORY);

	gtk_widget_class_set_css_name (widget_class, "sidebar");
	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-sidebar.ui");

	gtk_widget_class_bind_template_child (widget_class, GsSidebar, list_box);

	gtk_widget_class_bind_template_callback (widget_class, row_selected_cb);
}

static void
header_func (GtkListBoxRow *row, GtkListBoxRow *before, gpointer user_data)
{
	GsSidebar *self = GS_SIDEBAR (user_data);
	GtkListBoxRow *first_category_row = gtk_list_box_get_row_at_index (self->list_box, self->first_category_row_index);

	/* Put a separator before the first row which doesn’t come from the #GtkStack */
	if (first_category_row != NULL && row == first_category_row) {
		GtkWidget *separator;

		separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
		gtk_widget_set_hexpand (separator, TRUE);
		gtk_widget_show (separator);

		gtk_list_box_row_set_header (row, separator);
	} else {
		gtk_list_box_row_set_header (row, NULL);
	}
}

static void
gs_sidebar_init (GsSidebar *self)
{
	gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
	gtk_widget_init_template (GTK_WIDGET (self));

	self->stack_rows = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						  NULL, NULL);
	self->first_category_row_index = -1;  /* no rows yet */

	self->category_rows = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						     NULL, NULL);

	gtk_list_box_set_header_func (GTK_LIST_BOX (self->list_box),
				      header_func,
				      self,
				      NULL);
}

static void
update_row_needs_attention (GtkListBoxRow *row,
                            GtkWidget     *label,
                            const gchar   *title,
                            gboolean       needs_attention)
{
	GtkStyleContext *context;
	AtkObject *label_accessible;
	g_autofree gchar *name = NULL;

	label_accessible = gtk_widget_get_accessible (label);

	context = gtk_widget_get_style_context (GTK_WIDGET (row));
	if (needs_attention) {
		gtk_style_context_add_class (context, GTK_STYLE_CLASS_NEEDS_ATTENTION);
		name = (title != NULL) ? g_strdup_printf (_("%s (needs attention)"), title) : NULL;
	} else {
		gtk_style_context_remove_class (context, GTK_STYLE_CLASS_NEEDS_ATTENTION);
		name = g_strdup (title);
	}

	if (name != NULL)
		atk_object_set_name (label_accessible, name);
}

static GtkListBoxRow *
add_row (GsSidebar   *self,
         gint         position,
         const gchar *title,
         const gchar *icon_name,
         gboolean     needs_attention)
{
	GtkWidget *row, *box, *image, *label;
	GtkStyleContext *context;
	AtkObject *image_accessible, *label_accessible;
	g_autofree gchar *name = NULL;

	g_debug ("Adding row %s at %d (first non-stack row: %d)",
		 title, position, self->first_category_row_index);

	row = gtk_list_box_row_new ();
	gtk_widget_set_can_focus (row, TRUE);

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);
	gtk_container_add (GTK_CONTAINER (row), box);

	image = gtk_image_new ();
	gtk_image_set_from_icon_name (GTK_IMAGE (image), icon_name, GTK_ICON_SIZE_BUTTON);
	context = gtk_widget_get_style_context (image);
	gtk_style_context_add_class (context, "sidebar-icon");
	gtk_box_pack_start (GTK_BOX (box), image, FALSE, TRUE, 0);

	label = gtk_label_new (title);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 0);

	image_accessible = gtk_widget_get_accessible (image);
	label_accessible = gtk_widget_get_accessible (label);

	atk_object_add_relationship (image_accessible, ATK_RELATION_LABELLED_BY, label_accessible);

	update_row_needs_attention (GTK_LIST_BOX_ROW (row), label, title, needs_attention);

	gtk_list_box_insert (self->list_box, row, position);
	gtk_widget_show_all (row);

	return GTK_LIST_BOX_ROW (row);
}

static void
notify_visible_child_cb (GObject    *object,
                         GParamSpec *pspec,
                         gpointer    user_data)
{
	GsSidebar *self = GS_SIDEBAR (user_data);
	GtkWidget *child, *row;

	child = gtk_stack_get_visible_child (self->stack);
	row = g_hash_table_lookup (self->stack_rows, child);
	if (row != NULL)
		gtk_list_box_select_row (self->list_box, GTK_LIST_BOX_ROW (row));
}

static void
stack_widget_child_notify_cb (GtkWidget  *stack_widget,
                              GParamSpec *child_property,
                              gpointer    user_data)
{
	GsSidebar *self = GS_SIDEBAR (user_data);
	GtkListBoxRow *row;
	GtkWidget *label;
	g_autofree gchar *title = NULL;
	gboolean needs_attention;
	g_autoptr(GList) children = NULL;

	/* Currently we only support needs-attention being updated */
	if (g_param_spec_get_name (child_property) != g_intern_static_string ("needs-attention"))
		return;

	row = g_hash_table_lookup (self->stack_rows, stack_widget);
	if (row == NULL)
		return;

	gtk_container_child_get (GTK_CONTAINER (self->stack), stack_widget,
				 "title", &title,
				 "needs-attention", &needs_attention,
				 NULL);

	children = gtk_container_get_children (GTK_CONTAINER (gtk_bin_get_child (GTK_BIN (row))));
	label = g_list_nth_data (children, 1);
	g_assert (GTK_IS_LABEL (label));

	update_row_needs_attention (row, label, title, needs_attention);
}

static void
stack_widget_notify_visible_cb (GObject    *obj,
                                GParamSpec *pspec,
                                gpointer    user_data)
{
	GsSidebar *self = GS_SIDEBAR (user_data);
	GtkWidget *stack_widget = GTK_WIDGET (obj);
	GtkWidget *row;
	g_autofree gchar *title = NULL;
	g_autofree gchar *icon_name = NULL;

	gtk_container_child_get (GTK_CONTAINER (self->stack), stack_widget,
				 "title", &title,
				 "icon-name", &icon_name,
				 NULL);

	row = g_hash_table_lookup (self->stack_rows, stack_widget);
	if (row != NULL)
		gtk_widget_set_visible (row,
					gtk_widget_get_visible (stack_widget) && (title != NULL || icon_name != NULL));
}

static void
add_stack_row (GsSidebar *self,
               GtkWidget *stack_widget)
{
	g_autofree gchar *title = NULL;
	g_autofree gchar *icon_name = NULL;
	gboolean needs_attention = FALSE;
	GtkListBoxRow *new_row;
	gint old_index;

	gtk_container_child_get (GTK_CONTAINER (self->stack), stack_widget,
				 "title", &title,
				 "icon-name", &icon_name,
				 "needs-attention", &needs_attention,
				 NULL);

	old_index = self->first_category_row_index;
	self->first_category_row_index = (self->first_category_row_index < 0) ? -1 : self->first_category_row_index + 1;
	new_row = add_row (self, old_index, title, icon_name, needs_attention);
	g_hash_table_replace (self->stack_rows, stack_widget, new_row);

	g_signal_connect (stack_widget, "child-notify", G_CALLBACK (stack_widget_child_notify_cb), self);
	g_signal_connect (stack_widget, "notify::visible", G_CALLBACK (stack_widget_notify_visible_cb), self);

	gtk_widget_set_visible (GTK_WIDGET (new_row),
				gtk_widget_get_visible (stack_widget) && (title != NULL || icon_name != NULL));
}

static void
remove_stack_row (GsSidebar *self,
                  GtkWidget *stack_widget)
{
	/* Not implemented yet as it’s not needed yet */
	g_assert_not_reached ();
}

static void disconnect_stack_signals (GsSidebar *self);

static void
stack_destroy_cb (GtkWidget *widget,
                  gpointer   user_data)
{
	GsSidebar *self = GS_SIDEBAR (user_data);

	disconnect_stack_signals (self);
}

static void
remove_stack_row_cb (GtkWidget *stack_widget,
                     gpointer   user_data)
{
	GsSidebar *self = GS_SIDEBAR (user_data);

	remove_stack_row (self, stack_widget);
}

static void
clear_stack_rows (GsSidebar *self)
{
	gtk_container_foreach (GTK_CONTAINER (self->stack), remove_stack_row_cb, self);
}

static void
add_stack_row_cb (GtkWidget *stack_widget,
                  gpointer   user_data)
{
	GsSidebar *self = GS_SIDEBAR (user_data);

	add_stack_row (self, stack_widget);
}

static void
populate_stack_rows (GsSidebar *self)
{
	gtk_container_foreach (GTK_CONTAINER (self->stack), add_stack_row_cb, self);
	notify_visible_child_cb (G_OBJECT (self->stack), NULL, self);
}

static void
disconnect_stack_signals (GsSidebar *self)
{
	g_signal_handlers_disconnect_by_func (self->stack, notify_visible_child_cb, self);
	g_signal_handlers_disconnect_by_func (self->stack, stack_destroy_cb, self);
}

static void
connect_stack_signals (GsSidebar *self)
{
	g_signal_connect (self->stack, "notify::visible-child", G_CALLBACK (notify_visible_child_cb), self);
	g_signal_connect (self->stack, "destroy", G_CALLBACK (stack_destroy_cb), self);
}

static void
add_category_row (GsSidebar  *self,
                  GsCategory *category)
{
	GtkListBoxRow *new_row;

	new_row = add_row (self,
			   -1,
			   gs_category_get_name (category),
			   gs_category_get_icon_name (category),
			   FALSE  /* doesn’t need attention */);

	if (self->first_category_row_index < 0) {
		self->first_category_row_index = gtk_list_box_row_get_index (new_row);
		gtk_list_box_invalidate_headers (self->list_box);
	}

	g_hash_table_replace (self->category_rows, new_row, category);
}

static void
remove_category_row (GsSidebar  *self,
                     GsCategory *category)
{
	/* Not implemented yet as it’s not needed yet */
	g_assert_not_reached ();
}

static void
populate_category_rows (GsSidebar *self)
{
	GsCategory * const *categories;
	gsize n_categories;

	categories = gs_category_manager_get_categories (self->category_manager, &n_categories);

	for (gsize i = 0; i < n_categories; i++)
		add_category_row (self, categories[i]);
}

static void
clear_category_rows (GsSidebar *self)
{
	GsCategory * const *categories;
	gsize n_categories;

	categories = gs_category_manager_get_categories (self->category_manager, &n_categories);

	for (gsize i = 0; i < n_categories; i++)
		remove_category_row (self, categories[i]);
}

/**
 * gs_sidebar_new:
 *
 * Create a new #GsSidebar widget.
 *
 * Returns: (transfer full): a new #GsSidebar
 * Since: 41
 */
GtkWidget *
gs_sidebar_new (void)
{
	return g_object_new (GS_TYPE_SIDEBAR, NULL);
}

/**
 * gs_sidebar_get_stack:
 * @self: a #GsSidebar
 *
 * Get the value of #GsSidebar:stack.
 *
 * Returns: (transfer none) (nullable): the stack, or %NULL if none is set
 * Since: 41
 */
GtkStack *
gs_sidebar_get_stack (GsSidebar *self)
{
	g_return_val_if_fail (GS_IS_SIDEBAR (self), NULL);

	return self->stack;
}

/**
 * gs_sidebar_set_stack:
 * @self: a #GsSidebar
 * @stack: (transfer none) (nullable): a new stack, or %NULL to clear it
 *
 * Set the value of #GsSidebar:stack.
 *
 * Since: 41
 */
void
gs_sidebar_set_stack (GsSidebar *self,
                      GtkStack  *stack)
{
	g_return_if_fail (GS_IS_SIDEBAR (self));
	g_return_if_fail (stack == NULL || GTK_IS_STACK (stack));

	if (self->stack == stack)
		return;

	if (self->stack != NULL) {
		disconnect_stack_signals (self);
		clear_stack_rows (self);
	}

	g_set_object (&self->stack, stack);

	if (self->stack != NULL) {
		populate_stack_rows (self);
		connect_stack_signals (self);
	}

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_STACK]);
}

/**
 * gs_sidebar_set_category_manager:
 * @self: a #GsSidebar
 * @manager: (transfer none) (nullable): a new category manager, or %NULL to
 *     clear it
 *
 * Set the value of #GsSidebar:category-manager.
 *
 * Since: 41
 */
void
gs_sidebar_set_category_manager (GsSidebar         *self,
                                 GsCategoryManager *manager)
{
	g_return_if_fail (GS_IS_SIDEBAR (self));
	g_return_if_fail (manager == NULL || GS_IS_CATEGORY_MANAGER (manager));

	if (self->category_manager == manager)
		return;

	if (self->category_manager != NULL)
		clear_category_rows (self);

	g_set_object (&self->category_manager, manager);

	if (self->category_manager != NULL)
		populate_category_rows (self);

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CATEGORY_MANAGER]);
}
