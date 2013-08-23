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

#include "gs-shell-installed.h"
#include "gs-app.h"
#include "gs-app-widget.h"

static void	gs_shell_installed_finalize	(GObject	*object);

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

static void
gs_shell_installed_app_widget_activated_cb (GtkListBox *list_box,
                                            GtkListBoxRow *row,
					    GsShellInstalled *shell_installed)
{
	const gchar *tmp;
	GsApp *app;
	GtkWidget *details, *button, *grid;
	GtkWidget *image, *label;
	PangoAttrList *attr_list;
        GsAppWidget *app_widget;

        app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	app = gs_app_widget_get_app (app_widget);

	details = gtk_dialog_new_with_buttons (_("Details"),
					       GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (list_box))),
					       GTK_DIALOG_MODAL,
					       _("_Done"), GTK_RESPONSE_CLOSE,
					       NULL);
	gtk_container_set_border_width (GTK_CONTAINER (details), 20);
	button = gtk_dialog_get_widget_for_response (GTK_DIALOG (details), GTK_RESPONSE_CLOSE);
	gtk_style_context_add_class (gtk_widget_get_style_context (button), "suggested-action");
	g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_widget_destroy), details);

	grid = gtk_grid_new ();
	gtk_widget_show (grid);
	gtk_widget_set_halign (grid, GTK_ALIGN_FILL);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 20);
	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (details))), grid);

	image = gtk_image_new ();
	if (gs_app_get_pixbuf (app)) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (image), gs_app_get_pixbuf (app));	 gtk_widget_show (image);
	}
	gtk_grid_attach (GTK_GRID (grid), image, 0, 0, 1, 3);

	label = gtk_label_new (gs_app_get_name (app));
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_widget_set_margin_bottom (label, 10);
	attr_list = pango_attr_list_new ();
	pango_attr_list_insert (attr_list, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	pango_attr_list_insert (attr_list, pango_attr_scale_new (1));
	gtk_label_set_attributes (GTK_LABEL (label), attr_list);
	pango_attr_list_unref (attr_list);
	gtk_widget_show (label);
	gtk_grid_attach (GTK_GRID (grid), label, 1, 0, 1, 1);

	label = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_widget_set_margin_bottom (label, 20);
	if (gs_app_get_summary (app)) {
		gtk_label_set_label (GTK_LABEL (label), gs_app_get_summary (app));
		gtk_widget_show (label);
	}
	gtk_grid_attach (GTK_GRID (grid), label, 1, 1, 1, 1);
	tmp = gs_app_get_description (app);
	label = gtk_label_new (tmp);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_widget_show (label);
	gtk_grid_attach (GTK_GRID (grid), label, 0, 3, 2, 1);

	if (gs_app_get_url (app)) {
		button = gtk_link_button_new_with_label (gs_app_get_url (app), _("Visit website"));
		gtk_widget_set_halign (button, GTK_ALIGN_START);
		gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
		gtk_widget_show (button);
		gtk_grid_attach (GTK_GRID (grid), button, 0, 4, 2, 1);
	}

	gtk_window_present (GTK_WINDOW (details));
}

typedef struct {
	GsAppWidget		*app_widget;
	GsShellInstalled	*shell_installed;
} GsShellInstalledHelper;

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
		gtk_container_remove (GTK_CONTAINER (priv->list_box_installed),
				      gtk_widget_get_parent (GTK_WIDGET (helper->app_widget)));
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

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_spinner"));
        gtk_spinner_stop (GTK_SPINNER (widget));
        gtk_widget_hide (widget);
        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_wait"));
        gtk_widget_hide (widget);

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
		g_debug ("adding installed %s", gs_app_get_id (app));
		widget = gs_app_widget_new ();
		g_signal_connect (widget, "button-clicked",
				  G_CALLBACK (gs_shell_installed_app_remove_cb),
				  shell_installed);
		gs_app_widget_set_app (GS_APP_WIDGET (widget), app);
		gtk_container_add (GTK_CONTAINER (priv->list_box_installed), widget);
		gs_app_widget_set_size_groups (GS_APP_WIDGET (widget),
					       priv->sizegroup_image,
					       priv->sizegroup_name);
		gtk_widget_show (widget);
	}

	/* focus back to the text extry */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	gtk_widget_grab_focus (widget);
out: ;
}

/**
 * gs_shell_installed_refresh:
 **/
void
gs_shell_installed_refresh (GsShellInstalled *shell_installed)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
        GtkWidget *widget;

	/* no need to refresh */
	if (priv->cache_valid)
		return;

        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_spinner"));
        gtk_spinner_start (GTK_SPINNER (widget));
        gtk_widget_show (widget);
        widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_wait"));
        gtk_widget_show (widget);

        if (priv->waiting)
                return;

	/* remove old entries */
	_gtk_container_remove_all (GTK_CONTAINER (priv->list_box_installed));

	/* get popular apps */
	gs_plugin_loader_get_installed_async (priv->plugin_loader,
					      priv->cancellable,
					      gs_shell_installed_get_installed_cb,
					      shell_installed);
	priv->waiting = TRUE;
}

/**
 * gs_shell_installed_filter_text_changed_cb:
 **/
static gboolean
gs_shell_installed_filter_text_changed_cb (GtkEntry *entry,
					   GsShellInstalled *shell_installed)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	gtk_list_box_invalidate_filter (priv->list_box_installed);
	return FALSE;
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
	return g_strcmp0 (gs_app_get_name (a1),
			  gs_app_get_name (a2));
}

/**
 * gs_shell_installed_utf8_filter_helper:
 **/
static gboolean
gs_shell_installed_utf8_filter_helper (const gchar *haystack,
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
 * gs_shell_installed_filter_func:
 **/
static gboolean
gs_shell_installed_filter_func (GtkListBoxRow *row, void *user_data)
{
	const gchar *tmp;
	gboolean ret = TRUE;
	gchar *needle_utf8 = NULL;
	GsApp *app;
	GsAppWidget *app_widget = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	GsShellInstalled *shell_installed = GS_SHELL_INSTALLED (user_data);
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GtkWidget *widget;

	app = gs_app_widget_get_app (app_widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	tmp = gtk_entry_get_text (GTK_ENTRY (widget));
	if (tmp[0] == '\0')
		goto out;

	needle_utf8 = g_utf8_casefold (tmp, -1);
	ret = gs_shell_installed_utf8_filter_helper (gs_app_get_name (app),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_shell_installed_utf8_filter_helper (gs_app_get_summary (app),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_shell_installed_utf8_filter_helper (gs_app_get_version (app),
					  needle_utf8);
	if (ret)
		goto out;
	ret = gs_shell_installed_utf8_filter_helper (gs_app_get_id (app),
					  needle_utf8);
	if (ret)
		goto out;
out:
	g_free (needle_utf8);
	return ret;
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

	widget = GTK_WIDGET (gtk_builder_get_object (shell_installed->priv->builder,
						     "label_button_installed"));
	pending = gs_plugin_loader_get_pending (plugin_loader);
	if (pending->len == 0)
		label = g_strdup (_("Installed"));
	else
		label = g_strdup_printf (_("Installed (%d)"), pending->len);
	gtk_label_set_label (GTK_LABEL (widget), label);
	g_free (label);
	g_ptr_array_unref (pending);
}

/**
 * gs_shell_installed_setup:
 */
void
gs_shell_installed_setup (GsShellInstalled *shell_installed,
			  GsPluginLoader *plugin_loader,
			  GtkBuilder *builder,
			  GCancellable *cancellable)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL_INSTALLED (shell_installed));

	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (priv->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_shell_installed_pending_apps_changed_cb),
			  shell_installed);

	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* refilter on search box changing */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_search"));
	g_signal_connect (GTK_EDITABLE (widget), "changed",
			  G_CALLBACK (gs_shell_installed_filter_text_changed_cb), shell_installed);

	/* setup installed */
	priv->list_box_installed = GTK_LIST_BOX (gtk_list_box_new ());
	g_signal_connect (priv->list_box_installed, "row-activated",
			  G_CALLBACK (gs_shell_installed_app_widget_activated_cb), shell_installed);
	gtk_list_box_set_header_func (priv->list_box_installed,
				      gs_shell_installed_list_header_func,
				      shell_installed,
				      NULL);
	gtk_list_box_set_filter_func (priv->list_box_installed,
				      gs_shell_installed_filter_func,
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
