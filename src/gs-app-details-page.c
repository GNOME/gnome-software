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
	PROP_TITLE,
} GsAppDetailsPageProperty;

enum {
	SIGNAL_BACK_CLICKED,
	SIGNAL_LAST
};

static GParamSpec *obj_props[PROP_TITLE + 1] = { NULL, };

static guint signals[SIGNAL_LAST] = { 0 };

struct _GsAppDetailsPage
{
	GtkBox		 parent_instance;

	GtkWidget	*back_button;
	GtkWidget	*header_bar;
	GtkWidget	*label_details;
	GtkWidget	*permissions_section;
	GtkWidget	*permissions_section_list;
	GtkWidget	*status_page;
	AdwWindowTitle	*window_title;

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
populate_permissions_section (GsAppDetailsPage *page, GsAppPermissions permissions)
{
	gs_widget_remove_all (page->permissions_section_list, (GsRemoveFunc) gtk_list_box_remove);

	for (gsize i = 0; i < G_N_ELEMENTS (permission_display_data); i++) {
		GtkWidget *row, *image;

		if ((permissions & permission_display_data[i].permission) == 0)
			continue;

		row = adw_action_row_new ();
		if ((permission_display_data[i].permission & ~MEDIUM_PERMISSIONS) != 0) {
			gtk_style_context_add_class (gtk_widget_get_style_context (row), "permission-row-warning");
		}

		image = gtk_image_new_from_icon_name ("dialog-warning-symbolic");
		if ((permission_display_data[i].permission & ~MEDIUM_PERMISSIONS) == 0)
			gtk_widget_set_opacity (image, 0);

		adw_action_row_add_prefix (ADW_ACTION_ROW (row), image);
		adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(permission_display_data[i].title));
		adw_action_row_set_subtitle (ADW_ACTION_ROW (row), _(permission_display_data[i].subtitle));

		gtk_list_box_append (GTK_LIST_BOX (page->permissions_section_list), row);
	}
}

static void
set_updates_description_ui (GsAppDetailsPage *page, GsApp *app)
{
	AsComponentKind kind;
	g_autoptr(GIcon) icon = NULL;
	guint icon_size;
	const gchar *update_details;
#if ADW_CHECK_VERSION(1, 0, 1)
	GdkDisplay *display;
	g_autoptr (GtkIconPaintable) paintable = NULL;
#endif

	/* FIXME support app == NULL */

	/* set window title */
	kind = gs_app_get_kind (app);
	if (kind == AS_COMPONENT_KIND_GENERIC &&
	    gs_app_get_special_kind (app) == GS_APP_SPECIAL_KIND_OS_UPDATE) {
		adw_window_title_set_title (page->window_title, gs_app_get_name (app));
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
		adw_window_title_set_title (page->window_title, tmp);
	} else if (gs_app_get_source_default (app) != NULL) {
		adw_window_title_set_title (page->window_title,
					    gs_app_get_source_default (app));
	} else {
		adw_window_title_set_title (page->window_title,
					    gs_app_get_update_version (app));
	}

	g_object_notify_by_pspec (G_OBJECT (page), obj_props[PROP_TITLE]);

	/* set update header */
	update_details = gs_app_get_update_details_markup (app);
	if (update_details == NULL) {
		/* TRANSLATORS: this is where the packager did not write
		 * a description for the update */
		update_details = _("No update description available.");
	}
	gtk_label_set_label (GTK_LABEL (page->label_details), update_details);
	adw_status_page_set_title (ADW_STATUS_PAGE (page->status_page), gs_app_get_name (app));
	adw_status_page_set_description (ADW_STATUS_PAGE (page->status_page), gs_app_get_summary (app));

	/* set the icon; fall back to 64px if 96px isn’t available, which sometimes
	 * happens at 2× scale factor (hi-DPI) */
	icon_size = 96;
	icon = gs_app_get_icon_for_size (app,
					 icon_size,
					 gtk_widget_get_scale_factor (GTK_WIDGET (page)),
					 NULL);
	if (icon == NULL) {
		icon_size = 64;
		icon = gs_app_get_icon_for_size (app,
						 icon_size,
						 gtk_widget_get_scale_factor (GTK_WIDGET (page)),
						 NULL);
	}
	if (icon == NULL) {
		icon_size = 96;
		icon = gs_app_get_icon_for_size (app,
						 icon_size,
						 gtk_widget_get_scale_factor (GTK_WIDGET (page)),
						 "system-component-application");
	}

#if ADW_CHECK_VERSION(1, 0, 1)
	display = gdk_display_get_default ();
	paintable = gtk_icon_theme_lookup_by_gicon (gtk_icon_theme_get_for_display (display),
						    icon,
						    icon_size,
						    gtk_widget_get_scale_factor (GTK_WIDGET (page)),
						    gtk_widget_get_direction (GTK_WIDGET (page)),
						    GTK_ICON_LOOKUP_FORCE_REGULAR);
	adw_status_page_set_paintable (ADW_STATUS_PAGE (page->status_page), GDK_PAINTABLE (paintable));
#else
	if (G_IS_THEMED_ICON (icon)) {
		const gchar *const *names = g_themed_icon_get_names (G_THEMED_ICON (icon));

		adw_status_page_set_icon_name (ADW_STATUS_PAGE (page->status_page),
					       names ? names[0] : NULL);
	} else {
		adw_status_page_set_icon_name (ADW_STATUS_PAGE (page->status_page), NULL);
	}
#endif

	if (gs_app_has_quirk (app, GS_APP_QUIRK_NEW_PERMISSIONS)) {
		gtk_widget_show (page->permissions_section);
		populate_permissions_section (page, gs_app_get_update_permissions (app));
	} else {
		gtk_widget_hide (page->permissions_section);
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
	case PROP_TITLE:
		g_value_set_string (value, adw_window_title_get_title (page->window_title));
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
	gtk_widget_init_template (GTK_WIDGET (page));
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

	/**
	 * GsAppDetailsPage:title
	 *
	 * Read-only window title.
	 *
	 * Since: 42
	 */
	obj_props[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

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
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, header_bar);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, label_details);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, permissions_section);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, permissions_section_list);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, status_page);
	gtk_widget_class_bind_template_child (widget_class, GsAppDetailsPage, window_title);
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
