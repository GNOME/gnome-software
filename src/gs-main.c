/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-list-box.h"
#include "gs-app-widget.h"

#define CSS_FILE DATADIR "/gnome-software/gtk-style.css"
#define	GS_MAIN_ICON_SIZE	64

typedef enum {
	GS_MAIN_MODE_NEW,
	GS_MAIN_MODE_INSTALLED,
	GS_MAIN_MODE_UPDATES,
	GS_MAIN_MODE_WAITING
} GsMainMode;


enum {
	COLUMN_PACKAGE_ID,
	COLUMN_ICON_NAME,
	COLUMN_PACKAGE_NAME,
	COLUMN_PACKAGE_VERSION,
	COLUMN_PACKAGE_SUMMARY,
	COLUMN_LAST
};

enum {
	COLUMN_POPULAR_PACKAGE_ID,
	COLUMN_POPULAR_MARKUP,
	COLUMN_POPULAR_PIXBUF,
	COLUMN_POPULAR_LAST
};

typedef struct {
	GCancellable		*cancellable;
	GsMainMode		 mode;
	GtkApplication		*application;
	GtkBuilder		*builder;
	GtkIconSize		 custom_icon_size;
	PkDesktop		*desktop;
	PkTask			*task;
	guint			 waiting_tab_id;
	EggListBox		*list_box_installed;
	EggListBox		*list_box_updates;
	GtkWidget		*os_update_widget;
	GtkCssProvider		*provider;
	gboolean		ignore_primary_buttons;
} GsMainPrivate;

static void gs_main_set_overview_mode_ui (GsMainPrivate *priv, GsMainMode mode);

/**
 * gs_main_activate_cb:
 **/
static void
gs_main_activate_cb (GApplication *application, GsMainPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	gtk_window_present (window);
}


/**
 * gs_main_show_waiting_tab_cb:
 **/
static gboolean
gs_main_show_waiting_tab_cb (gpointer user_data)
{
	GsMainPrivate *priv = (GsMainPrivate *) user_data;
	gs_main_set_overview_mode_ui (priv, GS_MAIN_MODE_WAITING);
	priv->waiting_tab_id = 0;
	return FALSE;
}

/**
 * gs_main_get_app_widget_for_id:
 **/
static GsAppWidget *
gs_main_get_app_widget_for_id (EggListBox *list_box, const gchar *id)
{
	GList *list, *l;
	GsAppWidget *tmp;

	/* look for this widget */
	list = gtk_container_get_children (GTK_CONTAINER (list_box));
	for (l = list; l != NULL; l = l->next) {
		tmp = GS_APP_WIDGET (l->data);
		if (g_strcmp0 (gs_app_widget_get_id (tmp), id) == 0)
			goto out;
	}
	tmp = NULL;
out:
	g_list_free (list);
	return tmp;
}

/**
 * gs_main_progress_cb:
 **/
static void
gs_main_progress_cb (PkProgress *progress,
		     PkProgressType type,
		     GsMainPrivate *priv)
{
	const gchar *status_text = NULL;
	gboolean allow_cancel;
	gint percentage;
	GsAppWidget *app_widget;
	GtkWidget *widget;
	PkItemProgress *item_progress;
	PkRoleEnum role;
	PkStatusEnum status;

	/* action item, so no waiting panel */
	g_object_get (progress,
		      "role", &role,
		      NULL);
	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGES) {

		/* update this item in situ */
		if (type == PK_PROGRESS_TYPE_ITEM_PROGRESS) {
			g_object_get (progress,
				      "item-progress", &item_progress,
				      "status", &status,
				      NULL);
			g_warning ("need to find %s and update",
				   pk_item_progress_get_package_id (item_progress));
			app_widget = gs_main_get_app_widget_for_id (priv->list_box_installed,
								    pk_item_progress_get_package_id (item_progress));
			if (app_widget != NULL) {
				gs_app_widget_set_kind (app_widget, GS_APP_WIDGET_KIND_BUSY);
				gs_app_widget_set_status (app_widget, pk_status_enum_to_string (status));
			}
		}
		return;
	}

	g_object_get (progress,
		      "status", &status,
		      "percentage", &percentage,
		      "allow-cancel", &allow_cancel,
		      NULL);
	g_debug ("%s : %i (allow-cancel:%i",
		 pk_status_enum_to_string (status),
		 percentage,
		 allow_cancel);

	/* set label */
	switch (status) {
	case PK_STATUS_ENUM_SETUP:
	case PK_STATUS_ENUM_FINISHED:
	case PK_STATUS_ENUM_UNKNOWN:
		break;
	case PK_STATUS_ENUM_WAIT:
	case PK_STATUS_ENUM_WAITING_FOR_LOCK:
		/* TRANSLATORS: this is the transaction status */
		status_text = _("Waiting for package manager...");
		break;
	case PK_STATUS_ENUM_LOADING_CACHE:
		/* TRANSLATORS: this is the transaction status */
		status_text = _("Loading list of packages...");
		break;
	case PK_STATUS_ENUM_DOWNLOAD:
	case PK_STATUS_ENUM_DOWNLOAD_REPOSITORY:
	case PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST:
	case PK_STATUS_ENUM_DOWNLOAD_FILELIST:
	case PK_STATUS_ENUM_DOWNLOAD_CHANGELOG:
	case PK_STATUS_ENUM_DOWNLOAD_GROUP:
	case PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO:
		/* TRANSLATORS: this is the transaction status */
		status_text = _("Downloading...");
		break;
	case PK_STATUS_ENUM_QUERY:
	case PK_STATUS_ENUM_INFO:
		/* TRANSLATORS: this is the transaction status */
		status_text = _("Querying...");
		break;
	default:
		status_text = pk_status_enum_to_string (status);
		g_warning ("no translation for %s", status_text);
		break;
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_waiting"));
	if (status_text != NULL) {
		gtk_label_set_markup (GTK_LABEL (widget), status_text);
		gtk_widget_show (widget);
	} else {
		gtk_widget_hide (widget);
	}

	/* show the waiting panel if the delay is significant */
	if (status == PK_STATUS_ENUM_SETUP ||
	    status == PK_STATUS_ENUM_FINISHED) {
		gs_main_set_overview_mode_ui (priv, priv->mode);
		if (priv->waiting_tab_id > 0) {
			g_source_remove (priv->waiting_tab_id);
			priv->waiting_tab_id = 0;
		}
	} else {
		if (priv->waiting_tab_id == 0) {
			priv->waiting_tab_id = g_timeout_add (500,
							      gs_main_show_waiting_tab_cb,
							      priv);
		}
	}
}

/**
 * gs_main_get_pretty_version:
 *
 * convert 1:1.6.2-7.fc17 into 1.6.2
 **/
static gchar *
gs_main_get_pretty_version (const gchar *version)
{
	guint i;
	gchar *new;
	gchar *f;

	/* first remove any epoch */
	for (i = 0; version[i] != '\0'; i++) {
		if (version[i] == ':') {
			version = &version[i+1];
			break;
		}
		if (!g_ascii_isdigit (version[i]))
			break;
	}

	/* then remove any distro suffix */
	new = g_strdup_printf ("%s %s", _("Version"), version);
	f = g_strstr_len (new, -1, ".fc");
	if (f != NULL)
		*f= '\0';

	/* then remove any release */
	f = g_strrstr_len (new, -1, "-");
	if (f != NULL)
		*f= '\0';

	/* then remove any git suffix */
	f = g_strrstr_len (new, -1, ".2012");
	if (f != NULL)
		*f= '\0';

	return new;
}

/**
 * gs_main_is_pkg_installed_target:
 **/
static gboolean
gs_main_is_pkg_installed_target (PkPackage *pkg)
{
	gboolean ret;
	ret = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (pkg),
						  "gnome-software::target-installed"));
	return ret;
}

typedef struct {
	GsAppWidget	*app_widget;
	GsMainPrivate	*priv;
	GsAppWidgetKind	 original_kind;
	const gchar	*package_id;
} GsMainMethodData;

/**
 * gs_main_remove_packages_cb:
 **/
static void
gs_main_remove_packages_cb (PkClient *client,
			    GAsyncResult *res,
			    GsMainMethodData *data)
{
	GError *error = NULL;
	GPtrArray *array = NULL;
	guint i;
	PkError *error_code = NULL;
	PkPackage *package;
	PkResults *results;
	GsAppWidget *app_widget;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		/* reset this back to what it was before */
		gs_app_widget_set_kind (data->app_widget, data->original_kind);
		g_warning ("failed to remove packages: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		/* reset this back to what it was before */
		gs_app_widget_set_kind (data->app_widget, data->original_kind);
		g_warning ("failed to remove packages: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	array = pk_results_get_package_array (results);
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("removed %s", pk_package_get_id (package));
		app_widget = gs_main_get_app_widget_for_id (data->priv->list_box_installed,
							    pk_package_get_id (package));
		if (app_widget != NULL) {
			gtk_container_remove (GTK_CONTAINER (data->priv->list_box_installed),
					      GTK_WIDGET (app_widget));
		}
		app_widget = gs_main_get_app_widget_for_id (data->priv->list_box_updates,
							    pk_package_get_id (package));
		if (app_widget != NULL) {
			gtk_container_remove (GTK_CONTAINER (data->priv->list_box_updates),
					      GTK_WIDGET (app_widget));
		}
	}
out:
	g_free (data);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gs_main_app_widget_button_clicked_cb:
 **/
static void
gs_main_app_widget_button_clicked_cb (GsAppWidget *app_widget, GsMainPrivate *priv)
{
	const gchar *package_id;
	GsAppWidgetKind kind;
	const gchar *to_array[] = { NULL, NULL };
	GsMainMethodData *data;

	kind = gs_app_widget_get_kind (app_widget);
	package_id = gs_app_widget_get_id (app_widget);

	/* save, so we can recover a failed action */
	data = g_new0 (GsMainMethodData, 1);
	data->original_kind = kind;
	data->app_widget = app_widget;
	data->package_id = package_id;
	data->priv = priv;

	if (kind == GS_APP_WIDGET_KIND_UPDATE) {
		g_debug ("update %s", package_id);
		to_array[0] = package_id;
		pk_task_update_packages_async (priv->task,
					       (gchar**)to_array,
					       priv->cancellable,
					       (PkProgressCallback) gs_main_progress_cb,
					       priv,
					       (GAsyncReadyCallback) gs_main_remove_packages_cb,
					       data);
	} else if (kind == GS_APP_WIDGET_KIND_INSTALL) {
		g_debug ("install %s", package_id);
		to_array[0] = package_id;
		pk_task_install_packages_async (priv->task,
					        (gchar**)to_array,
					        priv->cancellable,
					        (PkProgressCallback) gs_main_progress_cb,
					        priv,
					        (GAsyncReadyCallback) gs_main_remove_packages_cb,
					        data);
	} else if (kind == GS_APP_WIDGET_KIND_REMOVE) {
		g_debug ("remove %s", package_id);
		to_array[0] = package_id;
		pk_task_remove_packages_async (priv->task,
					       (gchar**)to_array,
					       FALSE, /* allow deps */
					       FALSE, /* autoremove */
					       priv->cancellable,
					       (PkProgressCallback) gs_main_progress_cb,
					       priv,
					       (GAsyncReadyCallback) gs_main_remove_packages_cb,
					       data);
	}
	gs_app_widget_set_kind (app_widget, GS_APP_WIDGET_KIND_BUSY);
//	gs_app_widget_set_status (app_widget, "Installing...");
}

/**
 * gs_main_installed_add_package:
 **/
static void
gs_main_installed_add_package (GsMainPrivate *priv, PkPackage *pkg)
{
	const gchar *description;
	EggListBox *list_box;
	gboolean target_installed;
	gchar *tmp;
	gchar *update_changelog = NULL;
	gchar *update_text = NULL;
	GdkPixbuf *pixbuf;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_new"));
	pixbuf = gtk_widget_render_icon_pixbuf (widget,
						"icon-missing",
						priv->custom_icon_size);

	widget = gs_app_widget_new ();
	g_signal_connect (widget, "button-clicked",
			  G_CALLBACK (gs_main_app_widget_button_clicked_cb),
			  priv);
	target_installed = gs_main_is_pkg_installed_target (pkg);
	if (target_installed) {
		list_box = priv->list_box_installed;
		gs_app_widget_set_kind (GS_APP_WIDGET (widget),
					GS_APP_WIDGET_KIND_REMOVE);
	} else {
		list_box = priv->list_box_updates;
		gs_app_widget_set_kind (GS_APP_WIDGET (widget),
					GS_APP_WIDGET_KIND_UPDATE);
	}
	tmp = gs_main_get_pretty_version (pk_package_get_version (pkg));

	/* try to get update data if it's present */
	g_object_get (pkg,
		      "update-text", &update_text,
		      "update-changelog", &update_changelog,
		      NULL);
	if (update_text != NULL)
		description = update_text;
	else if (update_changelog != NULL)
		description = update_changelog;
	else
		description = pk_package_get_summary (pkg);
	gs_app_widget_set_description (GS_APP_WIDGET (widget), description);
	gs_app_widget_set_id (GS_APP_WIDGET (widget), pk_package_get_id (pkg));
	gs_app_widget_set_name (GS_APP_WIDGET (widget), pk_package_get_summary (pkg));
	gs_app_widget_set_pixbuf (GS_APP_WIDGET (widget), pixbuf);
	gs_app_widget_set_version (GS_APP_WIDGET (widget), tmp);
	gtk_container_add (GTK_CONTAINER (list_box), widget);
	gtk_widget_show (widget);
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	g_free (update_text);
	g_free (update_changelog);
	g_free (tmp);
}

/**
 * gs_main_installed_add_desktop_file:
 **/
static void
gs_main_installed_add_desktop_file (GsMainPrivate *priv,
				    PkPackage *pkg,
				    const gchar *desktop_file)
{
	EggListBox *list_box;
	gboolean ret;
	gboolean target_installed;
	gchar *comment = NULL;
	gchar *icon = NULL;
	gchar *name = NULL;
	gchar *version_tmp = NULL;
	GdkPixbuf *pixbuf = NULL;
	GError *error = NULL;
	GKeyFile *key_file;
	GtkWidget *widget;

	/* load desktop file */
	key_file = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file,
					 desktop_file,
					 G_KEY_FILE_NONE,
					 &error);
	if (!ret) {
		g_warning ("failed to get files for %s: %s",
			   pk_package_get_id (pkg),
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* get desktop data */
	name = g_key_file_get_string (key_file,
				      G_KEY_FILE_DESKTOP_GROUP,
				      G_KEY_FILE_DESKTOP_KEY_NAME,
				      NULL);
	if (name == NULL)
		name = g_strdup (pk_package_get_name (pkg));
	icon = g_key_file_get_string (key_file,
				      G_KEY_FILE_DESKTOP_GROUP,
				      G_KEY_FILE_DESKTOP_KEY_ICON,
				      NULL);
	if (icon == NULL)
		icon = g_strdup (GTK_STOCK_MISSING_IMAGE);

	/* prefer the update text */
	g_object_get (pkg,
		      "update-text", &comment,
		      NULL);
	if (comment == NULL) {
		g_object_get (pkg,
			      "update-changelog", &comment,
			      NULL);
	}
	if (comment == NULL) {
		comment = g_key_file_get_string (key_file,
						 G_KEY_FILE_DESKTOP_GROUP,
						 G_KEY_FILE_DESKTOP_KEY_COMMENT,
						 NULL);
	}
	if (comment == NULL)
		comment = g_strdup (pk_package_get_summary (pkg));

	/* load icon */
	if (icon != NULL && icon[0] == '/') {
		pixbuf = gdk_pixbuf_new_from_file_at_size (icon,
							   GS_MAIN_ICON_SIZE,
							   GS_MAIN_ICON_SIZE,
							   &error);
	} else {
		pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						   icon,
						   GS_MAIN_ICON_SIZE,
						   GTK_ICON_LOOKUP_USE_BUILTIN |
						   GTK_ICON_LOOKUP_FORCE_SIZE,
						   &error);
		if (pixbuf == NULL) {
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_new"));
			pixbuf = gtk_widget_render_icon_pixbuf (widget,
								icon,
								priv->custom_icon_size);
		}
	}
	if (pixbuf == NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_new"));
		pixbuf = gtk_widget_render_icon_pixbuf (widget,
							GTK_STOCK_MISSING_IMAGE,
							priv->custom_icon_size);
		g_warning ("Failed to open theme icon or builtin %s: %s",
			   icon,
			   error->message);
		g_error_free (error);
	}

	/* add to list store */
	widget = gs_app_widget_new ();
	g_signal_connect (widget, "button-clicked",
			  G_CALLBACK (gs_main_app_widget_button_clicked_cb),
			  priv);
	target_installed = gs_main_is_pkg_installed_target (pkg);
	if (target_installed) {
		list_box = priv->list_box_installed;
		gs_app_widget_set_kind (GS_APP_WIDGET (widget),
					GS_APP_WIDGET_KIND_REMOVE);
	} else {
		list_box = priv->list_box_updates;
		gs_app_widget_set_kind (GS_APP_WIDGET (widget),
					GS_APP_WIDGET_KIND_UPDATE);
	}
	version_tmp = gs_main_get_pretty_version (pk_package_get_version (pkg));
	gs_app_widget_set_description (GS_APP_WIDGET (widget), comment);
	gs_app_widget_set_id (GS_APP_WIDGET (widget), pk_package_get_id (pkg));
	gs_app_widget_set_name (GS_APP_WIDGET (widget), name);
	gs_app_widget_set_pixbuf (GS_APP_WIDGET (widget), pixbuf);
	gs_app_widget_set_version (GS_APP_WIDGET (widget), version_tmp);
	gtk_container_add (GTK_CONTAINER (list_box), widget);
	gtk_widget_show (widget);
out:
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	g_key_file_unref (key_file);
	g_free (name);
	g_free (comment);
	g_free (icon);
	g_free (version_tmp);
}

/**
 * gs_main_installed_add_os_update:
 **/
static void
gs_main_installed_add_os_update (GsMainPrivate *priv, PkPackage *pkg)
{
	GdkPixbuf *pixbuf = NULL;
	GError *error = NULL;

	/* try to find existing OS Update entry */
	if (priv->os_update_widget != NULL) {
		gs_app_widget_set_name (GS_APP_WIDGET (priv->os_update_widget), _("OS Updates"));
		goto out;
	}

	/* add OS Update entry */
	pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					   "software-update-available-symbolic",
					   GS_MAIN_ICON_SIZE,
					   GTK_ICON_LOOKUP_USE_BUILTIN |
					   GTK_ICON_LOOKUP_FORCE_SIZE,
					   &error);
	if (pixbuf == NULL) {
		g_warning ("Failed to find software-update-available-symbolic: %s",
			   error->message);
		g_error_free (error);
	}

	priv->os_update_widget = gs_app_widget_new ();
	g_signal_connect (priv->os_update_widget, "button-clicked",
			  G_CALLBACK (gs_main_app_widget_button_clicked_cb),
			  priv);
	gs_app_widget_set_kind (GS_APP_WIDGET (priv->os_update_widget),
				GS_APP_WIDGET_KIND_UPDATE);
	gs_app_widget_set_id (GS_APP_WIDGET (priv->os_update_widget), "");
	gs_app_widget_set_name (GS_APP_WIDGET (priv->os_update_widget), _("OS Update"));
	gs_app_widget_set_description (GS_APP_WIDGET (priv->os_update_widget),
				       _("Includes performance, stability and security improvements for all users"));
	gs_app_widget_set_pixbuf (GS_APP_WIDGET (priv->os_update_widget), pixbuf);
	gs_app_widget_set_version (GS_APP_WIDGET (priv->os_update_widget), "Version 3.4.3");
	/* TRANSLATORS: the update requires the user to reboot the computer */
	gs_app_widget_set_status (GS_APP_WIDGET (priv->os_update_widget), _("Requires restart"));
	gtk_container_add (GTK_CONTAINER (priv->list_box_updates), priv->os_update_widget);
	gtk_widget_show_all (priv->os_update_widget);
	g_object_add_weak_pointer (G_OBJECT (priv->os_update_widget),
				   (gpointer *) &priv->os_update_widget);
out:
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
}

/**
 * gs_main_installed_add_package:
 **/
static void
gs_main_installed_add_item (GsMainPrivate *priv, PkPackage *pkg)
{
	const gchar *desktop_file;
	gboolean target_installed;
	GError *error = NULL;
	GPtrArray *files = NULL;
	guint i;

	/* try to get the list of desktop files for this package */
	files = pk_desktop_get_shown_for_package (priv->desktop,
						  pk_package_get_name (pkg),
						  &error);
	if (files == NULL) {
		g_warning ("failed to get files for %s: %s",
			   pk_package_get_id (pkg),
			   error->message);
		g_error_free (error);
		gs_main_installed_add_package (priv, pkg);
		goto out;
	}
	if (files->len == 0) {
		g_debug ("not an application %s",
			 pk_package_get_id (pkg));
		target_installed = gs_main_is_pkg_installed_target (pkg);
		if (!target_installed)
//			gs_main_installed_add_package (priv, pkg);
//		else
			gs_main_installed_add_os_update (priv, pkg);
		goto out;
	}

	/* add each of the desktop files */
	for (i = 0; i < files->len; i++) {
		desktop_file = g_ptr_array_index (files, i);
		gs_main_installed_add_desktop_file (priv,
						    pkg,
						    desktop_file);
	}
out:
	if (files != NULL)
		g_ptr_array_unref (files);
}

/**
 * _gtk_container_remove_all_cb:
 **/
static void
_gtk_container_remove_all_cb (GtkWidget *widget, gpointer user_data)
{
	GtkContainer *container = GTK_CONTAINER (user_data);
	gtk_container_remove (container, widget);
}

/**
 * _gtk_container_remove_all:
 **/
static void
_gtk_container_remove_all (GtkContainer *container)
{
	gtk_container_foreach (container,
			       _gtk_container_remove_all_cb,
			       container);
}

/**
 * gs_main_get_update_details_cb:
 **/
static void
gs_main_get_update_details_cb (PkPackageSack *sack,
				GAsyncResult *res,
				GsMainPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array = NULL;
	guint i;
	PkPackage *package;

	/* add packages */
	ret = pk_package_sack_merge_generic_finish (sack, res, &error);
	if (!ret) {
		g_warning ("failed to get-update-details: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* clear existing updates */
	_gtk_container_remove_all (GTK_CONTAINER (priv->list_box_updates));
	array = pk_package_sack_get_array (sack);
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("add update %s", pk_package_get_id (package));
		gs_main_installed_add_item (priv, package);
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * gs_main_get_updates_cb:
 **/
static void
gs_main_get_updates_cb (PkClient *client,
			GAsyncResult *res,
			GsMainPrivate *priv)
{
	GError *error = NULL;
	PkError *error_code = NULL;
	PkPackageSack *sack = NULL;
	PkResults *results;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get-updates: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get-packages: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		goto out;
	}

	/* get the update details */
	sack = pk_results_get_package_sack (results);
	pk_package_sack_get_update_detail_async (sack,
						 priv->cancellable,
						 (PkProgressCallback) gs_main_progress_cb, priv,
						 (GAsyncReadyCallback) gs_main_get_update_details_cb, priv);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (sack != NULL)
		g_object_unref (sack);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gs_main_get_packages_cb:
 **/
static void
gs_main_get_packages_cb (PkClient *client,
			 GAsyncResult *res,
			 GsMainPrivate *priv)
{
	GError *error = NULL;
	GPtrArray *array = NULL;
	GtkWidget *widget;
	guint i;
	PkError *error_code = NULL;
	PkPackage *package;
	PkResults *results;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get-packages: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get-packages: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	if (pk_results_get_role (results) == PK_ROLE_ENUM_GET_PACKAGES)
		_gtk_container_remove_all (GTK_CONTAINER (priv->list_box_installed));
	array = pk_results_get_package_array (results);
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_object_set_data (G_OBJECT (package),
				   "gnome-software::target-installed",
				   GINT_TO_POINTER (TRUE));
		g_debug ("add %s", pk_package_get_id (package));
		gs_main_installed_add_item (priv, package);
	}

	/* focus back to the text extry */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	gtk_widget_grab_focus (widget);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gs_main_get_installed_packages:
 **/
static void
gs_main_get_installed_packages (GsMainPrivate *priv)
{
	PkBitfield filter;
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
					 PK_FILTER_ENUM_NEWEST,
					 PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 -1);
	pk_client_get_packages_async (PK_CLIENT(priv->task),
				      filter,
				      priv->cancellable,
				      (PkProgressCallback) gs_main_progress_cb, priv,
				      (GAsyncReadyCallback) gs_main_get_packages_cb, priv);
}

/**
 * gs_main_get_updates:
 **/
static void
gs_main_get_updates (GsMainPrivate *priv)
{
	PkBitfield filter;
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH,
					 -1);
	pk_client_get_updates_async (PK_CLIENT(priv->task),
				     filter,
				     priv->cancellable,
				     (PkProgressCallback) gs_main_progress_cb, priv,
				     (GAsyncReadyCallback) gs_main_get_updates_cb, priv);
}

/**
 * gs_main_get_popular:
 **/
static void
gs_main_get_popular (GsMainPrivate *priv)
{
	PkBitfield filter;
//	const gchar *packages[] = { "firefox", "gimp", "xchat", NULL };
	const gchar *packages[] = { "transmission-gtk", "cheese", "inkscape", "sound-juicer", "gedit", NULL };
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH,
					 PK_FILTER_ENUM_APPLICATION,
					 PK_FILTER_ENUM_NEWEST,
					 -1);
	pk_client_resolve_async (PK_CLIENT(priv->task),
				 filter,
				 (gchar **) packages,
				 priv->cancellable,
				 (PkProgressCallback) gs_main_progress_cb, priv,
				 (GAsyncReadyCallback) gs_main_get_packages_cb, priv);
}


/**
 * gs_main_set_overview_mode_ui:
 **/
static void
gs_main_set_overview_mode_ui (GsMainPrivate *priv, GsMainMode mode)
{
	GtkWidget *widget;

	if (priv->ignore_primary_buttons)
		return;

	priv->ignore_primary_buttons = TRUE;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_new"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_MAIN_MODE_NEW);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_MAIN_MODE_INSTALLED);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), mode == GS_MAIN_MODE_UPDATES);
	priv->ignore_primary_buttons = FALSE;

	widget = NULL;

	switch (mode) {
	case GS_MAIN_MODE_NEW:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_entry_set_text (GTK_ENTRY (widget), "");
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_stop (GTK_SPINNER (widget));
		break;
	case GS_MAIN_MODE_INSTALLED:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_entry_set_text (GTK_ENTRY (widget), "");
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_stop (GTK_SPINNER (widget));
		break;
	case GS_MAIN_MODE_UPDATES:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_all"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_stop (GTK_SPINNER (widget));
		break;
	case GS_MAIN_MODE_WAITING:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_start (GTK_SPINNER (widget));
		break;
	default:
		g_assert_not_reached ();
	}


	/* set panel */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_main"));
	gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), mode);
}

/**
 * gs_main_set_overview_mode:
 **/
static void
gs_main_set_overview_mode (GsMainPrivate *priv, GsMainMode mode)
{
	/* set controls */
	gs_main_set_overview_mode_ui (priv, mode);

	/* do action for mode */
	priv->mode = mode;
	switch (mode) {
	case GS_MAIN_MODE_NEW:
		gs_main_get_popular (priv);
		break;
	case GS_MAIN_MODE_INSTALLED:
		gs_main_get_installed_packages (priv);
		break;
	case GS_MAIN_MODE_UPDATES:
		gs_main_get_updates (priv);
		break;
	case GS_MAIN_MODE_WAITING:
		gs_main_get_updates (priv);
		break;
	default:
		g_assert_not_reached ();
	}
}

/**
 * gs_main_overview_button_cb:
 **/
static void
gs_main_overview_button_cb (GtkWidget *widget, GsMainPrivate *priv)
{
	GsMainMode mode;
	mode = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
						   "gnome-software::overview-mode"));
	gs_main_set_overview_mode (priv, mode);
}

/**
 * gs_main_setup_featured:
 **/
static void
gs_main_setup_featured (GsMainPrivate *priv)
{
	GError *error = NULL;
	GdkPixbuf *pixbuf;
	GtkImage *image;

	/* 1 : TODO: generate these automatically */
	image = GTK_IMAGE (gtk_builder_get_object (priv->builder, "image_featured1"));
	pixbuf = gdk_pixbuf_new_from_file_at_scale (DATADIR "/gnome-software/featured-firefox.png", -1, -1, TRUE, &error);
	if (pixbuf == NULL) {
		g_warning ("failed to load featured tile: %s", error->message);
		g_error_free (error);
		goto out;
	}
	gtk_image_set_from_pixbuf (image, pixbuf);
	g_object_unref (pixbuf);

	/* 2 */
	image = GTK_IMAGE (gtk_builder_get_object (priv->builder, "image_featured2"));
	pixbuf = gdk_pixbuf_new_from_file_at_scale (DATADIR "/gnome-software/featured-gimp.png", -1, -1, TRUE, &error);
	if (pixbuf == NULL) {
		g_warning ("failed to load featured tile: %s", error->message);
		g_error_free (error);
		goto out;
	}
	gtk_image_set_from_pixbuf (image, pixbuf);
	g_object_unref (pixbuf);

	/* 3 */
	image = GTK_IMAGE (gtk_builder_get_object (priv->builder, "image_featured3"));
	pixbuf = gdk_pixbuf_new_from_file_at_scale (DATADIR "/gnome-software/featured-xchat.png", -1, -1, TRUE, &error);
	if (pixbuf == NULL) {
		g_warning ("failed to load featured tile: %s", error->message);
		g_error_free (error);
		goto out;
	}
	gtk_image_set_from_pixbuf (image, pixbuf);
	g_object_unref (pixbuf);
out:
	return;
}

/**
 * gs_main_utf8_filter_helper:
 **/
static gboolean
gs_main_utf8_filter_helper (const gchar *haystack, const gchar *needle_utf8)
{
	gboolean ret;
	gchar *haystack_utf8;
	haystack_utf8 = g_utf8_casefold (haystack, -1);
	ret = strstr (haystack_utf8, needle_utf8) != NULL;
	g_free (haystack_utf8);
	return ret;
}

/**
 * gs_main_installed_filter_func:
 **/
static gboolean
gs_main_installed_filter_func (GtkWidget *child, void *user_data)
{
	const gchar *tmp;
	GtkWidget *widget;
	GsMainPrivate *priv = (GsMainPrivate *) user_data;
	GsAppWidget *app_widget = GS_APP_WIDGET (child);
	gchar *needle_utf8 = NULL;
	gboolean ret = TRUE;

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	tmp = gtk_entry_get_text (GTK_ENTRY (widget));
	if (tmp[0] == '\0')
		goto out;

	needle_utf8 = g_utf8_casefold (tmp, -1);
	ret = gs_main_utf8_filter_helper (gs_app_widget_get_name (app_widget),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_main_utf8_filter_helper (gs_app_widget_get_description (app_widget),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_main_utf8_filter_helper (gs_app_widget_get_version (app_widget),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_main_utf8_filter_helper (gs_app_widget_get_id (app_widget),
					  needle_utf8);
	if (ret)
		goto out;
out:
	g_free (needle_utf8);
	return ret;
}


/**
 * gs_main_filter_text_changed_cb:
 **/
static gboolean
gs_main_filter_text_changed_cb (GtkEntry *entry, GsMainPrivate *priv)
{
	egg_list_box_refilter (priv->list_box_installed);
	egg_list_box_refilter (priv->list_box_updates);
	return FALSE;
}

/**
 * gs_main_installed_sort_func:
 **/
static gint
gs_main_installed_sort_func (gconstpointer a,
			     gconstpointer b,
			     gpointer user_data)
{
	GsAppWidget *aw1 = GS_APP_WIDGET (a);
	GsAppWidget *aw2 = GS_APP_WIDGET (b);
	return g_strcmp0 (gs_app_widget_get_name (aw1),
			  gs_app_widget_get_name (aw2));
}

/**
 * gs_main_startup_cb:
 **/
static void
gs_main_startup_cb (GApplication *application, GsMainPrivate *priv)
{
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;
	GtkWidget *widget;

	/* get CSS */
	if (priv->provider == NULL) {
		priv->provider = gtk_css_provider_new ();


		gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
							   GTK_STYLE_PROVIDER (priv->provider),
							   G_MAXUINT);

		gtk_css_provider_load_from_path (priv->provider, CSS_FILE, &error);
		if (error != NULL) {
			g_warning ("Error loading stylesheet from file %s. %s", CSS_FILE, error->message);
			g_error_free (error);
			error = NULL;
		}
	}


	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_resource (priv->builder,
						"/org/gnome/software/gnome-software.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GS_DATA G_DIR_SEPARATOR_S "icons");

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_software"));
	gtk_application_add_window (priv->application, GTK_WINDOW (main_window));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_default_size (GTK_WINDOW (main_window), 1200, 400);

	/* setup callbacks */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_main"));
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (widget), FALSE);
	gs_main_set_overview_mode (priv, GS_MAIN_MODE_INSTALLED);

	/* set up popular icon vew */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "iconview_popular"));
	gtk_icon_view_set_markup_column (GTK_ICON_VIEW (widget), COLUMN_POPULAR_MARKUP);
	gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (widget), COLUMN_POPULAR_PIXBUF);

	/* setup featured tiles */
	gs_main_setup_featured (priv);

	/* setup installed */
	priv->list_box_installed = egg_list_box_new ();
	egg_list_box_set_filter_func (priv->list_box_installed,
				      gs_main_installed_filter_func,
				      priv,
				      NULL);
	egg_list_box_set_sort_func (priv->list_box_installed,
				    gs_main_installed_sort_func,
				    priv,
				    NULL);
	egg_list_box_set_selection_mode (priv->list_box_installed,
					 GTK_SELECTION_NONE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_install"));
	egg_list_box_add_to_scrolled (priv->list_box_installed,
				      GTK_SCROLLED_WINDOW (widget));
	gtk_widget_show (GTK_WIDGET (priv->list_box_installed));

	/* setup updates */
	priv->list_box_updates = egg_list_box_new ();
	egg_list_box_set_selection_mode (priv->list_box_updates,
					 GTK_SELECTION_NONE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	egg_list_box_add_to_scrolled (priv->list_box_updates,
				      GTK_SCROLLED_WINDOW (widget));
	gtk_widget_show (GTK_WIDGET (priv->list_box_updates));

	/* setup buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_new"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_MAIN_MODE_NEW));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_overview_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_MAIN_MODE_INSTALLED));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_overview_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	g_object_set_data (G_OBJECT (widget),
			   "gnome-software::overview-mode",
			   GINT_TO_POINTER (GS_MAIN_MODE_UPDATES));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_overview_button_cb), priv);

	/* refilter on search box changing */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (GTK_EDITABLE (widget), "changed",
			  G_CALLBACK (gs_main_filter_text_changed_cb), priv);

	/* show main UI */
	gtk_widget_show (main_window);
out:
	return;
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	GsMainPrivate *priv = NULL;
	GOptionContext *context;
	int status = 0;
	gboolean ret;
	GError *error = NULL;

	const GOptionEntry options[] = {
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	context = g_option_context_new ("gnome-color-manager profile main");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_warning ("failed to parse options: %s", error->message);
		g_error_free (error);
		goto out;
	}
	g_option_context_free (context);

	priv = g_new0 (GsMainPrivate, 1);
	priv->ignore_primary_buttons = FALSE;

	/* we want the large icon size according to the width of the window */
	priv->custom_icon_size = gtk_icon_size_register ("custom",
							 GS_MAIN_ICON_SIZE,
							 GS_MAIN_ICON_SIZE);

	/* ensure single instance */
	priv->application = gtk_application_new ("org.gnome.Software", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (gs_main_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (gs_main_activate_cb), priv);

	/* use PackageKit */
	priv->cancellable = g_cancellable_new ();
	priv->task = pk_task_new ();
	g_object_set (priv->task,
		      "background", FALSE,
		      NULL);

	/* get localized data from sqlite database */
	priv->desktop = pk_desktop_new ();
	ret = pk_desktop_open_database (priv->desktop, &error);
	if (!ret) {
		g_warning ("failed to open database: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get localized data from sqlite database */
	priv->desktop = pk_desktop_new ();
	ret = pk_desktop_open_database (priv->desktop, NULL);
	if (!ret)
		g_warning ("Failure opening database");

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

out:
	if (priv != NULL) {
		g_object_unref (priv->task);
		g_object_unref (priv->desktop);
		g_object_unref (priv->cancellable);
		g_object_unref (priv->application);
		if (priv->provider != NULL)
			g_object_unref (priv->provider);
		if (priv->builder != NULL)
			g_object_unref (priv->builder);
		if (priv->waiting_tab_id > 0)
			g_source_remove (priv->waiting_tab_id);
		g_free (priv);
	}
	return status;
}
