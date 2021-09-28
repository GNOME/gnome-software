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
 * SECTION:gs-storage-context-dialog
 * @short_description: A dialog showing storage information about an app
 *
 * #GsStorageContextDialog is a dialog which shows detailed information
 * about the download size of an uninstalled app, or the storage usage of
 * an installed one. It shows how those sizes are broken down into components
 * such as user data, cached data, or dependencies, where possible.
 *
 * It is designed to show a more detailed view of the information which the
 * app’s storage tile in #GsAppContextBar is derived from.
 *
 * The widget has no special appearance if the app is unset, so callers will
 * typically want to hide the dialog in that case.
 *
 * Since: 41
 */

#include "config.h"

#include <adwaita.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>

#include "gs-app.h"
#include "gs-common.h"
#include "gs-context-dialog-row.h"
#include "gs-storage-context-dialog.h"

struct _GsStorageContextDialog
{
	GsInfoWindow		 parent_instance;

	GsApp			*app;  /* (nullable) (owned) */
	gulong			 app_notify_handler;

	GtkSizeGroup		*lozenge_size_group;
	GtkLabel		*lozenge_content;
	GtkWidget		*lozenge;
	GtkLabel		*title;
	GtkListBox		*sizes_list;
	GtkLabel		*manage_storage_label;
};

G_DEFINE_TYPE (GsStorageContextDialog, gs_storage_context_dialog, GS_TYPE_INFO_WINDOW)

typedef enum {
	PROP_APP = 1,
} GsStorageContextDialogProperty;

static GParamSpec *obj_props[PROP_APP + 1] = { NULL, };

typedef enum {
	MATCH_STATE_NO_MATCH = 0,
	MATCH_STATE_MATCH = 1,
	MATCH_STATE_UNKNOWN,
} MatchState;

/* The arguments are all non-nullable. */
static void
add_size_row (GtkListBox   *list_box,
              GtkSizeGroup *lozenge_size_group,
              guint64       size_bytes,
              const gchar  *title,
              const gchar  *description)
{
	GtkListBoxRow *row;
	g_autofree gchar *size_bytes_str = NULL;

	if (size_bytes == GS_APP_SIZE_UNKNOWABLE)
		/* Translators: This is shown in a bubble if the storage
		 * size of an application is not known. The bubble is small,
		 * so the string should be as short as possible. */
		size_bytes_str = g_strdup (_("?"));
	else if (size_bytes == 0)
		/* Translators: This is shown in a bubble to represent a 0 byte
		 * storage size, so its context is “storage size: none”. The
		 * bubble is small, so the string should be as short as
		 * possible. */
		size_bytes_str = g_strdup (_("None"));
	else
		size_bytes_str = g_format_size (size_bytes);

	row = gs_context_dialog_row_new_text (size_bytes_str, GS_CONTEXT_DIALOG_ROW_IMPORTANCE_NEUTRAL,
					      title, description);
	gs_context_dialog_row_set_size_groups (GS_CONTEXT_DIALOG_ROW (row), lozenge_size_group, NULL, NULL);
	gtk_list_box_append (list_box, GTK_WIDGET (row));
}

static void
update_sizes_list (GsStorageContextDialog *self)
{
	guint64 title_size_bytes;
	g_autofree gchar *title_size_bytes_str = NULL;
	const gchar *title;
	gboolean cache_row_added = FALSE;

	gs_widget_remove_all (GTK_WIDGET (self->sizes_list), (GsRemoveFunc) gtk_list_box_remove);

	/* UI state is undefined if app is not set. */
	if (self->app == NULL)
		return;

	if (gs_app_is_installed (self->app)) {
		guint64 size_installed;
		guint64 size_user_data;
		guint64 size_cache_data;

		/* Don’t list the size of the dependencies as that space likely
		 * won’t be reclaimed unless many other apps are removed. */
		size_installed = gs_app_get_size_installed (self->app);
		size_user_data = gs_app_get_size_user_data (self->app);
		size_cache_data = gs_app_get_size_cache_data (self->app);

		title = _("Installed Size");
		title_size_bytes = size_installed;

		add_size_row (self->sizes_list, self->lozenge_size_group, size_installed,
			      _("Application Data"),
			      _("Data needed for the application to run"));

		if (size_user_data != GS_APP_SIZE_UNKNOWABLE) {
			add_size_row (self->sizes_list, self->lozenge_size_group, size_user_data,
				      _("User Data"),
				      _("Data created by you in the application"));
			title_size_bytes += size_user_data;
		}

		if (size_cache_data != GS_APP_SIZE_UNKNOWABLE) {
			add_size_row (self->sizes_list, self->lozenge_size_group, size_cache_data,
				      _("Cache Data"),
				      _("Temporary cached data"));
			title_size_bytes += size_cache_data;
			cache_row_added = TRUE;
		}
	} else {
		guint64 size_download;
		guint64 size_download_dependencies;

		size_download = gs_app_get_size_download (self->app);
		size_download_dependencies = gs_app_get_size_download_dependencies (self->app);

		title = _("Download Size");
		title_size_bytes = size_download;

		add_size_row (self->sizes_list, self->lozenge_size_group, size_download,
			      gs_app_get_name (self->app),
			      _("The application itself"));

		if (size_download_dependencies != GS_APP_SIZE_UNKNOWABLE) {
			add_size_row (self->sizes_list, self->lozenge_size_group, size_download_dependencies,
				      _("Required Dependencies"),
				      _("Shared system components required by this application"));
			title_size_bytes += size_download_dependencies;
		}

		/* FIXME: Addons, Potential Additional Downloads */
	}

	title_size_bytes_str = g_format_size (title_size_bytes);
	gtk_label_set_text (self->lozenge_content, title_size_bytes_str);
	gtk_label_set_text (self->title, title);

	/* Update the Manage Storage label. */
	gtk_widget_set_visible (GTK_WIDGET (self->manage_storage_label), cache_row_added);
}

static void
app_notify_cb (GObject    *obj,
               GParamSpec *pspec,
               gpointer    user_data)
{
	GsStorageContextDialog *self = GS_STORAGE_CONTEXT_DIALOG (user_data);
	GQuark pspec_name_quark = g_param_spec_get_name_quark (pspec);

	if (pspec_name_quark == g_quark_from_static_string ("state") ||
	    pspec_name_quark == g_quark_from_static_string ("size-installed") ||
	    pspec_name_quark == g_quark_from_static_string ("size-installed-dependencies") ||
	    pspec_name_quark == g_quark_from_static_string ("size-download") ||
	    pspec_name_quark == g_quark_from_static_string ("size-download-dependencies") ||
	    pspec_name_quark == g_quark_from_static_string ("size-cache-data") ||
	    pspec_name_quark == g_quark_from_static_string ("size-user-data"))
		update_sizes_list (self);
}

static gboolean
manage_storage_activate_link_cb (GtkLabel    *label,
                                 const gchar *uri,
                                 gpointer     user_data)
{
	GsStorageContextDialog *self = GS_STORAGE_CONTEXT_DIALOG (user_data);
	g_autoptr(GError) local_error = NULL;
	const gchar *desktop_id;
	const gchar *argv[] = {
		"gnome-control-center",
		"applications",
		"",  /* application ID */
		NULL
	};

	/* Button shouldn’t have been sensitive if the launchable ID isn’t available. */
	desktop_id = gs_app_get_launchable (self->app, AS_LAUNCHABLE_KIND_DESKTOP_ID);
	g_assert (desktop_id != NULL);

	argv[2] = desktop_id;

	if (!g_spawn_async (NULL, (gchar **) argv, NULL,
			    G_SPAWN_SEARCH_PATH |
			    G_SPAWN_STDOUT_TO_DEV_NULL |
			    G_SPAWN_STDERR_TO_DEV_NULL |
			    G_SPAWN_CLOEXEC_PIPES,
			    NULL, NULL, NULL, &local_error)) {
		g_warning ("Error opening GNOME Control Center: %s",
			   local_error->message);
		return TRUE;
	}

	return TRUE;
}

static void
gs_storage_context_dialog_init (GsStorageContextDialog *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_storage_context_dialog_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
	GsStorageContextDialog *self = GS_STORAGE_CONTEXT_DIALOG (object);

	switch ((GsStorageContextDialogProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, gs_storage_context_dialog_get_app (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_storage_context_dialog_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
	GsStorageContextDialog *self = GS_STORAGE_CONTEXT_DIALOG (object);

	switch ((GsStorageContextDialogProperty) prop_id) {
	case PROP_APP:
		gs_storage_context_dialog_set_app (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_storage_context_dialog_dispose (GObject *object)
{
	GsStorageContextDialog *self = GS_STORAGE_CONTEXT_DIALOG (object);

	gs_storage_context_dialog_set_app (self, NULL);

	G_OBJECT_CLASS (gs_storage_context_dialog_parent_class)->dispose (object);
}

static void
gs_storage_context_dialog_class_init (GsStorageContextDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_storage_context_dialog_get_property;
	object_class->set_property = gs_storage_context_dialog_set_property;
	object_class->dispose = gs_storage_context_dialog_dispose;

	/**
	 * GsStorageContextDialog:app: (nullable)
	 *
	 * The app to display the storage context details for.
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

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-storage-context-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsStorageContextDialog, lozenge_size_group);
	gtk_widget_class_bind_template_child (widget_class, GsStorageContextDialog, lozenge_content);
	gtk_widget_class_bind_template_child (widget_class, GsStorageContextDialog, lozenge);
	gtk_widget_class_bind_template_child (widget_class, GsStorageContextDialog, title);
	gtk_widget_class_bind_template_child (widget_class, GsStorageContextDialog, sizes_list);
	gtk_widget_class_bind_template_child (widget_class, GsStorageContextDialog, manage_storage_label);

	gtk_widget_class_bind_template_callback (widget_class, manage_storage_activate_link_cb);
}

/**
 * gs_storage_context_dialog_new:
 * @app: (nullable): the app to display storage context information for, or %NULL
 *
 * Create a new #GsStorageContextDialog and set its initial app to @app.
 *
 * Returns: (transfer full): a new #GsStorageContextDialog
 * Since: 41
 */
GsStorageContextDialog *
gs_storage_context_dialog_new (GsApp *app)
{
	g_return_val_if_fail (app == NULL || GS_IS_APP (app), NULL);

	return g_object_new (GS_TYPE_STORAGE_CONTEXT_DIALOG,
			     "app", app,
			     NULL);
}

/**
 * gs_storage_context_dialog_get_app:
 * @self: a #GsStorageContextDialog
 *
 * Gets the value of #GsStorageContextDialog:app.
 *
 * Returns: (nullable) (transfer none): app whose storage context information is
 *     being displayed, or %NULL if none is set
 * Since: 41
 */
GsApp *
gs_storage_context_dialog_get_app (GsStorageContextDialog *self)
{
	g_return_val_if_fail (GS_IS_STORAGE_CONTEXT_DIALOG (self), NULL);

	return self->app;
}

/**
 * gs_storage_context_dialog_set_app:
 * @self: a #GsStorageContextDialog
 * @app: (nullable) (transfer none): the app to display storage context
 *     information for, or %NULL for none
 *
 * Set the value of #GsStorageContextDialog:app.
 *
 * Since: 41
 */
void
gs_storage_context_dialog_set_app (GsStorageContextDialog *self,
                                   GsApp                  *app)
{
	g_return_if_fail (GS_IS_STORAGE_CONTEXT_DIALOG (self));
	g_return_if_fail (app == NULL || GS_IS_APP (app));

	if (app == self->app)
		return;

	g_clear_signal_handler (&self->app_notify_handler, self->app);

	g_set_object (&self->app, app);

	if (self->app != NULL)
		self->app_notify_handler = g_signal_connect (self->app, "notify", G_CALLBACK (app_notify_cb), self);

	/* Update the UI. */
	update_sizes_list (self);

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_APP]);
}
