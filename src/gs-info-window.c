/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Purism SPC
 *
 * Author: Adrien Plazas <adrien.plazas@puri.sm>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-info-window
 * @short_description: A minimalist window designed to present information
 *
 * #GsInfoWindow is a window with floating window buttons which can be closed
 * by pressing the Escape key. It is intended to present information and to not
 * give the user many interaction possibilities.
 *
 * Since: 42
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <locale.h>

#include "gs-common.h"
#include "gs-info-window.h"

typedef struct
{
	GtkWidget	*overlay;

	GtkWidget	*user_widget;
} GsInfoWindowPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (GsInfoWindow, gs_info_window, ADW_TYPE_WINDOW)

static gboolean
key_press_event_cb (GtkWidget            *sender,
                    GdkEvent             *event,
                    AdwPreferencesWindow *self)
{
	guint keyval;
	GdkModifierType state;
	GdkKeymap *keymap;
	GdkEventKey *key_event = (GdkEventKey *) event;

	gdk_event_get_state (event, &state);

	keymap = gdk_keymap_get_for_display (gtk_widget_get_display (sender));

	gdk_keymap_translate_keyboard_state (keymap,
					     key_event->hardware_keycode,
					     state,
					     key_event->group,
					     &keyval, NULL, NULL, NULL);

	if (keyval == GDK_KEY_Escape) {
		gtk_window_close (GTK_WINDOW (self));

		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static void
gs_info_window_init (GsInfoWindow *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_info_window_destroy (GtkWidget *widget)
{
	GsInfoWindow *self = GS_INFO_WINDOW (widget);
	GsInfoWindowPrivate *priv = gs_info_window_get_instance_private (self);

	if (priv->overlay) {
		gtk_container_remove (GTK_CONTAINER (self), priv->overlay);
		priv->user_widget = NULL;
	}

	GTK_WIDGET_CLASS (gs_info_window_parent_class)->destroy (widget);
}

static void
gs_info_window_add (GtkContainer *container, GtkWidget *child)
{
	GsInfoWindow *self = GS_INFO_WINDOW (container);
	GsInfoWindowPrivate *priv = gs_info_window_get_instance_private (self);

	if (!priv->overlay) {
		GTK_CONTAINER_CLASS (gs_info_window_parent_class)->add (container, child);
	} else if (!priv->user_widget) {
		gtk_container_add (GTK_CONTAINER (priv->overlay), child);
		priv->user_widget = child;
	} else {
		g_warning ("Attempting to add a second child to a GsInfoWindow, but a GsInfoWindow can only have one child");
	}
}

static void
gs_info_window_remove (GtkContainer *container, GtkWidget *child)
{
	GsInfoWindow *self = GS_INFO_WINDOW (container);
	GsInfoWindowPrivate *priv = gs_info_window_get_instance_private (self);

	if (child == priv->overlay) {
		GTK_CONTAINER_CLASS (gs_info_window_parent_class)->remove (container, child);
	} else if (child == priv->user_widget) {
		gtk_container_remove (GTK_CONTAINER (priv->overlay), child);
		priv->user_widget = NULL;
	} else {
		g_return_if_reached ();
	}
}

static void
gs_info_window_forall (GtkContainer *container, gboolean include_internals, GtkCallback callback, gpointer callback_data)
{
	GsInfoWindow *self = GS_INFO_WINDOW (container);
	GsInfoWindowPrivate *priv = gs_info_window_get_instance_private (self);

	if (include_internals) {
		GTK_CONTAINER_CLASS (gs_info_window_parent_class)->forall (container,
									   include_internals,
									   callback,
									   callback_data);
	} else if (priv->user_widget) {
		callback (priv->user_widget, callback_data);
	}
}

static void
gs_info_window_class_init (GsInfoWindowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

	widget_class->destroy = gs_info_window_destroy;
	container_class->add = gs_info_window_add;
	container_class->remove = gs_info_window_remove;
	container_class->forall = gs_info_window_forall;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-info-window.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsInfoWindow, overlay);

	gtk_widget_class_bind_template_callback (widget_class, key_press_event_cb);
}

/**
 * gs_info_window_new:
 *
 * Create a new #GsInfoWindow.
 *
 * Returns: (transfer full): a new #GsInfoWindow
 * Since: 42
 */
GsInfoWindow *
gs_info_window_new (void)
{
	return g_object_new (GS_TYPE_INFO_WINDOW, NULL);
}
