/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2013 Richard Hughes <richard@hughsie.com>
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

#include "gs-app-addon-row.h"
#include "gs-utils.h"

struct _GsAppAddonRow
{
	GtkListBoxRow	 parent_instance;

	GsApp		*app;
	GtkWidget	*name_label;
	GtkWidget	*description_label;
	GtkWidget	*label;
	GtkWidget	*checkbox;
};

G_DEFINE_TYPE (GsAppAddonRow, gs_app_addon_row, GTK_TYPE_LIST_BOX_ROW)

enum {
	PROP_ZERO,
	PROP_SELECTED
};

/**
 * gs_app_addon_row_get_summary:
 *
 * Return value: PangoMarkup
 **/
static GString *
gs_app_addon_row_get_summary (GsAppAddonRow *row)
{
	const gchar *tmp = NULL;
	g_autofree gchar *escaped = NULL;

	/* try all these things in order */
	if (gs_app_get_kind (row->app) == GS_APP_KIND_MISSING)
		tmp = gs_app_get_summary_missing (row->app);
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_summary (row->app);
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_description (row->app);

	escaped = g_markup_escape_text (tmp, -1);
	return g_string_new (escaped);
}

void
gs_app_addon_row_refresh (GsAppAddonRow *row)
{
	g_autoptr(GString) str = NULL;

	if (row->app == NULL)
		return;

	/* join the lines */
	str = gs_app_addon_row_get_summary (row);
	gs_string_replace (str, "\n", " ");
	gtk_label_set_markup (GTK_LABEL (row->description_label), str->str);
	gtk_label_set_label (GTK_LABEL (row->name_label),
			     gs_app_get_name (row->app));

	/* update the state label */
	switch (gs_app_get_state (row->app)) {
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), _("Pending"));
		break;
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_INSTALLED:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), _("Installed"));
		break;
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), _("Installing"));
		break;
	case AS_APP_STATE_REMOVING:
		gtk_widget_set_visible (row->label, TRUE);
		gtk_label_set_label (GTK_LABEL (row->label), _("Removing"));
		break;
	default:
		gtk_widget_set_visible (row->label, FALSE);
		break;
	}

	/* update the checkbox */
	switch (gs_app_get_state (row->app)) {
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_sensitive (row->checkbox, TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (row->checkbox), TRUE);
		break;
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
		gtk_widget_set_sensitive (row->checkbox, TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (row->checkbox), FALSE);
		break;
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_INSTALLED:
		gtk_widget_set_sensitive (row->checkbox, TRUE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (row->checkbox), TRUE);
		break;
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_sensitive (row->checkbox, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (row->checkbox), TRUE);
		break;
	case AS_APP_STATE_REMOVING:
		gtk_widget_set_sensitive (row->checkbox, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (row->checkbox), FALSE);
		break;
	default:
		gtk_widget_set_sensitive (row->checkbox, FALSE);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (row->checkbox), FALSE);
		break;
	}
}

GsApp *
gs_app_addon_row_get_addon (GsAppAddonRow *row)
{
	g_return_val_if_fail (GS_IS_APP_ADDON_ROW (row), NULL);
	return row->app;
}

static gboolean
gs_app_addon_row_refresh_idle (gpointer user_data)
{
	GsAppAddonRow *row = GS_APP_ADDON_ROW (user_data);

	gs_app_addon_row_refresh (row);

	g_object_unref (row);
	return G_SOURCE_REMOVE;
}

static void
gs_app_addon_row_notify_props_changed_cb (GsApp *app,
					  GParamSpec *pspec,
					  GsAppAddonRow *row)
{
	g_idle_add (gs_app_addon_row_refresh_idle, g_object_ref (row));
}

void
gs_app_addon_row_set_addon (GsAppAddonRow *row, GsApp *app)
{
	g_return_if_fail (GS_IS_APP_ADDON_ROW (row));
	g_return_if_fail (GS_IS_APP (app));

	row->app = g_object_ref (app);

	g_signal_connect_object (row->app, "notify::state",
				 G_CALLBACK (gs_app_addon_row_notify_props_changed_cb),
				 row, 0);
	gs_app_addon_row_refresh (row);
}

static void
gs_app_addon_row_destroy (GtkWidget *object)
{
	GsAppAddonRow *row = GS_APP_ADDON_ROW (object);

	if (row->app)
		g_signal_handlers_disconnect_by_func (row->app, gs_app_addon_row_notify_props_changed_cb, row);

	g_clear_object (&row->app);

	GTK_WIDGET_CLASS (gs_app_addon_row_parent_class)->destroy (object);
}

static void
gs_app_addon_row_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsAppAddonRow *row = GS_APP_ADDON_ROW (object);

	switch (prop_id) {
	case PROP_SELECTED:
		gs_app_addon_row_set_selected (row, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_addon_row_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsAppAddonRow *row = GS_APP_ADDON_ROW (object);

	switch (prop_id) {
	case PROP_SELECTED:
		g_value_set_boolean (value, gs_app_addon_row_get_selected (row));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_addon_row_class_init (GsAppAddonRowClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->set_property = gs_app_addon_row_set_property;
	object_class->get_property = gs_app_addon_row_get_property;

	widget_class->destroy = gs_app_addon_row_destroy;

	pspec = g_param_spec_boolean ("selected", NULL, NULL,
				      FALSE, G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SELECTED, pspec);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-addon-row.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAppAddonRow, name_label);
	gtk_widget_class_bind_template_child (widget_class, GsAppAddonRow, description_label);
	gtk_widget_class_bind_template_child (widget_class, GsAppAddonRow, label);
	gtk_widget_class_bind_template_child (widget_class, GsAppAddonRow, checkbox);
}

static void
checkbox_toggled (GtkWidget *widget, GsAppAddonRow *row)
{
	g_object_notify (G_OBJECT (row), "selected");
}

static void
gs_app_addon_row_init (GsAppAddonRow *row)
{
	gtk_widget_set_has_window (GTK_WIDGET (row), FALSE);
	gtk_widget_init_template (GTK_WIDGET (row));

	g_signal_connect (row->checkbox, "toggled",
			  G_CALLBACK (checkbox_toggled), row);
}

void
gs_app_addon_row_set_selected (GsAppAddonRow *row, gboolean selected)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (row->checkbox), selected);
}

gboolean
gs_app_addon_row_get_selected (GsAppAddonRow *row)
{
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (row->checkbox));
}

GtkWidget *
gs_app_addon_row_new (void)
{
	return g_object_new (GS_TYPE_APP_ADDON_ROW, NULL);
}

/* vim: set noexpandtab: */
