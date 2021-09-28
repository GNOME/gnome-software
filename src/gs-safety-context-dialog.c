/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-safety-context-dialog
 * @short_description: A dialog showing safety information about an app
 *
 * #GsSafetyContextDialog is a dialog which shows detailed information about
 * how safe or trustworthy an app is. This information is derived from the
 * permissions the app requires to run, its runtime, origin, and various other
 * sources.
 *
 * It is designed to show a more detailed view of the information which the
 * app’s safety tile in #GsAppContextBar is derived from.
 *
 * The widget has no special appearance if the app is unset, so callers will
 * typically want to hide the dialog in that case.
 *
 * Since: 41
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <handy.h>
#include <locale.h>

#include "gs-app.h"
#include "gs-common.h"
#include "gs-context-dialog-row.h"
#include "gs-safety-context-dialog.h"

struct _GsSafetyContextDialog
{
	GsInfoWindow		 parent_instance;

	GsApp			*app;  /* (nullable) (owned) */
	gulong			 app_notify_handler_permissions;
	gulong			 app_notify_handler_name;
	gulong			 app_notify_handler_quirk;
	gulong			 app_notify_handler_license;
	gulong			 app_notify_handler_related;

	GtkImage		*icon;
	GtkWidget		*lozenge;
	GtkLabel		*title;
	GtkListBox		*permissions_list;

	GtkLabel		*license_label;
	GBinding		*license_label_binding;  /* (owned) (nullable) */
	GtkLabel		*source_label;
	GBinding		*source_label_binding;  /* (owned) (nullable) */
	GtkLabel		*sdk_label;
	GtkWidget		*sdk_row;
};

G_DEFINE_TYPE (GsSafetyContextDialog, gs_safety_context_dialog, GS_TYPE_INFO_WINDOW)

typedef enum {
	PROP_APP = 1,
} GsSafetyContextDialogProperty;

static GParamSpec *obj_props[PROP_APP + 1] = { NULL, };

/* @icon_name_without_permission, @title_without_permission and
 * @description_without_permission are all nullable. If they are NULL, no row
 * is added if @has_permission is false. */
static void
add_permission_row (GtkListBox                   *list_box,
                    GsContextDialogRowImportance *chosen_rating,
                    gboolean                      has_permission,
                    GsContextDialogRowImportance  item_rating,
                    const gchar                  *icon_name_with_permission,
                    const gchar                  *title_with_permission,
                    const gchar                  *description_with_permission,
                    const gchar                  *icon_name_without_permission,
                    const gchar                  *title_without_permission,
                    const gchar                  *description_without_permission)
{
	GtkListBoxRow *row;

	if (has_permission && item_rating > *chosen_rating)
		*chosen_rating = item_rating;

	if (!has_permission && title_without_permission == NULL)
		return;

	row = gs_context_dialog_row_new (has_permission ? icon_name_with_permission : icon_name_without_permission,
					 has_permission ? item_rating : GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT,
					 has_permission ? title_with_permission : title_without_permission,
					 has_permission ? description_with_permission : description_without_permission);
	gtk_list_box_insert (list_box, GTK_WIDGET (row), -1);
}

static void
update_permissions_list (GsSafetyContextDialog *self)
{
	const gchar *icon_name, *css_class;
	g_autofree gchar *title = NULL;
	g_autoptr(GPtrArray) descriptions = g_ptr_array_new_with_free_func (NULL);
	g_autofree gchar *description = NULL;
	GsAppPermissions permissions;
	GtkStyleContext *context;
	GsContextDialogRowImportance chosen_rating;

	/* Treat everything as safe to begin with, and downgrade its safety
	 * based on app properties. */
	chosen_rating = GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT;

	gs_container_remove_all (GTK_CONTAINER (self->permissions_list));

	/* UI state is undefined if app is not set. */
	if (self->app == NULL)
		return;

	permissions = gs_app_get_permissions (self->app);

	/* Handle unknown permissions. This means the application isn’t
	 * sandboxed, so we can only really base decisions on whether it was
	 * packaged by an organisation we trust or not.
	 *
	 * FIXME: See the comment for GS_APP_PERMISSIONS_UNKNOWN in
	 * gs-app-context-bar.c. */
	if (permissions == GS_APP_PERMISSIONS_UNKNOWN) {
		add_permission_row (self->permissions_list, &chosen_rating,
				    !gs_app_has_quirk (self->app, GS_APP_QUIRK_PROVENANCE),
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING,
				    "channel-insecure-symbolic",
				    _("Provided by a third party"),
				    _("Check that you trust the vendor, as the application isn’t sandboxed"),
				    "channel-secure-symbolic",
				    _("Reviewed by your distribution"),
				    _("Application isn’t sandboxed but the distribution has checked that it is not malicious"));
	} else {
		add_permission_row (self->permissions_list, &chosen_rating,
				    (permissions & GS_APP_PERMISSIONS_NONE) != 0,
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT,
				    "folder-documents-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("No Permissions"),
				    _("App is fully sandboxed"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    (permissions & GS_APP_PERMISSIONS_NETWORK) != 0,
				    /* This isn’t actually unimportant (network access can expand a local
				     * vulnerability into a remotely exploitable one), but it’s
				     * needed commonly enough that marking it as
				     * %GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING is too noisy. */
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_NEUTRAL,
				    "network-wireless-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Network Access"),
				    _("Can access the internet"),
				    "network-wireless-disabled-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("No Network Access"),
				    _("Cannot access the internet"));
		add_permission_row (self->permissions_list, &chosen_rating,
				    (permissions & GS_APP_PERMISSIONS_SYSTEM_BUS) != 0,
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING,
				    "emblem-system-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Uses System Services"),
				    _("Can request data from system services"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    (permissions & GS_APP_PERMISSIONS_SESSION_BUS) != 0,
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT,
				    "emblem-system-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Uses Session Services"),
				    _("Can request data from session services"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    (permissions & GS_APP_PERMISSIONS_DEVICES) != 0,
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING,
				    "camera-photo-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Device Access"),
				    _("Can access devices such as webcams or gaming controllers"),
				    "camera-disabled-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("No Device Access"),
				    _("Cannot access devices such as webcams or gaming controllers"));
		add_permission_row (self->permissions_list, &chosen_rating,
				    (permissions & GS_APP_PERMISSIONS_X11) != 0,
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT,
				    "desktop-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Legacy Windowing System"),
				    _("Uses a legacy windowing system"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    (permissions & GS_APP_PERMISSIONS_ESCAPE_SANDBOX) != 0,
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT,
				    "dialog-warning-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Arbitrary Permissions"),
				    _("Can acquire arbitrary permissions"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    (permissions & GS_APP_PERMISSIONS_SETTINGS) != 0,
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING,
				    "preferences-system-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("User Settings"),
				    _("Can access and change user settings"),
				    NULL, NULL, NULL);

		/* File system permissions are a bit more complex, since there are
		 * varying scopes of what’s readable/writable, and a difference between
		 * read-only and writable access. */
		add_permission_row (self->permissions_list, &chosen_rating,
				    (permissions & GS_APP_PERMISSIONS_FILESYSTEM_FULL) != 0,
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT,
				    "folder-documents-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Full File System Read/Write Access"),
				    _("Can read and write all data on the file system"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    ((permissions & GS_APP_PERMISSIONS_HOME_FULL) != 0 &&
				     !(permissions & GS_APP_PERMISSIONS_FILESYSTEM_FULL)),
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT,
				    "user-home-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Home Folder Read/Write Access"),
				    _("Can read and write all data in your home directory"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    ((permissions & GS_APP_PERMISSIONS_FILESYSTEM_READ) != 0 &&
				     !(permissions & GS_APP_PERMISSIONS_FILESYSTEM_FULL)),
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT,
				    "folder-documents-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Full File System Read Access"),
				    _("Can read all data on the file system"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    ((permissions & GS_APP_PERMISSIONS_HOME_READ) != 0 &&
				     !(permissions & (GS_APP_PERMISSIONS_FILESYSTEM_FULL |
						      GS_APP_PERMISSIONS_FILESYSTEM_READ))),
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT,
				    "user-home-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Home Folder Read Access"),
				    _("Can read all data in your home directory"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    ((permissions & GS_APP_PERMISSIONS_DOWNLOADS_FULL) != 0 &&
				     !(permissions & (GS_APP_PERMISSIONS_FILESYSTEM_FULL |
						      GS_APP_PERMISSIONS_HOME_FULL))),
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING,
				    "folder-download-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Download Folder Read/Write Access"),
				    _("Can read and write all data in your downloads directory"),
				    NULL, NULL, NULL);
		add_permission_row (self->permissions_list, &chosen_rating,
				    ((permissions & GS_APP_PERMISSIONS_DOWNLOADS_READ) != 0 &&
				     !(permissions & (GS_APP_PERMISSIONS_FILESYSTEM_FULL |
						      GS_APP_PERMISSIONS_FILESYSTEM_READ |
						      GS_APP_PERMISSIONS_HOME_FULL |
						      GS_APP_PERMISSIONS_HOME_READ))),
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING,
				    "folder-download-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("Download Folder Read Access"),
				    _("Can read all data in your downloads directory"),
				    NULL, NULL, NULL);

		add_permission_row (self->permissions_list, &chosen_rating,
				    !(permissions & (GS_APP_PERMISSIONS_FILESYSTEM_FULL |
						     GS_APP_PERMISSIONS_FILESYSTEM_READ |
						     GS_APP_PERMISSIONS_HOME_FULL |
						     GS_APP_PERMISSIONS_HOME_READ |
						     GS_APP_PERMISSIONS_DOWNLOADS_FULL |
						     GS_APP_PERMISSIONS_DOWNLOADS_READ)),
				    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT,
				    "folder-documents-symbolic",
				    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
				    _("No File System Access"),
				    _("Cannot access the file system at all"),
				    NULL, NULL, NULL);
	}

	/* Is the code FOSS and hence inspectable? This doesn’t distinguish
	 * between closed source and open-source-but-not-FOSS software, even
	 * though the code of the latter is technically publicly auditable. This
	 * is because I don’t want to get into the business of maintaining lists
	 * of ‘auditable’ source code licenses. */
	add_permission_row (self->permissions_list, &chosen_rating,
			    !gs_app_get_license_is_free (self->app),
			    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING,
			    "dialog-warning-symbolic",
			    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
			    _("Proprietary Code"),
			    _("The source code is not public, so it cannot be independently audited and might be unsafe"),
			    "test-pass-symbolic",
			    /* Translators: This refers to permissions (for example, from flatpak) which an app requests from the user. */
			    _("Auditable Code"),
			    _("The source code is public and can be independently audited, which makes the app more likely to be safe"));

	add_permission_row (self->permissions_list, &chosen_rating,
			    gs_app_has_quirk (self->app, GS_APP_QUIRK_DEVELOPER_VERIFIED),
			    GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT,
			    "test-pass-symbolic",
			    /* Translators: This indicates an app was written and released by a developer who has been verified.
			     * It’s used in a context tile, so should be short. */
			    _("App developer is verified"),
			    _("The developer of this app has been verified to be who they say they are"),
			    NULL, NULL, NULL);

	/* Update the UI. */
	switch (chosen_rating) {
	case GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT:
		icon_name = "safety-symbolic";
		/* Translators: The app is considered safe to install and run.
		 * The placeholder is the app name. */
		title = g_strdup_printf (_("%s is safe"), gs_app_get_name (self->app));
		css_class = "green";
		break;
	case GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING:
		icon_name = "dialog-question-symbolic";
		/* Translators: The app is considered potentially unsafe to install and run.
		 * The placeholder is the app name. */
		title = g_strdup_printf (_("%s is potentially unsafe"), gs_app_get_name (self->app));
		css_class = "yellow";
		break;
	case GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT:
		icon_name = "dialog-warning-symbolic";
		/* Translators: The app is considered unsafe to install and run.
		 * The placeholder is the app name. */
		title = g_strdup_printf (_("%s is unsafe"), gs_app_get_name (self->app));
		css_class = "red";
		break;
	default:
		g_assert_not_reached ();
	}

	gtk_image_set_from_icon_name (GTK_IMAGE (self->icon), icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_label_set_text (self->title, title);

	context = gtk_widget_get_style_context (self->lozenge);

	gtk_style_context_remove_class (context, "green");
	gtk_style_context_remove_class (context, "yellow");
	gtk_style_context_remove_class (context, "red");

	gtk_style_context_add_class (context, css_class);
}

static void
app_notify_cb (GObject    *obj,
               GParamSpec *pspec,
               gpointer    user_data)
{
	GsSafetyContextDialog *self = GS_SAFETY_CONTEXT_DIALOG (user_data);

	update_permissions_list (self);
}

static void
update_sdk (GsSafetyContextDialog *self)
{
	GsApp *runtime;

	/* UI state is undefined if app is not set. */
	if (self->app == NULL)
		return;

	runtime = gs_app_get_runtime (self->app);

	if (runtime != NULL) {
		g_autofree gchar *label = NULL;
		const gchar *version = gs_app_get_version_ui (runtime);

		if (version != NULL) {
			/* Translators: The first placeholder is an app runtime
			 * name, the second is its version number. */
			label = g_strdup_printf (_("%s (%s)"),
						 gs_app_get_name (runtime),
						 version);
		} else {
			label = g_strdup (gs_app_get_name (runtime));
		}

		gtk_label_set_label (self->sdk_label, label);
	}

	/* Only show the row if a runtime was found. */
	gtk_widget_set_visible (self->sdk_row, (runtime != NULL));
}

static void
app_notify_related_cb (GObject    *obj,
                       GParamSpec *pspec,
                       gpointer    user_data)
{
	GsSafetyContextDialog *self = GS_SAFETY_CONTEXT_DIALOG (user_data);

	update_sdk (self);
}

static void
gs_safety_context_dialog_init (GsSafetyContextDialog *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_safety_context_dialog_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
	GsSafetyContextDialog *self = GS_SAFETY_CONTEXT_DIALOG (object);

	switch ((GsSafetyContextDialogProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, gs_safety_context_dialog_get_app (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_safety_context_dialog_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
	GsSafetyContextDialog *self = GS_SAFETY_CONTEXT_DIALOG (object);

	switch ((GsSafetyContextDialogProperty) prop_id) {
	case PROP_APP:
		gs_safety_context_dialog_set_app (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_safety_context_dialog_dispose (GObject *object)
{
	GsSafetyContextDialog *self = GS_SAFETY_CONTEXT_DIALOG (object);

	gs_safety_context_dialog_set_app (self, NULL);

	G_OBJECT_CLASS (gs_safety_context_dialog_parent_class)->dispose (object);
}

static void
gs_safety_context_dialog_class_init (GsSafetyContextDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_safety_context_dialog_get_property;
	object_class->set_property = gs_safety_context_dialog_set_property;
	object_class->dispose = gs_safety_context_dialog_dispose;

	/**
	 * GsSafetyContextDialog:app: (nullable)
	 *
	 * The app to display the safety context details for.
	 *
	 * This may be %NULL; if so, the content of the widget will be
	 * undefined.
	 *
	 * Since: 41
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-safety-context-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsSafetyContextDialog, icon);
	gtk_widget_class_bind_template_child (widget_class, GsSafetyContextDialog, lozenge);
	gtk_widget_class_bind_template_child (widget_class, GsSafetyContextDialog, title);
	gtk_widget_class_bind_template_child (widget_class, GsSafetyContextDialog, permissions_list);
	gtk_widget_class_bind_template_child (widget_class, GsSafetyContextDialog, license_label);
	gtk_widget_class_bind_template_child (widget_class, GsSafetyContextDialog, source_label);
	gtk_widget_class_bind_template_child (widget_class, GsSafetyContextDialog, sdk_label);
	gtk_widget_class_bind_template_child (widget_class, GsSafetyContextDialog, sdk_row);
}

/**
 * gs_safety_context_dialog_new:
 * @app: (nullable): the app to display safety context information for, or %NULL
 *
 * Create a new #GsSafetyContextDialog and set its initial app to @app.
 *
 * Returns: (transfer full): a new #GsSafetyContextDialog
 * Since: 41
 */
GsSafetyContextDialog *
gs_safety_context_dialog_new (GsApp *app)
{
	g_return_val_if_fail (app == NULL || GS_IS_APP (app), NULL);

	return g_object_new (GS_TYPE_SAFETY_CONTEXT_DIALOG,
			     "app", app,
			     NULL);
}

/**
 * gs_safety_context_dialog_get_app:
 * @self: a #GsSafetyContextDialog
 *
 * Gets the value of #GsSafetyContextDialog:app.
 *
 * Returns: (nullable) (transfer none): app whose safety context information is
 *     being displayed, or %NULL if none is set
 * Since: 41
 */
GsApp *
gs_safety_context_dialog_get_app (GsSafetyContextDialog *self)
{
	g_return_val_if_fail (GS_IS_SAFETY_CONTEXT_DIALOG (self), NULL);

	return self->app;
}

/**
 * gs_safety_context_dialog_set_app:
 * @self: a #GsSafetyContextDialog
 * @app: (nullable) (transfer none): the app to display safety context
 *     information for, or %NULL for none
 *
 * Set the value of #GsSafetyContextDialog:app.
 *
 * Since: 41
 */
void
gs_safety_context_dialog_set_app (GsSafetyContextDialog *self,
                                  GsApp                 *app)
{
	g_return_if_fail (GS_IS_SAFETY_CONTEXT_DIALOG (self));
	g_return_if_fail (app == NULL || GS_IS_APP (app));

	if (app == self->app)
		return;

	g_clear_signal_handler (&self->app_notify_handler_permissions, self->app);
	g_clear_signal_handler (&self->app_notify_handler_name, self->app);
	g_clear_signal_handler (&self->app_notify_handler_quirk, self->app);
	g_clear_signal_handler (&self->app_notify_handler_license, self->app);
	g_clear_signal_handler (&self->app_notify_handler_related, self->app);

	g_clear_object (&self->license_label_binding);
	g_clear_object (&self->source_label_binding);

	g_set_object (&self->app, app);

	if (self->app != NULL) {
		self->app_notify_handler_permissions = g_signal_connect (self->app, "notify::permissions", G_CALLBACK (app_notify_cb), self);
		self->app_notify_handler_name = g_signal_connect (self->app, "notify::name", G_CALLBACK (app_notify_cb), self);
		self->app_notify_handler_quirk = g_signal_connect (self->app, "notify::quirk", G_CALLBACK (app_notify_cb), self);
		self->app_notify_handler_license = g_signal_connect (self->app, "notify::license", G_CALLBACK (app_notify_cb), self);

		self->app_notify_handler_related = g_signal_connect (self->app, "notify::related", G_CALLBACK (app_notify_related_cb), self);

		self->license_label_binding = g_object_bind_property (self->app, "license", self->license_label, "label", G_BINDING_SYNC_CREATE);
		self->source_label_binding = g_object_bind_property (self->app, "origin-ui", self->source_label, "label", G_BINDING_SYNC_CREATE);
	}

	/* Update the UI. */
	update_permissions_list (self);
	update_sdk (self);

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_APP]);
}
