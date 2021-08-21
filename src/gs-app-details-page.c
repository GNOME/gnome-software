/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-app-details-page
 * @title: GsAppDetailsPage
 * @include: gnome-software.h
 * @stability: Stable
 * @short_description: A small page showing an application's details
 *
 * This is a page from #GsUpdateDialog.
 */

#include "config.h"

#include <adwaita.h>
#include <glib/gi18n.h>

#include "gs-app-details-page.h"
#include "gs-app-row.h"
#include "gs-update-list.h"
#include "gs-common.h"

typedef enum {
	PROP_APP = 1,
	PROP_SHOW_BACK_BUTTON,
} GsAppDetailsPageProperty;

enum {
	SIGNAL_BACK_CLICKED,
	SIGNAL_LAST
};

static GParamSpec *obj_props[PROP_SHOW_BACK_BUTTON + 1] = { NULL, };

static guint signals[SIGNAL_LAST] = { 0 };

struct _GsAppDetailsPage
{
	GtkBox		 parent_instance;

	GtkWidget	*back_button;
	GtkWidget	*box_header;
	GtkWidget	*header_bar;
	GtkWidget	*image_icon;
	GtkWidget	*label_details;
	GtkWidget	*label_name;
	GtkWidget	*label_summary;
	GtkWidget	*permissions_section_box;
	GtkWidget	*permissions_section_content;
	GtkWidget	*scrolledwindow_details;

	GsApp		*app;  /* (owned) (nullable) */
};

G_DEFINE_TYPE (GsAppDetailsPage, gs_app_details_page, GTK_TYPE_BOX)

static const struct {
        GsAppPermissions permission;
        const char *title;
        const char *subtitle;
} permission_display_data[] = {
  { GS_APP_PERMISSIONS_NETWORK, N_("Network"), N_("Can communicate over the network") },
  { GS_APP_PERMISSIONS_SYSTEM_BUS, N_("System Services"), N_("Can access D-Bus services on the system bus") },
  { GS_APP_PERMISSIONS_SESSION_BUS, N_("Session Services"), N_("Can access D-Bus services on the session bus") },
  { GS_APP_PERMISSIONS_DEVICES, N_("Devices"), N_("Can access system device files") },
  { GS_APP_PERMISSIONS_HOME_FULL, N_("Home folder"), N_("Can view, edit and create files") },
  { GS_APP_PERMISSIONS_HOME_READ, N_("Home folder"), N_("Can view files") },
  { GS_APP_PERMISSIONS_FILESYSTEM_FULL, N_("File system"), N_("Can view, edit and create files") },
  { GS_APP_PERMISSIONS_FILESYSTEM_READ, N_("File system"), N_("Can view files") },
  { GS_APP_PERMISSIONS_DOWNLOADS_FULL, N_("Downloads folder"), N_("Can view, edit and create files") },
  { GS_APP_PERMISSIONS_DOWNLOADS_READ, N_("Downloads folder"), N_("Can view files") },
  { GS_APP_PERMISSIONS_SETTINGS, N_("Settings"), N_("Can view and change any settings") },
  { GS_APP_PERMISSIONS_X11, N_("Legacy display system"), N_("Uses an old, insecure display system") },
  { GS_APP_PERMISSIONS_ESCAPE_SANDBOX, N_("Sandbox escape"), N_("Can escape the sandbox and circumvent any other restrictions") },
};

static void
destroy_cb (GtkWidget *widget, gpointer data)
{
	gtk_widget_destroy (widget);
}

static void
populate_permissions_section (GsAppDetailsPage *page, GsAppPermissions permissions)
{
	gtk_container_foreach (GTK_CONTAINER (page->permissions_section_content), destroy_cb, NULL);

	for (gsize i = 0; i < G_N_ELEMENTS (permission_display_data); i++) {
		GtkWidget *row, *image, *box, *label;

		if ((permissions & permission_display_data[i].permission) == 0)
			continue;

		row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_widget_show (row);
		if ((permission_display_data[i].permission & ~MEDIUM_PERMISSIONS) != 0) {
			gtk_style_context_add_class (gtk_widget_get_style_context (row), "permission-row-warning");
		}

		image = gtk_image_new_from_icon_name ("dialog-warning-symbolic", GTK_ICON_SIZE_MENU);
		if ((permission_display_data[i].permission & ~MEDIUM_PERMISSIONS) == 0)
			gtk_widget_set_opacity (image, 0);

		gtk_widget_show (image);
		gtk_container_add (GTK_CONTAINER (row), image);

		box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_show (box);
		gtk_container_add (GTK_CONTAINER (row), box);

		label = gtk_label_new (_(permission_display_data[i].title));
		gtk_label_set_xalign (GTK_LABEL (label), 0);
		gtk_widget_show (label);
		gtk_container_add (GTK_CONTAINER (box), label);

		label = gtk_label_new (_(permission_display_data[i].subtitle));
		gtk_label_set_xalign (GTK_LABEL (label), 0);
		gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
		gtk_widget_show (label);
		gtk_container_add (GTK_CONTAINER (box), label);

		gtk_container_add (GTK_CONTAINER (page->permissions_section_content), row);
	}
}

static void
set_updates_description_ui (GsAppDetailsPage *page, GsApp *app)
{
	AsComponentKind kind;
	g_autoptr(GIcon) icon = NULL;
	guint icon_size;
	const gchar *update_details;

	/* FIXME support app == NULL */

	/* set window title */
	kind = gs_app_get_kind (app);
	if (kind == AS_COMPONENT_KIND_GENERIC &&
	    gs_app_get_special_kind (app) == GS_APP_SPECIAL_KIND_OS_UPDATE) {
		adw_header_bar_set_title (ADW_HEADER_BAR (page->header_bar),
					  gs_app_get_name (app));
	} else if (gs_app_get_source_default (app) != NULL &&
		   gs_app_get_update_version (app) != NULL) {
		g_autofree gchar *tmp = NULL;
		/* Translators: This is the source and upgrade version of an
		 * application, shown to the user when they view more detailed
		 * information about pending updates. The source is of the form
		 * ‘deja-dup’ (a package name) or
		 * ‘app/org.gnome.Builder/x86_64/main’ (a flatpak ID), and the
		 * version is of the form ‘40.4-1.fc34’ (a version number). */
		tmp = g_strdup_printf (_("%s %s"),
				       gs_app_get_source_default (app),
				       gs_app_get_update_version (app));
		adw_header_bar_set_title (ADW_HEADER_BAR (page->header_bar), tmp);
	} else if (gs_app_get_source_default (app) != NULL) {
		adw_header_bar_set_title (ADW_HEADER_BAR (page->header_bar),
					  gs_app_get_source_default (app));
	} else {
		adw_header_bar_set_title (ADW_HEADER_BAR (page->header_bar),
					  gs_app_get_update_version (app));
	}

	/* set update header */
	gtk_widget_set_visible (page->box_header, kind == AS_COMPONENT_KIND_DESKTOP_APP);
	update_details = gs_app_get_update_details (app);
	if (update_details == NULL) {
		/* TRANSLATORS: this is where the packager did not write
		 * a description for the update */
		update_details = _("No update description available.");
	}
	gtk_label_set_label (GTK_LABEL (page->label_details), update_details);
	gtk_label_set_label (GTK_LABEL (page->label_name), gs_app_get_name (app));
	gtk_label_set_label (GTK_LABEL (page->label_summary), gs_app_get_summary (app));

	/* set the icon; fall back to 64px if 96px isn’t available, which sometimes
	 * happens at 2× scale factor (hi-DPI) */
	icon_size = 96;
	icon = gs_app_get_icon_for_size (app,
					 icon_size,
					 gtk_widget_get_scale_factor (page->image_icon),
					 NULL);
	if (icon == NULL) {
		icon_size = 64;
		icon = gs_app_get_icon_for_size (app,
						 icon_size,
						 gtk_widget_get_scale_factor (page->image_icon),
						 NULL);
	}
	if (icon == NULL) {
		icon_size = 96;
		icon = gs_app_get_icon_for_size (app,
						 icon_size,
						 gtk_widget_get_scale_factor (page->image_icon),
						 "system-component-application");
	}

	gtk_image_set_pixel_size (GTK_IMAGE (page->image_icon), icon_size);
	gtk_image_set_from_gicon (GTK_IMAGE (page->image_icon), icon,
				  GTK_ICON_SIZE_INVALID);

	if (gs_app_has_quirk (app, GS_APP_QUIRK_NEW_PERMISSIONS)) {
		gtk_widget_show (page->permissions_section_box);
		populate_permissions_section (page, gs_app_get_update_permissions (app));
	} else {
		gtk_widget_hide (page->permissions_section_box);
	}
}

/**
 * gs_app_details_page_get_app:
 * @page: a #GsAppDetailsPage
 *
 * Get the value of #GsAppDetailsPage:app.
 *
 * Returns: (nullable) (transfer none): the app
 *
 * Since: 41
 */
GsApp *
gs_app_details_page_get_app (GsAppDetailsPage *page)
{
	g_return_val_if_fail (GS_IS_APP_DETAILS_PAGE (page), NULL);
	return page->app;
}

/**
 * gs_app_details_page_set_app:
 * @page: a #GsAppDetailsPage
 * @app: (transfer none) (nullable): new app
 *
 * Set the value of #GsAppDetailsPage:app.
 *
 * Since: 41
 */
void
gs_app_details_page_set_app (GsAppDetailsPage *page, GsApp *app)
{
	g_return_if_fail (GS_IS_APP_DETAILS_PAGE (page));
	g_return_if_fail (!app || GS_IS_APP (app));

	if (page->app == app)
		return;

	g_set_object (&page->app, app);

	set_updates_description_ui (page, app);

	g_object_notify_by_pspec (G_OBJECT (page), obj_props[PROP_APP]);
}

/**
 * gs_app_details_page_get_show_back_button:
 * @page: a #GsAppDetailsPage
 *
 * Get the value of #GsAppDetailsPage:show-back-button.
 *
 * Returns: whether to show the back button
 *
 * Since: 41
 */
gboolean
gs_app_details_page_get_show_back_button (GsAppDetailsPage *page)
{
	g_return_val_if_fail (GS_IS_APP_DETAILS_PAGE (page), FALSE);
	return gtk_widget_get_visible (page->back_button);
}

/**
 * gs_app_details_page_set_show_back_button:
 * @page: a #GsAppDetailsPage
 * @show_back_button: whether to show the back button
 *
 * Set the value of #GsAppDetailsPage:show-back-button.
 *
 * Since: 41
 */
void
gs_app_details_page_set_show_back_button (GsAppDetailsPage *page, gboolean show_back_button)
{
	g_return_if_fail (GS_IS_APP_DETAILS_PAGE (page));

	show_back_button = !!show_back_button;

	if (gtk_widget_get_visible (page->back_button) == show_back_button)
		return;

	gtk_widget_set_visible (page->back_button, show_back_button);

	g_object_notify_by_pspec (G_OBJECT (page), obj_props[PROP_SHOW_BACK_BUTTON]);
}

static void
back_clicked_cb (GtkWidget *widget, GsAppDetailsPage *page)
{
	g_signal_emit (page, signals[SIGNAL_BACK_CLICKED], 0);
}

static void
scrollbar_mapped_cb (GtkWidget *sb, GtkScrolledWindow *swin)
{
	GtkWidget *frame;

	frame = gtk_bin_get_child (GTK_BIN (gtk_bin_get_child (GTK_BIN (swin))));

	if (gtk_widget_get_mapped (GTK_WIDGET (sb))) {
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_IN);
		if (GTK_IS_FRAME (frame))
			gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	} else {
		if (GTK_IS_FRAME (frame))
			gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_NONE);
	}
}

static void
gs_app_details_page_dispose (GObject *object)
{
	GsAppDetailsPage *page = GS_APP_DETAILS_PAGE (object);

	g_clear_object (&page->app);

	G_OBJECT_CLASS (gs_app_details_page_parent_class)->dispose (object);
}

static void
gs_app_details_page_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsAppDetailsPage *page = GS_APP_DETAILS_PAGE (object);

	switch ((GsAppDetailsPageProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, gs_app_details_page_get_app (page));
		break;
	case PROP_SHOW_BACK_BUTTON:
		g_value_set_boolean (value, gs_app_details_page_get_show_back_button (page));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_details_page_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsAppDetailsPage *page = GS_APP_DETAILS_PAGE (object);

	switch ((GsAppDetailsPageProperty) prop_id) {
	case PROP_APP:
		gs_app_details_page_set_app (page, g_value_get_object (value));
		break;
	case PROP_SHOW_BACK_BUTTON:
		gs_app_details_page_set_show_back_button (page, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_details_page_init (GsAppDetailsPage *page)
{
	GtkWidget *scrollbar;

	gtk_widget_init_template (GTK_WIDGET (page));

	scrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (page->scrolledwindow_details));
	g_signal_connect (scrollbar, "map", G_CALLBACK (scrollbar_mapped_cb), page->scrolledwindow_details);
	g_signal_connect (scrollbar, "unmap", G_CALLBACK (scrollbar_mapped_cb), page->scrolledwindow_details);

}

static void
gs_app_details_page_class_init (GsAppDetailsPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_app_details_page_dispose;
	object_class->get_property = gs_app_details_page_get_property;
	object_class->set_property = gs_app_details_page_set_property;

	/**
	 * GsAppDetailsPage:app: (nullable)
	 *
	 * The app to present.
	 *
	 * Since: 41
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppDetailsPage:show-back-button
	 *
	 * Whether to show the back button.
	 *
	 * Since: 41
	 */
	obj_props[PROP_SHOW_BACK_BUTTON] =
		g_param_spec_boolean ("show-back-button", NULL, NULL,
				     TRUE,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	/**
	 * GsAppDetailsPage:back-clicked:
	 * @app: a #GsApp
	 *
	 * Emitted when the back button got activated and the #GsUpdateDialog
	 * containing this page is expected to go back.
	 *
	 * Since: 41
	 */
	signals[SIGNAL_BACK_CLICKED] =
		g_signal_new ("back-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-details-page.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, back_button);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, box_header);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, header_bar);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, image_icon);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, label_details);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, label_name);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, label_summary);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, permissions_section_box);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, permissions_section_content);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, scrolledwindow_details);
	gtk_widget_class_bind_template_callback (widget_class, back_clicked_cb);
}

/**
 * gs_app_details_page_new:
 *
 * Create a new #GsAppDetailsPage.
 *
 * Returns: (transfer full): a new #GsAppDetailsPage
 * Since: 41
 */
GtkWidget *
gs_app_details_page_new (void)
{
	return GTK_WIDGET (g_object_new (GS_TYPE_APP_DETAILS_PAGE, NULL));
}
