/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Rafał Lużyński <digitalfreak@lingonborough.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-common.h"
#include "gs-info-bar.h"

struct _GsInfoBar
{
	GtkWidget	 parent_instance;

	GtkInfoBar	*info_bar;
	GtkWidget	*label_title;
	GtkWidget	*label_body;
	GtkWidget	*label_warning;
};

G_DEFINE_TYPE (GsInfoBar, gs_info_bar, GTK_TYPE_WIDGET)

enum {
	PROP_0,
	PROP_TITLE,
	PROP_BODY,
	PROP_WARNING,
	PROP_MESSAGE_TYPE,
};

static void
gs_info_bar_get_property (GObject *object,
			  guint prop_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	GsInfoBar *infobar = GS_INFO_BAR (object);

	switch (prop_id) {
	case PROP_TITLE:
		g_value_set_string (value, gs_info_bar_get_title (infobar));
		break;
	case PROP_BODY:
		g_value_set_string (value, gs_info_bar_get_body (infobar));
		break;
	case PROP_WARNING:
		g_value_set_string (value, gs_info_bar_get_warning (infobar));
		break;
	case PROP_MESSAGE_TYPE:
		g_value_set_enum (value, gtk_info_bar_get_message_type (infobar->info_bar));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_info_bar_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec)
{
	GsInfoBar *infobar = GS_INFO_BAR (object);

	switch (prop_id) {
	case PROP_TITLE:
		gs_info_bar_set_title (infobar, g_value_get_string (value));
		break;
	case PROP_BODY:
		gs_info_bar_set_body (infobar, g_value_get_string (value));
		break;
	case PROP_WARNING:
		gs_info_bar_set_warning (infobar, g_value_get_string (value));
		break;
	case PROP_MESSAGE_TYPE:
		gtk_info_bar_set_message_type (infobar->info_bar, g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_info_bar_dispose (GObject *object)
{
	gs_widget_remove_all (GTK_WIDGET (object), NULL);

	G_OBJECT_CLASS (gs_info_bar_parent_class)->dispose (object);
}

static void
gs_info_bar_init (GsInfoBar *infobar)
{
	gtk_widget_init_template (GTK_WIDGET (infobar));
}

static void
gs_info_bar_class_init (GsInfoBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_info_bar_get_property;
	object_class->set_property = gs_info_bar_set_property;
	object_class->dispose = gs_info_bar_dispose;

	g_object_class_install_property (object_class, PROP_TITLE,
		g_param_spec_string ("title",
				     "Title Text",
				     "The title (header) text of the info bar",
				     "",
				     G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_BODY,
		g_param_spec_string ("body",
				     "Body Text",
				     "The body (main) text of the info bar",
				     NULL,
				     G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_WARNING,
		g_param_spec_string ("warning",
				     "Warning Text",
				     "The warning text of the info bar, below the body text",
				     NULL,
				     G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_MESSAGE_TYPE,
		g_param_spec_enum ("message-type",
				   "Message Type",
				   "The type of message",
				   GTK_TYPE_MESSAGE_TYPE,
				   GTK_MESSAGE_INFO,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-info-bar.ui");
	gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);

	gtk_widget_class_bind_template_child (widget_class,
					      GsInfoBar, info_bar);
	gtk_widget_class_bind_template_child (widget_class,
					      GsInfoBar, label_title);
	gtk_widget_class_bind_template_child (widget_class,
					      GsInfoBar, label_body);
	gtk_widget_class_bind_template_child (widget_class,
					      GsInfoBar, label_warning);
}

GtkWidget *
gs_info_bar_new (void)
{
	GsInfoBar *infobar;

	infobar = g_object_new (GS_TYPE_INFO_BAR, NULL);

	return GTK_WIDGET (infobar);
}

static const gchar *
retrieve_from_label (GtkWidget *label)
{
	if (!gtk_widget_get_visible (label))
		return NULL;
	else
		return gtk_label_get_label (GTK_LABEL (label));
}

static void
update_label (GtkWidget *label, const gchar *text)
{
	gtk_label_set_label (GTK_LABEL (label), text);
	gtk_widget_set_visible (label,
				text != NULL && *text != '\0');
}

const gchar *
gs_info_bar_get_title (GsInfoBar *bar)
{
	g_return_val_if_fail (GS_IS_INFO_BAR (bar), NULL);

	return retrieve_from_label (bar->label_title);
}

void
gs_info_bar_set_title (GsInfoBar *bar, const gchar *text)
{
	g_return_if_fail (GS_IS_INFO_BAR (bar));

	update_label (bar->label_title, text);
}

const gchar *
gs_info_bar_get_body (GsInfoBar *bar)
{
	g_return_val_if_fail (GS_IS_INFO_BAR (bar), NULL);

	return retrieve_from_label (bar->label_body);
}

void
gs_info_bar_set_body (GsInfoBar *bar, const gchar *text)
{
	g_return_if_fail (GS_IS_INFO_BAR (bar));

	update_label (bar->label_body, text);
}

const gchar *
gs_info_bar_get_warning (GsInfoBar *bar)
{
	g_return_val_if_fail (GS_IS_INFO_BAR (bar), NULL);

	return retrieve_from_label (bar->label_warning);
}

void
gs_info_bar_set_warning (GsInfoBar *bar, const gchar *text)
{
	g_return_if_fail (GS_IS_INFO_BAR (bar));

	update_label (bar->label_warning, text);
}
