/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-app-details-page.h"
#include "gs-app-update-details-dialog.h"
#include "gs-os-update-page.h"
#include "gs-common.h"

struct _GsAppUpdateDetailsDialog
{
	AdwDialog	 parent_instance;

	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
	GsApp		*app;
	GtkWidget	*navigation_view;
};

G_DEFINE_TYPE (GsAppUpdateDetailsDialog, gs_app_update_details_dialog, ADW_TYPE_DIALOG)

typedef enum {
	PROP_PLUGIN_LOADER = 1,
	PROP_APP,
} GsAppUpdateDetailsDialogProperty;

static GParamSpec *obj_props[PROP_APP + 1]  = { NULL, };

static void gs_app_update_details_dialog_show_update_details (GsAppUpdateDetailsDialog *dialog, GsApp *app);

static void
unset_focus (GtkWidget *widget)
{
	GtkWidget *focus;

	focus = adw_dialog_get_focus (ADW_DIALOG (widget));
	if (GTK_IS_LABEL (focus))
		gtk_label_select_region (GTK_LABEL (focus), 0, 0);
}

static void
app_activated_cb (GtkWidget *widget, GsApp *app, GsAppUpdateDetailsDialog *page)
{
	gs_app_update_details_dialog_show_update_details (page, app);
}

static void
gs_app_update_details_dialog_show_update_details (GsAppUpdateDetailsDialog *dialog, GsApp *app)
{
	GtkWidget *page;
	AsComponentKind kind;
	g_autofree gchar *str = NULL;

	/* debug */
	str = gs_app_to_string (app);
	g_debug ("%s", str);

	/* workaround a gtk+ issue where the dialog comes up with a label selected,
	 * https://bugzilla.gnome.org/show_bug.cgi?id=734033 */
	unset_focus (GTK_WIDGET (dialog));

	/* set update description */
	kind = gs_app_get_kind (app);
	if (kind == AS_COMPONENT_KIND_GENERIC &&
	    gs_app_get_special_kind (app) == GS_APP_SPECIAL_KIND_OS_UPDATE) {
		page = gs_os_update_page_new ();
		gs_os_update_page_set_app (GS_OS_UPDATE_PAGE (page), app);
		g_signal_connect (page, "app-activated",
				  G_CALLBACK (app_activated_cb), dialog);
	} else {
		page = gs_app_details_page_new (dialog->plugin_loader);
		gs_app_details_page_set_app (GS_APP_DETAILS_PAGE (page), app);
	}

	adw_navigation_view_push (ADW_NAVIGATION_VIEW (dialog->navigation_view), ADW_NAVIGATION_PAGE (page));
}

static void
gs_app_update_details_dialog_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
	GsAppUpdateDetailsDialog *dialog = GS_APP_UPDATE_DETAILS_DIALOG (object);

	switch ((GsAppUpdateDetailsDialogProperty) prop_id) {
	case PROP_PLUGIN_LOADER:
		g_value_set_object (value, dialog->plugin_loader);
		break;
	case PROP_APP:
		g_value_set_object (value, dialog->app);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_update_details_dialog_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
	GsAppUpdateDetailsDialog *dialog = GS_APP_UPDATE_DETAILS_DIALOG (object);

	switch ((GsAppUpdateDetailsDialogProperty) prop_id) {
	case PROP_PLUGIN_LOADER:
		dialog->plugin_loader = g_object_ref (g_value_get_object (value));
		break;
	case PROP_APP:
		dialog->app = g_value_get_object (value);
		if (dialog->app != NULL)
			g_object_ref (dialog->app);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_update_details_dialog_constructed (GObject *object)
{
	GsAppUpdateDetailsDialog *dialog = GS_APP_UPDATE_DETAILS_DIALOG (object);

	g_assert (dialog->plugin_loader != NULL);
	g_assert (dialog->app != NULL);

	gs_app_update_details_dialog_show_update_details (dialog, dialog->app);

	G_OBJECT_CLASS (gs_app_update_details_dialog_parent_class)->constructed (object);
}

static void
gs_app_update_details_dialog_dispose (GObject *object)
{
	GsAppUpdateDetailsDialog *dialog = GS_APP_UPDATE_DETAILS_DIALOG (object);

	g_cancellable_cancel (dialog->cancellable);
	g_clear_object (&dialog->cancellable);

	g_clear_object (&dialog->plugin_loader);
	g_clear_object (&dialog->app);

	G_OBJECT_CLASS (gs_app_update_details_dialog_parent_class)->dispose (object);
}

static void
gs_app_update_details_dialog_init (GsAppUpdateDetailsDialog *dialog)
{
	gtk_widget_init_template (GTK_WIDGET (dialog));

	dialog->cancellable = g_cancellable_new ();

	g_signal_connect_after (dialog, "show", G_CALLBACK (unset_focus), NULL);
}

static void
gs_app_update_details_dialog_class_init (GsAppUpdateDetailsDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_app_update_details_dialog_get_property;
	object_class->set_property = gs_app_update_details_dialog_set_property;
	object_class->constructed = gs_app_update_details_dialog_constructed;
	object_class->dispose = gs_app_update_details_dialog_dispose;

	/**
	 * GsAppUpdateDetailsDialog:plugin-loader
	 *
	 * The plugin loader of the dialog.
	 *
	 * Since: 50
	 */
	obj_props[PROP_PLUGIN_LOADER] =
		g_param_spec_object ("plugin-loader", NULL, NULL,
				     GS_TYPE_PLUGIN_LOADER,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * GsAppUpdateDetailsDialog:app: (not nullable)
	 *
	 * The app whose details to display.
	 *
	 * Since: 50
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-update-details-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAppUpdateDetailsDialog, navigation_view);
}

GtkWidget *
gs_app_update_details_dialog_new (GsPluginLoader *plugin_loader, GsApp *app)
{
	return GTK_WIDGET (g_object_new (GS_TYPE_APP_UPDATE_DETAILS_DIALOG,
					 "plugin-loader", plugin_loader,
					 "app", app,
					 NULL));
}
