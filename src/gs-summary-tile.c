/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-summary-tile.h"
#include "gs-summary-tile-layout.h"
#include "gs-common.h"

struct _GsSummaryTile
{
	GsAppTile	 parent_instance;

	GtkWidget	*image;
	GtkWidget	*name;
	GtkWidget	*summary;
	GtkWidget	*bin;
	GtkWidget	*stack;
	gint		 preferred_width;
};

G_DEFINE_TYPE (GsSummaryTile, gs_summary_tile, GS_TYPE_APP_TILE)

typedef enum {
	PROP_PREFERRED_WIDTH = 1,
} GsSummaryTileProperty;

static GParamSpec *obj_props[PROP_PREFERRED_WIDTH + 1] = { NULL, };

static void
gs_summary_tile_refresh (GsAppTile *self)
{
	GsSummaryTile *tile = GS_SUMMARY_TILE (self);
	GsApp *app = gs_app_tile_get_app (self);
	g_autoptr(GIcon) icon = NULL;
	gboolean installed;
	g_autofree gchar *name = NULL;
	const gchar *summary;

	if (app == NULL)
		return;

	gtk_image_set_pixel_size (GTK_IMAGE (tile->image), 64);
	gtk_stack_set_visible_child_name (GTK_STACK (tile->stack), "content");

	/* set name */
	gtk_label_set_label (GTK_LABEL (tile->name), gs_app_get_name (app));

	summary = gs_app_get_summary (app);
	gtk_label_set_label (GTK_LABEL (tile->summary), summary);
	gtk_widget_set_visible (tile->summary, summary && summary[0]);

	icon = gs_app_get_icon_for_size (app,
					 gtk_image_get_pixel_size (GTK_IMAGE (tile->image)),
					 gtk_widget_get_scale_factor (tile->image),
					 "system-component-application");
	gtk_image_set_from_gicon (GTK_IMAGE (tile->image), icon);

	switch (gs_app_get_state (app)) {
	case GS_APP_STATE_INSTALLED:
	case GS_APP_STATE_UPDATABLE:
	case GS_APP_STATE_UPDATABLE_LIVE:
		installed = TRUE;
		name = g_strdup_printf (_("%s (Installed)"),
					gs_app_get_name (app));
		break;
	case GS_APP_STATE_INSTALLING:
		installed = FALSE;
		name = g_strdup_printf (_("%s (Installing)"),
					gs_app_get_name (app));
		break;
	case GS_APP_STATE_REMOVING:
		installed = TRUE;
		name = g_strdup_printf (_("%s (Removing)"),
					gs_app_get_name (app));
		break;
	case GS_APP_STATE_QUEUED_FOR_INSTALL:
	case GS_APP_STATE_AVAILABLE:
	default:
		installed = FALSE;
		name = g_strdup (gs_app_get_name (app));
		break;
	}

	gtk_widget_set_visible (tile->bin, installed);

	if (name != NULL) {
		gtk_accessible_update_property (GTK_ACCESSIBLE (tile),
						GTK_ACCESSIBLE_PROPERTY_LABEL, name,
						GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, gs_app_get_summary (app),
						-1);
	}
}

static void
gs_summary_tile_init (GsSummaryTile *tile)
{
	tile->preferred_width = -1;
	gtk_widget_init_template (GTK_WIDGET (tile));
}

static void
gs_summary_tile_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	GsSummaryTile *app_tile = GS_SUMMARY_TILE (object);

	switch ((GsSummaryTileProperty) prop_id) {
	case PROP_PREFERRED_WIDTH:
		g_value_set_int (value, app_tile->preferred_width);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_summary_tile_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	GsSummaryTile *app_tile = GS_SUMMARY_TILE (object);
	GtkLayoutManager *layout_manager;

	switch ((GsSummaryTileProperty) prop_id) {
	case PROP_PREFERRED_WIDTH:
		app_tile->preferred_width = g_value_get_int (value);
		layout_manager = gtk_widget_get_layout_manager (GTK_WIDGET (app_tile));
		gs_summary_tile_layout_set_preferred_width (GS_SUMMARY_TILE_LAYOUT (layout_manager),
							    g_value_get_int (value));
		g_object_notify_by_pspec (object, obj_props[PROP_PREFERRED_WIDTH]);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_summary_tile_class_init (GsSummaryTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GsAppTileClass *tile_class = GS_APP_TILE_CLASS (klass);

	object_class->get_property = gs_summary_tile_get_property;
	object_class->set_property = gs_summary_tile_set_property;

	tile_class->refresh = gs_summary_tile_refresh;

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
	obj_props[PROP_PREFERRED_WIDTH] =
		g_param_spec_int ("preferred-width",
				  "Preferred width",
				  "The preferred width of this widget, its only purpose is to trick the parent container",
				  -1, G_MAXINT, -1,
				  G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-summary-tile.ui");
	gtk_widget_class_set_layout_manager_type (widget_class, GS_TYPE_SUMMARY_TILE_LAYOUT);

	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      image);
	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      name);
	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      summary);
	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      bin);
	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      stack);
}

GtkWidget *
gs_summary_tile_new (GsApp *app)
{
	return g_object_new (GS_TYPE_SUMMARY_TILE,
			     "app", app,
			     NULL);
}
