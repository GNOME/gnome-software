/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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
#include <appstream-glib.h>

#include "gs-cleanup.h"
#include "gs-shell.h"
#include "gs-shell-installed.h"
#include "gs-app.h"
#include "gs-utils.h"
#include "gs-app-row.h"
#include "gs-app-folder-dialog.h"
#include "gs-folders.h"

struct GsShellInstalledPrivate
{
	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GListStore		*installed_apps;
	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	gboolean		 cache_valid;
	gboolean		 waiting;
	GsShell			*shell;
	gboolean 		 selection_mode;

	GtkWidget		*bottom_install;
	GtkWidget		*button_folder_add;
	GtkWidget		*button_folder_move;
	GtkWidget		*button_folder_remove;
	GtkWidget		*list_box_install;
	GtkWidget		*scrolledwindow_install;
	GtkWidget		*spinner_install;
	GtkWidget		*stack_install;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsShellInstalled, gs_shell_installed, GS_TYPE_PAGE)

static void gs_shell_installed_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
							GsShellInstalled *shell_installed);
static void set_selection_mode (GsShellInstalled *shell_installed, gboolean selection_mode);

/**
 * gs_shell_installed_invalidate:
 **/
void
gs_shell_installed_invalidate (GsShellInstalled *shell_installed)
{
	shell_installed->priv->cache_valid = FALSE;
}

static void
gs_shell_installed_app_row_activated_cb (GtkListBox *list_box,
					 GtkListBoxRow *row,
					 GsShellInstalled *shell_installed)
{
	if (shell_installed->priv->selection_mode) {
		gboolean selected;
		selected = gs_app_row_get_selected (GS_APP_ROW (row));
		gs_app_row_set_selected (GS_APP_ROW (row), !selected);
	} else {
		GsApp *app;
		app = gs_app_row_get_app (GS_APP_ROW (row));
		gs_shell_show_app (shell_installed->priv->shell, app);
	}
}

static void
row_unrevealed (GObject *row, GParamSpec *pspec, gpointer data)
{
	GtkWidget *list;

	list = gtk_widget_get_parent (GTK_WIDGET (row));
	gtk_container_remove (GTK_CONTAINER (list), GTK_WIDGET (row));
}

static void
gs_shell_installed_app_removed (GsPage *page, GsApp *app)
{
	GsShellInstalled *shell_installed = GS_SHELL_INSTALLED (page);
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GList *l;
	_cleanup_list_free_ GList *children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (priv->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		if (gs_app_row_get_app (app_row) == app) {
			gs_app_row_unreveal (app_row);
			g_signal_connect (app_row, "unrevealed",
			                  G_CALLBACK (row_unrevealed), NULL);
		}
	}
}

/**
 * gs_shell_installed_app_remove_cb:
 **/
static void
gs_shell_installed_app_remove_cb (GsAppRow *app_row,
				  GsShellInstalled *shell_installed)
{
	GsApp *app;

	app = gs_app_row_get_app (app_row);
	gs_page_remove_app (GS_PAGE (shell_installed), app);
}

static gboolean
gs_shell_installed_invalidate_sort_idle (gpointer user_data)
{
	GsAppRow *app_row = user_data;

	gtk_list_box_row_changed (GTK_LIST_BOX_ROW (app_row));

	g_object_unref (app_row);
	return G_SOURCE_REMOVE;
}

/**
 * gs_shell_installed_notify_state_changed_cb:
 **/
static void
gs_shell_installed_notify_state_changed_cb (GsApp *app,
					    GParamSpec *pspec,
					    GsAppRow *app_row)
{
	g_idle_add (gs_shell_installed_invalidate_sort_idle, g_object_ref (app_row));
}

static void selection_changed (GsShellInstalled *shell);
static gchar *gs_shell_installed_get_app_sort_key (GsApp *app);

static gint
app_sort_func (GsApp *app1,
               GsApp *app2,
               gpointer user_data)
{
	_cleanup_free_ gchar *key1 = NULL;
	_cleanup_free_ gchar *key2 = NULL;

	key1 = gs_shell_installed_get_app_sort_key (app1);
	key2 = gs_shell_installed_get_app_sort_key (app2);

	/* compare the keys according to the algorithm above */
	return g_strcmp0 (key1, key2);
}

static void
gs_shell_installed_add_app (GsShellInstalled *shell, GsApp *app)
{
	GsShellInstalledPrivate *priv = shell->priv;

	g_list_store_insert_sorted (priv->installed_apps,
	                            app,
	                            (GCompareDataFunc) app_sort_func,
	                            NULL);
}

static GtkWidget *
create_app_row (gpointer item,
                gpointer user_data)

{
	GsShellInstalled *shell = user_data;
	GsShellInstalledPrivate *priv = shell->priv;
	GsApp *app = item;
	GtkWidget *app_row;

	app_row = gs_app_row_new ();
	gs_app_row_set_colorful (GS_APP_ROW (app_row), FALSE);
	g_signal_connect (app_row, "button-clicked",
			  G_CALLBACK (gs_shell_installed_app_remove_cb), shell);
	g_signal_connect_object (app, "notify::state",
				 G_CALLBACK (gs_shell_installed_notify_state_changed_cb),
				 app_row, 0);
	g_signal_connect_swapped (app_row, "notify::selected",
			 	  G_CALLBACK (selection_changed), shell);
	gs_app_row_set_app (GS_APP_ROW (app_row), app);
	gs_app_row_set_size_groups (GS_APP_ROW (app_row),
				    priv->sizegroup_image,
				    priv->sizegroup_name);

	gs_app_row_set_selectable (GS_APP_ROW (app_row),
				   priv->selection_mode);

	gtk_widget_show (app_row);
	return app_row;
}

/**
 * gs_shell_installed_get_installed_cb:
 **/
static void
gs_shell_installed_get_installed_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GList *l;
	GList *list;
	GsApp *app;
	GsShellInstalled *shell_installed = GS_SHELL_INSTALLED (user_data);
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	_cleanup_error_free_ GError *error = NULL;

	gs_stop_spinner (GTK_SPINNER (priv->spinner_install));
	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_install), "view");

	priv->waiting = FALSE;
	priv->cache_valid = TRUE;

	list = gs_plugin_loader_get_installed_finish (plugin_loader,
						      res,
						      &error);
	if (list == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to get installed apps: %s", error->message);
		goto out;
	}
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		gs_shell_installed_add_app (shell_installed, app);
	}
out:
	gs_plugin_list_free (list);
	gs_shell_installed_pending_apps_changed_cb (plugin_loader, shell_installed);
}

/**
 * gs_shell_installed_load:
 */
static void
gs_shell_installed_load (GsShellInstalled *shell_installed)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;

	if (priv->waiting)
		return;
	priv->waiting = TRUE;

	/* remove old entries */
	g_list_store_remove_all (priv->installed_apps);

	/* get popular apps */
	gs_plugin_loader_get_installed_async (priv->plugin_loader,
					      GS_PLUGIN_REFINE_FLAGS_DEFAULT |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
					      priv->cancellable,
					      gs_shell_installed_get_installed_cb,
					      shell_installed);
	gs_start_spinner (GTK_SPINNER (priv->spinner_install));
	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_install), "spinner");
}

/**
 * gs_shell_installed_reload:
 */
void
gs_shell_installed_reload (GsShellInstalled *shell_installed)
{
	gs_shell_installed_invalidate (shell_installed);
	gs_shell_installed_load (shell_installed);
}

/**
 * gs_shell_installed_switch_to:
 **/
void
gs_shell_installed_switch_to (GsShellInstalled *shell_installed, gboolean scroll_up)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GtkWidget *widget;

	if (gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_INSTALLED) {
		g_warning ("Called switch_to(installed) when in mode %s",
			   gs_shell_get_mode_string (priv->shell));
		return;
	}

	set_selection_mode (shell_installed, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
	gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_select"));
	gtk_widget_show (widget);

	if (scroll_up) {
		GtkAdjustment *adj;
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_install));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}
	if (gs_shell_get_mode (priv->shell) == GS_SHELL_MODE_INSTALLED) {
		gs_grab_focus_when_mapped (priv->scrolledwindow_install);
	}

	/* no need to refresh */
	if (priv->cache_valid)
		return;

	gs_shell_installed_load (shell_installed);
}

/**
 * gs_shell_installed_get_app_sort_key:
 *
 * Get a sort key to achive this:
 *
 * 1. state:installing applications
 * 2. state:removing applications
 * 3. kind:normal applications
 * 4. kind:system applications
 *
 * Within each of these groups, they are sorted by the install date and then
 * by name.
 **/
static gchar *
gs_shell_installed_get_app_sort_key (GsApp *app)
{
	GString *key;
	_cleanup_free_ gchar *casefolded_name = NULL;

	key = g_string_sized_new (64);

	/* sort installed, removing, other */
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_INSTALLING:
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		g_string_append (key, "1:");
		break;
	case AS_APP_STATE_REMOVING:
		g_string_append (key, "2:");
		break;
	default:
		g_string_append (key, "3:");
		break;
	}

	/* sort desktop files, then addons */
	switch (gs_app_get_id_kind (app)) {
	case AS_ID_KIND_DESKTOP:
	case AS_ID_KIND_WEB_APP:
		g_string_append (key, "1:");
		break;
	default:
		g_string_append (key, "2:");
		break;
	}

	/* sort normal, system, other */
	switch (gs_app_get_kind (app)) {
	case GS_APP_KIND_NORMAL:
		g_string_append (key, "1:");
		break;
	case GS_APP_KIND_SYSTEM:
		g_string_append (key, "2:");
		break;
	default:
		g_string_append (key, "3:");
		break;
	}

	/* finally, sort by short name */
	casefolded_name = g_utf8_casefold (gs_app_get_name (app), -1);
	g_string_append (key, casefolded_name);

	return g_string_free (key, FALSE);
}

/**
 * gs_shell_installed_is_addon_id_kind
 **/
static gboolean
gs_shell_installed_is_addon_id_kind (GsApp *app)
{
	AsIdKind id_kind;
	id_kind = gs_app_get_id_kind (app);
	if (id_kind == AS_ID_KIND_DESKTOP)
		return FALSE;
	if (id_kind == AS_ID_KIND_WEB_APP)
		return FALSE;
	return TRUE;
}

static gboolean
gs_shell_installed_is_system_application (GsApp *app)
{
	if (gs_app_get_id_kind (app) == AS_ID_KIND_DESKTOP &&
	    gs_app_get_kind (app) == GS_APP_KIND_SYSTEM)
		return TRUE;
	return FALSE;
}

/**
 * gs_shell_installed_list_header_func
 **/
static void
gs_shell_installed_list_header_func (GtkListBoxRow *row,
				     GtkListBoxRow *before,
				     gpointer user_data)
{
	GtkStyleContext *context;
	GtkWidget *header;

	/* reset */
	gtk_list_box_row_set_header (row, NULL);
	if (before == NULL)
		return;

	if (!gs_shell_installed_is_system_application (gs_app_row_get_app (GS_APP_ROW (before))) &&
	    gs_shell_installed_is_system_application (gs_app_row_get_app (GS_APP_ROW (row)))) {
		/* TRANSLATORS: This is the header dividing the normal
		 * applications and the system ones */
		header = gtk_label_new (_("System Applications"));
		g_object_set (header,
			      "xalign", 0.0,
			      NULL);
		context = gtk_widget_get_style_context (header);
		gtk_style_context_add_class (context, "header-label");
	} else if (!gs_shell_installed_is_addon_id_kind (gs_app_row_get_app (GS_APP_ROW (before))) &&
		   gs_shell_installed_is_addon_id_kind (gs_app_row_get_app (GS_APP_ROW (row)))) {
		/* TRANSLATORS: This is the header dividing the normal
		 * applications and the addons */
		header = gtk_label_new (_("Add-ons"));
		g_object_set (header,
			      "xalign", 0.0,
			      NULL);
		context = gtk_widget_get_style_context (header);
		gtk_style_context_add_class (context, "header-label");
	} else {
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	}
	gtk_list_box_row_set_header (row, header);
}

static gboolean
gs_shell_installed_has_app (GsShellInstalled *shell_installed,
			    GsApp *app)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GList *l;
	gboolean ret = FALSE;
	_cleanup_list_free_ GList *children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (priv->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		if (gs_app_row_get_app (app_row) == app) {
			ret = TRUE;
			break;
		}
	}
	return ret;
}

/**
 * gs_shell_installed_pending_apps_changed_cb:
 */
static void
gs_shell_installed_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
					    GsShellInstalled *shell_installed)
{
	GsApp *app;
	GtkWidget *widget;
	guint i;
	_cleanup_ptrarray_unref_ GPtrArray *pending = NULL;

	widget = GTK_WIDGET (gtk_builder_get_object (shell_installed->priv->builder,
						     "button_installed_counter"));
	pending = gs_plugin_loader_get_pending (plugin_loader);
	if (pending->len == 0) {
		gtk_widget_hide (widget);
	} else {
		_cleanup_free_ gchar *label = NULL;
		gtk_widget_show (widget);
		label = g_strdup_printf ("%d", pending->len);
		gtk_label_set_label (GTK_LABEL (widget), label);
	}
	for (i = 0; i < pending->len; i++) {
		app = GS_APP (g_ptr_array_index (pending, i));
		/* Be careful not to add pending apps more than once. */
		if (gs_shell_installed_has_app (shell_installed, app) == FALSE)
			gs_shell_installed_add_app (shell_installed, app);
	}
}

static void
set_selection_mode (GsShellInstalled *shell_installed, gboolean selection_mode)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GList *l;
	GtkWidget *header;
	GtkWidget *widget;
	GtkStyleContext *context;
	_cleanup_list_free_ GList *children = NULL;
	
	if (priv->selection_mode == selection_mode)
		return;

	priv->selection_mode = selection_mode;

	header = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header"));
	context = gtk_widget_get_style_context (header);
	if (priv->selection_mode) {
		gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header), FALSE);
		gtk_style_context_add_class (context, "selection-mode");
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_select"));
		gtk_button_set_image (GTK_BUTTON (widget), NULL);
		gtk_button_set_label (GTK_BUTTON (widget), _("_Cancel"));
		gtk_button_set_use_underline (GTK_BUTTON (widget), TRUE);
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_selection_menu_button"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_selection_label"));
		gtk_label_set_label (GTK_LABEL (widget), _("Click on items to select them"));
	} else {
		gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header), TRUE);
		gtk_style_context_remove_class (context, "selection-mode");
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_select"));
		gtk_button_set_image (GTK_BUTTON (widget), gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU));
		gtk_button_set_label (GTK_BUTTON (widget), NULL);
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "header_selection_menu_button"));
		gtk_widget_hide (widget);

		gtk_widget_hide (priv->button_folder_add);
		gtk_widget_hide (priv->button_folder_move);
		gtk_widget_hide (priv->button_folder_remove);
	}

	children = gtk_container_get_children (GTK_CONTAINER (priv->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		gs_app_row_set_selectable (app_row,
					   priv->selection_mode);
	}

	gtk_revealer_set_reveal_child (GTK_REVEALER (priv->bottom_install), priv->selection_mode);
}

static void
selection_mode_cb (GtkButton *button, GsShellInstalled *shell_installed)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;

	set_selection_mode (shell_installed, !priv->selection_mode);
}

static GList *
get_selected_apps (GsShellInstalled *shell_installed)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GList *l, *list;
	_cleanup_list_free_ GList *children = NULL;

	list = NULL;
	children = gtk_container_get_children (GTK_CONTAINER (priv->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		if (gs_app_row_get_selected (app_row)) {
			list = g_list_prepend (list, gs_app_row_get_app (app_row));
		}
	}
	return list;
}

static void
selection_changed (GsShellInstalled *shell_installed)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GList *l;
	GsApp *app;
	gboolean has_folders, has_nonfolders;
	_cleanup_list_free_ GList *apps = NULL;
	_cleanup_object_unref_ GsFolders *folders = NULL;

	folders = gs_folders_get ();
	has_folders = has_nonfolders = FALSE;
	apps = get_selected_apps (shell_installed);
	for (l = apps; l; l = l->next) {
		app = l->data;
		if (gs_folders_get_app_folder (folders,
					       gs_app_get_id (app),
					       gs_app_get_categories (app))) {
			has_folders = TRUE;
		} else {
			has_nonfolders = TRUE;
		}
	}

	gtk_widget_set_visible (priv->button_folder_add, has_nonfolders);
	gtk_widget_set_visible (priv->button_folder_move, has_folders && !has_nonfolders);
	gtk_widget_set_visible (priv->button_folder_remove, has_folders);
}

static gboolean
folder_dialog_done (GsShellInstalled *shell_installed)
{
	set_selection_mode (shell_installed, FALSE);
	return FALSE;
}

static void
show_folder_dialog (GtkButton *button, GsShellInstalled *shell_installed)
{
	GtkWidget *toplevel;
	GtkWidget *dialog;
	_cleanup_list_free_ GList *apps = NULL;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
	apps = get_selected_apps (shell_installed);
	dialog = gs_app_folder_dialog_new (GTK_WINDOW (toplevel), apps);
	gtk_window_present (GTK_WINDOW (dialog));
	g_signal_connect_swapped (dialog, "delete-event",
				  G_CALLBACK (folder_dialog_done), shell_installed);
}

static void
remove_folders (GtkButton *button, GsShellInstalled *shell_installed)
{
	GList *l;
	GsApp *app;
	_cleanup_list_free_ GList *apps = NULL;
	_cleanup_object_unref_ GsFolders *folders = NULL;

	folders = gs_folders_get ();
	apps = get_selected_apps (shell_installed);
	for (l = apps; l; l = l->next) {
		app = l->data;
		gs_folders_set_app_folder (folders,
					   gs_app_get_id (app),
					   gs_app_get_categories (app),
					   NULL);
	}

	gs_folders_save (folders);

	set_selection_mode (shell_installed, FALSE);
}

static void
select_all_cb (GtkMenuItem *item, GsShellInstalled *shell_installed)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GList *l;
	_cleanup_list_free_ GList *children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (priv->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		gs_app_row_set_selected (app_row, TRUE);
	}
}

static void
select_none_cb (GtkMenuItem *item, GsShellInstalled *shell_installed)
{
	GsShellInstalledPrivate *priv = shell_installed->priv;
	GList *l;
	_cleanup_list_free_ GList *children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (priv->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		gs_app_row_set_selected (app_row, FALSE);
	}
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
	g_signal_connect (priv->list_box_install, "row-activated",
			  G_CALLBACK (gs_shell_installed_app_row_activated_cb), shell_installed);
	gtk_list_box_set_header_func (GTK_LIST_BOX (priv->list_box_install),
				      gs_shell_installed_list_header_func,
				      shell_installed, NULL);
	gtk_list_box_bind_model (GTK_LIST_BOX (priv->list_box_install),
	                         G_LIST_MODEL (priv->installed_apps),
	                         create_app_row,
	                         shell_installed, NULL);

	g_signal_connect (priv->button_folder_add, "clicked",
			  G_CALLBACK (show_folder_dialog), shell_installed);
	
	g_signal_connect (priv->button_folder_move, "clicked",
			  G_CALLBACK (show_folder_dialog), shell_installed);
	
	g_signal_connect (priv->button_folder_remove, "clicked",
			  G_CALLBACK (remove_folders), shell_installed);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_select"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (selection_mode_cb), shell_installed);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_select"));
	gtk_button_set_image (GTK_BUTTON (widget), gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU));
	gtk_button_set_label (GTK_BUTTON (widget), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "select_all_menuitem"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (select_all_cb), shell_installed);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "select_none_menuitem"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (select_none_cb), shell_installed);

	/* chain up */
	gs_page_setup (GS_PAGE (shell_installed),
	               shell,
	               plugin_loader,
	               cancellable);
}

/**
 * gs_shell_installed_dispose:
 **/
static void
gs_shell_installed_dispose (GObject *object)
{
	GsShellInstalled *shell_installed = GS_SHELL_INSTALLED (object);
	GsShellInstalledPrivate *priv = shell_installed->priv;

	g_clear_object (&priv->installed_apps);
	g_clear_object (&priv->sizegroup_image);
	g_clear_object (&priv->sizegroup_name);

	g_clear_object (&priv->builder);
	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->cancellable);

	G_OBJECT_CLASS (gs_shell_installed_parent_class)->dispose (object);
}

/**
 * gs_shell_installed_class_init:
 **/
static void
gs_shell_installed_class_init (GsShellInstalledClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_installed_dispose;
	page_class->app_removed = gs_shell_installed_app_removed;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-installed.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsShellInstalled, bottom_install);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellInstalled, button_folder_add);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellInstalled, button_folder_move);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellInstalled, button_folder_remove);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellInstalled, list_box_install);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellInstalled, scrolledwindow_install);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellInstalled, spinner_install);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellInstalled, stack_install);
}

/**
 * gs_shell_installed_init:
 **/
static void
gs_shell_installed_init (GsShellInstalled *shell_installed)
{
	gtk_widget_init_template (GTK_WIDGET (shell_installed));

	shell_installed->priv = gs_shell_installed_get_instance_private (shell_installed);
	shell_installed->priv->installed_apps = g_list_store_new (GS_TYPE_APP);
	shell_installed->priv->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	shell_installed->priv->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
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

/* vim: set noexpandtab: */
