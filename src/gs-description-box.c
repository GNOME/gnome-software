/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2020 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-desription-box
 * @title: GsDescriptionBox
 * @stability: Stable
 * @short_description: Show description text in a way that can show more/less lines
 *
 * Show a description in an expandable form with "Show More" button when
 * there are too many lines to be shown. The button is hidden when
 * the description is short enough. The button changes to "Show Less"
 * to be able to collapse it.
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-description-box.h"

#define MAX_COLLAPSED_LINES 4

struct _GsDescriptionBox {
	GtkBox parent;
	GtkLabel *label;
	GtkButton *button;
	gchar *text;
	gboolean is_collapsed;
	gboolean needs_recalc;
	gint last_width;
	gint last_height;
};

G_DEFINE_TYPE (GsDescriptionBox, gs_description_box, GTK_TYPE_BOX)

static void
gs_description_box_update_content (GsDescriptionBox *box)
{
	GtkAllocation allocation;
	PangoLayout *layout;
	gint n_lines;

	if (!box->text || !*(box->text)) {
		gtk_widget_hide (GTK_WIDGET (box));
		box->needs_recalc = TRUE;
		return;
	}

	gtk_widget_get_allocation (GTK_WIDGET (box), &allocation);

	if (!box->needs_recalc && box->last_width == allocation.width && box->last_height == allocation.height)
		return;

	box->needs_recalc = allocation.width <= 1 || allocation.height <= 1;
	box->last_width = allocation.width;
	box->last_height = allocation.height;

	gtk_button_set_label (box->button, box->is_collapsed ? _("_Show More") : _("_Show Less"));

	gtk_label_set_text (box->label, box->text);
	gtk_label_set_lines (box->label, -1);
	gtk_label_set_ellipsize (box->label, PANGO_ELLIPSIZE_NONE);

	layout = gtk_label_get_layout (box->label);
	n_lines = pango_layout_get_line_count (layout);

	gtk_widget_set_visible (GTK_WIDGET (box->button), n_lines > MAX_COLLAPSED_LINES);

	if (box->is_collapsed && n_lines > MAX_COLLAPSED_LINES) {
		PangoLayoutLine *line;
		GString *str;

		line = pango_layout_get_line_readonly (layout, MAX_COLLAPSED_LINES);

		str = g_string_sized_new (line->start_index);
		g_string_append_len (str, box->text, line->start_index);

		/* Cut white spaces from the end of the string, thus it doesn't look bad when it's ellipsized. */
		while (str->len > 0 && strchr ("\r\n\t ", str->str[str->len - 1])) {
			str->len--;
		}

		str->str[str->len] = '\0';

		gtk_label_set_lines (box->label, MAX_COLLAPSED_LINES);
		gtk_label_set_ellipsize (box->label, PANGO_ELLIPSIZE_END);
		gtk_label_set_text (box->label, str->str);

		g_string_free (str, TRUE);
	}
}

static void
gs_description_box_read_button_clicked_cb (GtkButton *button,
					   gpointer user_data)
{
	GsDescriptionBox *box = user_data;

	g_return_if_fail (GS_IS_DESCRIPTION_BOX (box));

	box->is_collapsed = !box->is_collapsed;
	box->needs_recalc = TRUE;

	gs_description_box_update_content (box);
}

static void
gs_description_box_size_allocate (GtkWidget *widget,
				  GtkAllocation *allocation)
{
	GsDescriptionBox *box = GS_DESCRIPTION_BOX (widget);

	GTK_WIDGET_CLASS (gs_description_box_parent_class)->size_allocate (widget, allocation);

	gs_description_box_update_content (box);
}

static void
gs_description_box_finalize (GObject *object)
{
	GsDescriptionBox *box = GS_DESCRIPTION_BOX (object);

	g_clear_pointer (&box->text, g_free);

	G_OBJECT_CLASS (gs_description_box_parent_class)->finalize (object);
}

static void
gs_description_box_init (GsDescriptionBox *box)
{
	GtkStyleContext *style_context;
	GtkWidget *widget;

	box->is_collapsed = TRUE;

	style_context = gtk_widget_get_style_context (GTK_WIDGET (box));
	gtk_style_context_add_class (style_context, "application-details-description");

	widget = gtk_label_new ("");
	g_object_set (G_OBJECT (widget),
		"hexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"vexpand", FALSE,
		"valign", GTK_ALIGN_START,
		"visible", TRUE,
		"can-focus", FALSE,
		"max-width-chars", 40,
		"width-chars", 40,
		"selectable", TRUE,
		"wrap", TRUE,
		"xalign", 0.0,
		NULL);

	gtk_box_pack_start (GTK_BOX (box), widget, TRUE, TRUE, 0);

	style_context = gtk_widget_get_style_context (widget);
	gtk_style_context_add_class (style_context, "label");

	box->label = GTK_LABEL (widget);

	widget = gtk_button_new_with_mnemonic (_("_Show More"));

	g_object_set (G_OBJECT (widget),
		"hexpand", FALSE,
		"halign", GTK_ALIGN_CENTER,
		"vexpand", FALSE,
		"valign", GTK_ALIGN_CENTER,
		"visible", TRUE,
		NULL);

	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

	style_context = gtk_widget_get_style_context (widget);
	gtk_style_context_add_class (style_context, "button");
	gtk_style_context_add_class (style_context, "circular");

	box->button = GTK_BUTTON (widget);

	g_signal_connect (box->button, "clicked",
		G_CALLBACK (gs_description_box_read_button_clicked_cb), box);
}

static void
gs_description_box_class_init (GsDescriptionBoxClass *klass)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_description_box_finalize;

	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->size_allocate = gs_description_box_size_allocate;
}

GtkWidget *
gs_description_box_new (void)
{
	return g_object_new (GS_TYPE_DESCRIPTION_BOX,
		"orientation", GTK_ORIENTATION_VERTICAL,
		"spacing", 24,
		NULL);
}

const gchar *
gs_description_box_get_text (GsDescriptionBox *box)
{
	g_return_val_if_fail (GS_IS_DESCRIPTION_BOX (box), NULL);

	return box->text;
}

void
gs_description_box_set_text (GsDescriptionBox *box,
			     const gchar *text)
{
	g_return_if_fail (GS_IS_DESCRIPTION_BOX (box));

	if (g_strcmp0 (text, box->text) != 0) {
		g_free (box->text);
		box->text = g_strdup (text);
		box->needs_recalc = TRUE;

		gtk_widget_set_visible (GTK_WIDGET (box), text && *text);

		gtk_widget_queue_resize (GTK_WIDGET (box));
	}
}

gboolean
gs_description_box_get_collapsed (GsDescriptionBox *box)
{
	g_return_val_if_fail (GS_IS_DESCRIPTION_BOX (box), FALSE);

	return box->is_collapsed;
}

void
gs_description_box_set_collapsed (GsDescriptionBox *box,
				  gboolean collapsed)
{
	g_return_if_fail (GS_IS_DESCRIPTION_BOX (box));

	if ((collapsed ? 1 : 0) != (box->is_collapsed ? 1 : 0)) {
		box->is_collapsed = collapsed;
		box->needs_recalc = TRUE;

		gtk_widget_queue_resize (GTK_WIDGET (box));
	}
}
