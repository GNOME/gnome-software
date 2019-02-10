/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-progress-button.h"

struct _GsProgressButton
{
	GtkButton	 parent_instance;

	GtkCssProvider	*css_provider;
};

G_DEFINE_TYPE (GsProgressButton, gs_progress_button, GTK_TYPE_BUTTON)

void
gs_progress_button_set_progress (GsProgressButton *button, guint percentage)
{
	g_autofree gchar *css = NULL;

	if (percentage == 0)
		css = g_strdup (".install-progress { background-size: 0; }");
	else if (percentage == 100)
		css = g_strdup (".install-progress { background-size: 100%; }");
	else
		css = g_strdup_printf (".install-progress { background-size: %u%%; }", percentage);

	gtk_css_provider_load_from_data (button->css_provider, css, -1, NULL);
}

void
gs_progress_button_set_show_progress (GsProgressButton *button, gboolean show_progress)
{
	GtkStyleContext *context;

	context = gtk_widget_get_style_context (GTK_WIDGET (button));
	if (show_progress)
		gtk_style_context_add_class (context, "install-progress");
	else
		gtk_style_context_remove_class (context, "install-progress");
}

static void
gs_progress_button_dispose (GObject *object)
{
	GsProgressButton *button = GS_PROGRESS_BUTTON (object);

	g_clear_object (&button->css_provider);

	G_OBJECT_CLASS (gs_progress_button_parent_class)->dispose (object);
}

static void
gs_progress_button_init (GsProgressButton *button)
{
	button->css_provider = gtk_css_provider_new ();
	gtk_style_context_add_provider (gtk_widget_get_style_context (GTK_WIDGET (button)),
					GTK_STYLE_PROVIDER (button->css_provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
gs_progress_button_class_init (GsProgressButtonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_progress_button_dispose;
}

GtkWidget *
gs_progress_button_new (void)
{
	return g_object_new (GS_TYPE_PROGRESS_BUTTON, NULL);
}
