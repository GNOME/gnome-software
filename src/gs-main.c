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
	GtkWidget *widget;
	PkStatusEnum status;

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
	case PK_STATUS_ENUM_QUERY:
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

/**
 * gs_main_installed_add_package:
 **/
static void
gs_main_installed_add_package (GsMainPrivate *priv, PkPackage *pkg)
{
	gboolean target_installed;
	gchar *tmp;
	GtkListStore *list_store;
	GtkTreeIter iter;

	target_installed = gs_main_is_pkg_installed_target (pkg);
	if (target_installed) {
		list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder,
								     "liststore_installed"));
	} else {
		list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder,
								     "liststore_updates"));
	}
	gtk_list_store_append (list_store, &iter);
	tmp = gs_main_get_pretty_version (pk_package_get_version (pkg));
	gtk_list_store_set (list_store, &iter,
			    COLUMN_PACKAGE_ID, pk_package_get_id (pkg),
			    COLUMN_ICON_NAME, "icon-missing",
			    COLUMN_PACKAGE_NAME, pk_package_get_name (pkg),
			    COLUMN_PACKAGE_VERSION, tmp,
			    COLUMN_PACKAGE_SUMMARY, pk_package_get_summary (pkg),
			    -1);
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
	gboolean ret;
	gboolean target_installed;
	gchar *comment = NULL;
	gchar *icon = NULL;
	gchar *name = NULL;
	gchar *version_tmp = NULL;
	GError *error = NULL;
	GKeyFile *key_file;
	GtkListStore *list_store;
	GtkTreeIter iter;

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
		icon = g_strdup ("icon-missing");
	comment = g_key_file_get_string (key_file,
					 G_KEY_FILE_DESKTOP_GROUP,
					 G_KEY_FILE_DESKTOP_KEY_COMMENT,
					 NULL);
	if (comment == NULL)
		comment = g_strdup (pk_package_get_summary (pkg));

	/* add to list store */
	target_installed = gs_main_is_pkg_installed_target (pkg);
	if (target_installed) {
		list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder,
								     "liststore_installed"));
	} else {
		list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder,
								     "liststore_updates"));
	}
	gtk_list_store_append (list_store, &iter);
	version_tmp = gs_main_get_pretty_version (pk_package_get_version (pkg));
	gtk_list_store_set (list_store, &iter,
			    COLUMN_PACKAGE_ID, pk_package_get_id (pkg),
			    COLUMN_ICON_NAME, icon,
			    COLUMN_PACKAGE_NAME, name,
			    COLUMN_PACKAGE_VERSION, version_tmp,
			    COLUMN_PACKAGE_SUMMARY, comment,
			    -1);
out:
	g_key_file_unref (key_file);
	g_free (name);
	g_free (comment);
	g_free (icon);
	g_free (version_tmp);
}

/**
 * gs_main_installed_add_package:
 **/
static void
gs_main_installed_add_item (GsMainPrivate *priv, PkPackage *pkg)
{
	const gchar *desktop_file;
	GError *error = NULL;
	GPtrArray *files;
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
 * gs_main_get_packages_cb:
 **/
static void
gs_main_get_packages_cb (PkClient *client,
			 GAsyncResult *res,
			 GsMainPrivate *priv)
{
	GError *error = NULL;
	GPtrArray *array = NULL;
	GtkListStore *list_store;
	GtkWidget *widget;
	guint i;
	PkError *error_code = NULL;
	PkPackage *item;
	PkResults *results;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to search: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to search: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	list_store = GTK_LIST_STORE (gtk_builder_get_object (priv->builder, "liststore_installed"));
	gtk_list_store_clear (list_store);
	array = pk_results_get_package_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_debug ("add %s", pk_package_get_id (item));

		/* use different listviews for each kind of request */
		if (pk_results_get_role (results) != PK_ROLE_ENUM_GET_UPDATES) {
			g_object_set_data (G_OBJECT (item),
					   "gnome-software::target-installed",
					   GINT_TO_POINTER (TRUE));
		}
		gs_main_installed_add_item (priv, item);
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
					 PK_FILTER_ENUM_APPLICATION,
					 -1);
	pk_client_get_updates_async (PK_CLIENT(priv->task),
				     filter,
				     priv->cancellable,
				     (PkProgressCallback) gs_main_progress_cb, priv,
				     (GAsyncReadyCallback) gs_main_get_packages_cb, priv);
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

	/* fix sensitivities */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_new"));
	gtk_widget_set_sensitive (widget, mode != GS_MAIN_MODE_NEW);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_updates"));
	gtk_widget_set_sensitive (widget, mode != GS_MAIN_MODE_UPDATES);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_installed"));
	gtk_widget_set_sensitive (widget, mode != GS_MAIN_MODE_INSTALLED);

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

static void
gs_main_add_columns (GsMainPrivate *priv, GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkCellArea *area;

	/* column for images */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", priv->custom_icon_size, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "icon-name", COLUMN_ICON_NAME);
	gtk_tree_view_append_column (treeview, column);

	/* column for name|version */
	area = gtk_cell_area_box_new ();
	gtk_orientable_set_orientation (GTK_ORIENTABLE (area), GTK_ORIENTATION_VERTICAL);
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer,
		      "weight", 800,
		      "yalign", 0.0f,
		      "ypad", 18,
		      NULL);
	gtk_cell_area_box_pack_start (GTK_CELL_AREA_BOX (area), renderer,
				      FALSE, /* expand */
				      FALSE, /* align */
				      FALSE); /* fixed */
	gtk_cell_area_attribute_connect (area, renderer, "markup", COLUMN_PACKAGE_NAME);
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer,
		      "yalign", 0.0f,
		      "ypad", 0,
		      NULL);
	g_object_set (G_OBJECT (renderer), "xalign", 0.0F, NULL);
	gtk_cell_area_box_pack_start (GTK_CELL_AREA_BOX (area), renderer,
				      TRUE, /* expand */
				      FALSE, /* align */
				      FALSE); /* fixed */
	gtk_cell_area_attribute_connect (area, renderer, "markup", COLUMN_PACKAGE_VERSION);
	column = gtk_tree_view_column_new_with_area (area);
	gtk_tree_view_append_column (treeview, column);

	/* column for summary */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer,
		      "yalign", 0.0f,
		      "ypad", 18,
		      "wrap-mode", PANGO_WRAP_WORD,
		      NULL);
	column = gtk_tree_view_column_new_with_attributes (NULL, renderer,
							   "markup", COLUMN_PACKAGE_SUMMARY, NULL);
	gtk_tree_view_append_column (treeview, column);
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
	pixbuf = gdk_pixbuf_new_from_file_at_scale ("./featured-firefox.png", -1, -1, TRUE, &error);
	if (pixbuf == NULL) {
		g_warning ("failed to load featured tile: %s", error->message);
		g_error_free (error);
		goto out;
	}
	gtk_image_set_from_pixbuf (image, pixbuf);
	g_object_unref (pixbuf);

	/* 2 */
	image = GTK_IMAGE (gtk_builder_get_object (priv->builder, "image_featured2"));
	pixbuf = gdk_pixbuf_new_from_file_at_scale ("./featured-gimp.png", -1, -1, TRUE, &error);
	if (pixbuf == NULL) {
		g_warning ("failed to load featured tile: %s", error->message);
		g_error_free (error);
		goto out;
	}
	gtk_image_set_from_pixbuf (image, pixbuf);
	g_object_unref (pixbuf);

	/* 3 */
	image = GTK_IMAGE (gtk_builder_get_object (priv->builder, "image_featured3"));
	pixbuf = gdk_pixbuf_new_from_file_at_scale ("./featured-xchat.png", -1, -1, TRUE, &error);
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
 * gs_main_startup_cb:
 **/
static void
gs_main_startup_cb (GApplication *application, GsMainPrivate *priv)
{
	GError *error = NULL;
	gint retval;
	GtkTreeView *treeview;
	GtkWidget *main_window;
	GtkWidget *widget;

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

	/* add columns to the tree view */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_installed"));
	gs_main_add_columns (priv, treeview);
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_updates"));
	gs_main_add_columns (priv, treeview);

	/* set up popular icon vew */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "iconview_popular"));
	gtk_icon_view_set_markup_column (GTK_ICON_VIEW (widget), COLUMN_POPULAR_MARKUP);
	gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (widget), COLUMN_POPULAR_PIXBUF);

	/* setup featured tiles */
	gs_main_setup_featured (priv);

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

	/* we want the large icon size according to the width of the window */
	priv->custom_icon_size = gtk_icon_size_register ("custom", 96, 96);

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
		g_warning ("failed to parse options: %s", error->message);
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
		if (priv->builder != NULL)
			g_object_unref (priv->builder);
		if (priv->waiting_tab_id > 0)
			g_source_remove (priv->waiting_tab_id);
		g_free (priv);
	}
	return status;
}
