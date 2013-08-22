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
#include "gs-shell.h"
#include "gs-plugin-loader.h"

typedef struct {
	GCancellable		*cancellable;
	GtkApplication		*application;
	PkTask			*task;
	guint			 waiting_tab_id;
	GtkCssProvider		*provider;
	GsPluginLoader		*plugin_loader;
	gint			 pending_apps;
	GsShell			*shell;
} GsMainPrivate;

#if 0
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
#endif

#if 0
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
//			app_widget = gs_main_get_app_widget_for_id (priv->list_box_installed,
//								    pk_item_progress_get_package_id (item_progress));
//			if (app_widget != NULL) {
//				gs_app_widget_set_kind (app_widget, GS_APP_WIDGET_KIND_BUSY);
//				gs_app_widget_set_status (app_widget, pk_status_enum_to_string (status));
//			}
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
#endif

#if 0
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
#endif

#if 0
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
//		app_widget = gs_main_get_app_widget_for_id (data->priv->list_box_installed,
//							    pk_package_get_id (package));
//		if (app_widget != NULL) {
//			gtk_container_remove (GTK_CONTAINER (data->priv->list_box_installed),
//					      GTK_WIDGET (app_widget));
//		}
//		app_widget = gs_main_get_app_widget_for_id (data->priv->list_box_updates,
//							    pk_package_get_id (package));
//		if (app_widget != NULL) {
//			gtk_container_remove (GTK_CONTAINER (data->priv->list_box_updates),
//					      GTK_WIDGET (app_widget));
//		}
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
#endif

#if 0
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

	app = gs_app_widget_get_app (app_widget);
	package_id = gs_app_get_id (app);

	/* save, so we can recover a failed action */
	data = g_new0 (GsMainMethodData, 1);
	data->original_kind = kind;
	data->app_widget = app_widget;
	data->package_id = package_id;
	data->priv = priv;

	if (kind == GS_APP_WIDGET_KIND_UPDATE) {
	} else if (kind == GS_APP_WIDGET_KIND_INSTALL) {
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
	}
}
#endif

/**
 * gs_main_startup_cb:
 **/
static void
gs_main_startup_cb (GApplication *application, GsMainPrivate *priv)
{
	GBytes *data = NULL;
	GError *error = NULL;
	gboolean ret;
	GtkWindow *window;

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

	/* setup UI */
	window = gs_shell_setup (priv->shell, priv->plugin_loader, priv->cancellable);
	gtk_application_add_window (priv->application, GTK_WINDOW (window));

	/* show the status on a different page */
//	g_signal_connect (priv->plugin_loader, "status-changed",
//			  G_CALLBACK (gs_main_plugin_loader_status_changed_cb), priv);
out:
	if (data != NULL)
		g_bytes_unref (data);
}

/**
 * gs_main_activate_cb:
 **/
static void
gs_main_activate_cb (GApplication *application, GsMainPrivate *priv)
{
	gs_shell_activate (priv->shell);
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
		  _("Start up mode, either 'updates', 'installed' or 'overview'"), _("MODE") },
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
	priv->shell = gs_shell_new ();
	if (mode != NULL) {
		if (g_strcmp0 (mode, "updates") == 0) {
			gs_shell_set_default_mode (priv->shell, GS_SHELL_MODE_UPDATES);
		} else if (g_strcmp0 (mode, "installed") == 0) {
			gs_shell_set_default_mode (priv->shell, GS_SHELL_MODE_INSTALLED);
		} else if (g_strcmp0 (mode, "overview") == 0) {
			gs_shell_set_default_mode (priv->shell, GS_SHELL_MODE_OVERVIEW);
		} else {
			g_warning ("Mode '%s' not recognised", mode);
		}
	} else {
		gs_shell_set_default_mode (priv->shell, GS_SHELL_MODE_OVERVIEW);
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
		if (priv->shell != NULL)
			g_object_unref (priv->shell);
		if (priv->provider != NULL)
			g_object_unref (priv->provider);
		if (priv->waiting_tab_id > 0)
			g_source_remove (priv->waiting_tab_id);
		g_free (priv);
	}
	return status;
}
