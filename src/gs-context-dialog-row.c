/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-context-dialog-row
 * @short_description: A list box row for context dialogs
 *
 * #GsContextDialogRow is a #GtkListBox row designed to be used in context
 * dialogs such as #GsHardwareSupportContextDialog. Each row indicates how well
 * the app supports a certain feature, attribute or permission. Each row
 * contains an image in a lozenge, a title, a description, and has an
 * ‘importance’ which is primarily indicated through the colour of the image.
 *
 * Since: 41
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-context-dialog-row.h"
#include "gs-enums.h"

struct _GsContextDialogRow
{
	GtkListBoxRow			 parent_instance;

	GsContextDialogRowImportance	 importance;

	GtkWidget			*lozenge;  /* (unowned) */
	GtkImage			*lozenge_content;  /* (unowned) */
	GtkLabel			*title;  /* (unowned) */
	GtkLabel			*description;  /* (unowned) */
};

G_DEFINE_TYPE (GsContextDialogRow, gs_context_dialog_row, GTK_TYPE_LIST_BOX_ROW)

typedef enum {
	PROP_ICON_NAME = 1,
	PROP_IMPORTANCE,
	PROP_TITLE,
	PROP_DESCRIPTION,
} GsContextDialogRowProperty;

static GParamSpec *obj_props[PROP_DESCRIPTION + 1] = { NULL, };

/* These match the CSS classes from gtk-style.css. */
static const gchar *
css_class_for_importance (GsContextDialogRowImportance importance)
{
	switch (importance) {
	case GS_CONTEXT_DIALOG_ROW_IMPORTANCE_NEUTRAL:
		return "grey";
	case GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT:
		return "green";
	case GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING:
		return "yellow";
	case GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT:
		return "red";
	default:
		g_assert_not_reached ();
	}
}

static void
gs_context_dialog_row_init (GsContextDialogRow *self)
{
	gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_context_dialog_row_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	GsContextDialogRow *self = GS_CONTEXT_DIALOG_ROW (object);

	switch ((GsContextDialogRowProperty) prop_id) {
	case PROP_ICON_NAME:
		g_value_set_string (value, gs_context_dialog_row_get_icon_name (self));
		break;
	case PROP_IMPORTANCE:
		g_value_set_enum (value, gs_context_dialog_row_get_importance (self));
		break;
	case PROP_TITLE:
		g_value_set_string (value, gs_context_dialog_row_get_title (self));
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, gs_context_dialog_row_get_description (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_context_dialog_row_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	GsContextDialogRow *self = GS_CONTEXT_DIALOG_ROW (object);

	switch ((GsContextDialogRowProperty) prop_id) {
	case PROP_ICON_NAME:
		gtk_image_set_from_icon_name (self->lozenge_content, g_value_get_string (value), GTK_ICON_SIZE_BUTTON);
		break;
	case PROP_IMPORTANCE: {
		GtkStyleContext *context;
		const gchar *css_class;

		self->importance = g_value_get_enum (value);
		css_class = css_class_for_importance (self->importance);

		context = gtk_widget_get_style_context (self->lozenge);

		gtk_style_context_remove_class (context, "green");
		gtk_style_context_remove_class (context, "yellow");
		gtk_style_context_remove_class (context, "red");
		gtk_style_context_remove_class (context, "grey");

		gtk_style_context_add_class (context, css_class);
		break;
	}
	case PROP_TITLE:
		gtk_label_set_text (self->title, g_value_get_string (value));
		break;
	case PROP_DESCRIPTION:
		gtk_label_set_text (self->description, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_context_dialog_row_class_init (GsContextDialogRowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_context_dialog_row_get_property;
	object_class->set_property = gs_context_dialog_row_set_property;

	/**
	 * GsContextDialogRow:icon-name: (not nullable)
	 *
	 * Name of the icon to display in the row.
	 *
	 * This may not be %NULL.
	 *
	 * Since: 41
	 */
	obj_props[PROP_ICON_NAME] =
		g_param_spec_string ("icon-name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * GsContextDialogRow:importance:
	 *
	 * Importance of the information in the row to the user’s decision
	 * making about an app. This is primarily represented as the row’s
	 * colour.
	 *
	 * Since: 41
	 */
	obj_props[PROP_IMPORTANCE] =
		g_param_spec_enum ("importance", NULL, NULL,
				   GS_TYPE_CONTEXT_DIALOG_ROW_IMPORTANCE, GS_CONTEXT_DIALOG_ROW_IMPORTANCE_NEUTRAL,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * GsContextDialogRow:title: (not nullable)
	 *
	 * The human readable and translated title of the row.
	 *
	 * This may not be %NULL.
	 *
	 * Since: 41
	 */
	obj_props[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * GsContextDialogRow:description: (not nullable)
	 *
	 * The human readable and translated description of the row.
	 *
	 * This may not be %NULL.
	 *
	 * Since: 41
	 */
	obj_props[PROP_DESCRIPTION] =
		g_param_spec_string ("description", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	/* This uses the same CSS name as a standard #GtkListBoxRow in order to
	 * get the default styling from GTK. */
	gtk_widget_class_set_css_name (widget_class, "row");
	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-context-dialog-row.ui");

	gtk_widget_class_bind_template_child (widget_class, GsContextDialogRow, lozenge);
	gtk_widget_class_bind_template_child (widget_class, GsContextDialogRow, lozenge_content);
	gtk_widget_class_bind_template_child (widget_class, GsContextDialogRow, title);
	gtk_widget_class_bind_template_child (widget_class, GsContextDialogRow, description);
}

/**
 * gs_context_dialog_row_new:
 * @icon_name: (not nullable): name of the icon for the row
 * @importance: importance of the information in the row
 * @title: (not nullable): title for the row
 * @description: (not nullable): description for the row
 *
 * Create a new #GsContextDialogRow.
 *
 * Returns: (transfer full): a new #GsContextDialogRow
 * Since: 41
 */
GtkListBoxRow *
gs_context_dialog_row_new (const gchar                  *icon_name,
                           GsContextDialogRowImportance  importance,
                           const gchar                  *title,
                           const gchar                  *description)
{
	g_return_val_if_fail (icon_name != NULL, NULL);
	g_return_val_if_fail (title != NULL, NULL);
	g_return_val_if_fail (description != NULL, NULL);

	return g_object_new (GS_TYPE_CONTEXT_DIALOG_ROW,
			     "icon-name", icon_name,
			     "importance", importance,
			     "title", title,
			     "description", description,
			     NULL);
}

/**
 * gs_context_dialog_row_get_icon_name:
 * @self: a #GsContextDialogRow
 *
 * Get the value of #GsContextDialogRow:icon-name.
 *
 * Returns: the name of the icon used in the row
 * Since: 41
 */
const gchar *
gs_context_dialog_row_get_icon_name (GsContextDialogRow *self)
{
	const gchar *icon_name = NULL;

	g_return_val_if_fail (GS_IS_CONTEXT_DIALOG_ROW (self), NULL);

	gtk_image_get_icon_name (self->lozenge_content, &icon_name, NULL);

	return icon_name;
}

/**
 * gs_context_dialog_row_get_importance:
 * @self: a #GsContextDialogRow
 *
 * Get the value of #GsContextDialogRow:importance.
 *
 * Returns: the importance of the information in the row
 * Since: 41
 */
GsContextDialogRowImportance
gs_context_dialog_row_get_importance (GsContextDialogRow *self)
{
	g_return_val_if_fail (GS_IS_CONTEXT_DIALOG_ROW (self), GS_CONTEXT_DIALOG_ROW_IMPORTANCE_NEUTRAL);

	return self->importance;
}

/**
 * gs_context_dialog_row_get_title:
 * @self: a #GsContextDialogRow
 *
 * Get the value of #GsContextDialogRow:title.
 *
 * Returns: (not nullable): title for the row
 * Since: 41
 */
const gchar *
gs_context_dialog_row_get_title (GsContextDialogRow *self)
{
	g_return_val_if_fail (GS_IS_CONTEXT_DIALOG_ROW (self), NULL);

	return gtk_label_get_text (self->title);
}

/**
 * gs_context_dialog_row_get_description:
 * @self: a #GsContextDialogRow
 *
 * Get the value of #GsContextDialogRow:description.
 *
 * Returns: (not nullable): description for the row
 * Since: 41
 */
const gchar *
gs_context_dialog_row_get_description (GsContextDialogRow *self)
{
	g_return_val_if_fail (GS_IS_CONTEXT_DIALOG_ROW (self), NULL);

	return gtk_label_get_text (self->description);
}
