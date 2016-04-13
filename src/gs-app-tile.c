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

#include "gs-app-tile.h"
#include "gs-star-widget.h"
#include "gs-utils.h"

struct _GsAppTile
{
	GtkButton	 parent_instance;

	GsApp		*app;
	GtkWidget	*image;
	GtkWidget	*name;
	GtkWidget	*summary;
	GtkWidget	*eventbox;
	GtkWidget	*stack;
	gint		 preferred_width;
};

G_DEFINE_TYPE (GsAppTile, gs_app_tile, GTK_TYPE_BUTTON)

enum {
	PROP_0,
	PROP_PREFERRED_WIDTH
};

GsApp *
gs_app_tile_get_app (GsAppTile *tile)
{
	g_return_val_if_fail (GS_IS_APP_TILE (tile), NULL);

	return tile->app;
}

static gboolean
app_state_changed_idle (gpointer user_data)
{
	GsAppTile *tile = GS_APP_TILE (user_data);
	AtkObject *accessible;
	GtkWidget *label;
	gboolean installed;
	g_autofree gchar *name = NULL;

	accessible = gtk_widget_get_accessible (GTK_WIDGET (tile));

	label = gtk_bin_get_child (GTK_BIN (tile->eventbox));
	switch (gs_app_get_state (tile->app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (tile->app),
					_("Installed"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user the application is installed */
		gtk_label_set_label (GTK_LABEL (label), _("Installed"));
		break;
	case AS_APP_STATE_INSTALLING:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (tile->app),
					_("Installing"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user the application is being installed */
		gtk_label_set_label (GTK_LABEL (label), _("Installing"));
		break;
	case AS_APP_STATE_REMOVING:
		installed = TRUE;
		name = g_strdup_printf ("%s (%s)",
					gs_app_get_name (tile->app),
					_("Removing"));
		/* TRANSLATORS: this is the small blue label on the tile
		 * that tells the user the application is being removed */
		gtk_label_set_label (GTK_LABEL (label), _("Removing"));
		break;
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
	case AS_APP_STATE_AVAILABLE:
	default:
		installed = FALSE;
		name = g_strdup (gs_app_get_name (tile->app));
		break;
	}

	gtk_widget_set_visible (tile->eventbox, installed);

	if (GTK_IS_ACCESSIBLE (accessible)) {
		atk_object_set_name (accessible, name);
		atk_object_set_description (accessible, gs_app_get_summary (tile->app));
	}

	g_object_unref (tile);
	return G_SOURCE_REMOVE;
}

static void
app_state_changed (GsApp *app, GParamSpec *pspec, GsAppTile *tile)
{
	g_idle_add (app_state_changed_idle, g_object_ref (tile));
}

void
gs_app_tile_set_app (GsAppTile *tile, GsApp *app)
{
	const GdkPixbuf *pixbuf;
	const gchar *css;
	g_autofree gchar *text = NULL;

	g_return_if_fail (GS_IS_APP_TILE (tile));
	g_return_if_fail (GS_IS_APP (app) || app == NULL);

	gtk_image_clear (GTK_IMAGE (tile->image));
	gtk_image_set_pixel_size (GTK_IMAGE (tile->image), 64);

	if (tile->app)
		g_signal_handlers_disconnect_by_func (tile->app, app_state_changed, tile);

	g_set_object (&tile->app, app);
	if (!app)
		return;

	gtk_stack_set_visible_child_name (GTK_STACK (tile->stack), "content");

	g_signal_connect (tile->app, "notify::state",
			  G_CALLBACK (app_state_changed), tile);
	app_state_changed (tile->app, NULL, tile);

	pixbuf = gs_app_get_pixbuf (app);
	if (pixbuf != NULL)
		gs_image_set_from_pixbuf (GTK_IMAGE (tile->image), pixbuf);
	gtk_label_set_label (GTK_LABEL (tile->name), gs_app_get_name (app));

	/* set custom css */
	css = gs_app_get_metadata_item (app, "GnomeSoftware::AppTile-css");
	if (css != NULL)
		gs_utils_widget_set_custom_css (GTK_WIDGET (tile), css);

	/* some kinds have boring summaries */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_SHELL_EXTENSION:
		text = g_strdup (gs_app_get_description (app));
		g_strdelimit (text, "\n\t", ' ');
		break;
	default:
		text = g_strdup (gs_app_get_summary (app));
		break;
	}

	gtk_label_set_label (GTK_LABEL (tile->summary), text);
	gtk_widget_set_visible (tile->summary, text && text[0]);
}

static void
gs_app_tile_destroy (GtkWidget *widget)
{
	GsAppTile *tile = GS_APP_TILE (widget);

	if (tile->app)
		g_signal_handlers_disconnect_by_func (tile->app, app_state_changed, tile);
	g_clear_object (&tile->app);

	GTK_WIDGET_CLASS (gs_app_tile_parent_class)->destroy (widget);
}

static void
gs_app_tile_init (GsAppTile *tile)
{
	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	tile->preferred_width = -1;
	gtk_widget_init_template (GTK_WIDGET (tile));
}

static void
gs_app_tile_get_property (GObject *object,
			  guint prop_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	GsAppTile *app_tile = GS_APP_TILE (object);

	switch (prop_id) {
	case PROP_PREFERRED_WIDTH:
		g_value_set_int (value, app_tile->preferred_width);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_tile_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec)
{
	GsAppTile *app_tile = GS_APP_TILE (object);

	switch (prop_id) {
	case PROP_PREFERRED_WIDTH:
		app_tile->preferred_width = g_value_get_int (value);
		gtk_widget_queue_resize (GTK_WIDGET (app_tile));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_get_preferred_width (GtkWidget *widget,
			    gint *min, gint *nat)
{
#if GTK_CHECK_VERSION(3,20,0)
	gint m;
#else
	gint m, n;
#endif
	GsAppTile *app_tile = GS_APP_TILE (widget);

	if (app_tile->preferred_width < 0) {
		/* Just retrieve the default values */
		GTK_WIDGET_CLASS (gs_app_tile_parent_class)->get_preferred_width (widget, min, nat);
		return;
	}

/* It's because of some bugs in gtkbutton.c 3.18 and before.
 * We can remove this when we branch for 3.20 *and* require GTK 3.20. */
#if GTK_CHECK_VERSION(3,20,0)
	GTK_WIDGET_CLASS (gs_app_tile_parent_class)->get_preferred_width (widget, &m, NULL);
#else
	GTK_WIDGET_CLASS (gs_app_tile_parent_class)->get_preferred_width (widget, &m, &n);
#endif

	if (min != NULL)
		*min = m;
	if (nat != NULL)
		*nat = MAX (m, app_tile->preferred_width);
}

static void
gs_app_tile_class_init (GsAppTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_app_tile_get_property;
	object_class->set_property = gs_app_tile_set_property;

	widget_class->destroy = gs_app_tile_destroy;
	widget_class->get_preferred_width = gs_app_get_preferred_width;

	/**
	 * GsAppTile:preferred-width:
	 *
	 * The only purpose of this property is to be retrieved as the
	 * natural width by gtk_widget_get_preferred_width() fooling the
	 * parent #GtkFlowBox container and making it switch to more columns
	 * (children per row) if it is able to place n+1 children in a row
	 * having this specified width.  If this value is less than a minimum
	 * width of this app tile then the minimum is returned instead.  Set
	 * this property to -1 to turn off this feature and return the default
	 * natural width instead.
	 */
	g_object_class_install_property (object_class, PROP_PREFERRED_WIDTH,
		g_param_spec_int ("preferred-width",
				  "Preferred width",
				  "The preferred width of this widget, its only purpose is to trick the parent container",
				  -1, G_MAXINT, -1,
				  G_PARAM_READWRITE));

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-tile.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAppTile, image);
	gtk_widget_class_bind_template_child (widget_class, GsAppTile, name);
	gtk_widget_class_bind_template_child (widget_class, GsAppTile, summary);
	gtk_widget_class_bind_template_child (widget_class, GsAppTile, eventbox);
	gtk_widget_class_bind_template_child (widget_class, GsAppTile, stack);
}

GtkWidget *
gs_app_tile_new (GsApp *cat)
{
	GsAppTile *tile;

	tile = g_object_new (GS_TYPE_APP_TILE, NULL);
	gs_app_tile_set_app (tile, cat);

	return GTK_WIDGET (tile);
}

/* vim: set noexpandtab: */
