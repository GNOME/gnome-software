/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <string.h>
#include <glib/gi18n.h>

#include "gs-shell-search.h"
#include "gs-shell.h"
#include "gs-app.h"
#include "gs-utils.h"
#include "gs-app-widget.h"

static void	gs_shell_search_finalize	(GObject	*object);

#define GS_SHELL_SEARCH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_SEARCH, GsShellSearchPrivate))

struct GsShellSearchPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GtkListBox		*list_box_search;
	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	gboolean		 waiting;
        GsShell                 *shell;
};

G_DEFINE_TYPE (GsShellSearch, gs_shell_search, G_TYPE_OBJECT)

static void
gs_shell_search_app_widget_activated_cb (GtkListBox *list_box,
                                         GtkListBoxRow *row,
					 GsShellSearch *shell_search)
{
	GsApp *app;
        GsAppWidget *app_widget;

        app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	app = gs_app_widget_get_app (app_widget);
        gs_shell_show_app (shell_search->priv->shell, app);
}

/**
 * gs_shell_search_finished_func:
 **/
static void
gs_shell_search_finished_func (GsPluginLoader *plugin_loader, GsApp *app, gpointer user_data)
{
}

/**
 * gs_shell_search_app_remove:
 **/
static void
gs_shell_search_app_remove (GsShellSearch *shell_search, GsApp *app)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GString *markup;
	GtkResponseType response;
	GtkWidget *dialog;
	GtkWindow *window;

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
		g_debug ("remove %s", gs_app_get_id (app));
		gs_plugin_loader_app_remove (priv->plugin_loader,
					     app,
					     priv->cancellable,
					     gs_shell_search_finished_func,
					     shell_search);
	}
	g_string_free (markup, TRUE);
	gtk_widget_destroy (dialog);
}

/**
 * gs_shell_search_app_install:
 **/
static void
gs_shell_search_app_install (GsShellSearch *shell_search, GsApp *app)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	gs_plugin_loader_app_install (priv->plugin_loader,
				      app,
				      priv->cancellable,
				      gs_shell_search_finished_func,
				      shell_search);
}

/**
 * gs_shell_search_app_widget_clicked_cb:
 **/
static void
gs_shell_search_app_widget_clicked_cb (GsAppWidget *app_widget,
				       GsShellSearch *shell_search)
{
	GsApp *app;
	app = gs_app_widget_get_app (app_widget);
	if (gs_app_get_state (app) == GS_APP_STATE_AVAILABLE)
		gs_shell_search_app_install (shell_search, app);
	else if (gs_app_get_state (app) == GS_APP_STATE_INSTALLED)
		gs_shell_search_app_remove (shell_search, app);
}

/**
 * gs_shell_search_get_search_cb:
 **/
static void
gs_shell_search_get_search_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	GsShellSearch *shell_search = GS_SHELL_SEARCH (user_data);
	GsShellSearchPrivate *priv = shell_search->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *widget;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_search"));
        gs_stop_spinner (GTK_SPINNER (widget));
        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_search"));

        priv->waiting = FALSE;

	list = gs_plugin_loader_search_finish (plugin_loader, res, &error);
	if (list == NULL) {
		g_warning ("failed to get search apps: %s", error->message);
		g_error_free (error);
                gtk_stack_set_visible_child_name (GTK_STACK (widget), "no-results");
		goto out;
	}
        gtk_stack_set_visible_child_name (GTK_STACK (widget), "results");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		g_debug ("adding search %s", gs_app_get_id (app));
		widget = gs_app_widget_new ();
		g_signal_connect (widget, "button-clicked",
				  G_CALLBACK (gs_shell_search_app_widget_clicked_cb),
				  shell_search);
		gs_app_widget_set_app (GS_APP_WIDGET (widget), app);
		gtk_container_add (GTK_CONTAINER (priv->list_box_search), widget);
		gs_app_widget_set_size_groups (GS_APP_WIDGET (widget),
					       priv->sizegroup_image,
					       priv->sizegroup_name);
		gtk_widget_show (widget);
	}

out: ;
}

/**
 * gs_shell_search_refresh:
 **/
void
gs_shell_search_refresh (GsShellSearch *shell_search, const gchar *value)
{
	GsShellSearchPrivate *priv = shell_search->priv;
        GtkWidget *widget;
        GtkSpinner *spinner;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
        gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	gtk_widget_show (widget);

        if (priv->waiting)
                return;

	/* remove old entries */
	gs_container_remove_all (GTK_CONTAINER (priv->list_box_search));

	/* search for apps */
	gs_plugin_loader_search_async (priv->plugin_loader,
				       value,
				       priv->cancellable,
				       gs_shell_search_get_search_cb,
				       shell_search);

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_search"));
        gtk_stack_set_visible_child_name (GTK_STACK (widget), "spinner");
        spinner = GTK_SPINNER (gtk_builder_get_object (priv->builder, "spinner_search"));
        gs_start_spinner (spinner);
	priv->waiting = TRUE;
}

/**
 * gs_shell_search_filter_text_changed_cb:
 **/
static void
gs_shell_search_filter_text_changed_cb (GtkEntry *entry,
					GsShellSearch *shell_search)
{
	/* FIXME: do something? */
}

/**
 * gs_shell_search_sort_func:
 **/
static gint
gs_shell_search_sort_func (GtkListBoxRow *a,
			      GtkListBoxRow *b,
			      gpointer user_data)
{
	GsAppWidget *aw1 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (a)));
	GsAppWidget *aw2 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (b)));
	GsApp *a1 = gs_app_widget_get_app (aw1);
	GsApp *a2 = gs_app_widget_get_app (aw2);
        guint64 date1 = gs_app_get_install_date (a1);
        guint64 date2 = gs_app_get_install_date (a2);

        if (date1 < date2)
                return 1;
        else if (date2 < date1)
                return -1;

	return g_strcmp0 (gs_app_get_name (a1),
			  gs_app_get_name (a2));
}

/**
 * gs_shell_search_utf8_filter_helper:
 **/
static gboolean
gs_shell_search_utf8_filter_helper (const gchar *haystack,
				       const gchar *needle_utf8)
{
	gboolean ret;
	gchar *haystack_utf8;
	haystack_utf8 = g_utf8_casefold (haystack, -1);
	ret = strstr (haystack_utf8, needle_utf8) != NULL;
	g_free (haystack_utf8);
	return ret;
}

/**
 * gs_shell_search_filter_func:
 **/
static gboolean
gs_shell_search_filter_func (GtkListBoxRow *row, void *user_data)
{
	const gchar *tmp;
	gboolean ret = TRUE;
	gchar *needle_utf8 = NULL;
	GsApp *app;
	GsAppWidget *app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	GsShellSearch *shell_search = GS_SHELL_SEARCH (user_data);
	GsShellSearchPrivate *priv = shell_search->priv;
	GtkWidget *widget;

	app = gs_app_widget_get_app (app_widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	tmp = gtk_entry_get_text (GTK_ENTRY (widget));
	if (tmp[0] == '\0')
		goto out;

	needle_utf8 = g_utf8_casefold (tmp, -1);
	ret = gs_shell_search_utf8_filter_helper (gs_app_get_name (app),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_shell_search_utf8_filter_helper (gs_app_get_summary (app),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_shell_search_utf8_filter_helper (gs_app_get_version (app),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_shell_search_utf8_filter_helper (gs_app_get_id (app),
					  needle_utf8);
	if (ret)
		goto out;
out:
	g_free (needle_utf8);
	return ret;
}

/**
 * gs_shell_search_list_header_func
 **/
static void
gs_shell_search_list_header_func (GtkListBoxRow *row,
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
 * gs_shell_search_setup:
 */
void
gs_shell_search_setup (GsShellSearch *shell_search,
                       GsShell *shell,
			  GsPluginLoader *plugin_loader,
			  GtkBuilder *builder,
			  GCancellable *cancellable)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL_SEARCH (shell_search));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);
        priv->shell = shell;

	/* refilter on search box changing */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (GTK_EDITABLE (widget), "search-changed",
			  G_CALLBACK (gs_shell_search_filter_text_changed_cb), shell_search);

	/* setup search */
	priv->list_box_search = GTK_LIST_BOX (gtk_builder_get_object (priv->builder, "list_box_search"));
	g_signal_connect (priv->list_box_search, "row-activated",
			  G_CALLBACK (gs_shell_search_app_widget_activated_cb), shell_search);
	gtk_list_box_set_header_func (priv->list_box_search,
				      gs_shell_search_list_header_func,
				      shell_search, NULL);
	gtk_list_box_set_filter_func (priv->list_box_search,
				      gs_shell_search_filter_func,
				      shell_search, NULL);
	gtk_list_box_set_sort_func (priv->list_box_search,
				    gs_shell_search_sort_func,
				    shell_search, NULL);
}

/**
 * gs_shell_search_class_init:
 **/
static void
gs_shell_search_class_init (GsShellSearchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_search_finalize;

	g_type_class_add_private (klass, sizeof (GsShellSearchPrivate));
}

/**
 * gs_shell_search_init:
 **/
static void
gs_shell_search_init (GsShellSearch *shell_search)
{
	shell_search->priv = GS_SHELL_SEARCH_GET_PRIVATE (shell_search);
	shell_search->priv->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	shell_search->priv->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
}

/**
 * gs_shell_search_finalize:
 **/
static void
gs_shell_search_finalize (GObject *object)
{
	GsShellSearch *shell_search = GS_SHELL_SEARCH (object);
	GsShellSearchPrivate *priv = shell_search->priv;

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);

	G_OBJECT_CLASS (gs_shell_search_parent_class)->finalize (object);
}

/**
 * gs_shell_search_new:
 **/
GsShellSearch *
gs_shell_search_new (void)
{
	GsShellSearch *shell_search;
	shell_search = g_object_new (GS_TYPE_SHELL_SEARCH, NULL);
	return GS_SHELL_SEARCH (shell_search);
}
