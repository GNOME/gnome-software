/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-removal-dialog.h"
#include "gs-utils.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

struct _GsRemovalDialog
{
	GtkDialog		 parent_instance;
	GtkLabel		*label;
	GtkWidget		*listbox;
	GtkLabel		*secondary_label;
};

G_DEFINE_TYPE (GsRemovalDialog, gs_removal_dialog, GTK_TYPE_DIALOG)

static gint
list_sort_func (GtkListBoxRow *a,
                GtkListBoxRow *b,
                gpointer user_data)
{
	GObject *o1 = G_OBJECT (gtk_list_box_row_get_child (a));
	GObject *o2 = G_OBJECT (gtk_list_box_row_get_child (b));
	const gchar *key1 = g_object_get_data (o1, "sort");
	const gchar *key2 = g_object_get_data (o2, "sort");
	return g_strcmp0 (key1, key2);
}

static void
add_app (GtkListBox *listbox, GsApp *app)
{
	GtkWidget *box;
	GtkWidget *widget;
	GtkWidget *row;
	g_autofree gchar *sort_key = NULL;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_top (box, 12);
	gtk_widget_set_margin_start (box, 12);
	gtk_widget_set_margin_bottom (box, 12);
	gtk_widget_set_margin_end (box, 12);

	widget = gtk_label_new (gs_app_get_name (app));
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_widget_set_tooltip_text (widget, gs_app_get_name (app));
	gtk_label_set_ellipsize (GTK_LABEL (widget), PANGO_ELLIPSIZE_END);
	gtk_box_append (GTK_BOX (box), widget);

	if (gs_app_get_name (app) != NULL) {
		sort_key = gs_utils_sort_key (gs_app_get_name (app));
	}

	g_object_set_data_full (G_OBJECT (box),
	                        "sort",
	                        g_steal_pointer (&sort_key),
	                        g_free);

	gtk_list_box_prepend (listbox, box);
	gtk_widget_show (widget);
	gtk_widget_show (box);

	row = gtk_widget_get_parent (box);
	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
}

void
gs_removal_dialog_show_upgrade_removals (GsRemovalDialog *self,
                                         GsApp *upgrade)
{
	GsAppList *removals;
	g_autofree gchar *secondary_text = NULL;
	g_autofree gchar *name_version = NULL;

	name_version = g_strdup_printf ("%s %s",
	                                gs_app_get_name (upgrade),
	                                gs_app_get_version (upgrade));
	/* TRANSLATORS: This is a text displayed during a distro upgrade. %s
	   will be replaced by the name and version of distro, e.g. 'Fedora 23'. */
	secondary_text = g_strdup_printf (_("Some of the currently installed software is not compatible with %s. "
					    "If you continue, the following will be automatically removed during the upgrade:"),
					  name_version);

	gtk_widget_add_css_class (GTK_WIDGET (self->label), "title");
	gtk_widget_show (GTK_WIDGET (self->secondary_label));
	gtk_label_set_text (self->secondary_label, secondary_text);

	removals = gs_app_get_related (upgrade);
	for (guint i = 0; i < gs_app_list_length (removals); i++) {
		GsApp *app = gs_app_list_index (removals, i);
		g_autofree gchar *tmp = NULL;

		if (gs_app_get_state (app) != GS_APP_STATE_UNAVAILABLE)
			continue;
		tmp = gs_app_to_string (app);
		g_debug ("removal %u: %s", i, tmp);
		add_app (GTK_LIST_BOX (self->listbox), app);
	}
}

static void
gs_removal_dialog_init (GsRemovalDialog *self)
{
	GtkWidget *action_area;
	GtkSettings *settings;
	gboolean use_caret;

	gtk_widget_init_template (GTK_WIDGET (self));

	action_area = gtk_dialog_get_content_area (GTK_DIALOG (self));
	action_area = gtk_widget_get_next_sibling (action_area);
	gtk_widget_set_halign (action_area, GTK_ALIGN_FILL);
	gtk_box_set_homogeneous (GTK_BOX (action_area), TRUE);

	settings = gtk_widget_get_settings (GTK_WIDGET (self));
	g_object_get (settings, "gtk-keynav-use-caret", &use_caret, NULL);
	gtk_label_set_selectable (self->label, use_caret);
	gtk_label_set_selectable (self->secondary_label, use_caret);

	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->listbox),
	                            list_sort_func,
	                            self, NULL);
}

static void
gs_removal_dialog_class_init (GsRemovalDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-removal-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsRemovalDialog, label);
	gtk_widget_class_bind_template_child (widget_class, GsRemovalDialog, listbox);
	gtk_widget_class_bind_template_child (widget_class, GsRemovalDialog, secondary_label);
}

GtkWidget *
gs_removal_dialog_new (void)
{
	GsRemovalDialog *dialog;

	dialog = g_object_new (GS_TYPE_REMOVAL_DIALOG,
	                       NULL);
	return GTK_WIDGET (dialog);
}
