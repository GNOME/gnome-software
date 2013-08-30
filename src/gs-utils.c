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

#include "gs-utils.h"

#define SPINNER_DELAY 500

static gboolean
fade_in (gpointer data)
{
        GtkWidget *spinner = data;
        gdouble opacity;

        opacity = gtk_widget_get_opacity (spinner);
        opacity = opacity + 0.1;
        gtk_widget_set_opacity (spinner, opacity);

        if (opacity >= 1.0)
                return G_SOURCE_REMOVE;

        return G_SOURCE_CONTINUE;
}

static void
remove_source (gpointer data)
{
        g_source_remove (GPOINTER_TO_UINT (data));
}

static gboolean
start_spinning (gpointer data)
{
        GtkWidget *spinner = data;
        guint id;

        gtk_widget_set_opacity (spinner, 0);
        gtk_spinner_start (GTK_SPINNER (spinner));
        gtk_widget_show (spinner);
        id = g_timeout_add (100, fade_in, spinner);
        g_object_set_data_full (G_OBJECT (spinner), "fade-timeout",
                                GUINT_TO_POINTER (id), remove_source);

        return G_SOURCE_REMOVE;
}

void
gs_stop_spinner (GtkSpinner *spinner)
{
        gtk_widget_hide (GTK_WIDGET (spinner));
        gtk_spinner_stop (spinner);
}

void
gs_start_spinner (GtkSpinner *spinner)
{
        guint id;

        gtk_widget_set_opacity (GTK_WIDGET (spinner), 0);
        id = g_timeout_add (SPINNER_DELAY, start_spinning, spinner);

        g_object_set_data_full (G_OBJECT (spinner), "start-timeout",
                                GUINT_TO_POINTER (id), remove_source);
}

static void
remove_all_cb (GtkWidget *widget, gpointer user_data)
{
        GtkContainer *container = GTK_CONTAINER (user_data);
        gtk_container_remove (container, widget);
}

void
gs_container_remove_all (GtkContainer *container)
{
        gtk_container_foreach (container, remove_all_cb, container);
}

