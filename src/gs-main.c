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

#include "gs-app-widget.h"
#include "gs-resources.h"
#include "gs-plugin-loader.h"

typedef enum {
	GS_MAIN_MODE_NEW,
	GS_MAIN_MODE_INSTALLED,
	GS_MAIN_MODE_UPDATES,
	GS_MAIN_MODE_WAITING,
	GS_MAIN_MODE_DETAILS,
        GS_MAIN_MODE_CATEGORY
} GsMainMode;

enum {
	COLUMN_UPDATE_APP,
	COLUMN_UPDATE_NAME,
	COLUMN_UPDATE_VERSION,
	COLUMN_UPDATE_LAST
};

typedef struct {
	GCancellable		*cancellable;
	GsMainMode		 mode;
	GsMainMode		 app_startup_mode;
	GtkApplication		*application;
	GtkBuilder		*builder;
	PkTask			*task;
	guint			 waiting_tab_id;
	GtkListBox		*list_box_installed;
	GtkListBox		*list_box_updates;
	GtkCssProvider		*provider;
	gboolean		 ignore_primary_buttons;
	GsPluginLoader		*plugin_loader;
	guint			 tab_back_id;
        gint                     pending_apps;
} GsMainPrivate;

static void gs_main_set_overview_mode_ui (GsMainPrivate *priv, GsMainMode mode, GsApp *app);
static void gs_main_set_overview_mode (GsMainPrivate *priv, GsMainMode mode, GsApp *app, const gchar *category);
static void app_tile_clicked (GtkButton *button, gpointer data);

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
	gs_main_set_overview_mode_ui (priv, GS_MAIN_MODE_WAITING, NULL);
	priv->waiting_tab_id = 0;
	return FALSE;
}

/**
 * gs_main_get_app_widget_for_id:
 **/
static GsAppWidget *
gs_main_get_app_widget_for_id (GtkListBox *list_box, const gchar *id)
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
		gs_main_set_overview_mode_ui (priv, priv->mode, NULL);
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
		gs_main_set_overview_mode_ui (priv, priv->mode, NULL);
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

static void
update_pending_apps (GsMainPrivate *priv, gint delta)
{
        GtkWidget *widget;
        gchar *label;

        priv->pending_apps += delta;
        g_assert (priv->pending_apps >= 0);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_button_installed"));

        if (priv->pending_apps == 0)
                label = g_strdup (_("Installed"));
        else
                label = g_strdup_printf (_("Installed (%d)"), priv->pending_apps);

        gtk_label_set_label (GTK_LABEL (widget), label);
        g_free (label);
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

        update_pending_apps (data->priv, -1);

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
 * gs_main_set_updates_description_ui:
 **/
static void
gs_main_set_updates_description_ui (GsMainPrivate *priv, GsApp *app)
{
	gchar *tmp;
	GsAppKind kind;
	GtkWidget *widget;

	/* set window title */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	kind = gs_app_get_kind (app);
	if (kind == GS_APP_KIND_OS_UPDATE) {
		gtk_window_set_title (GTK_WINDOW (widget), gs_app_get_name (app));
	} else {
		tmp = g_strdup_printf ("%s %s",
				       gs_app_get_name (app),
				       gs_app_get_version (app));
		gtk_window_set_title (GTK_WINDOW (widget), tmp);
		g_free (tmp);
	}

	/* set update header */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_update_header"));
	gtk_widget_set_visible (widget, kind == GS_APP_KIND_NORMAL);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_details"));
	gtk_widget_set_visible (widget, kind != GS_APP_KIND_OS_UPDATE);
	gtk_label_set_label (GTK_LABEL (widget), gs_app_get_metadata_item (app, "update-details"));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_update_icon"));
	gtk_image_set_from_pixbuf (GTK_IMAGE (widget), gs_app_get_pixbuf (app));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_name"));
	gtk_label_set_label (GTK_LABEL (widget), gs_app_get_name (app));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_summary"));
	gtk_label_set_label (GTK_LABEL (widget), gs_app_get_summary (app));
}

/**
 * gs_main_updates_unselect_treeview_cb:
 **/
static gboolean
gs_main_updates_unselect_treeview_cb (gpointer user_data)
{
	GsMainPrivate *priv = (GsMainPrivate *) user_data;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_update"));
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (treeview));

	return FALSE;
}

/**
 * gs_main_update_row_activated_cb:
 **/
static void
gs_main_update_row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
				 GtkTreeViewColumn *col, GsMainPrivate *priv)
{
	gboolean ret;
	GsApp *app = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkWidget *widget;

	/* get selection */
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter (model, &iter, path);
	if (!ret) {
		g_warning ("failed to get selection");
		goto out;
	}

	/* get data */
	gtk_tree_model_get (model, &iter,
			    COLUMN_UPDATE_APP, &app,
			    -1);

	/* setup package view */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update"));
	gtk_widget_hide (widget);
	gs_main_set_updates_description_ui (priv, app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	gtk_widget_show (widget);
out:
	if (app != NULL)
		g_object_unref (app);
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
                update_pending_apps (data->priv, 1);
		g_debug ("update %s", package_id);
		to_array[0] = package_id;
		gs_app_widget_set_kind (app_widget, GS_APP_WIDGET_KIND_BUSY);
		gs_app_widget_set_status (app_widget, "Updating");
		pk_task_update_packages_async (priv->task,
					       (gchar**)to_array,
					       priv->cancellable,
					       (PkProgressCallback) gs_main_progress_cb,
					       priv,
					       (GAsyncReadyCallback) gs_main_remove_packages_cb,
					       data);
	} else if (kind == GS_APP_WIDGET_KIND_INSTALL) {
                update_pending_apps (data->priv, 1);
		g_debug ("install %s", package_id);
		to_array[0] = package_id;
		gs_app_widget_set_kind (app_widget, GS_APP_WIDGET_KIND_BUSY);
		gs_app_widget_set_status (app_widget, "Installing");
		pk_task_install_packages_async (priv->task,
					        (gchar**)to_array,
					        priv->cancellable,
					        (PkProgressCallback) gs_main_progress_cb,
					        priv,
					        (GAsyncReadyCallback) gs_main_remove_packages_cb,
					        data);
	} else if (kind == GS_APP_WIDGET_KIND_REMOVE) {

		GtkWidget *dialog;
		GtkWindow *window;
		GtkResponseType response;
		GString *markup;

		window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
		markup = g_string_new ("");
		g_string_append_printf (markup,
					_("Are you sure you want to remove %s?"),
					gs_app_get_name (app));
		g_string_prepend (markup, "<b>");
		g_string_append (markup, "</b>");
		dialog = gtk_message_dialog_new (window,
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_QUESTION,
						 GTK_BUTTONS_CANCEL,
						 NULL);
		gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), markup->str);
		gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
							    _("%s will be removed, and you will have to install it to use it again."),
							    gs_app_get_name (app));
		gtk_dialog_add_button (GTK_DIALOG (dialog), _("Remove"), GTK_RESPONSE_OK);
		response = gtk_dialog_run (GTK_DIALOG (dialog));
		if (response == GTK_RESPONSE_OK) {
                        update_pending_apps (data->priv, 1);
			g_debug ("remove %s", package_id);
			to_array[0] = package_id;
			gs_app_widget_set_kind (app_widget, GS_APP_WIDGET_KIND_BUSY);
			gs_app_widget_set_status (app_widget, "Removing");

			pk_task_remove_packages_async (priv->task,
						       (gchar**)to_array,
						       TRUE, /* allow deps */
						       FALSE, /* autoremove */
						       priv->cancellable,
						       (PkProgressCallback) gs_main_progress_cb,
						       priv,
						       (GAsyncReadyCallback) gs_main_remove_packages_cb,
						       data);
		}
		g_string_free (markup, TRUE);
		gtk_widget_destroy (dialog);
	}
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_updates_up_to_date"));
	gtk_widget_set_visible (widget, list == NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	gtk_widget_set_visible (widget, list != NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
	gtk_widget_set_visible (widget, list != NULL);
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

static void
app_tile_clicked (GtkButton *button, gpointer data)
{
        GsMainPrivate *priv = data;
        GsApp *app;

        app = g_object_get_data (G_OBJECT (button), "app");
        gs_main_set_overview_mode (priv, GS_MAIN_MODE_DETAILS, app, NULL);
}

static GtkWidget *
create_popular_tile (GsMainPrivate *priv, GsApp *app)
{
        GtkWidget *button, *frame, *box, *image, *label;
        GtkWidget *f;

        f = gtk_aspect_frame_new (NULL, 0.5, 0.5, 1, FALSE);
        gtk_frame_set_shadow_type (GTK_FRAME (f), GTK_SHADOW_NONE);
        button = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
        frame = gtk_aspect_frame_new (NULL, 0.5, 1, 1, FALSE);
        gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
        gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
        box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add (GTK_CONTAINER (frame), box);
        image = gtk_image_new_from_pixbuf (gs_app_get_pixbuf (app));
        g_object_set (box, "margin", 12, NULL);
        gtk_box_pack_start (GTK_BOX (box), image, FALSE, TRUE, 0);
        label = gtk_label_new (gs_app_get_name (app));
        g_object_set (label, "margin", 6, NULL);
        gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 0);
        gtk_widget_set_halign (frame, GTK_ALIGN_FILL);
        gtk_widget_set_valign (frame, GTK_ALIGN_FILL);
        gtk_container_add (GTK_CONTAINER (button), frame);
        gtk_container_add (GTK_CONTAINER (f), button);
        gtk_widget_show_all (f);
        g_object_set_data_full (G_OBJECT (button), "app", g_object_ref (app), g_object_unref);
        g_signal_connect (button, "clicked",
                          G_CALLBACK (app_tile_clicked), priv);

        return f;
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
        gint i;
        GtkWidget *tile;
	GsMainPrivate *priv = (GsMainPrivate *) user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
        GtkWidget *grid;

	/* get popular apps */
	list = gs_plugin_loader_get_popular_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		g_warning ("failed to get popular apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_popular"));
	for (l = list, i = 0; l != NULL; l = l->next, i++) {
		app = GS_APP (l->data);
                tile = create_popular_tile (priv, app);
                gtk_grid_attach (GTK_GRID (grid), tile, i, 0, 1, 1);
	}
out:
	return;
}

static void
container_remove_all (GtkContainer *container)
{
  GList *children, *l;

  children = gtk_container_get_children (container);

  for (l = children; l; l = l->next)
    gtk_container_remove (container, GTK_WIDGET (l->data));

  g_list_free (children);
}

/**
 * gs_main_get_popular:
 **/
static void
gs_main_get_popular (GsMainPrivate *priv)
{
        GtkWidget *grid;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_popular"));
        container_remove_all (GTK_CONTAINER (grid));

	/* get popular apps */
	gs_plugin_loader_get_popular_async (priv->plugin_loader,
					    priv->cancellable,
					    gs_main_get_popular_cb,
					    priv);
}

static void
category_tile_clicked (GtkButton *button, gpointer data)
{
        GsMainPrivate *priv = data;
        const gchar *category;

        category = g_object_get_data (G_OBJECT (button), "category");
        gs_main_set_overview_mode (priv, GS_MAIN_MODE_CATEGORY, NULL, category);
}

static GtkWidget *
create_category_tile (GsMainPrivate *priv, const gchar *category)
{
        GtkWidget *button, *frame, *label;

        button = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
        frame = gtk_frame_new (NULL);
        gtk_container_add (GTK_CONTAINER (button), frame);
        gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
        gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
        label = gtk_label_new (category);
        g_object_set (label, "margin", 12, "xalign", 0, NULL);
        gtk_container_add (GTK_CONTAINER (frame), label);
        gtk_widget_show_all (button);
        g_object_set_data (G_OBJECT (button), "category", (gpointer)category);
        g_signal_connect (button, "clicked",
                          G_CALLBACK (category_tile_clicked), priv);

        return button;
}

static void
gs_main_get_categories (GsMainPrivate *priv)
{
        /* FIXME get real categories */
        GtkWidget *grid;
        const gchar *categories[] = {
          "Add-ons", "Books", "Business & Finance",
          "Entertainment", "Education", "Games",
          "Lifestyle", "Music", "Navigation",
          "News", "Photo & Video", "Productivity",
          "Social Networking", "Utility", "Weather",
        };
        guint i;
        GtkWidget *tile;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_categories"));
        container_remove_all (GTK_CONTAINER (grid));

        for (i = 0; i < G_N_ELEMENTS (categories); i++) {
                tile = create_category_tile (priv, categories[i]);
                gtk_grid_attach (GTK_GRID (grid), tile, i % 3, i / 3, 1, 1);
        }
}

static GtkWidget *
create_app_tile (GsMainPrivate *priv, GsApp *app)
{
        GtkWidget *button, *frame, *label;
        GtkWidget *image, *grid;
        const gchar *tmp;
        PangoAttrList *attrs;

        button = gtk_button_new ();
        gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
        frame = gtk_frame_new (NULL);
        gtk_container_add (GTK_CONTAINER (button), frame);
        gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
        gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
        grid = gtk_grid_new ();
        gtk_container_add (GTK_CONTAINER (frame), grid);
        g_object_set (grid, "margin", 12, "row-spacing", 6, "column-spacing", 6, NULL);
        image = gtk_image_new_from_pixbuf (gs_app_get_pixbuf (app));
        gtk_grid_attach (GTK_GRID (grid), image, 0, 0, 1, 2);
        label = gtk_label_new (gs_app_get_name (app));
        attrs = pango_attr_list_new ();
        pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes (GTK_LABEL (label), attrs);
        pango_attr_list_unref (attrs);
        g_object_set (label, "xalign", 0, NULL);
        gtk_grid_attach (GTK_GRID (grid), label, 1, 0, 1, 1);
        tmp = gs_app_get_summary (app);
        if (tmp != NULL && tmp[0] != '\0') {
                label = gtk_label_new (tmp);
                g_object_set (label, "xalign", 0, NULL);
                gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
                gtk_grid_attach (GTK_GRID (grid), label, 1, 1, 1, 1);
        }

        gtk_widget_show_all (button);

        g_object_set_data_full (G_OBJECT (button), "app", g_object_ref (app), g_object_unref);
        g_signal_connect (button, "clicked",
                          G_CALLBACK (app_tile_clicked), priv);

        return button;
}

static void
gs_main_populate_filtered_category (GsMainPrivate *priv,
                                    const gchar   *category,
                                    const gchar   *filter)
{
        gint i;
        GtkWidget *tile;
        GsApp *app;
        GtkWidget *grid;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
        gtk_grid_remove_column (GTK_GRID (grid), 2);
        gtk_grid_remove_column (GTK_GRID (grid), 1);
        if (filter == NULL) {
                gtk_grid_remove_column (GTK_GRID (grid), 0);
        }

        /* FIXME load apps for this category and filter */
        app = gs_app_new ("gnome-boxes");
        gs_app_set_name (app, "Boxes");
        gs_app_set_summary (app, "View and use virtual machines");
        gs_app_set_url (app, "http://www.box.org");
        gs_app_set_kind (app, GS_APP_KIND_NORMAL);
        gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
        gs_app_set_pixbuf (app, gdk_pixbuf_new_from_file ("/usr/share/icons/hicolor/48x48/apps/gnome-boxes.png", NULL));

        for (i = 0; i < 30; i++) {
                tile = create_app_tile (priv, app);
                if (filter) {
                        gtk_grid_attach (GTK_GRID (grid), tile, 1 + (i % 2), i / 2, 1, 1);
                }
                else {
                        gtk_grid_attach (GTK_GRID (grid), tile, i % 3, i / 3, 1, 1);
                }
        }

        g_object_unref (app);
}

static void
add_separator (GtkListBoxRow *row,
               GtkListBoxRow *before,
               gpointer       data)
{
        if (!before) {
                return;
        }

        gtk_list_box_row_set_header (row, gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
}

static void
filter_selected (GtkListBox    *filters,
                 GtkListBoxRow *row,
                 gpointer       data)
{
        GsMainPrivate *priv = data;
        const gchar *filter;
        const gchar *category;

        if (row == NULL)
                return;

        filter = gtk_label_get_label (GTK_LABEL (gtk_bin_get_child (GTK_BIN (row))));
        category = (const gchar*)g_object_get_data (G_OBJECT (filters), "category");
        gs_main_populate_filtered_category (priv, category, filter);
}

static void
create_filter_list (GsMainPrivate *priv, const gchar *category, const gchar *filters[])
{
        GtkWidget *grid;
        GtkWidget *list;
        GtkWidget *row;
        GtkWidget *frame;
        guint i;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
        list = gtk_list_box_new ();
        g_object_set_data (G_OBJECT (list), "category", (gpointer)category);
        gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_BROWSE);
        g_signal_connect (list, "row-selected", G_CALLBACK (filter_selected), priv);
        gtk_list_box_set_header_func (GTK_LIST_BOX (list), add_separator, NULL, NULL);
        for (i = 0; filters[i]; i++) {
                row = gtk_label_new (filters[i]);
                g_object_set (row, "xalign", 0.0, "margin", 6, NULL);
                gtk_list_box_insert (GTK_LIST_BOX (list), row, i);
        }
        frame = gtk_frame_new (NULL);
        g_object_set (frame, "margin", 6, NULL);
        gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
        gtk_style_context_add_class (gtk_widget_get_style_context (frame), "view");
        gtk_container_add (GTK_CONTAINER (frame), list);
        gtk_widget_show_all (frame);
        gtk_widget_set_valign (frame, GTK_ALIGN_START);
        gtk_grid_attach (GTK_GRID (grid), frame, 0, 0, 1, 5);
        gtk_list_box_select_row (GTK_LIST_BOX (list),
                                 gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), 0));
}

static void
gs_main_populate_category (GsMainPrivate *priv, const gchar *category)
{
        GtkWidget *grid;

	grid = GTK_WIDGET (gtk_builder_get_object (priv->builder, "category_detail_grid"));
        container_remove_all (GTK_CONTAINER (grid));

        /* FIXME: get actual filters */
        if (g_str_equal (category, "Games")) {
                const gchar *filters[] = {
                        "Popular", "Action", "Arcade", "Board",
                        "Blocks", "Card", "Kids", "Logic", "Role Playing",
                        "Shooter", "Simulation", "Sports", "Strategy",
                        NULL
                };
                create_filter_list (priv, category, filters);
        }
        else if (g_str_equal (category, "Add-ons")) {
                const gchar *filters[] = {
                        "Popular", "Codecs", "Fonts",
                        "Input Sources", "Language Packs",
                        NULL
                };
                create_filter_list (priv, category, filters);
        }
        else {
                gs_main_populate_filtered_category (priv, category, NULL);
        }
}

/**
 * gs_main_set_overview_mode_ui:
 **/
static void
gs_main_set_overview_mode_ui (GsMainPrivate *priv, GsMainMode mode, GsApp *app)
{
	GtkWidget *widget;
        GsAppState state;

	priv->ignore_primary_buttons = TRUE;

	switch (mode) {
	case GS_MAIN_MODE_NEW:
	case GS_MAIN_MODE_INSTALLED:
	case GS_MAIN_MODE_UPDATES:
	case GS_MAIN_MODE_WAITING:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
		gtk_widget_set_visible (widget, FALSE);
		break;
	case GS_MAIN_MODE_DETAILS:
	case GS_MAIN_MODE_CATEGORY:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
		gtk_widget_set_visible (widget, TRUE);
                if (app) {
                        state = gs_app_get_state (app);
                }
                else {
                        state = GS_APP_STATE_UNKNOWN;
                }
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
		gtk_widget_set_visible (widget, state == GS_APP_STATE_AVAILABLE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
		gtk_widget_set_visible (widget, state == GS_APP_STATE_INSTALLED);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_set_visible (widget, FALSE);
		break;
	default:
                g_assert_not_reached ();
		break;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_all"));
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
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_entry_set_text (GTK_ENTRY (widget), "");
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_stop (GTK_SPINNER (widget));
		break;
	case GS_MAIN_MODE_INSTALLED:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
		gtk_entry_set_text (GTK_ENTRY (widget), "");
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_stop (GTK_SPINNER (widget));
		break;
	case GS_MAIN_MODE_UPDATES:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_stop (GTK_SPINNER (widget));
		break;
	case GS_MAIN_MODE_WAITING:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_waiting"));
		gtk_spinner_start (GTK_SPINNER (widget));
		break;
	case GS_MAIN_MODE_DETAILS:
	case GS_MAIN_MODE_CATEGORY:
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
gs_main_set_overview_mode (GsMainPrivate *priv, GsMainMode mode, GsApp *app, const gchar *category)
{
	GtkWidget *widget;
	GtkWidget *widget2;
	const gchar *tmp;
	GdkPixbuf *pixbuf;

	if (priv->ignore_primary_buttons)
		return;

	/* set controls */
	gs_main_set_overview_mode_ui (priv, mode, app);

	/* do action for mode */
	priv->mode = mode;
	switch (mode) {
	case GS_MAIN_MODE_NEW:
		gs_main_get_popular (priv);
		gs_main_get_categories (priv);
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
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_title"));
		widget2 = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
		if (tmp != NULL && tmp[0] != '\0') {
			gtk_label_set_label (GTK_LABEL (widget), tmp);
			gtk_label_set_label (GTK_LABEL (widget2), tmp);
			gtk_widget_set_visible (widget, TRUE);
		}
                else {
			gtk_widget_set_visible (widget, FALSE);
			gtk_label_set_label (GTK_LABEL (widget2), "");
                }
		tmp = gs_app_get_summary (app);
	        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_summary"));
		if (tmp != NULL && tmp[0] != '\0') {
			gtk_label_set_label (GTK_LABEL (widget), tmp);
			gtk_widget_set_visible (widget, TRUE);
		}
                else {
			gtk_widget_set_visible (widget, FALSE);
                }
		tmp = gs_app_get_description (app);
		if (tmp == NULL)
			tmp = _("The author of this software has not included a 'Description' in the desktop file...");

		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_description"));
		gtk_label_set_label (GTK_LABEL (widget), tmp);
		gtk_widget_set_visible (widget, TRUE);

		pixbuf = gs_app_get_pixbuf (app);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_icon"));
		if (pixbuf != NULL) {
			gtk_image_set_from_pixbuf (GTK_IMAGE (widget), pixbuf);
			gtk_widget_set_visible (widget, TRUE);
		}
                else {
			gtk_widget_set_visible (widget, FALSE);
                }

                tmp = gs_app_get_url (app);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_url"));
                if (tmp != NULL && tmp[0] != '\0') {
                        gtk_link_button_set_uri (GTK_LINK_BUTTON (widget), tmp);
                        gtk_widget_set_visible (widget, TRUE);
                }
                else {
			gtk_widget_set_visible (widget, FALSE);
                }

		break;
	case GS_MAIN_MODE_CATEGORY:
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "application_details_header"));
                gtk_label_set_label (GTK_LABEL (widget), category);
		gs_main_populate_category (priv, category);

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
	gs_main_set_overview_mode (priv, mode, NULL, NULL);
}

/**
 * gs_main_button_updates_close_cb:
 **/
static void
gs_main_button_updates_close_cb (GtkWidget *widget, GsMainPrivate *priv)
{
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	gtk_widget_hide (widget);
}

/**
 * gs_main_button_updates_back_cb:
 **/
static void
gs_main_button_updates_back_cb (GtkWidget *widget, GsMainPrivate *priv)
{
	/* return to the list view */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_update_header"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_details"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update"));
	gtk_widget_show (widget);
}

/**
 * gs_main_back_button_cb:
 **/
static void
gs_main_back_button_cb (GtkWidget *widget, GsMainPrivate *priv)
{
	gs_main_set_overview_mode (priv, priv->tab_back_id, NULL, NULL);
}

/**
 * gs_main_get_featured_cb:
 **/
static void
gs_main_get_featured_cb (GObject *source_object,
			  GAsyncResult *res,
			  gpointer user_data)
{
	GdkPixbuf *pixbuf;
	GError *error = NULL;
	GList *list;
	GsApp *app;
	GsMainPrivate *priv = (GsMainPrivate *) user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkImage *image;
	GtkWidget *button;
	GtkWidget *widget;

	list = gs_plugin_loader_get_featured_finish (plugin_loader,
						     res,
						     &error);
	if (list == NULL) {
		g_warning ("failed to get featured apps: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* at the moment, we only care about the first app */
	app = GS_APP (list->data);
	image = GTK_IMAGE (gtk_builder_get_object (priv->builder, "featured_image"));
	pixbuf = gs_app_get_featured_pixbuf (app);
	gtk_image_set_from_pixbuf (image, pixbuf);
	button = GTK_WIDGET (gtk_builder_get_object (priv->builder, "featured_button"));
	g_object_set_data_full (G_OBJECT (button), "app", app, g_object_unref);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (app_tile_clicked), priv);

	/* focus back to the text extry */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	gtk_widget_grab_focus (widget);
out:
	g_list_free (list);
	return;
}

/**
 * gs_main_setup_featured:
 **/
static void
gs_main_setup_featured (GsMainPrivate *priv)
{
	/* get popular apps */
	gs_plugin_loader_get_featured_async (priv->plugin_loader,
					     priv->cancellable,
					     gs_main_get_featured_cb,
					     priv);
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
 * gs_main_list_header_func
 **/
static void
gs_main_list_header_func (GtkListBoxRow *row,
			  GtkListBoxRow *before,
			  gpointer user_data)
{
	GtkWidget *header;

	/* first entry */
	header = gtk_list_box_row_get_header (row);
	if (before == NULL) {
		gtk_list_box_row_set_header (row, NULL);
		return;
	}

	/* already set */
	if (header != NULL)
		return;

	/* set new */
	header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

/**
 * gs_main_installed_filter_func:
 **/
static gboolean
gs_main_installed_filter_func (GtkListBoxRow *row, void *user_data)
{
	const gchar *tmp;
	GtkWidget *widget;
	GsMainPrivate *priv = (GsMainPrivate *) user_data;
	GsAppWidget *app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
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
	gtk_list_box_invalidate_filter (priv->list_box_installed);
	return FALSE;
}

/**
 * gs_main_installed_sort_func:
 **/
static gint
gs_main_installed_sort_func (GtkListBoxRow *a,
			     GtkListBoxRow *b,
			     gpointer user_data)
{
	GsAppWidget *aw1 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (a)));
	GsAppWidget *aw2 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (b)));
	GsApp *a1 = gs_app_widget_get_app (aw1);
	GsApp *a2 = gs_app_widget_get_app (aw2);
	return g_strcmp0 (gs_app_get_name (a1),
			  gs_app_get_name (a2));
}

/**
 * gs_main_updates_activated_cb:
 **/
static void
gs_main_updates_activated_cb (GtkListBox *list_box, GtkListBoxRow *row, GsMainPrivate *priv)
{
	GsAppWidget *app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	GsApp *app = gs_app_widget_get_app (app_widget);
	GsApp *app_related;
	GsAppKind kind;
	GtkWidget *widget;

	app = gs_app_widget_get_app (app_widget);
	kind = gs_app_get_kind (app);

	/* set update header */
	gs_main_set_updates_description_ui (priv, app);

	/* only OS updates can go back, and only on selection */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	gtk_widget_hide (widget);

	/* set update description */
	if (kind == GS_APP_KIND_OS_UPDATE) {
		GPtrArray *related;
		GtkListStore *liststore;
		GtkTreeIter iter;
		guint i;

		/* add the related packages to the list view */
		liststore = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_update"));
		gtk_list_store_clear (liststore);
		related = gs_app_get_related (app);
		for (i = 0; i < related->len; i++) {
			app_related = g_ptr_array_index (related, i);
			gtk_list_store_append (liststore, &iter);
			gtk_list_store_set (liststore,
					    &iter,
					    COLUMN_UPDATE_APP, app_related,
					    COLUMN_UPDATE_NAME, gs_app_get_name (app_related),
					    COLUMN_UPDATE_VERSION, gs_app_get_version (app_related),
					    -1);
		}

		/* unselect treeview by default */
		g_idle_add (gs_main_updates_unselect_treeview_cb, priv);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	gtk_window_present (GTK_WINDOW (widget));
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

	/* setup featured tiles */
	gs_main_setup_featured (priv);
	/* setup installed */
	priv->list_box_installed = GTK_LIST_BOX (gtk_list_box_new ());
	gtk_list_box_set_header_func (priv->list_box_installed,
				      gs_main_list_header_func,
				      priv,
				      NULL);
	gtk_list_box_set_filter_func (priv->list_box_installed,
				      gs_main_installed_filter_func,
				      priv,
				      NULL);
	gtk_list_box_set_sort_func (priv->list_box_installed,
				    gs_main_installed_sort_func,
				    priv,
				    NULL);
	gtk_list_box_set_selection_mode (priv->list_box_installed,
					 GTK_SELECTION_NONE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_install"));
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (priv->list_box_installed));
	gtk_widget_show (GTK_WIDGET (priv->list_box_installed));

	/* setup updates */
	priv->list_box_updates = GTK_LIST_BOX (gtk_list_box_new ());
	g_signal_connect (priv->list_box_updates, "row-activated",
			  G_CALLBACK (gs_main_updates_activated_cb), priv);
	gtk_list_box_set_header_func (priv->list_box_updates,
				      gs_main_list_header_func,
				      priv,
				      NULL);
	gtk_list_box_set_selection_mode (priv->list_box_updates,
					 GTK_SELECTION_NONE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (priv->list_box_updates));
	gtk_widget_show (GTK_WIDGET (priv->list_box_updates));

	/* setup buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_back_button_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_all"));
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

	/* setup update details window */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_button_updates_close_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_main_button_updates_back_cb), priv);
	//FIXME: connect to dialog_update and just hide widget if user closes modal


{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_update"));

	/* column for name */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer,
		      "xpad", 6,
		      "ypad", 6,
		      NULL);
	column = gtk_tree_view_column_new_with_attributes ("name", renderer,
							   "markup", COLUMN_UPDATE_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, COLUMN_UPDATE_NAME);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_append_column (treeview, column);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer,
		      "xpad", 6,
		      "ypad", 6,
		      NULL);
	column = gtk_tree_view_column_new_with_attributes ("version", renderer,
							   "markup", COLUMN_UPDATE_VERSION, NULL);
	gtk_tree_view_append_column (treeview, column);
	g_signal_connect (treeview, "row-activated",
			  G_CALLBACK (gs_main_update_row_activated_cb), priv);

}



	/* refilter on search box changing */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (GTK_EDITABLE (widget), "changed",
			  G_CALLBACK (gs_main_filter_text_changed_cb), priv);

	/* show the status on a different page */
	g_signal_connect (priv->plugin_loader, "status-changed",
			  G_CALLBACK (gs_main_plugin_loader_status_changed_cb), priv);

	/* show main UI */
	gtk_widget_show (main_window);
	gs_main_set_overview_mode (priv, priv->app_startup_mode, NULL, NULL);
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
	gchar *mode = NULL;
	GError *error = NULL;
	GOptionContext *context;
	GsMainPrivate *priv = NULL;
	int status = 0;

	const GOptionEntry options[] = {
		{ "mode", '\0', 0, G_OPTION_ARG_STRING, &mode,
		  _("Start up mode, either 'updates', 'installed' or 'new'"), _("MODE") },
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

	/* specified what page to open */
	if (mode != NULL) {
		if (g_strcmp0 (mode, "updates") == 0) {
			priv->app_startup_mode = GS_MAIN_MODE_UPDATES;
		} else if (g_strcmp0 (mode, "installed") == 0) {
			priv->app_startup_mode = GS_MAIN_MODE_INSTALLED;
		} else if (g_strcmp0 (mode, "new") == 0) {
			priv->app_startup_mode = GS_MAIN_MODE_NEW;
		} else {
			g_warning ("Mode '%s' not recognised", mode);
		}
	} else {
		priv->app_startup_mode = GS_MAIN_MODE_NEW;
	}

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
	gs_plugin_loader_set_enabled (priv->plugin_loader, "hardcoded-descriptions", TRUE);
	gs_plugin_loader_set_enabled (priv->plugin_loader, "hardcoded-featured", TRUE);
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
	g_free (mode);
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
