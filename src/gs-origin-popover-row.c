/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
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

#include "gs-origin-popover-row.h"

#include <glib/gi18n.h>

typedef struct
{
	GsApp		*app;
	GtkWidget	*name_label;
	GtkWidget	*url_box;
	GtkWidget	*url_label;
	GtkWidget	*format_box;
	GtkWidget	*format_label;
	GtkWidget	*selected_image;
} GsOriginPopoverRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsOriginPopoverRow, gs_origin_popover_row, GTK_TYPE_LIST_BOX_ROW)

static void
refresh_ui (GsOriginPopoverRow *row)
{
	GsOriginPopoverRowPrivate *priv = gs_origin_popover_row_get_instance_private (row);
	const gchar *origin_ui = NULL;
	const gchar *url;
	const gchar *packaging_format;
	g_autoptr(GsOsRelease) os_release = NULL;

	g_assert (GS_IS_ORIGIN_POPOVER_ROW (row));
	g_assert (GS_IS_APP (priv->app));

	/* use the distro name for official packages */
	if (gs_app_has_quirk (priv->app, AS_APP_QUIRK_PROVENANCE)) {
		os_release = gs_os_release_new (NULL);
		if (os_release != NULL)
			origin_ui = gs_os_release_get_name (os_release);
	}

	/* fall back to origin */
	if (origin_ui == NULL)
		origin_ui = gs_app_get_origin (priv->app);

	if (origin_ui != NULL) {
		gtk_label_set_text (GTK_LABEL (priv->name_label), origin_ui);
	}

	url = gs_app_get_origin_hostname (priv->app);
	if (url != NULL) {
		gtk_label_set_text (GTK_LABEL (priv->url_label), url);
		gtk_widget_show (priv->url_box);
	} else {
		gtk_widget_hide (priv->url_box);
	}

	packaging_format = gs_app_get_metadata_item (priv->app, "GnomeSoftware::PackagingFormat");
	if (packaging_format == NULL) {
		AsBundleKind bundle_kind;
		bundle_kind = gs_app_get_bundle_kind (priv->app);
		if (bundle_kind != AS_BUNDLE_KIND_UNKNOWN)
			packaging_format = as_bundle_kind_to_string (bundle_kind);
	}

	if (packaging_format != NULL) {
		gtk_label_set_text (GTK_LABEL (priv->format_label), packaging_format);
		gtk_widget_show (priv->format_box);
	} else {
		gtk_widget_hide (priv->format_box);
	}
}

static void
gs_origin_popover_row_set_app (GsOriginPopoverRow *row, GsApp *app)
{
	GsOriginPopoverRowPrivate *priv = gs_origin_popover_row_get_instance_private (row);

	g_assert (priv->app == NULL);

	priv->app = g_object_ref (app);
	refresh_ui (row);
}

GsApp *
gs_origin_popover_row_get_app (GsOriginPopoverRow *row)
{
	GsOriginPopoverRowPrivate *priv = gs_origin_popover_row_get_instance_private (row);
	return priv->app;
}

void
gs_origin_popover_row_set_selected (GsOriginPopoverRow *row, gboolean selected)
{
	GsOriginPopoverRowPrivate *priv = gs_origin_popover_row_get_instance_private (row);

	gtk_widget_set_visible (priv->selected_image, selected);
}

static void
gs_origin_popover_row_destroy (GtkWidget *object)
{
	GsOriginPopoverRow *row = GS_ORIGIN_POPOVER_ROW (object);
	GsOriginPopoverRowPrivate *priv = gs_origin_popover_row_get_instance_private (row);

	g_clear_object (&priv->app);

	GTK_WIDGET_CLASS (gs_origin_popover_row_parent_class)->destroy (object);
}

static void
gs_origin_popover_row_init (GsOriginPopoverRow *row)
{
	gtk_widget_init_template (GTK_WIDGET (row));
}

static void
gs_origin_popover_row_class_init (GsOriginPopoverRowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_origin_popover_row_destroy;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-origin-popover-row.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, name_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, url_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, url_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, format_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, format_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, selected_image);
}

GtkWidget *
gs_origin_popover_row_new (GsApp *app)
{
	GsOriginPopoverRow *row = g_object_new (GS_TYPE_ORIGIN_POPOVER_ROW, NULL);
	gs_origin_popover_row_set_app (row, app);
	return GTK_WIDGET (row);
}

/* vim: set noexpandtab: */
