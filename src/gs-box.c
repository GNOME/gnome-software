/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include "gs-box.h"

typedef struct {
	GtkWidget       *widget;
	gdouble	  relative_size;
} GsBoxChild;

struct _GsBox {
	GtkContainer parent;
	GList *children;
	gdouble total;
};

struct _GsBoxClass {
	GtkContainerClass parent_class;
};

G_DEFINE_TYPE (GsBox, gs_box, GTK_TYPE_CONTAINER)

enum {
	CHILD_PROP_0,
	CHILD_PROP_RELATIVE_SIZE
};

static void
gs_box_real_add (GtkContainer *container, GtkWidget *widget)
{
	gs_box_add (GS_BOX (container), widget, 1.0);
}

static void
gs_box_remove (GtkContainer *container, GtkWidget *widget)
{
	GsBox *box = GS_BOX (container);
	GList *l;

	for (l = box->children; l; l = l->next) {
		GsBoxChild *child = l->data;
		if (child->widget == widget) {
			gtk_widget_unparent (child->widget);
			box->children = g_list_delete_link (box->children, l);
			box->total -= child->relative_size;
			g_free (child);
			gtk_widget_queue_resize (GTK_WIDGET (container));
			break;
		}
	}
}

static void
gs_box_forall (GtkContainer *container,
	       gboolean      include_internals,
	       GtkCallback   callback,
	       gpointer      callback_data)
{
	GsBox *box = GS_BOX (container);
	GsBoxChild *child;
	GList *children;

	children = box->children;
	while (children) {
		child = children->data;
		children = children->next;
		(* callback) (child->widget, callback_data);
	}
}

static void
gs_box_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	GsBox *box = GS_BOX (widget);
	GsBoxChild *child;
	GtkAllocation child_allocation;
	gint x;
	GList *l;
	gboolean rtl;

	rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;

	gtk_widget_set_allocation (widget, allocation);

	x = allocation->x;
	for (l = box->children; l; l = l->next) {
		child = l->data;
		child_allocation.x = x;
		child_allocation.y = allocation->y;
		if (l->next == NULL)
			child_allocation.width = allocation->x + allocation->width - child_allocation.x;
		else
			child_allocation.width = allocation->width * (child->relative_size / box->total);
		child_allocation.height = allocation->height;
		if (rtl) {
			child_allocation.x = allocation->x + allocation->width - child_allocation.x - child_allocation.width;
		}
		gtk_widget_size_allocate (child->widget, &child_allocation);
		x += child_allocation.width;
	}
}

static void
gs_box_get_preferred_width (GtkWidget *widget, gint *min, gint *nat)
{
	GsBox *box = GS_BOX (widget);
	GsBoxChild *child;
	gint cm, *cn;
	gint n_children;
	gint ms, m, n;
	GList *l;
	gint i;

	n_children = g_list_length (box->children);

	cn = g_new0 (gint, n_children);

	ms = 0;
	for (l = box->children, i = 0; l; l = l->next, i++) {
		child = l->data;
		gtk_widget_get_preferred_width (child->widget, &cm, cn + i);
		ms = MAX (ms, cm / child->relative_size);
	}

	m = n = 0;
	for (l = box->children, i = 0; l; l = l->next, i++) {
		cm = ms * child->relative_size;
		m += cm;
		n += MAX (cn[i], cm);
	}

	g_free (cn);

	if (min)
		*min = m;
	if (nat)
		*nat = n;
}

static void
gs_box_get_preferred_height (GtkWidget *widget, gint *min, gint *nat)
{
	GsBox *box = GS_BOX (widget);
	gint m, n;
	gint cm, cn;
	GsBoxChild *child;
	GList *l;

	m = n = 0;
	for (l = box->children; l; l = l->next) {
		child = l->data;
		gtk_widget_get_preferred_height (child->widget, &cm, &cn);
		m = MAX (m, cm);
		n = MAX (n, cn);
	}
	if (min)
		*min = m;
	if (nat)
		*nat = n;
}

static void
gs_box_get_child_property (GtkContainer *container,
			   GtkWidget    *widget,
			   guint	 property_id,
			   GValue       *value,
			   GParamSpec   *pspec)
{
	GsBox *box = GS_BOX (container);
	GList *l;
	GsBoxChild *child;

	child = NULL;
	for (l = box->children; l; l = l->next) {
		child = l->data;
		if (child->widget == widget) {
			break;
		}
	}
	if (child == NULL) {
		GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container, property_id, pspec);
		return;
	}

	switch (property_id) {
	case CHILD_PROP_RELATIVE_SIZE:
		g_value_set_double (value, child->relative_size);
		break;
	default:
		GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container, property_id, pspec);
	}
}

static void
gs_box_set_child_property (GtkContainer *container,
			   GtkWidget    *widget,
			   guint	 property_id,
			   const GValue *value,
			   GParamSpec   *pspec)
{
	GsBox *box = GS_BOX (container);
	GList *l;
	GsBoxChild *child;

	child = NULL;
	for (l = box->children; l; l = l->next) {
		child = l->data;
		if (child->widget == widget) {
			break;
		}
	}
	if (child == NULL) {
		GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container, property_id, pspec);
		return;
	}

	switch (property_id) {
	case CHILD_PROP_RELATIVE_SIZE:
		box->total -= child->relative_size;
		child->relative_size = g_value_get_double (value);
		box->total += child->relative_size;
		break;
	default:
		GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID (container, property_id, pspec);
	}
}

static void
gs_box_init (GsBox *box)
{
	gtk_widget_set_has_window (GTK_WIDGET (box), FALSE);
}

static void
gs_box_class_init (GsBoxClass *class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
	GtkContainerClass *container_class = GTK_CONTAINER_CLASS (class);

	widget_class->size_allocate = gs_box_size_allocate;
	widget_class->get_preferred_width = gs_box_get_preferred_width;
	widget_class->get_preferred_height = gs_box_get_preferred_height;

	container_class->add = gs_box_real_add;
	container_class->remove = gs_box_remove;
	container_class->forall = gs_box_forall;
	container_class->get_child_property = gs_box_get_child_property;
	container_class->set_child_property = gs_box_set_child_property;

	gtk_container_class_install_child_property (container_class,
		CHILD_PROP_RELATIVE_SIZE,
		g_param_spec_double ("relative-size", NULL, NULL,
				     0.0, G_MAXDOUBLE, 1.0,
				     G_PARAM_READWRITE));
}

GtkWidget *
gs_box_new (void)
{
	return g_object_new (GS_TYPE_BOX, NULL);
}

void
gs_box_add (GsBox *box, GtkWidget *widget, gdouble relative_size)
{
	GsBoxChild *child;

	child = g_new (GsBoxChild, 1);

	child->widget = widget;
	child->relative_size = relative_size;

	box->total += relative_size;

	box->children = g_list_append (box->children, child);
	gtk_widget_set_parent (widget, GTK_WIDGET (box));
}

/* vim: set noexpandtab: */
