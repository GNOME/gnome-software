/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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

#include "gs-shell-extras.h"

#include "gs-app.h"
#include "gs-app-row.h"
#include "gs-language.h"
#include "gs-shell.h"
#include "gs-common.h"
#include "gs-vendor.h"

#include <glib/gi18n.h>

typedef enum {
	GS_SHELL_EXTRAS_STATE_LOADING,
	GS_SHELL_EXTRAS_STATE_READY,
	GS_SHELL_EXTRAS_STATE_NO_RESULTS,
	GS_SHELL_EXTRAS_STATE_FAILED
} GsShellExtrasState;

typedef struct {
	gchar		*title;
	gchar		*search;
	gchar		*search_filename;
	gchar		*package_filename;
	gchar		*url_not_found;
	GsShellExtras	*self;
} SearchData;

struct _GsShellExtras
{
	GsPage			  parent_instance;

	GsPluginLoader		 *plugin_loader;
	GtkBuilder		 *builder;
	GCancellable		 *search_cancellable;
	GsShell			 *shell;
	GsShellExtrasState	  state;
	GtkSizeGroup		 *sizegroup_image;
	GtkSizeGroup		 *sizegroup_name;
	GPtrArray		 *array_search_data;
	GsShellExtrasMode	  mode;
	GsLanguage		 *language;
	GsVendor		 *vendor;
	guint			  pending_search_cnt;

	GtkWidget		 *label_failed;
	GtkWidget		 *label_no_results;
	GtkWidget		 *list_box_results;
	GtkWidget		 *scrolledwindow;
	GtkWidget		 *spinner;
	GtkWidget		 *stack;
};

G_DEFINE_TYPE (GsShellExtras, gs_shell_extras, GS_TYPE_PAGE)

static void
search_data_free (SearchData *search_data)
{
	if (search_data->self != NULL)
		g_object_unref (search_data->self);
	g_free (search_data->title);
	g_free (search_data->search);
	g_free (search_data->search_filename);
	g_free (search_data->package_filename);
	g_free (search_data->url_not_found);
	g_slice_free (SearchData, search_data);
}

static GsShellExtrasMode
gs_shell_extras_mode_from_string (const gchar *str)
{
	if (g_strcmp0 (str, "install-package-files") == 0)
		return GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_FILES;
	if (g_strcmp0 (str, "install-provide-files") == 0)
		return GS_SHELL_EXTRAS_MODE_INSTALL_PROVIDE_FILES;
	if (g_strcmp0 (str, "install-package-names") == 0)
		return GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_NAMES;
	if (g_strcmp0 (str, "install-mime-types") == 0)
		return GS_SHELL_EXTRAS_MODE_INSTALL_MIME_TYPES;
	if (g_strcmp0 (str, "install-fontconfig-resources") == 0)
		return GS_SHELL_EXTRAS_MODE_INSTALL_FONTCONFIG_RESOURCES;
	if (g_strcmp0 (str, "install-gstreamer-resources") == 0)
		return GS_SHELL_EXTRAS_MODE_INSTALL_GSTREAMER_RESOURCES;
	if (g_strcmp0 (str, "install-plasma-resources") == 0)
		return GS_SHELL_EXTRAS_MODE_INSTALL_PLASMA_RESOURCES;
	if (g_strcmp0 (str, "install-printer-drivers") == 0)
		return GS_SHELL_EXTRAS_MODE_INSTALL_PRINTER_DRIVERS;

	g_assert_not_reached ();
}

const gchar *
gs_shell_extras_mode_to_string (GsShellExtrasMode mode)
{
	if (mode == GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_FILES)
		return "install-package-files";
	if (mode == GS_SHELL_EXTRAS_MODE_INSTALL_PROVIDE_FILES)
		return "install-provide-files";
	if (mode == GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_NAMES)
		return "install-package-names";
	if (mode == GS_SHELL_EXTRAS_MODE_INSTALL_MIME_TYPES)
		return "install-mime-types";
	if (mode == GS_SHELL_EXTRAS_MODE_INSTALL_FONTCONFIG_RESOURCES)
		return "install-fontconfig-resources";
	if (mode == GS_SHELL_EXTRAS_MODE_INSTALL_GSTREAMER_RESOURCES)
		return "install-gstreamer-resources";
	if (mode == GS_SHELL_EXTRAS_MODE_INSTALL_PLASMA_RESOURCES)
		return "install-plasma-resources";
	if (mode == GS_SHELL_EXTRAS_MODE_INSTALL_PRINTER_DRIVERS)
		return "install-printer-drivers";

	g_assert_not_reached ();
}

static gchar *
build_comma_separated_list (gchar **items)
{
	guint len;

	len = g_strv_length (items);
	if (len == 2) {
		/* TRANSLATORS: separator for a list of items */
		return g_strjoinv (_(" and "), items);
	} else {
		/* TRANSLATORS: separator for a list of items */
		return g_strjoinv (_(", "), items);
	}
}

static gchar *
build_title (GsShellExtras *self)
{
	guint i;
	g_autofree gchar *titles = NULL;
	g_autoptr(GPtrArray) title_array = NULL;

	title_array = g_ptr_array_new ();
	for (i = 0; i < self->array_search_data->len; i++) {
		SearchData *search_data;

		search_data = g_ptr_array_index (self->array_search_data, i);
		g_ptr_array_add (title_array, search_data->title);
	}
	g_ptr_array_add (title_array, NULL);

	titles = build_comma_separated_list ((gchar **) title_array->pdata);

	switch (self->mode) {
	case GS_SHELL_EXTRAS_MODE_INSTALL_FONTCONFIG_RESOURCES:
		/* TRANSLATORS: Application window title for fonts installation.
		   %s will be replaced by name of the script we're searching for. */
		return g_strdup_printf (ngettext ("Available fonts for the %s script",
		                                  "Available fonts for the %s scripts",
		                                  self->array_search_data->len),
		                        titles);
		break;
	default:
		/* TRANSLATORS: Application window title for codec installation.
		   %s will be replaced by actual codec name(s) */
		return g_strdup_printf (ngettext ("Available software for %s",
		                                  "Available software for %s",
		                                  self->array_search_data->len),
		                        titles);
		break;
	}
}

static void
gs_shell_extras_update_ui_state (GsShellExtras *self)
{
	GtkWidget *widget;
	g_autofree gchar *title = NULL;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_EXTRAS)
		return;

	/* main spinner */
	switch (self->state) {
	case GS_SHELL_EXTRAS_STATE_LOADING:
		gs_start_spinner (GTK_SPINNER (self->spinner));
		break;
	case GS_SHELL_EXTRAS_STATE_READY:
	case GS_SHELL_EXTRAS_STATE_NO_RESULTS:
	case GS_SHELL_EXTRAS_STATE_FAILED:
		gs_stop_spinner (GTK_SPINNER (self->spinner));
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* headerbar title */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "application_details_header"));
	switch (self->state) {
	case GS_SHELL_EXTRAS_STATE_LOADING:
	case GS_SHELL_EXTRAS_STATE_READY:
		title = build_title (self);
		gtk_label_set_label (GTK_LABEL (widget), title);
		break;
	case GS_SHELL_EXTRAS_STATE_NO_RESULTS:
	case GS_SHELL_EXTRAS_STATE_FAILED:
		gtk_label_set_label (GTK_LABEL (widget), _("Unable to Find Requested Software"));
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* stack */
	switch (self->state) {
	case GS_SHELL_EXTRAS_STATE_LOADING:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "spinner");
		break;
	case GS_SHELL_EXTRAS_STATE_READY:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "results");
		break;
	case GS_SHELL_EXTRAS_STATE_NO_RESULTS:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "no-results");
		break;
	case GS_SHELL_EXTRAS_STATE_FAILED:
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "failed");
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
gs_shell_extras_set_state (GsShellExtras *self,
			    GsShellExtrasState state)
{
	self->state = state;
	gs_shell_extras_update_ui_state (self);
}

static void
app_row_button_clicked_cb (GsAppRow *app_row,
                           GsShellExtras *self)
{
	GsApp *app;
	app = gs_app_row_get_app (app_row);
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE ||
	    gs_app_get_state (app) == AS_APP_STATE_AVAILABLE_LOCAL)
		gs_page_install_app (GS_PAGE (self), app);
	else if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
		gs_page_remove_app (GS_PAGE (self), app);
	else
		g_critical ("extras: app in unexpected state %d", gs_app_get_state (app));
}

static void
gs_shell_extras_add_app (GsShellExtras *self, GsApp *app, SearchData *search_data)
{
	GtkWidget *app_row;
	GList *l;
	g_autoptr(GList) list = NULL;

	/* Don't add same app twice */
	list = gtk_container_get_children (GTK_CONTAINER (self->list_box_results));
	for (l = list; l != NULL; l = l->next) {
		GsApp *existing_app;

		existing_app = gs_app_row_get_app (GS_APP_ROW (l->data));
		if (app == existing_app)
			gtk_container_remove (GTK_CONTAINER (self->list_box_results),
			                      GTK_WIDGET (l->data));
	}

	app_row = gs_app_row_new (app);
	gs_app_row_set_show_codec (GS_APP_ROW (app_row), TRUE);

	g_object_set_data_full (G_OBJECT (app_row), "missing-title", g_strdup (search_data->title), g_free);

	g_signal_connect (app_row, "button-clicked",
	                  G_CALLBACK (app_row_button_clicked_cb),
	                  self);

	gtk_container_add (GTK_CONTAINER (self->list_box_results), app_row);
	gs_app_row_set_size_groups (GS_APP_ROW (app_row),
				    self->sizegroup_image,
				    self->sizegroup_name);
	gtk_widget_show (app_row);
}

static GsApp *
create_missing_app (SearchData *search_data)
{
	GsShellExtras *self = search_data->self;
	GsApp *app;
	GString *summary_missing;
	g_autofree gchar *name = NULL;
	g_autofree gchar *url = NULL;

	app = gs_app_new ("missing-codec");

	/* TRANSLATORS: This string is used for codecs that weren't found */
	name = g_strdup_printf (_("%s not found"), search_data->title);
	gs_app_set_name (app, GS_APP_QUALITY_HIGHEST, name);

	/* TRANSLATORS: hyperlink title */
	url = g_strdup_printf ("<a href=\"%s\">%s</a>", search_data->url_not_found, _("on the website"));

	summary_missing = g_string_new ("");
	switch (self->mode) {
	case GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_FILES:
		/* TRANSLATORS: this is when we know about an application or
		 * addon, but it can't be listed for some reason */
		g_string_append_printf (summary_missing, _("No applications are available that provide the file %s."), search_data->title);
		g_string_append (summary_missing, "\n");
		/* TRANSLATORS: first %s is the codec name, and second %s is a
                 * hyperlink with the "on the website" text */
		g_string_append_printf (summary_missing, _("Information about %s, as well as options "
					"for how to get missing applications "
					"might be found %s."), search_data->title, url);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_PROVIDE_FILES:
		/* TRANSLATORS: this is when we know about an application or
		 * addon, but it can't be listed for some reason */
		g_string_append_printf (summary_missing, _("No applications are available for %s support."), search_data->title);
		g_string_append (summary_missing, "\n");
		/* TRANSLATORS: first %s is the codec name, and second %s is a
                 * hyperlink with the "on the website" text */
		g_string_append_printf (summary_missing, _("Information about %s, as well as options "
					"for how to get missing applications "
					"might be found %s."), search_data->title, url);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_NAMES:
		/* TRANSLATORS: this is when we know about an application or
		 * addon, but it can't be listed for some reason */
		g_string_append_printf (summary_missing, _("%s is not available."), search_data->title);
		g_string_append (summary_missing, "\n");
		/* TRANSLATORS: first %s is the codec name, and second %s is a
                 * hyperlink with the "on the website" text */
		g_string_append_printf (summary_missing, _("Information about %s, as well as options "
					"for how to get missing applications "
					"might be found %s."), search_data->title, url);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_MIME_TYPES:
		/* TRANSLATORS: this is when we know about an application or
		 * addon, but it can't be listed for some reason */
		g_string_append_printf (summary_missing, _("No applications are available for %s support."), search_data->title);
		g_string_append (summary_missing, "\n");
		/* TRANSLATORS: first %s is the codec name, and second %s is a
                 * hyperlink with the "on the website" text */
		g_string_append_printf (summary_missing, _("Information about %s, as well as options "
					"for how to get an application that can support this format "
					"might be found %s."), search_data->title, url);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_FONTCONFIG_RESOURCES:
		/* TRANSLATORS: this is when we know about an application or
		 * addon, but it can't be listed for some reason */
		g_string_append_printf (summary_missing, _("No fonts are available for the %s script support."), search_data->title);
		g_string_append (summary_missing, "\n");
		/* TRANSLATORS: first %s is the codec name, and second %s is a
                 * hyperlink with the "on the website" text */
		g_string_append_printf (summary_missing, _("Information about %s, as well as options "
					"for how to get additional fonts "
					"might be found %s."), search_data->title, url);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_GSTREAMER_RESOURCES:
		/* TRANSLATORS: this is when we know about an application or
		 * addon, but it can't be listed for some reason */
		g_string_append_printf (summary_missing, _("No addon codecs are available for the %s format."), search_data->title);
		g_string_append (summary_missing, "\n");
		/* TRANSLATORS: first %s is the codec name, and second %s is a
                 * hyperlink with the "on the website" text */
		g_string_append_printf (summary_missing, _("Information about %s, as well as options "
					"for how to get a codec that can play this format "
					"might be found %s."), search_data->title, url);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_PLASMA_RESOURCES:
		/* TRANSLATORS: this is when we know about an application or
		 * addon, but it can't be listed for some reason */
		g_string_append_printf (summary_missing, _("No Plasma resources are available for %s support."), search_data->title);
		g_string_append (summary_missing, "\n");
		/* TRANSLATORS: first %s is the codec name, and second %s is a
                 * hyperlink with the "on the website" text */
		g_string_append_printf (summary_missing, _("Information about %s, as well as options "
					"for how to get additional Plasma resources "
					"might be found %s."), search_data->title, url);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_PRINTER_DRIVERS:
		/* TRANSLATORS: this is when we know about an application or
		 * addon, but it can't be listed for some reason */
		g_string_append_printf (summary_missing, _("No printer drivers are available for %s."), search_data->title);
		g_string_append (summary_missing, "\n");
		/* TRANSLATORS: first %s is the codec name, and second %s is a
		 * hyperlink with the "on the website" text */
		g_string_append_printf (summary_missing, _("Information about %s, as well as options "
					"for how to get a driver that supports this printer "
					"might be found %s."), search_data->title, url);

		break;
	default:
		g_assert_not_reached ();
		break;
	}
	gs_app_set_summary_missing (app, g_string_free (summary_missing, FALSE));

	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);
	gs_app_set_url (app, AS_URL_KIND_MISSING, search_data->url_not_found);

	return app;
}

static gchar *
build_no_results_label (GsShellExtras *self)
{
	GList *l;
	GsApp *app = NULL;
	guint num;
	g_autofree gchar *codec_titles = NULL;
	g_autofree gchar *url = NULL;
	g_autoptr(GList) list = NULL;
	g_autoptr(GPtrArray) array = NULL;

	list = gtk_container_get_children (GTK_CONTAINER (self->list_box_results));
	num = g_list_length (list);

	g_assert (num > 0);

	array = g_ptr_array_new ();
	for (l = list; l != NULL; l = l->next) {
		app = gs_app_row_get_app (GS_APP_ROW (l->data));
		g_ptr_array_add (array,
		                 g_object_get_data (G_OBJECT (l->data), "missing-title"));
	}
	g_ptr_array_add (array, NULL);

	/* TRANSLATORS: hyperlink title */
	url = g_strdup_printf ("<a href=\"%s\">%s</a>",
	                       gs_app_get_url (app, AS_URL_KIND_MISSING),
                               _("this website"));

	codec_titles = build_comma_separated_list ((gchar **) array->pdata);
	/* TRANSLATORS: no codecs were found. First %s will be replaced by actual codec name(s), second %s is a link titled "this website" */
	return g_strdup_printf (ngettext ("Unfortunately, the %s you were searching for could not be found. Please see %s for more information.",
	                                  "Unfortunately, the %s you were searching for could not be found. Please see %s for more information.",
	                                  num),
	                        codec_titles,
	                        url);
}

static void
show_search_results (GsShellExtras *self)
{
	GsApp *app;
	GList *l;
	guint n_children;
	guint n_missing;
	g_autoptr(GList) list = NULL;

	list = gtk_container_get_children (GTK_CONTAINER (self->list_box_results));
	n_children = g_list_length (list);

	/* count the number of rows with missing codecs */
	n_missing = 0;
	for (l = list; l != NULL; l = l->next) {
		app = gs_app_row_get_app (GS_APP_ROW (l->data));
		if (g_strcmp0 (gs_app_get_id (app), "missing-codec") == 0) {
			n_missing++;
		}
	}

	if (n_children == 0 || n_children == n_missing) {
		g_autofree gchar *str = NULL;

		/* no results */
		g_debug ("extras: failed to find any results, %d", n_missing);
		str = build_no_results_label (self);
		gtk_label_set_label (GTK_LABEL (self->label_no_results), str);
		gs_shell_extras_set_state (self,
		                           GS_SHELL_EXTRAS_STATE_NO_RESULTS);
	} else if (n_children == 1) {
		/* switch directly to details view */
		g_debug ("extras: found one result, showing in details view");
		app = gs_app_row_get_app (GS_APP_ROW (list->data));
		gs_shell_change_mode (self->shell, GS_SHELL_MODE_DETAILS, app, TRUE);
	} else {
		/* show what we got */
		g_debug ("extras: got %d search results, showing", n_children);
		gs_shell_extras_set_state (self,
		                           GS_SHELL_EXTRAS_STATE_READY);
	}
}

static void
search_files_cb (GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
	SearchData *search_data = (SearchData *) user_data;
	GsShellExtras *self = search_data->self;
	g_autoptr(GsAppList) list = NULL;
	guint i;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;

	list = gs_plugin_loader_search_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_debug ("extras: search files cancelled");
			return;
		}
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED)) {
			GsApp *app;

			g_debug ("extras: no search result for %s, showing as missing", search_data->title);
			app = create_missing_app (search_data);
			gs_app_list_add (list, app);
		} else {
			g_autofree gchar *str = NULL;

			g_warning ("failed to find any search results: %s", error->message);
			str = g_strdup_printf ("%s: %s", _("Failed to find any search results"), error->message);
			gtk_label_set_label (GTK_LABEL (self->label_failed), str);
			gs_shell_extras_set_state (self,
						    GS_SHELL_EXTRAS_STATE_FAILED);
			return;
		}
	}

	for (i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		g_debug ("%s\n\n", gs_app_to_string (app));
		gs_shell_extras_add_app (self, app, search_data);
	}

	self->pending_search_cnt--;

	/* have all searches finished? */
	if (self->pending_search_cnt == 0)
		show_search_results (self);
}

static void
file_to_app_cb (GObject *source_object,
                GAsyncResult *res,
                gpointer user_data)
{
	SearchData *search_data = (SearchData *) user_data;
	GsShellExtras *self = search_data->self;
	GsApp *app;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;

	app = gs_plugin_loader_file_to_app_finish (plugin_loader, res, &error);
	if (app == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_debug ("extras: search what provides cancelled");
			return;
		}
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED)) {
			g_debug ("extras: no search result for %s, showing as missing", search_data->title);
			app = create_missing_app (search_data);
		} else {
			g_autofree gchar *str = NULL;

			g_warning ("failed to find any search results: %s", error->message);
			str = g_strdup_printf ("%s: %s", _("Failed to find any search results"), error->message);
			gtk_label_set_label (GTK_LABEL (self->label_failed), str);
			gs_shell_extras_set_state (self,
						    GS_SHELL_EXTRAS_STATE_FAILED);
			return;
		}
	}

	g_debug ("%s\n\n", gs_app_to_string (app));
	gs_shell_extras_add_app (self, app, search_data);

	self->pending_search_cnt--;

	/* have all searches finished? */
	if (self->pending_search_cnt == 0)
		show_search_results (self);

	g_object_unref (app);
}

static void
get_search_what_provides_cb (GObject *source_object,
                             GAsyncResult *res,
                             gpointer user_data)
{
	SearchData *search_data = (SearchData *) user_data;
	GsShellExtras *self = search_data->self;
	g_autoptr(GsAppList) list = NULL;
	guint i;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;

	list = gs_plugin_loader_search_what_provides_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_debug ("extras: search what provides cancelled");
			return;
		}
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED)) {
			GsApp *app;

			g_debug ("extras: no search result for %s, showing as missing", search_data->title);
			app = create_missing_app (search_data);
			gs_app_list_add (list, app);
		} else {
			g_autofree gchar *str = NULL;

			g_warning ("failed to find any search results: %s", error->message);
			str = g_strdup_printf ("%s: %s", _("Failed to find any search results"), error->message);
			gtk_label_set_label (GTK_LABEL (self->label_failed), str);
			gs_shell_extras_set_state (self,
						    GS_SHELL_EXTRAS_STATE_FAILED);
			return;
		}
	}

	for (i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		g_debug ("%s\n\n", gs_app_to_string (app));
		gs_shell_extras_add_app (self, app, search_data);
	}

	self->pending_search_cnt--;

	/* have all searches finished? */
	if (self->pending_search_cnt == 0)
		show_search_results (self);
}

static void
gs_shell_extras_load (GsShellExtras *self, GPtrArray *array_search_data)
{
	guint i;

	/* cancel any pending searches */
	if (self->search_cancellable != NULL) {
		g_cancellable_cancel (self->search_cancellable);
		g_object_unref (self->search_cancellable);
	}
	self->search_cancellable = g_cancellable_new ();

	if (array_search_data != NULL) {
		if (self->array_search_data != NULL)
			g_ptr_array_unref (self->array_search_data);
		self->array_search_data = g_ptr_array_ref (array_search_data);
	}

	self->pending_search_cnt = 0;

	/* remove old entries */
	gs_container_remove_all (GTK_CONTAINER (self->list_box_results));

	/* set state as loading */
	self->state = GS_SHELL_EXTRAS_STATE_LOADING;

	/* start new searches, separate one for each codec */
	for (i = 0; i < self->array_search_data->len; i++) {
		SearchData *search_data;

		search_data = g_ptr_array_index (self->array_search_data, i);
		if (search_data->search_filename != NULL) {
			g_debug ("searching filename: '%s'", search_data->search_filename);
			gs_plugin_loader_search_files_async (self->plugin_loader,
			                                     search_data->search_filename,
			                                     GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
			                                     GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
			                                     GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES,
			                                     self->search_cancellable,
			                                     search_files_cb,
			                                     search_data);
		} else if (search_data->package_filename != NULL) {
			g_autoptr (GFile) file = NULL;
			g_debug ("resolving filename to app: '%s'", search_data->package_filename);
			file = g_file_new_for_path (search_data->package_filename);
			gs_plugin_loader_file_to_app_async (self->plugin_loader,
			                                    file,
			                                    GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
			                                    GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
			                                    GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES,
			                                    self->search_cancellable,
			                                    file_to_app_cb,
			                                    search_data);
		} else {
			g_debug ("searching what provides: '%s'", search_data->search);
			gs_plugin_loader_search_what_provides_async (self->plugin_loader,
			                                             search_data->search,
			                                             GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
			                                             GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
			                                             GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
			                                             GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
			                                             GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
			                                             GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
			                                             GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
			                                             GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING |
			                                             GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES,
			                                             self->search_cancellable,
			                                             get_search_what_provides_cb,
			                                             search_data);
		}
		self->pending_search_cnt++;
	}
}

void
gs_shell_extras_reload (GsShellExtras *self)
{
	if (self->array_search_data != NULL)
		gs_shell_extras_load (self, NULL);
}

static void
gs_shell_extras_search_package_files (GsShellExtras *self, gchar **files)
{
	g_autoptr(GPtrArray) array_search_data = g_ptr_array_new_with_free_func ((GDestroyNotify) search_data_free);
	guint i;

	for (i = 0; files[i] != NULL; i++) {
		SearchData *search_data;

		search_data = g_slice_new0 (SearchData);
		search_data->title = g_strdup (files[i]);
		search_data->package_filename = g_strdup (files[i]);
		search_data->url_not_found = gs_vendor_get_not_found_url (self->vendor, GS_VENDOR_URL_TYPE_DEFAULT);
		search_data->self = g_object_ref (self);
		g_ptr_array_add (array_search_data, search_data);
	}

	gs_shell_extras_load (self, array_search_data);
}

static void
gs_shell_extras_search_provide_files (GsShellExtras *self, gchar **files)
{
	g_autoptr(GPtrArray) array_search_data = g_ptr_array_new_with_free_func ((GDestroyNotify) search_data_free);
	guint i;

	for (i = 0; files[i] != NULL; i++) {
		SearchData *search_data;

		search_data = g_slice_new0 (SearchData);
		search_data->title = g_strdup (files[i]);
		search_data->search_filename = g_strdup (files[i]);
		search_data->url_not_found = gs_vendor_get_not_found_url (self->vendor, GS_VENDOR_URL_TYPE_DEFAULT);
		search_data->self = g_object_ref (self);
		g_ptr_array_add (array_search_data, search_data);
	}

	gs_shell_extras_load (self, array_search_data);
}

static void
gs_shell_extras_search_package_names (GsShellExtras *self, gchar **package_names)
{
	g_autoptr(GPtrArray) array_search_data = g_ptr_array_new_with_free_func ((GDestroyNotify) search_data_free);
	guint i;

	for (i = 0; package_names[i] != NULL; i++) {
		SearchData *search_data;

		search_data = g_slice_new0 (SearchData);
		search_data->title = g_strdup (package_names[i]);
		search_data->search = g_strdup (package_names[i]);
		search_data->url_not_found = gs_vendor_get_not_found_url (self->vendor, GS_VENDOR_URL_TYPE_DEFAULT);
		search_data->self = g_object_ref (self);
		g_ptr_array_add (array_search_data, search_data);
	}

	gs_shell_extras_load (self, array_search_data);
}

static void
gs_shell_extras_search_mime_types (GsShellExtras *self, gchar **mime_types)
{
	g_autoptr(GPtrArray) array_search_data = g_ptr_array_new_with_free_func ((GDestroyNotify) search_data_free);
	guint i;

	for (i = 0; mime_types[i] != NULL; i++) {
		SearchData *search_data;

		search_data = g_slice_new0 (SearchData);
		search_data->title = g_strdup_printf (_("%s file format"), mime_types[i]);
		search_data->search = g_strdup (mime_types[i]);
		search_data->url_not_found = gs_vendor_get_not_found_url (self->vendor, GS_VENDOR_URL_TYPE_MIME);
		search_data->self = g_object_ref (self);
		g_ptr_array_add (array_search_data, search_data);
	}

	gs_shell_extras_load (self, array_search_data);
}

static gchar *
font_tag_to_lang (const gchar *tag)
{
	if (g_str_has_prefix (tag, ":lang="))
		return g_strdup (tag + 6);

	return NULL;
}

static gchar *
gs_shell_extras_font_tag_to_localised_name (GsShellExtras *self, const gchar *tag)
{
	gchar *name;
	g_autofree gchar *lang = NULL;
	g_autofree gchar *language = NULL;

	/* use fontconfig to get the language code */
	lang = font_tag_to_lang (tag);
	if (lang == NULL) {
		g_warning ("Could not parse language tag '%s'", tag);
		return NULL;
	}

	/* convert to localisable name */
	language = gs_language_iso639_to_language (self->language, lang);
	if (language == NULL) {
		g_warning ("Could not match language code '%s' to an ISO639 language", lang);
		return NULL;
	}

	/* get translation, or return untranslated string */
	name = g_strdup (dgettext("iso_639", language));
	if (name == NULL)
		name = g_strdup (language);

	return name;
}

static void
gs_shell_extras_search_fontconfig_resources (GsShellExtras *self, gchar **resources)
{
	g_autoptr(GPtrArray) array_search_data = g_ptr_array_new_with_free_func ((GDestroyNotify) search_data_free);
	guint i;

	for (i = 0; resources[i] != NULL; i++) {
		SearchData *search_data;

		search_data = g_slice_new0 (SearchData);
		search_data->title = gs_shell_extras_font_tag_to_localised_name (self, resources[i]);
		search_data->search = g_strdup (resources[i]);
		search_data->url_not_found = gs_vendor_get_not_found_url (self->vendor, GS_VENDOR_URL_TYPE_FONT);
		search_data->self = g_object_ref (self);
		g_ptr_array_add (array_search_data, search_data);
	}

	gs_shell_extras_load (self, array_search_data);
}

static void
gs_shell_extras_search_gstreamer_resources (GsShellExtras *self, gchar **resources)
{
	g_autoptr(GPtrArray) array_search_data = g_ptr_array_new_with_free_func ((GDestroyNotify) search_data_free);
	guint i;

	for (i = 0; resources[i] != NULL; i++) {
		SearchData *search_data;
		g_auto(GStrv) parts = NULL;

		parts = g_strsplit (resources[i], "|", 2);

		search_data = g_slice_new0 (SearchData);
		search_data->title = g_strdup (parts[0]);
		search_data->search = g_strdup (parts[1]);
		search_data->url_not_found = gs_vendor_get_not_found_url (self->vendor, GS_VENDOR_URL_TYPE_CODEC);
		search_data->self = g_object_ref (self);
		g_ptr_array_add (array_search_data, search_data);
	}

	gs_shell_extras_load (self, array_search_data);
}

static void
gs_shell_extras_search_plasma_resources (GsShellExtras *self, gchar **resources)
{
	g_autoptr(GPtrArray) array_search_data = g_ptr_array_new_with_free_func ((GDestroyNotify) search_data_free);
	guint i;

	for (i = 0; resources[i] != NULL; i++) {
		SearchData *search_data;

		search_data = g_slice_new0 (SearchData);
		search_data->title = g_strdup (resources[i]);
		search_data->search = g_strdup (resources[i]);
		search_data->url_not_found = gs_vendor_get_not_found_url (self->vendor, GS_VENDOR_URL_TYPE_DEFAULT);
		search_data->self = g_object_ref (self);
		g_ptr_array_add (array_search_data, search_data);
	}

	gs_shell_extras_load (self, array_search_data);
}

static void
gs_shell_extras_search_printer_drivers (GsShellExtras *self, gchar **device_ids)
{
	g_autoptr(GPtrArray) array_search_data = g_ptr_array_new_with_free_func ((GDestroyNotify) search_data_free);
	guint i, j;
	guint len;

	len = g_strv_length (device_ids);
	if (len > 1)
		/* hardcode for now as we only support one at a time */
		len = 1;

	/* make a list of provides tags */
	for (i = 0; i < len; i++) {
		SearchData *search_data;
		gchar *p;
		guint n_fields;
		g_autofree gchar *tag = NULL;
		g_autofree gchar *mfg = NULL;
		g_autofree gchar *mdl = NULL;
		g_auto(GStrv) fields = NULL;

		fields = g_strsplit (device_ids[i], ";", 0);
		n_fields = g_strv_length (fields);
		mfg = mdl = NULL;
		for (j = 0; j < n_fields && (!mfg || !mdl); j++) {
			if (g_str_has_prefix (fields[j], "MFG:"))
				mfg = g_strdup (fields[j] + 4);
			else if (g_str_has_prefix (fields[j], "MDL:"))
				mdl = g_strdup (fields[j] + 4);
		}

		if (!mfg || !mdl) {
			g_warning("invalid line '%s', missing field",
				    device_ids[i]);
			continue;
		}

		tag = g_strdup_printf ("%s;%s;", mfg, mdl);

		/* Replace spaces with underscores */
		for (p = tag; *p != '\0'; p++)
			if (*p == ' ')
				*p = '_';

		search_data = g_slice_new0 (SearchData);
		search_data->title = g_strdup_printf ("%s %s", mfg, mdl);
		search_data->search = g_ascii_strdown (tag, -1);
		search_data->url_not_found = gs_vendor_get_not_found_url (self->vendor, GS_VENDOR_URL_TYPE_HARDWARE);
		search_data->self = g_object_ref (self);
		g_ptr_array_add (array_search_data, search_data);
	}

	gs_shell_extras_load (self, array_search_data);
}

void
gs_shell_extras_search (GsShellExtras  *self,
                        const gchar    *mode_str,
                        gchar         **resources)
{
	self->mode = gs_shell_extras_mode_from_string (mode_str);
	switch (self->mode) {
	case GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_FILES:
		gs_shell_extras_search_package_files (self, resources);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_PROVIDE_FILES:
		gs_shell_extras_search_provide_files (self, resources);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_NAMES:
		gs_shell_extras_search_package_names (self, resources);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_MIME_TYPES:
		gs_shell_extras_search_mime_types (self, resources);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_FONTCONFIG_RESOURCES:
		gs_shell_extras_search_fontconfig_resources (self, resources);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_GSTREAMER_RESOURCES:
		gs_shell_extras_search_gstreamer_resources (self, resources);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_PLASMA_RESOURCES:
		gs_shell_extras_search_plasma_resources (self, resources);
		break;
	case GS_SHELL_EXTRAS_MODE_INSTALL_PRINTER_DRIVERS:
		gs_shell_extras_search_printer_drivers (self, resources);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
gs_shell_extras_switch_to (GsPage *page,
                           gboolean scroll_up)
{
	GsShellExtras *self = GS_SHELL_EXTRAS (page);
	GtkWidget *widget;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_EXTRAS) {
		g_warning ("Called switch_to(codecs) when in mode %s",
			   gs_shell_get_mode_string (self->shell));
		return;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "application_details_header"));
	gtk_widget_show (widget);

	if (scroll_up) {
		GtkAdjustment *adj;
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	gs_shell_extras_update_ui_state (self);
}

static void
row_activated_cb (GtkListBox *list_box,
                  GtkListBoxRow *row,
                  GsShellExtras *self)
{
	GsApp *app;

	app = gs_app_row_get_app (GS_APP_ROW (row));

	if (gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE &&
	    gs_app_get_url (app, AS_URL_KIND_MISSING) != NULL) {
		gs_app_show_url (app, AS_URL_KIND_MISSING);
	} else {
		gs_shell_show_app (self->shell, app);
	}
}

static gchar *
get_app_sort_key (GsApp *app)
{
	g_autoptr(GString) key = NULL;

	key = g_string_sized_new (64);

	/* sort missing applications as last */
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_UNAVAILABLE:
		g_string_append (key, "9:");
		break;
	default:
		g_string_append (key, "1:");
		break;
	}

	/* finally, sort by short name */
	g_string_append (key, gs_app_get_name (app));

	return g_utf8_casefold (key->str, key->len);
}

static gint
list_sort_func (GtkListBoxRow *a,
                GtkListBoxRow *b,
                gpointer user_data)
{
	GsApp *a1 = gs_app_row_get_app (GS_APP_ROW (a));
	GsApp *a2 = gs_app_row_get_app (GS_APP_ROW (b));
	g_autofree gchar *key1 = get_app_sort_key (a1);
	g_autofree gchar *key2 = get_app_sort_key (a2);

	/* compare the keys according to the algorithm above */
	return g_strcmp0 (key1, key2);
}

static void
list_header_func (GtkListBoxRow *row,
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

void
gs_shell_extras_setup (GsShellExtras *self,
			GsShell *shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	g_return_if_fail (GS_IS_SHELL_EXTRAS (self));

	self->shell = shell;

	self->plugin_loader = g_object_ref (plugin_loader);
	self->builder = g_object_ref (builder);

	g_signal_connect (self->list_box_results, "row-activated",
			  G_CALLBACK (row_activated_cb), self);
	gtk_list_box_set_header_func (GTK_LIST_BOX (self->list_box_results),
				      list_header_func,
				      self, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->list_box_results),
				    list_sort_func,
				    self, NULL);

	/* chain up */
	gs_page_setup (GS_PAGE (self),
	               shell,
	               plugin_loader,
	               cancellable);
}

static void
gs_shell_extras_dispose (GObject *object)
{
	GsShellExtras *self = GS_SHELL_EXTRAS (object);

	if (self->search_cancellable != NULL) {
		g_cancellable_cancel (self->search_cancellable);
		g_clear_object (&self->search_cancellable);
	}

	g_clear_object (&self->sizegroup_image);
	g_clear_object (&self->sizegroup_name);
	g_clear_object (&self->language);
	g_clear_object (&self->vendor);
	g_clear_object (&self->builder);
	g_clear_object (&self->plugin_loader);

	g_clear_pointer (&self->array_search_data, g_ptr_array_unref);

	G_OBJECT_CLASS (gs_shell_extras_parent_class)->dispose (object);
}

static void
gs_shell_extras_init (GsShellExtras *self)
{
	g_autoptr(GError) error = NULL;

	gtk_widget_init_template (GTK_WIDGET (self));

	self->state = GS_SHELL_EXTRAS_STATE_LOADING;
	self->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->vendor = gs_vendor_new ();

	/* map ISO639 to language names */
	self->language = gs_language_new ();
	gs_language_populate (self->language, &error);
	if (error != NULL)
		g_error ("Failed to map ISO639 to language names: %s", error->message);
}

static void
gs_shell_extras_class_init (GsShellExtrasClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_extras_dispose;
	page_class->switch_to = gs_shell_extras_switch_to;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-extras.ui");

	gtk_widget_class_bind_template_child (widget_class, GsShellExtras, label_failed);
	gtk_widget_class_bind_template_child (widget_class, GsShellExtras, label_no_results);
	gtk_widget_class_bind_template_child (widget_class, GsShellExtras, list_box_results);
	gtk_widget_class_bind_template_child (widget_class, GsShellExtras, scrolledwindow);
	gtk_widget_class_bind_template_child (widget_class, GsShellExtras, spinner);
	gtk_widget_class_bind_template_child (widget_class, GsShellExtras, stack);
}

GsShellExtras *
gs_shell_extras_new (void)
{
	GsShellExtras *self;
	self = g_object_new (GS_TYPE_SHELL_EXTRAS, NULL);
	return GS_SHELL_EXTRAS (self);
}

/* vim: set noexpandtab: */
