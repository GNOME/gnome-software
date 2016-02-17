/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Rafał Lużyński <digitalfreak@lingonborough.com>
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
#include <gtk/gtk.h>

#include "gs-hiding-box.h"

enum {
	PROP_0,
	PROP_SPACING
};

struct _GsHidingBox
{
	GtkContainer parent_instance;

	GList *children;
	gint16 spacing;
};

static void
gs_hiding_box_buildable_add_child (GtkBuildable *buildable,
				   GtkBuilder   *builder,
				   GObject      *child,
				   const gchar  *type)
{
	if (!type)
		gtk_container_add (GTK_CONTAINER (buildable), GTK_WIDGET (child));
	else
		GTK_BUILDER_WARN_INVALID_CHILD_TYPE (GS_HIDING_BOX (buildable), type);
}

static void
gs_hiding_box_buildable_init (GtkBuildableIface *iface)
{
	iface->add_child = gs_hiding_box_buildable_add_child;
}

G_DEFINE_TYPE_WITH_CODE (GsHidingBox, gs_hiding_box, GTK_TYPE_CONTAINER,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, gs_hiding_box_buildable_init))


static void
gs_hiding_box_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
	GsHidingBox *box = GS_HIDING_BOX (object);

	switch (prop_id) {
	case PROP_SPACING:
		gs_hiding_box_set_spacing (box, g_value_get_int (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_hiding_box_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
	GsHidingBox *box = GS_HIDING_BOX (object);

	switch (prop_id) {
	case PROP_SPACING:
		g_value_set_int (value, box->spacing);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_hiding_box_add (GtkContainer *container, GtkWidget *widget)
{
	GsHidingBox *box = GS_HIDING_BOX (container);

	box->children = g_list_append (box->children, widget);
	gtk_widget_set_parent (widget, GTK_WIDGET (box));
}

static void
gs_hiding_box_remove (GtkContainer *container, GtkWidget *widget)
{
	GList *child;
	GsHidingBox *box = GS_HIDING_BOX (container);

	for (child = box->children; child != NULL; child = child->next) {
		if (child->data == widget) {
			gboolean was_visible = gtk_widget_get_visible (widget) &&
			                       gtk_widget_get_child_visible (widget);

			gtk_widget_unparent (widget);
			box->children = g_list_delete_link (box->children, child);

			if (was_visible)
				gtk_widget_queue_resize (GTK_WIDGET (container));

			break;
		}
	}
}

static void
gs_hiding_box_forall (GtkContainer *container,
		      gboolean      include_internals,
		      GtkCallback   callback,
		      gpointer      callback_data)
{
	GsHidingBox *box = GS_HIDING_BOX (container);
	GtkWidget *child;
	GList *children;

	children = box->children;
	while (children) {
		child = children->data;
		children = children->next;
		(* callback) (child, callback_data);
	}
}

static void
gs_hiding_box_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	GsHidingBox *box = GS_HIDING_BOX (widget);
	gint nvis_children;

	GtkTextDirection direction;
	GtkAllocation child_allocation;
	GtkRequestedSize *sizes;

	gint size;
	gint extra = 0;
	gint n_extra_widgets = 0; /* Number of widgets that receive 1 extra px */
	gint x = 0, i;
	GList *child;
	GtkWidget *child_widget;
	gint spacing = box->spacing;
	gint children_size;
	GtkAllocation clip, child_clip;

	gtk_widget_set_allocation (widget, allocation);

	nvis_children = 0;
	for (child = box->children; child != NULL; child = child->next) {
		if (gtk_widget_get_visible (child->data))
			++nvis_children;
	}

	/* If there is no visible child, simply return. */
	if (nvis_children <= 0)
		return;

	direction = gtk_widget_get_direction (widget);
	sizes = g_newa (GtkRequestedSize, nvis_children);

	size = allocation->width;
	children_size = -spacing;
	/* Retrieve desired size for visible children. */
	for (i = 0, child = box->children; child != NULL; child = child->next) {

		child_widget = GTK_WIDGET (child->data);
		if (!gtk_widget_get_visible (child_widget))
			continue;

		gtk_widget_get_preferred_width_for_height (child_widget,
							   allocation->height,
							   &sizes[i].minimum_size,
							   &sizes[i].natural_size);

		/* Assert the api is working properly */
		if (sizes[i].minimum_size < 0)
			g_error ("GsHidingBox child %s minimum width: %d < 0 for height %d",
				 gtk_widget_get_name (child_widget),
				 sizes[i].minimum_size, allocation->height);

		if (sizes[i].natural_size < sizes[i].minimum_size)
			g_error ("GsHidingBox child %s natural width: %d < minimum %d for height %d",
				 gtk_widget_get_name (child_widget),
				 sizes[i].natural_size, sizes[i].minimum_size,
				 allocation->height);

		children_size += sizes[i].minimum_size + spacing;
		if (i > 0 && children_size > allocation->width)
			break;

		size -= sizes[i].minimum_size;
		sizes[i].data = child_widget;

		i++;
	}
	nvis_children = i;

	/* Bring children up to size first */
	size = gtk_distribute_natural_allocation (MAX (0, size), nvis_children, sizes);
	/* Only now we can subtract the spacings */
	size -= (nvis_children - 1) * spacing;

	if (nvis_children > 1) {
		extra = size / nvis_children;
		n_extra_widgets = size % nvis_children;
	}

	x = allocation->x;
	for (i = 0, child = box->children; child != NULL; child = child->next) {

		child_widget = GTK_WIDGET (child->data);
		if (!gtk_widget_get_visible (child_widget))
			continue;

		/* Hide the overflowing children even if they have visible=TRUE */
		if (i >= nvis_children) {
			while (child) {
				gtk_widget_set_child_visible (child->data, FALSE);
				child = child->next;
			}
			break;
		}

		child_allocation.x = x;
		child_allocation.y = allocation->y;
		child_allocation.width = sizes[i].minimum_size + extra;
		child_allocation.height = allocation->height;
		if (n_extra_widgets) {
			++child_allocation.width;
			--n_extra_widgets;
		}
		if (direction == GTK_TEXT_DIR_RTL) {
			child_allocation.x = allocation->x + allocation->width - (child_allocation.x - allocation->x) - child_allocation.width;
		}

		/* Let this child be visible */
		gtk_widget_set_child_visible (child_widget, TRUE);
		gtk_widget_size_allocate (child_widget, &child_allocation);
		x += child_allocation.width + spacing;
		++i;
	}

	/*
	 * The code below is inspired by _gtk_widget_set_simple_clip.
	 * Note: Here we ignore the "box-shadow" CSS property of the
	 * hiding box because we don't use it.
	 */
	clip = *allocation;
	if (gtk_widget_get_has_window (widget)) {
		clip.x = clip.y = 0;
	}

	for (child = box->children; child != NULL; child = child->next) {
		child_widget = GTK_WIDGET (child->data);
		if (gtk_widget_get_visible (child_widget) &&
		    gtk_widget_get_child_visible (child_widget)) {
			gtk_widget_get_clip (child_widget, &child_clip);
			gdk_rectangle_union (&child_clip, &clip, &clip);
		}
	}

	if (gtk_widget_get_has_window (widget)) {
		clip.x += allocation->x;
		clip.y += allocation->y;
	}
	gtk_widget_set_clip (widget, &clip);
}

static void
gs_hiding_box_get_preferred_width (GtkWidget *widget, gint *min, gint *nat)
{
	GsHidingBox *box = GS_HIDING_BOX (widget);
	gint cm, cn;
	gint m, n;
	GList *child;
	gint nvis_children;
	gboolean have_min = FALSE;

	m = n = nvis_children = 0;
	for (child = box->children; child != NULL; child = child->next) {
		if (!gtk_widget_is_visible (child->data))
			continue;

		++nvis_children;
		gtk_widget_get_preferred_width (child->data, &cm, &cn);
		/* Minimum is a minimum of the first visible child */
		if (!have_min) {
			m = cm;
			have_min = TRUE;
		}
		/* Natural is a sum of all visible children */
		n += cn;
	}

	/* Natural must also include the spacing */
	if (box->spacing && nvis_children > 1)
		n += box->spacing * (nvis_children - 1);

	if (min)
		*min = m;
	if (nat)
		*nat = n;
}

static void
gs_hiding_box_get_preferred_height (GtkWidget *widget, gint *min, gint *nat)
{
	gint m, n;
	gint cm, cn;
	GList *child;

	GsHidingBox *box = GS_HIDING_BOX (widget);
	m = n = 0;
	for (child = box->children; child != NULL; child = child->next) {
		if (!gtk_widget_is_visible (child->data))
			continue;

		gtk_widget_get_preferred_height (child->data, &cm, &cn);
		m = MAX (m, cm);
		n = MAX (n, cn);
	}

	if (min)
		*min = m;
	if (nat)
		*nat = n;
}

static void
gs_hiding_box_init (GsHidingBox *box)
{
	gtk_widget_set_has_window (GTK_WIDGET (box), FALSE);
	gtk_widget_set_redraw_on_allocate (GTK_WIDGET (box), FALSE);

	box->spacing = 0;
}

static void
gs_hiding_box_class_init (GsHidingBoxClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
	GtkContainerClass *container_class = GTK_CONTAINER_CLASS (class);

	object_class->set_property = gs_hiding_box_set_property;
	object_class->get_property = gs_hiding_box_get_property;

	widget_class->size_allocate = gs_hiding_box_size_allocate;
	widget_class->get_preferred_width = gs_hiding_box_get_preferred_width;
	widget_class->get_preferred_height = gs_hiding_box_get_preferred_height;

	container_class->add = gs_hiding_box_add;
	container_class->remove = gs_hiding_box_remove;
	container_class->forall = gs_hiding_box_forall;

	g_object_class_install_property (object_class,
		PROP_SPACING,
		g_param_spec_int ("spacing",
				/* TRANSLATORS: Here are 2 strings the same as in gtk/gtkbox.c
				   in GTK+ project. Please use the same translation. */
				_("Spacing"),
				_("The amount of space between children"),
				0, G_MAXINT, 0,
				G_PARAM_READWRITE|G_PARAM_EXPLICIT_NOTIFY));
}

/**
 * gs_hiding_box_new:
 *
 * Creates a new #GsHidingBox.
 *
 * Returns: a new #GsHidingBox.
 **/
GtkWidget *
gs_hiding_box_new (void)
{
	return g_object_new (GS_TYPE_HIDING_BOX, NULL);
}

/**
 * gs_hiding_box_set_spacing:
 * @box: a #GsHidingBox
 * @spacing: the number of pixels to put between children
 *
 * Sets the #GsHidingBox:spacing property of @box, which is the
 * number of pixels to place between children of @box.
 */
void
gs_hiding_box_set_spacing (GsHidingBox *box, gint spacing)
{
	g_return_if_fail (GS_IS_HIDING_BOX (box));

	if (box->spacing != spacing) {
		box->spacing = spacing;

		g_object_notify (G_OBJECT (box), "spacing");

		gtk_widget_queue_resize (GTK_WIDGET (box));
	}
}

/**
 * gs_hiding_box_get_spacing:
 * @box: a #GsHidingBox
 *
 * Gets the value set by gs_hiding_box_set_spacing().
 *
 * Returns: spacing between children
 **/
gint
gs_hiding_box_get_spacing (GsHidingBox *box)
{
	g_return_val_if_fail (GS_IS_HIDING_BOX (box), 0);

	return box->spacing;
}

/* vim: set noexpandtab: */
