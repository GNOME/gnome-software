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

#include "gs-shell.h"
#include "gs-shell-installed.h"
#include "gs-app.h"
#include "gs-utils.h"
#include "gs-app-widget.h"

static void	gs_shell_installed_finalize	(GObject	*object);
static void     remove_row                      (GtkListBox *list_box,
                                                 GtkWidget *child);

#define GS_SHELL_INSTALLED_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_SHELL_INSTALLED, GsShellInstalledPrivate))

struct GsShellInstalledPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GtkListBox		*list_box_installed;
	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	gboolean		 cache_valid;
	gboolean		 waiting;
        GsShell                 *shell;
};

G_DEFINE_TYPE (GsShellInstalled, gs_shell_installed, G_TYPE_OBJECT)

/**
 * gs_shell_installed_invalidate:
 **/
void
gs_shell_installed_invalidate (GsShellInstalled *shell_installed)
{
	shell_installed->priv->cache_valid = FALSE;
}

static void
gs_shell_installed_app_widget_activated_cb (GtkListBox *list_box,
                                            GtkListBoxRow *row,
					    GsShellInstalled *shell_installed)
{
        GsAppWidget *app_widget;
        GsApp *app;

        app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	app = gs_app_widget_get_app (app_widget);
        gs_shell_show_app (shell_installed->priv->shell, app);
}

typedef struct {
	GsAppWidget		*app_widget;
	GsShellInstalled	*shell_installed;
} GsShellInstalledHelper;

static void
row_unrevealed (GObject *revealer, GParamSpec *pspec, gpointer data)
{
        GtkWidget *row, *list;

        row = gtk_widget_get_parent (GTK_WIDGET (revealer));
        list = gtk_widget_get_parent (row);

        gtk_container_remove (GTK_CONTAINER (list), row);
}

static void
remove_row (GtkListBox *list_box, GtkWidget *child)
{
        GtkWidget *row, *revealer;

        gtk_widget_set_sensitive (child, FALSE);
        row = gtk_widget_get_parent (child);
        revealer = gtk_revealer_new ();
        gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), TRUE);
        gtk_widget_show (revealer);
        gtk_widget_reparent (child, revealer);
        gtk_container_add (GTK_CONTAINER (row), revealer);
        g_signal_connect (revealer, "notify::child-revealed",
                          G_CALLBACK (row_unrevealed), NULL);
        gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), FALSE);
}

/**
 * gs_shell_installed_finished_func:
 **/
static void
gs_shell_installed_finished_func (GsPluginLoader *plugin_loader, GsApp *app, gpointer user_data)
{
	GsShellInstalledHelper *helper = (GsShellInstalledHelper *) user_data;
	GsShellInstalledPrivate *priv = helper->shell_installed->priv;

	/* remove from the list */
	if (app != NULL) {
                remove_row (GTK_LIST_BOX (priv->list_box_installed),
                            GTK_WIDGET (helper->app_widget));
	}
	g_object_unref (helper->app_widget);
	g_object_unref (helper->shell_installed);
	g_free (helper);
}

/**
 * gs_shell_installed_app_remove_cb:
 **/
static void
gs_shell_installed_app_remove_cb (GsAppWidget *app_widget,
				  GsShellInstalled *shell_installed)
{
	GsApp *app;
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GString *markup;
	GtkResponseType response;
	GtkWidget *dialog;
	GtkWindow *window;
	GsShellInstalledHelper *helper;

	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_software"));
	markup = g_string_new ("");
	app = gs_app_widget_get_app (app_widget);
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
		helper = g_new0 (GsShellInstalledHelper, 1);
		helper->shell_installed = g_object_ref (shell_installed);
		helper->app_widget = g_object_ref (app_widget);
		gs_plugin_loader_app_remove (priv->plugin_loader,
					     app,
					     priv->cancellable,
					     gs_shell_installed_finished_func,
					     helper);
	}
	g_string_free (markup, TRUE);
	gtk_widget_destroy (dialog);
}

static void
app_state_changed (GsApp *app, GtkWidget *widget)
{
        GtkWidget *row, *list;

        if (gs_app_get_state (app) == GS_APP_STATE_AVAILABLE) {
                row = gtk_widget_get_parent (widget);
                list = gtk_widget_get_parent (row);
                remove_row (GTK_LIST_BOX (list), widget);
        }
}

static void
gs_shell_installed_add_app (GsShellInstalled *shell, GsApp *app)
{
	GsShellInstalledPrivate *priv = shell->priv;
        GtkWidget *widget;

	g_debug ("adding to installed list: %s", gs_app_get_id (app));
	widget = gs_app_widget_new ();
        gs_app_widget_set_colorful (GS_APP_WIDGET (widget), FALSE);
	g_signal_connect (widget, "button-clicked",
			  G_CALLBACK (gs_shell_installed_app_remove_cb), shell);
        g_signal_connect_object (app, "state-changed",
                                 G_CALLBACK (app_state_changed), widget, 0);
	gs_app_widget_set_app (GS_APP_WIDGET (widget), app);
	gtk_container_add (GTK_CONTAINER (priv->list_box_installed), widget);
	gs_app_widget_set_size_groups (GS_APP_WIDGET (widget),
				       priv->sizegroup_image,
				       priv->sizegroup_name);
	gtk_widget_show (widget);
}

/**
 * gs_shell_installed_get_installed_cb:
 **/
static void
gs_shell_installed_get_installed_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GError *error = NULL;
	GList *l;
	GList *list;
	GsApp *app;
	GsShellInstalled *shell_installed = GS_SHELL_INSTALLED (user_data);
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *widget;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_install"));
        gs_stop_spinner (GTK_SPINNER (widget));

        priv->waiting = FALSE;
        priv->cache_valid = TRUE;

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
                gs_shell_installed_add_app (shell_installed, app);
	}
out: ;
}

static void
reset_date (GtkWidget *row, gpointer data)
{
        GtkWidget *child;
        GsApp *app;

        child = gtk_bin_get_child (GTK_BIN (row));
        app = gs_app_widget_get_app (GS_APP_WIDGET (child));

        if (gs_app_get_state (app) == GS_APP_STATE_REMOVING) {
                /* sort removing apps above installed apps,
                 * below installing apps
                 */
                gs_app_set_install_date (app, G_MAXUINT - 2);
                gtk_list_box_row_changed (GTK_LIST_BOX_ROW (row));
        }
}

static void
resort_list (GsShellInstalled *shell)
{
	GsShellInstalledPrivate *priv = shell->priv;

        gtk_container_foreach (GTK_CONTAINER (priv->list_box_installed),
                               reset_date, NULL);
}

/**
 * gs_shell_installed_refresh:
 **/
void
gs_shell_installed_refresh (GsShellInstalled *shell_installed, gboolean scroll_up)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
        GtkWidget *widget;
        GtkSpinner *spinner;

        if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_INSTALLED) {
                widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
                gtk_widget_show (widget);
        }

        resort_list (shell_installed);

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_install"));
        if (scroll_up) {
                GtkAdjustment *adj;
                adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (widget));
                gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
        }
        if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_INSTALLED) {
                gs_grab_focus_when_mapped (widget);
        }

	/* no need to refresh */
	if (priv->cache_valid)
		return;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "spinner_install"));
        gtk_widget_show (widget);

        if (priv->waiting)
                return;

	/* remove old entries */
	gs_container_remove_all (GTK_CONTAINER (priv->list_box_installed));

	/* get popular apps */
	gs_plugin_loader_get_installed_async (priv->plugin_loader,
					      priv->cancellable,
					      gs_shell_installed_get_installed_cb,
					      shell_installed);

        spinner = GTK_SPINNER (gtk_builder_get_object (shell_installed->priv->builder, "spinner_install"));
        gs_start_spinner (spinner);

	priv->waiting = TRUE;
}

/**
 * gs_shell_installed_sort_func:
 **/
static gint
gs_shell_installed_sort_func (GtkListBoxRow *a,
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
 * gs_shell_installed_list_header_func
 **/
static void
gs_shell_installed_list_header_func (GtkListBoxRow *row,
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
 * gs_shell_installed_pending_apps_changed_cb:
 */
static void
gs_shell_installed_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
					    GsShellInstalled *shell_installed)
{
	gchar *label;
	GPtrArray *pending;
	GtkWidget *widget;
        guint i;
        GsApp *app;

	widget = GTK_WIDGET (gtk_builder_get_object (shell_installed->priv->builder,
						     "label_button_installed"));
	pending = gs_plugin_loader_get_pending (plugin_loader);
	if (pending->len == 0)
		label = g_strdup (_("Installed"));
	else
		label = g_strdup_printf (_("Installed (%d)"), pending->len);
        for (i = 0; i < pending->len; i++) {
                app = GS_APP (g_ptr_array_index (pending, i));
                if (gs_app_get_state (app) == GS_APP_STATE_INSTALLING) {
                        /* sort installing apps above removing and
                         * installed apps
                         */
                        gs_app_set_install_date (app, G_MAXUINT - 1);
                        gs_shell_installed_add_app (shell_installed, app);
                }
        }

	gtk_label_set_label (GTK_LABEL (widget), label);
	g_free (label);
	g_ptr_array_unref (pending);
}

/**
 * gs_shell_installed_setup:
 */
void
gs_shell_installed_setup (GsShellInstalled *shell_installed,
                          GsShell *shell,
			  GsPluginLoader *plugin_loader,
			  GtkBuilder *builder,
			  GCancellable *cancellable)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL_INSTALLED (shell_installed));

        priv->shell = shell;
	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (priv->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_shell_installed_pending_apps_changed_cb),
			  shell_installed);

	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* setup installed */
	priv->list_box_installed = GTK_LIST_BOX (gtk_list_box_new ());
	g_signal_connect (priv->list_box_installed, "row-activated",
			  G_CALLBACK (gs_shell_installed_app_widget_activated_cb), shell_installed);
	gtk_list_box_set_header_func (priv->list_box_installed,
				      gs_shell_installed_list_header_func,
				      shell_installed,
				      NULL);
	gtk_list_box_set_sort_func (priv->list_box_installed,
				    gs_shell_installed_sort_func,
				    shell_installed,
				    NULL);
	gtk_list_box_set_selection_mode (priv->list_box_installed,
					 GTK_SELECTION_NONE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_install"));
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (priv->list_box_installed));
	gtk_widget_show (GTK_WIDGET (priv->list_box_installed));
}

/**
 * gs_shell_installed_class_init:
 **/
static void
gs_shell_installed_class_init (GsShellInstalledClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_shell_installed_finalize;

	g_type_class_add_private (klass, sizeof (GsShellInstalledPrivate));
}

/**
 * gs_shell_installed_init:
 **/
static void
gs_shell_installed_init (GsShellInstalled *shell_installed)
{
	shell_installed->priv = GS_SHELL_INSTALLED_GET_PRIVATE (shell_installed);
	shell_installed->priv->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	shell_installed->priv->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
}

/**
 * gs_shell_installed_finalize:
 **/
static void
gs_shell_installed_finalize (GObject *object)
{
	GsShellInstalled *shell_installed = GS_SHELL_INSTALLED (object);
	GsShellInstalledPrivate *priv = shell_installed->priv;

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);

	G_OBJECT_CLASS (gs_shell_installed_parent_class)->finalize (object);
}

/**
 * gs_shell_installed_new:
 **/
GsShellInstalled *
gs_shell_installed_new (void)
{
	GsShellInstalled *shell_installed;
	shell_installed = g_object_new (GS_TYPE_SHELL_INSTALLED, NULL);
	return GS_SHELL_INSTALLED (shell_installed);
}
