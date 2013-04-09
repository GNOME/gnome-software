/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2013 Richard Hughes <richard@hughsie.com>
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
#include "gs-resources.h"
#include "gs-plugin-loader.h"

#define	GS_MAIN_ICON_SIZE	64

typedef enum {
	GS_MAIN_MODE_NEW,
	GS_MAIN_MODE_INSTALLED,
	GS_MAIN_MODE_UPDATES,
	GS_MAIN_MODE_WAITING,
	GS_MAIN_MODE_DETAILS
} GsMainMode;

enum {
	COLUMN_POPULAR_APP,
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
	PkTask			*task;
	guint			 waiting_tab_id;
	EggListBox		*list_box_installed;
	EggListBox		*list_box_updates;
	GtkCssProvider		*provider;
	gboolean		ignore_primary_buttons;
	GsPluginLoader		*plugin_loader;
	guint			 tab_back_id;
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
	GsApp *app;

	/* look for this widget */
	list = gtk_container_get_children (GTK_CONTAINER (list_box));
	for (l = list; l != NULL; l = l->next) {
		tmp = GS_APP_WIDGET (l->data);
		app = gs_app_widget_get_app (tmp);
		if (g_strcmp0 (gs_app_get_id (app), id) == 0)
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

	/* don't flicker between GetUpdates and GetUpdateDetails */
	g_object_get (progress,
		      "status", &status,
		      "role", &role,
		      NULL);
	if (role == PK_ROLE_ENUM_GET_UPDATES &&
	    status == PK_STATUS_ENUM_FINISHED) {
		return;
	}

	/* action item, so no waiting panel */
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
 * gs_main_progress_cb:
 **/
static void
gs_main_plugin_loader_status_changed_cb (GsPluginLoader *plugin_loader,
					 GsApp *app,
					 GsPluginStatus status,
					 GsMainPrivate	*priv)
{
	GtkWidget *widget;
	const gchar *status_text = NULL;

	/* translate */
	switch (status) {
	case GS_PLUGIN_STATUS_WAITING:
		/* TRANSLATORS: we're waiting for something to happen */
		status_text = _("Waiting...");
		break;
	case GS_PLUGIN_STATUS_SETUP:
		/* TRANSLATORS: we're waiting for something to happen */
		status_text = _("Setting up...");
		break;
	case GS_PLUGIN_STATUS_DOWNLOADING:
		/* TRANSLATORS: we're waiting for something to happen */
		status_text = _("Downloading...");
		break;
	case GS_PLUGIN_STATUS_QUERYING:
		/* TRANSLATORS: we're waiting for something to happen */
		status_text = _("Querying...");
		break;
	default:
		break;
	}

	/* update the label */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_waiting"));
	if (status_text != NULL) {
		gtk_label_set_markup (GTK_LABEL (widget), status_text);
		gtk_widget_show (widget);
	} else {
		gtk_widget_hide (widget);
	}

	/* show the waiting panel if the delay is significant */
	if (status == GS_PLUGIN_STATUS_FINISHED) {
		gs_main_set_overview_mode_ui (priv, priv->mode);
		if (priv->waiting_tab_id > 0) {
			g_source_remove (priv->waiting_tab_id);
			priv->waiting_tab_id = 0;
		}
	} else {
		if (priv->waiting_tab_id == 0) {
			priv->waiting_tab_id = g_timeout_add (50,
							      gs_main_show_waiting_tab_cb,
							      priv);
		}
	}
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
	GsApp *app;
	const gchar *to_array[] = { NULL, NULL };
	GsMainMethodData *data;

	kind = gs_app_widget_get_kind (app_widget);
	app = gs_app_widget_get_app (app_widget);
	package_id = gs_app_get_id (app);

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

#if 0
/**
 * gs_main_installed_add_os_update:
 **/
static void
gs_main_installed_add_os_update (GsMainPrivate *priv, PkPackage *pkg)
{
	/* TRANSLATORS: the update requires the user to reboot the computer */
	gs_app_widget_set_status (GS_APP_WIDGET (widget), _("Requires restart"));
}
#endif

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
 * gs_main_get_updates_cb:
 **/
static void
gs_main_get_updates_cb (GsPluginLoader *plugin_loader,
			GAsyncResult *res,
			GsMainPrivate *priv)
{
	GError *error = NULL;
	GList *list;
	GList *l;
	GsApp *app;
	GtkWidget *widget;

	/* get the results */
	list = gs_plugin_loader_get_updates_finish (plugin_loader, res, &error);
	if (list == NULL) {
		g_warning ("failed to get updates: %s", error->message);
		g_error_free (error);
		goto out;
	}
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		g_debug ("adding update %s", gs_app_get_id (app));
		widget = gs_app_widget_new ();
		g_signal_connect (widget, "button-clicked",
				  G_CALLBACK (gs_main_app_widget_button_clicked_cb),
				  priv);
		gs_app_widget_set_kind (GS_APP_WIDGET (widget),
					GS_APP_WIDGET_KIND_UPDATE);
		gs_app_widget_set_app (GS_APP_WIDGET (widget), app);
		gtk_container_add (GTK_CONTAINER (priv->list_box_updates), widget);
		gtk_widget_show (widget);
	}

	/* focus back to the text extry */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	gtk_widget_grab_focus (widget);
out:
	if (list != NULL)
		g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

/**
 * gs_main_get_installed_cb:
 **/
static void
gs_main_get_installed_cb (GObject *source_object,
			  GAsyncResult *res,
			  gpointer user_data)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	GsMainPrivate *priv = (GsMainPrivate *) user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *widget;

	list = gs_plugin_loader_get_installed_finish (plugin_loader,
						      res,
						      &error);
	if (list == NULL) {
		g_warning ("failed to get installed apps: %s", error->message);
		g_error_free (error);
		goto out;
	}
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		g_debug ("adding installed %s", gs_app_get_id (app));
		widget = gs_app_widget_new ();
		g_signal_connect (widget, "button-clicked",
				  G_CALLBACK (gs_main_app_widget_button_clicked_cb),
				  priv);
		gs_app_widget_set_kind (GS_APP_WIDGET (widget),
					gs_app_get_kind (app) == GS_APP_KIND_SYSTEM ? GS_APP_WIDGET_KIND_BLANK : GS_APP_WIDGET_KIND_REMOVE);
		gs_app_widget_set_app (GS_APP_WIDGET (widget), app);
		gtk_container_add (GTK_CONTAINER (priv->list_box_installed), widget);
		gtk_widget_show (widget);
	}

	/* focus back to the text extry */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	gtk_widget_grab_focus (widget);
out:
	return;
}

/**
 * gs_main_get_installed:
 **/
static void
gs_main_get_installed (GsMainPrivate *priv)
{
	/* remove old entries */
	_gtk_container_remove_all (GTK_CONTAINER (priv->list_box_installed));

	/* get popular apps */
	gs_plugin_loader_get_installed_async (priv->plugin_loader,
					      priv->cancellable,
					      gs_main_get_installed_cb,
					      priv);
}

/**
 * gs_main_get_updates:
 **/
static void
gs_main_get_updates (GsMainPrivate *priv)
{
	_gtk_container_remove_all (GTK_CONTAINER (priv->list_box_updates));
	gs_plugin_loader_get_updates_async (priv->plugin_loader,
					    priv->cancellable,
					    (GAsyncReadyCallback) gs_main_get_updates_cb, priv);
}


/**
 * gs_main_get_popular_cb:
 **/
static void
gs_main_get_popular_cb (GObject *source_object,
			GAsyncResult *res,
			gpointer user_data)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	GsMainPrivate *priv = (GsMainPrivate *) user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkListStore *liststore;
	GtkTreeIter iter;

	/* get popular apps */
	list = gs_plugin_loader_get_popular_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		g_warning ("failed to get popular apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	liststore = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_popular"));
	gtk_list_store_clear (liststore);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		g_debug ("adding popular %s", gs_app_get_id (app));
		gtk_list_store_append (liststore, &iter);
		gtk_list_store_set (liststore,
				    &iter,
				    COLUMN_POPULAR_APP, app,
				    COLUMN_POPULAR_MARKUP, gs_app_get_name (app),
				    COLUMN_POPULAR_PIXBUF, gs_app_get_pixbuf (app),
				    -1);
	}
out:
	return;
}

/**
 * gs_main_get_popular:
 **/
static void
gs_main_get_popular (GsMainPrivate *priv)
{
	GtkListStore *liststore;

	/* remove old entries */
	liststore = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_popular"));
	gtk_list_store_clear (liststore);

	/* get popular apps */
	gs_plugin_loader_get_popular_async (priv->plugin_loader,
					    priv->cancellable,
					    gs_main_get_popular_cb,
					    priv);
}

/**
 * gs_main_set_overview_mode_ui:
 **/
static void
gs_main_set_overview_mode_ui (GsMainPrivate *priv, GsMainMode mode)
{
	GtkWidget *widget;

	priv->ignore_primary_buttons = TRUE;

	switch (mode) {
	case GS_MAIN_MODE_DETAILS:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_detail"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_set_visible (widget, FALSE);
		break;
	case GS_MAIN_MODE_NEW:
	case GS_MAIN_MODE_INSTALLED:
	case GS_MAIN_MODE_UPDATES:
	case GS_MAIN_MODE_WAITING:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_detail"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
		gtk_widget_set_visible (widget, FALSE);
		break;
	default:
		break;
	}

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
	case GS_MAIN_MODE_DETAILS:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_detail_name"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_detail_summary"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_detail_description"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_detail_screenshot"));
		gtk_widget_hide (widget);
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
gs_main_set_overview_mode (GsMainPrivate *priv, GsMainMode mode, GsApp *app)
{
	GtkWidget *widget;
	const gchar *tmp;
	GdkPixbuf *pixbuf;
	gint rating;

	if (priv->ignore_primary_buttons)
		return;

	/* set controls */
	gs_main_set_overview_mode_ui (priv, mode);

	/* do action for mode */
	priv->mode = mode;
	switch (mode) {
	case GS_MAIN_MODE_NEW:
		gs_main_get_popular (priv);
		break;
	case GS_MAIN_MODE_INSTALLED:
		gs_main_get_installed (priv);
		break;
	case GS_MAIN_MODE_UPDATES:
		gs_main_get_updates (priv);
		break;
	case GS_MAIN_MODE_WAITING:
		gs_main_get_updates (priv);
		break;
	case GS_MAIN_MODE_DETAILS:

		tmp = gs_app_get_name (app);
		if (tmp != NULL && tmp[0] != '\0') {
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_detail_name"));
			gtk_label_set_label (GTK_LABEL (widget), tmp);
			gtk_widget_set_visible (widget, TRUE);
		}
		tmp = gs_app_get_summary (app);
		if (tmp != NULL && tmp[0] != '\0') {
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_detail_summary"));
			gtk_label_set_label (GTK_LABEL (widget), tmp);
			gtk_widget_set_visible (widget, TRUE);
		}
		tmp = NULL; // gs_app_get_description (app);
		if (tmp == NULL)
			tmp = _("The author of this software has not included a 'Description' in the desktop file...");
		if (tmp != NULL && tmp[0] != '\0') {
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_detail_description"));
			gtk_label_set_label (GTK_LABEL (widget), tmp);
			gtk_widget_set_visible (widget, TRUE);
		}
		pixbuf = gs_app_get_pixbuf (app);
		if (pixbuf != NULL) {
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_detail_icon"));
			gtk_image_set_from_pixbuf (GTK_IMAGE (widget), pixbuf);
			gtk_widget_set_visible (widget, TRUE);
		}
		tmp = gs_app_get_screenshot (app);
		if (tmp != NULL && tmp[0] != '\0') {
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_detail_screenshot"));
			pixbuf = gdk_pixbuf_new_from_file_at_size (tmp, 1000, 500, NULL);
			gtk_image_set_from_pixbuf (GTK_IMAGE (widget), pixbuf);
			g_object_unref (pixbuf);
			gtk_widget_set_visible (widget, TRUE);
		}

		/* show rating */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_detail_rating"));
		rating = gs_app_get_rating (app);
		if (rating < 20) {
			gtk_image_set_from_file (GTK_IMAGE (widget), DATADIR "/gnome-software/stars0.png");
		} else if (rating < 40) {
			gtk_image_set_from_file (GTK_IMAGE (widget), DATADIR "/gnome-software/stars1.png");
		} else if (rating < 60) {
			gtk_image_set_from_file (GTK_IMAGE (widget), DATADIR "/gnome-software/stars2.png");
		} else if (rating < 80) {
			gtk_image_set_from_file (GTK_IMAGE (widget), DATADIR "/gnome-software/stars3.png");
		} else {
			gtk_image_set_from_file (GTK_IMAGE (widget), DATADIR "/gnome-software/stars4.png");
		}

		/* add install button if available */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
		gtk_widget_set_visible (widget, gs_app_get_state (app) == GS_APP_STATE_AVAILABLE);
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
	gs_main_set_overview_mode (priv, mode, NULL);
}

/**
 * gs_main_back_button_cb:
 **/
static void
gs_main_back_button_cb (GtkWidget *widget, GsMainPrivate *priv)
{
	gs_main_set_overview_mode (priv, priv->tab_back_id, NULL);
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
 * gs_main_egg_list_separator_func
 **/
static void
gs_main_egg_list_separator_func (GtkWidget **separator,
				 GtkWidget *child,
				 GtkWidget *before,
				 gpointer user_data)
{
	/* first entry */
	if (before == NULL) {
		g_clear_object (separator);
		return;
	}

	if (*separator != NULL)
		return;

	*separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	g_object_ref_sink (*separator);
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
	GsApp *app;

	app = gs_app_widget_get_app (app_widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	tmp = gtk_entry_get_text (GTK_ENTRY (widget));
	if (tmp[0] == '\0')
		goto out;

	needle_utf8 = g_utf8_casefold (tmp, -1);
	ret = gs_main_utf8_filter_helper (gs_app_get_name (app),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_main_utf8_filter_helper (gs_app_get_summary (app),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_main_utf8_filter_helper (gs_app_get_version (app),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_main_utf8_filter_helper (gs_app_get_id (app),
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
	GsApp *a1 = gs_app_widget_get_app (aw1);
	GsApp *a2 = gs_app_widget_get_app (aw2);
	return g_strcmp0 (gs_app_get_name (a1),
			  gs_app_get_name (a2));
}

/**
 * gs_main_popular_activated_cb:
 **/
static void
gs_main_popular_activated_cb (GtkIconView *iconview, GtkTreePath *path, GsMainPrivate *priv)
{
	gboolean ret;
	GsApp *app;
	GtkTreeIter iter;
	GtkTreeModel *model;

	model = gtk_icon_view_get_model (iconview);
	ret = gtk_tree_model_get_iter_from_string (model, &iter, gtk_tree_path_to_string (path));
	if (!ret)
		return;

	gtk_tree_model_get (model, &iter,
			    COLUMN_POPULAR_APP, &app,
			    -1);
	g_debug ("show details with %s", gs_app_get_id (app));

	/* save current mode */
	priv->tab_back_id = priv->mode;

	/* switch to overview mode */
	gs_main_set_overview_mode (priv, GS_MAIN_MODE_DETAILS, app);
	g_object_unref (app);
}

/**
 * gs_main_startup_cb:
 **/
static void
gs_main_startup_cb (GApplication *application, GsMainPrivate *priv)
{
	GBytes *data = NULL;
	GError *error = NULL;
	gint retval;
	GtkWidget *main_window;
	GtkWidget *widget;
	gboolean ret;

	/* get CSS */
	if (priv->provider == NULL) {
		priv->provider = gtk_css_provider_new ();
		gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
							   GTK_STYLE_PROVIDER (priv->provider),
							   G_MAXUINT);
		data = g_resource_lookup_data (gs_get_resource (),
					       "/org/gnome/software/gtk-style.css",
					       G_RESOURCE_LOOKUP_FLAGS_NONE,
					       &error);
		if (data == NULL) {
			g_warning ("failed to load stylesheet data: %s",
				   error->message);
			g_error_free (error);
			goto out;
		}
		ret = gtk_css_provider_load_from_data (priv->provider,
						       g_bytes_get_data (data, NULL),
						       g_bytes_get_size (data),
						       &error);
		if (!ret) {
			g_warning ("failed to load stylesheet: %s",
				   error->message);
			g_error_free (error);
			goto out;
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
	gtk_window_set_default_size (GTK_WINDOW (main_window), 1200, 800);

	/* setup callbacks */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "notebook_main"));
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (widget), FALSE);

	/* set up popular icon vew */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "iconview_popular"));
	gtk_icon_view_set_markup_column (GTK_ICON_VIEW (widget), COLUMN_POPULAR_MARKUP);
	gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (widget), COLUMN_POPULAR_PIXBUF);
	gtk_icon_view_set_activate_on_single_click (GTK_ICON_VIEW (widget), TRUE);
	g_signal_connect (widget, "item-activated",
			  G_CALLBACK (gs_main_popular_activated_cb), priv);

	/* setup featured tiles */
	gs_main_setup_featured (priv);
	/* setup installed */
	priv->list_box_installed = egg_list_box_new ();
	egg_list_box_set_separator_funcs (priv->list_box_installed,
					  gs_main_egg_list_separator_func,
					  priv,
					  NULL);
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
	egg_list_box_set_separator_funcs (priv->list_box_updates,
					  gs_main_egg_list_separator_func,
					  priv,
					  NULL);
	egg_list_box_set_selection_mode (priv->list_box_updates,
					 GTK_SELECTION_NONE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	egg_list_box_add_to_scrolled (priv->list_box_updates,
				      GTK_SCROLLED_WINDOW (widget));
	gtk_widget_show (GTK_WIDGET (priv->list_box_updates));

	/* setup buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_back_button_cb), priv);
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

	/* show the status on a different page */
	g_signal_connect (priv->plugin_loader, "status-changed",
			  G_CALLBACK (gs_main_plugin_loader_status_changed_cb), priv);

	/* show main UI */
	gtk_widget_show (main_window);
	gs_main_set_overview_mode (priv, GS_MAIN_MODE_INSTALLED, NULL);
out:
	if (data != NULL)
		g_bytes_unref (data);
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	gboolean ret;
	GError *error = NULL;
	GOptionContext *context;
	GsMainPrivate *priv = NULL;
	int status = 0;

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

	/* load the plugins */
	priv->plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_location (priv->plugin_loader, NULL);
	ret = gs_plugin_loader_setup (priv->plugin_loader, &error);
	if (!ret) {
		g_warning ("Failed to setup plugins: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* FIXME: use GSettings key rather than hard-coding this */
	gs_plugin_loader_set_enabled (priv->plugin_loader, "hardcoded-kind", TRUE);
	gs_plugin_loader_set_enabled (priv->plugin_loader, "hardcoded-popular", TRUE);
	gs_plugin_loader_set_enabled (priv->plugin_loader, "hardcoded-ratings", TRUE);
	gs_plugin_loader_set_enabled (priv->plugin_loader, "hardcoded-screenshots", TRUE);
	gs_plugin_loader_set_enabled (priv->plugin_loader, "local-ratings", TRUE);
	gs_plugin_loader_set_enabled (priv->plugin_loader, "packagekit", TRUE);
	gs_plugin_loader_set_enabled (priv->plugin_loader, "desktopdb", TRUE);
	gs_plugin_loader_set_enabled (priv->plugin_loader, "datadir-apps", TRUE);
	gs_plugin_loader_set_enabled (priv->plugin_loader, "datadir-filename", TRUE);

	/* wait */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);
out:
	if (priv != NULL) {
		g_object_unref (priv->plugin_loader);
		g_object_unref (priv->task);
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
