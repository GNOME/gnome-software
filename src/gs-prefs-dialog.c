/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-prefs-dialog.h"

#include "gnome-software-private.h"
#include "gs-common.h"
#include "gs-os-release.h"
#include "gs-repo-row.h"
#include "gs-third-party-repo-row.h"
#include <glib/gi18n.h>

struct _GsPrefsDialog
{
	GtkDialog	 parent_instance;
	GSettings	*settings;

	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
	GtkWidget	*switch_updates_notify;
};

G_DEFINE_TYPE (GsPrefsDialog, gs_prefs_dialog, GTK_TYPE_DIALOG)

static void
gs_prefs_dialog_dispose (GObject *object)
{
	GsPrefsDialog *dialog = GS_PREFS_DIALOG (object);
	g_clear_object (&dialog->plugin_loader);
	g_cancellable_cancel (dialog->cancellable);
	g_clear_object (&dialog->cancellable);
	g_clear_object (&dialog->settings);

	G_OBJECT_CLASS (gs_prefs_dialog_parent_class)->dispose (object);
}

static void
gs_prefs_dialog_init (GsPrefsDialog *dialog)
{
	gtk_widget_init_template (GTK_WIDGET (dialog));

	dialog->cancellable = g_cancellable_new ();
	dialog->settings = g_settings_new ("org.gnome.software");
	g_settings_bind (dialog->settings,
			 "download-updates-notify",
			 dialog->switch_updates_notify,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);
}

static void
gs_prefs_dialog_class_init (GsPrefsDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_prefs_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-prefs-dialog.ui");
	gtk_widget_class_bind_template_child (widget_class, GsPrefsDialog, switch_updates_notify);
}

GtkWidget *
gs_prefs_dialog_new (GtkWindow *parent, GsPluginLoader *plugin_loader)
{
	GsPrefsDialog *dialog;
	dialog = g_object_new (GS_TYPE_PREFS_DIALOG,
			       "use-header-bar", TRUE,
			       "transient-for", parent,
			       "modal", TRUE,
			       NULL);
	dialog->plugin_loader = g_object_ref (plugin_loader);
	return GTK_WIDGET (dialog);
}
