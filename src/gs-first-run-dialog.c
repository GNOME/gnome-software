/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2014-2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <gtk/gtk.h>

#include "gs-first-run-dialog.h"

struct _GsFirstRunDialog
{
	AdwWindow	 parent_instance;

	GtkWidget	*button;
};

G_DEFINE_TYPE (GsFirstRunDialog, gs_first_run_dialog, ADW_TYPE_WINDOW)

static gboolean
key_press_event_cb (GtkWidget            *sender,
                    GdkEvent             *event,
                    AdwPreferencesWindow *self)
{
	guint keyval;
	GdkModifierType state;
	GdkKeymap *keymap;
	GdkEventKey *key_event = (GdkEventKey *) event;

	gdk_event_get_state (event, &state);

	keymap = gdk_keymap_get_for_display (gtk_widget_get_display (sender));

	gdk_keymap_translate_keyboard_state (keymap,
					     key_event->hardware_keycode,
					     state,
					     key_event->group,
					     &keyval, NULL, NULL, NULL);

	if (keyval == GDK_KEY_Escape) {
		gtk_window_close (GTK_WINDOW (self));

		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static void
button_clicked_cb (GtkWidget *widget, GsFirstRunDialog *dialog)
{
	gtk_window_close (GTK_WINDOW (dialog));
}

static void
gs_first_run_dialog_init (GsFirstRunDialog *dialog)
{
	GtkWidget *button_label;

	gtk_widget_init_template (GTK_WIDGET (dialog));

	button_label = gtk_button_get_child (GTK_BUTTON (dialog->button));
	gtk_widget_set_margin_start (button_label, 16);
	gtk_widget_set_margin_end (button_label, 16);

	g_signal_connect (dialog->button, "clicked", G_CALLBACK (button_clicked_cb), dialog);
}

static void
gs_first_run_dialog_class_init (GsFirstRunDialogClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-first-run-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsFirstRunDialog, button);

	gtk_widget_class_bind_template_callback (widget_class, key_press_event_cb);
}

GtkWidget *
gs_first_run_dialog_new (void)
{
	return GTK_WIDGET (g_object_new (GS_TYPE_FIRST_RUN_DIALOG, NULL));
}
