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

#include <glib/gi18n.h>

#include "gs-shell.h"
#include "gs-shell-updates.h"
#include "gs-utils.h"
#include "gs-app.h"
#include "gs-app-widget.h"

static void	gs_shell_updates_finalize	(GObject	*object);
static void     show_update_details	     (GsAppWidget *app_widget, GsShellUpdates *shell_updates);

#define GS_SHELL_UPDATES_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_UPDATES, GsShellUpdatesPrivate))

struct GsShellUpdatesPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GtkListBox		*list_box_updates;
	gboolean		 cache_valid;
	gboolean		 waiting;
        GsShell                 *shell;
};

enum {
	COLUMN_UPDATE_APP,
	COLUMN_UPDATE_NAME,
	COLUMN_UPDATE_VERSION,
	COLUMN_UPDATE_LAST
};

G_DEFINE_TYPE (GsShellUpdates, gs_shell_updates, G_TYPE_OBJECT)

/**
 * gs_shell_updates_invalidate:
 **/
void
gs_shell_updates_invalidate (GsShellUpdates *shell_updates)
{
	shell_updates->priv->cache_valid = FALSE;
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
 * gs_shell_updates_get_updates_cb:
 **/
static void
gs_shell_updates_get_updates_cb (GsPluginLoader *plugin_loader,
				 GAsyncResult *res,
				 GsShellUpdates *shell_updates)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkWidget *widget;

        priv->waiting = FALSE;
        priv->cache_valid = TRUE;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_updates"));
        gs_stop_spinner (GTK_SPINNER (widget));

	/* get the results */
	list = gs_plugin_loader_get_updates_finish (plugin_loader, res, &error);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_updates_up_to_date"));
	gtk_widget_set_visible (widget, list == NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	gtk_widget_set_visible (widget, list != NULL);

        if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_UPDATES) {
        	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
	        gtk_widget_set_visible (widget, list != NULL);
        }
	if (list == NULL) {
		g_warning ("failed to get updates: %s", error->message);
		g_error_free (error);
		goto out;
	}
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		g_debug ("adding update %s, kind %d state %d", gs_app_get_id (app), gs_app_get_kind (app), gs_app_get_state (app));
		widget = gs_app_widget_new ();
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
 * gs_shell_updates_refresh:
 **/
void
gs_shell_updates_refresh (GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
        GtkWidget *widget;
        GtkSpinner *spinner;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
        gtk_widget_show (widget);

	/* no need to refresh */
	if (priv->cache_valid) {
                widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
                gtk_widget_show (widget);
		return;
        }

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_updates"));
        gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_updates_up_to_date"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	gtk_widget_show (widget);

        if (priv->waiting)
                return;

	_gtk_container_remove_all (GTK_CONTAINER (priv->list_box_updates));

	gs_plugin_loader_get_updates_async (priv->plugin_loader,
					    priv->cancellable,
					    (GAsyncReadyCallback) gs_shell_updates_get_updates_cb,
					    shell_updates);

        spinner = GTK_SPINNER (gtk_builder_get_object (priv->builder, "spinner_updates"));
        gs_start_spinner (spinner);

	priv->waiting = TRUE;
}

/**
 * gs_shell_updates_set_updates_description_ui:
 **/
static void
gs_shell_updates_set_updates_description_ui (GsShellUpdates *shell_updates, GsApp *app)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
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
	gtk_widget_set_visible (widget, kind == GS_APP_KIND_NORMAL || kind == GS_APP_KIND_SYSTEM);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update_details"));
	gtk_widget_set_visible (widget, kind != GS_APP_KIND_OS_UPDATE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update"));
	gtk_widget_set_visible (widget, kind == GS_APP_KIND_OS_UPDATE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_details"));
	gtk_label_set_label (GTK_LABEL (widget), gs_app_get_metadata_item (app, "update-details"));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "image_update_icon"));
	gtk_image_set_from_pixbuf (GTK_IMAGE (widget), gs_app_get_pixbuf (app));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_name"));
	gtk_label_set_label (GTK_LABEL (widget), gs_app_get_name (app));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_update_summary"));
	gtk_label_set_label (GTK_LABEL (widget), gs_app_get_summary (app));
}

/**
 * gs_shell_updates_row_activated_cb:
 **/
static void
gs_shell_updates_row_activated_cb (GtkTreeView *treeview,
				   GtkTreePath *path,
				   GtkTreeViewColumn *col,
				   GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
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
	gs_shell_updates_set_updates_description_ui (shell_updates, app);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	gtk_widget_show (widget);
out:
	if (app != NULL)
		g_object_unref (app);
}

/**
 * gs_shell_updates_unselect_treeview_cb:
 **/
static gboolean
gs_shell_updates_unselect_treeview_cb (gpointer user_data)
{
	GsShellUpdates *shell_updates = GS_SHELL_UPDATES (user_data);
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_update"));
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (treeview));

	return FALSE;
}

static void
show_update_details (GsAppWidget *app_widget, GsShellUpdates *shell_updates)
{
	GsApp *app = gs_app_widget_get_app (app_widget);
	GsApp *app_related;
	GsAppKind kind;
	GtkWidget *widget;
	GsShellUpdatesPrivate *priv = shell_updates->priv;

	app = gs_app_widget_get_app (app_widget);
	kind = gs_app_get_kind (app);

	/* set update header */
	gs_shell_updates_set_updates_description_ui (shell_updates, app);

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
		g_idle_add (gs_shell_updates_unselect_treeview_cb, shell_updates);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * gs_shell_updates_activated_cb:
 **/
static void
gs_shell_updates_activated_cb (GtkListBox *list_box,
			       GtkListBoxRow *row,
			       GsShellUpdates *shell_updates)
{
	GsAppWidget *app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));

	show_update_details (app_widget, shell_updates);
}

/**
 * gs_shell_updates_list_header_func
 **/
static void
gs_shell_updates_list_header_func (GtkListBoxRow *row,
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
 * gs_shell_updates_button_close_cb:
 **/
static void
gs_shell_updates_button_close_cb (GtkWidget *widget, GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	gtk_widget_hide (widget);
}

/**
 * gs_shell_updates_button_back_cb:
 **/
static void
gs_shell_updates_button_back_cb (GtkWidget *widget, GsShellUpdates *shell_updates)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;

	/* return to the list view */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_update_header"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update_details"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_update"));
	gtk_widget_show (widget);
}

/**
 * gs_shell_updates_pending_apps_changed_cb:
 */
static void
gs_shell_updates_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
					  GsShellUpdates *shell_updates)
{
	gs_shell_updates_invalidate (shell_updates);
}

static void
reboot_failed (GObject      *source,
               GAsyncResult *res,
               gpointer      data)
{
        GVariant *ret;
        const gchar *command;
        GError *error = NULL;

        ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
        if (ret)
                g_variant_unref (ret);

        if (error) {
                g_warning ("Calling org.gnome.SessionManager.Reboot failed: %s\n", error->message);
                g_error_free (error);
                return;
        }

        command = "pkexec /usr/libexec/pk-trigger-offline-update --cancel";
        g_debug ("calling '%s'", command);
        if (!g_spawn_command_line_sync (command, NULL, NULL, NULL, &error)) {
                g_warning ("Failed to call '%s': %s\n", command, error->message);
                g_error_free (error);
        }
}

static void
gs_shell_updates_button_update_all_cb (GtkButton      *button,
                                       GsShellUpdates *updates)
{
        GDBusConnection *bus;
        const gchar *command;
        GError *error = NULL;

        command = "pkexec /usr/libexec/pk-trigger-offline-update";
        g_debug ("calling '%s'", command);
        if (!g_spawn_command_line_sync (command, NULL, NULL, NULL, &error)) {
                g_warning ("Failed to call '%s': %s\n", command, error->message);
                g_error_free (error);
                return;
        }

        g_debug ("calling org.gnome.SessionManager.Reboot");
        bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
        g_dbus_connection_call (bus,
                                "org.gnome.SessionManager",
                                "/org/gnome/SessionManager",
                                "org.gnome.SessionManager",
                                "Reboot",
                                NULL,
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                G_MAXINT,
                                NULL,
                                reboot_failed,
                                NULL);
        g_object_unref (bus);
}

/**
 * gs_shell_updates_setup:
 */
void
gs_shell_updates_setup (GsShellUpdates *shell_updates,
                        GsShell *shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	GsShellUpdatesPrivate *priv = shell_updates->priv;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL_UPDATES (shell_updates));

        priv->shell = shell;

	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (priv->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_shell_updates_pending_apps_changed_cb),
			  shell_updates);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* setup updates */
	priv->list_box_updates = GTK_LIST_BOX (gtk_list_box_new ());
	g_signal_connect (priv->list_box_updates, "row-activated",
			  G_CALLBACK (gs_shell_updates_activated_cb), shell_updates);
	gtk_list_box_set_header_func (priv->list_box_updates,
				      gs_shell_updates_list_header_func,
				      shell_updates,
				      NULL);
	gtk_list_box_set_selection_mode (priv->list_box_updates,
					 GTK_SELECTION_NONE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (priv->list_box_updates));
	gtk_widget_show (GTK_WIDGET (priv->list_box_updates));

	/* column for name */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_update"));
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
			  G_CALLBACK (gs_shell_updates_row_activated_cb),
			  shell_updates);

       widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_all"));
        g_signal_connect (widget, "clicked", G_CALLBACK (gs_shell_updates_button_update_all_cb), shell_updates);

	/* setup update details window */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_updates_button_close_cb),
			  shell_updates);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_update"));
	g_signal_connect (widget, "delete-event",
			  G_CALLBACK (gtk_widget_hide_on_delete),
			  shell_updates);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_update_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_shell_updates_button_back_cb),
			  shell_updates);
}

/**
 * gs_shell_updates_class_init:
 **/
static void
gs_shell_updates_class_init (GsShellUpdatesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_updates_finalize;

	g_type_class_add_private (klass, sizeof (GsShellUpdatesPrivate));
}

/**
 * gs_shell_updates_init:
 **/
static void
gs_shell_updates_init (GsShellUpdates *shell_updates)
{
	shell_updates->priv = GS_SHELL_UPDATES_GET_PRIVATE (shell_updates);
}

/**
 * gs_shell_updates_finalize:
 **/
static void
gs_shell_updates_finalize (GObject *object)
{
	GsShellUpdates *shell_updates = GS_SHELL_UPDATES (object);
	GsShellUpdatesPrivate *priv = shell_updates->priv;

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);

	G_OBJECT_CLASS (gs_shell_updates_parent_class)->finalize (object);
}

/**
 * gs_shell_updates_new:
 **/
GsShellUpdates *
gs_shell_updates_new (void)
{
	GsShellUpdates *shell_updates;
	shell_updates = g_object_new (GS_TYPE_SHELL_UPDATES, NULL);
	return GS_SHELL_UPDATES (shell_updates);
}
