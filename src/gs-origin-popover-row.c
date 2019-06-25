/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-origin-popover-row.h"

#include <glib/gi18n.h>

typedef struct
{
	GsApp		*app;
	GtkWidget	*name_label;
	GtkWidget	*url_box;
	GtkWidget	*url_title;
	GtkWidget	*url_label;
	GtkWidget	*format_box;
	GtkWidget	*format_title;
	GtkWidget	*format_label;
	GtkWidget	*installation_box;
	GtkWidget	*installation_title;
	GtkWidget	*installation_label;
	GtkWidget	*branch_box;
	GtkWidget	*branch_title;
	GtkWidget	*branch_label;
	GtkWidget	*selected_image;
} GsOriginPopoverRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsOriginPopoverRow, gs_origin_popover_row, GTK_TYPE_LIST_BOX_ROW)

static void
refresh_ui (GsOriginPopoverRow *row)
{
	GsOriginPopoverRowPrivate *priv = gs_origin_popover_row_get_instance_private (row);
	g_autofree gchar *origin_ui = NULL;
	g_autofree gchar *packaging_format = NULL;
	g_autofree gchar *url = NULL;

	g_assert (GS_IS_ORIGIN_POPOVER_ROW (row));
	g_assert (GS_IS_APP (priv->app));

	origin_ui = gs_app_get_origin_ui (priv->app);
	if (origin_ui != NULL) {
		gtk_label_set_text (GTK_LABEL (priv->name_label), origin_ui);
	}

	if (gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL) {
		GFile *local_file = gs_app_get_local_file (priv->app);
		url = g_file_get_basename (local_file);
		/* TRANSLATORS: This is followed by a file name, e.g. "Name: gedit.rpm" */
		gtk_label_set_text (GTK_LABEL (priv->url_title), _("Name"));
	} else {
		url = g_strdup (gs_app_get_origin_hostname (priv->app));
	}

	if (url != NULL) {
		gtk_label_set_text (GTK_LABEL (priv->url_label), url);
		gtk_widget_show (priv->url_box);
	} else {
		gtk_widget_hide (priv->url_box);
	}

	packaging_format = gs_app_get_packaging_format (priv->app);
	if (packaging_format != NULL) {
		gtk_label_set_text (GTK_LABEL (priv->format_label), packaging_format);
		gtk_widget_show (priv->format_box);
	} else {
		gtk_widget_hide (priv->format_box);
	}

	if (gs_app_get_bundle_kind (priv->app) == AS_BUNDLE_KIND_FLATPAK &&
	    gs_app_get_scope (priv->app) != AS_APP_SCOPE_UNKNOWN) {
		AsAppScope scope = gs_app_get_scope (priv->app);
		if (scope == AS_APP_SCOPE_SYSTEM) {
			/* TRANSLATORS: the installation location for flatpaks */
			gtk_label_set_text (GTK_LABEL (priv->installation_label), _("system"));
		} else if (scope == AS_APP_SCOPE_USER) {
			/* TRANSLATORS: the installation location for flatpaks */
			gtk_label_set_text (GTK_LABEL (priv->installation_label), _("user"));
		}
		gtk_widget_show (priv->installation_box);
	} else {
		gtk_widget_hide (priv->installation_box);
	}

	if (gs_app_get_branch (priv->app) != NULL) {
		gtk_label_set_text (GTK_LABEL (priv->branch_label), gs_app_get_branch (priv->app));
		gtk_widget_show (priv->branch_box);
	} else {
		gtk_widget_hide (priv->branch_box);
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

void
gs_origin_popover_row_set_size_group (GsOriginPopoverRow *row, GtkSizeGroup *size_group)
{
	GsOriginPopoverRowPrivate *priv = gs_origin_popover_row_get_instance_private (row);

	gtk_size_group_add_widget (size_group, priv->url_title);
	gtk_size_group_add_widget (size_group, priv->format_title);
	gtk_size_group_add_widget (size_group, priv->installation_title);
	gtk_size_group_add_widget (size_group, priv->branch_title);
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
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, url_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, url_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, format_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, format_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, format_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, installation_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, installation_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, installation_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, branch_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, branch_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, branch_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsOriginPopoverRow, selected_image);
}

GtkWidget *
gs_origin_popover_row_new (GsApp *app)
{
	GsOriginPopoverRow *row = g_object_new (GS_TYPE_ORIGIN_POPOVER_ROW, NULL);
	gs_origin_popover_row_set_app (row, app);
	return GTK_WIDGET (row);
}
