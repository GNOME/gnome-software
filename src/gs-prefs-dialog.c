/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
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
#include <glib/gi18n.h>

struct _GsPrefsDialog
{
	AdwPreferencesWindow	 parent_instance;
	GSettings		*settings;

	GCancellable		*cancellable;
	GsPluginLoader		*plugin_loader;
	GtkWidget		*switch_updates;
	GtkWidget		*switch_updates_notify;
	AdwActionRow		*automatic_updates_row;
	AdwActionRow		*automatic_update_notifications_row;
};

G_DEFINE_TYPE (GsPrefsDialog, gs_prefs_dialog, ADW_TYPE_PREFERENCES_WINDOW)

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
	g_settings_bind (dialog->settings,
			 "download-updates",
			 dialog->switch_updates,
			 "active",
			 G_SETTINGS_BIND_DEFAULT);

#if ADW_CHECK_VERSION(1,2,0)
	adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (dialog->automatic_updates_row), FALSE);
	adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (dialog->automatic_update_notifications_row), FALSE);
#endif
}

static void
gs_prefs_dialog_class_init (GsPrefsDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_prefs_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-prefs-dialog.ui");
	gtk_widget_class_bind_template_child (widget_class, GsPrefsDialog, switch_updates);
	gtk_widget_class_bind_template_child (widget_class, GsPrefsDialog, switch_updates_notify);
	gtk_widget_class_bind_template_child (widget_class, GsPrefsDialog, automatic_updates_row);
	gtk_widget_class_bind_template_child (widget_class, GsPrefsDialog, automatic_update_notifications_row);
}

GtkWidget *
gs_prefs_dialog_new (GtkWindow *parent, GsPluginLoader *plugin_loader)
{
	GsPrefsDialog *dialog;
	dialog = g_object_new (GS_TYPE_PREFS_DIALOG,
			       "transient-for", parent,
			       NULL);
	dialog->plugin_loader = g_object_ref (plugin_loader);
	return GTK_WIDGET (dialog);
}
