/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-summary-tile.h"
#include "gs-common.h"

struct _GsSummaryTile
{
	GsAppTile	 parent_instance;
	GtkWidget	*image;
	GtkWidget	*name;
	GtkWidget	*summary;
	GtkWidget	*eventbox;
	GtkWidget	*stack;
	gint		 preferred_width;
};

G_DEFINE_TYPE (GsSummaryTile, gs_summary_tile, GS_TYPE_APP_TILE)

enum {
	PROP_0,
	PROP_PREFERRED_WIDTH
};

static void
gs_summary_tile_refresh (GsAppTile *self)
{
	GsSummaryTile *tile = GS_SUMMARY_TILE (self);
	GsApp *app = gs_app_tile_get_app (self);
	AtkObject *accessible;
	GtkStyleContext *context;
	const GdkPixbuf *pixbuf;
	gboolean installed;
	g_autofree gchar *name = NULL;
	g_autofree gchar *summary = NULL;
	const gchar *css;

	if (app == NULL)
		return;

	gtk_image_set_pixel_size (GTK_IMAGE (tile->image), 64);
	gtk_stack_set_visible_child_name (GTK_STACK (tile->stack), "content");

	/* set name */
	gtk_label_set_label (GTK_LABEL (tile->name), gs_app_get_name (app));

	/* some kinds have boring summaries */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_SHELL_EXTENSION:
		summary = g_strdup (gs_app_get_description (app));
		if (summary != NULL)
			g_strdelimit (summary, "\n\t", ' ');
		break;
	default:
		summary = g_strdup (gs_app_get_summary (app));
		break;
	}

	gtk_label_set_label (GTK_LABEL (tile->summary), summary);
	gtk_widget_set_visible (tile->summary, summary && summary[0]);

	pixbuf = gs_app_get_pixbuf (app);
	if (pixbuf != NULL) {
		gs_image_set_from_pixbuf (GTK_IMAGE (tile->image), pixbuf);
	} else {
		gtk_image_set_from_icon_name (GTK_IMAGE (tile->image),
					      "application-x-executable",
					      GTK_ICON_SIZE_DIALOG);
	}
	context = gtk_widget_get_style_context (tile->image);
	if (gs_app_get_use_drop_shadow (app))
		gtk_style_context_add_class (context, "icon-dropshadow");
	else
		gtk_style_context_remove_class (context, "icon-dropshadow");

	/* perhaps set custom css */
	css = gs_app_get_metadata_item (app, "GnomeSoftware::AppTile-css");
	gs_utils_widget_set_css (GTK_WIDGET (tile), css);

	accessible = gtk_widget_get_accessible (GTK_WIDGET (tile));

	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		installed = TRUE;
		name = g_strdup_printf (_("%s (Installed)"),
					gs_app_get_name (app));
		break;
	case AS_APP_STATE_INSTALLING:
		installed = FALSE;
		name = g_strdup_printf (_("%s (Installing)"),
					gs_app_get_name (app));
		break;
	case AS_APP_STATE_REMOVING:
		installed = TRUE;
		name = g_strdup_printf (_("%s (Removing)"),
					gs_app_get_name (app));
		break;
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
	case AS_APP_STATE_AVAILABLE:
	default:
		installed = FALSE;
		name = g_strdup (gs_app_get_name (app));
		break;
	}

	gtk_widget_set_visible (tile->eventbox, installed);

	if (GTK_IS_ACCESSIBLE (accessible) && name != NULL) {
		atk_object_set_name (accessible, name);
		atk_object_set_description (accessible, gs_app_get_summary (app));
	}
}

static void
gs_summary_tile_init (GsSummaryTile *tile)
{
	gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
	tile->preferred_width = -1;
//	gtk_image_clear (GTK_IMAGE (tile->image));
	gtk_widget_init_template (GTK_WIDGET (tile));
}

static void
gs_summary_tile_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	GsSummaryTile *app_tile = GS_SUMMARY_TILE (object);

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
gs_summary_tile_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	GsSummaryTile *app_tile = GS_SUMMARY_TILE (object);

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
	gint m;
	GsSummaryTile *app_tile = GS_SUMMARY_TILE (widget);

	if (app_tile->preferred_width < 0) {
		/* Just retrieve the default values */
		GTK_WIDGET_CLASS (gs_summary_tile_parent_class)->get_preferred_width (widget, min, nat);
		return;
	}

	GTK_WIDGET_CLASS (gs_summary_tile_parent_class)->get_preferred_width (widget, &m, NULL);

	if (min != NULL)
		*min = m;
	if (nat != NULL)
		*nat = MAX (m, app_tile->preferred_width);
}

static void
gs_summary_tile_class_init (GsSummaryTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GsAppTileClass *tile_class = GS_APP_TILE_CLASS (klass);

	object_class->get_property = gs_summary_tile_get_property;
	object_class->set_property = gs_summary_tile_set_property;

	widget_class->get_preferred_width = gs_app_get_preferred_width;

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
	g_object_class_install_property (object_class, PROP_PREFERRED_WIDTH,
					 g_param_spec_int ("preferred-width",
							   "Preferred width",
							   "The preferred width of this widget, its only purpose is to trick the parent container",
							   -1, G_MAXINT, -1,
							   G_PARAM_READWRITE));

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-summary-tile.ui");

	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      image);
	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      name);
	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      summary);
	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      eventbox);
	gtk_widget_class_bind_template_child (widget_class, GsSummaryTile,
					      stack);
}

GtkWidget *
gs_summary_tile_new (GsApp *cat)
{
	GsAppTile *tile = g_object_new (GS_TYPE_SUMMARY_TILE, NULL);
	gs_app_tile_set_app (tile, cat);
	return GTK_WIDGET (tile);
}
