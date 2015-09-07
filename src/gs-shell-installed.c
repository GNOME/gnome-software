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

struct _GsShellInstalled
{
	GsPage			 parent_instance;

	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
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

G_DEFINE_TYPE (GsShellInstalled, gs_shell_installed, GS_TYPE_PAGE)

static void gs_shell_installed_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
							GsShellInstalled *self);
static void set_selection_mode (GsShellInstalled *self, gboolean selection_mode);

/**
 * gs_shell_installed_invalidate:
 **/
void
gs_shell_installed_invalidate (GsShellInstalled *self)
{
	self->cache_valid = FALSE;
}

static void
gs_shell_installed_app_row_activated_cb (GtkListBox *list_box,
					 GtkListBoxRow *row,
					 GsShellInstalled *self)
{
	if (self->selection_mode) {
		gboolean selected;
		selected = gs_app_row_get_selected (GS_APP_ROW (row));
		gs_app_row_set_selected (GS_APP_ROW (row), !selected);
	} else {
		GsApp *app;
		app = gs_app_row_get_app (GS_APP_ROW (row));
		gs_shell_show_app (self->shell, app);
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
	GsShellInstalled *self = GS_SHELL_INSTALLED (page);
	GList *l;
	g_autoptr(GList) children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
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
				  GsShellInstalled *self)
{
	GsApp *app;

	app = gs_app_row_get_app (app_row);
	gs_page_remove_app (GS_PAGE (self), app);
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

static void selection_changed (GsShellInstalled *self);

static void
gs_shell_installed_add_app (GsShellInstalled *self, GsApp *app)
{
	GtkWidget *app_row;

	app_row = gs_app_row_new ();
	gs_app_row_set_colorful (GS_APP_ROW (app_row), FALSE);
	g_signal_connect (app_row, "button-clicked",
			  G_CALLBACK (gs_shell_installed_app_remove_cb), self);
	g_signal_connect_object (app, "notify::state",
				 G_CALLBACK (gs_shell_installed_notify_state_changed_cb),
				 app_row, 0);
	g_signal_connect_swapped (app_row, "notify::selected",
				  G_CALLBACK (selection_changed), self);
	gs_app_row_set_app (GS_APP_ROW (app_row), app);
	gtk_container_add (GTK_CONTAINER (self->list_box_install), app_row);
	gs_app_row_set_size_groups (GS_APP_ROW (app_row),
				    self->sizegroup_image,
				    self->sizegroup_name);

	gs_app_row_set_selectable (GS_APP_ROW (app_row),
				   self->selection_mode);

	gtk_widget_show (app_row);
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
	GsShellInstalled *self = GS_SHELL_INSTALLED (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;

	gs_stop_spinner (GTK_SPINNER (self->spinner_install));
	gtk_stack_set_visible_child_name (GTK_STACK (self->stack_install), "view");

	self->waiting = FALSE;
	self->cache_valid = TRUE;

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
		gs_shell_installed_add_app (self, app);
	}
out:
	gs_plugin_list_free (list);
	gs_shell_installed_pending_apps_changed_cb (plugin_loader, self);
}

/**
 * gs_shell_installed_load:
 */
static void
gs_shell_installed_load (GsShellInstalled *self)
{
	if (self->waiting)
		return;
	self->waiting = TRUE;

	/* remove old entries */
	gs_container_remove_all (GTK_CONTAINER (self->list_box_install));

	/* get popular apps */
	gs_plugin_loader_get_installed_async (self->plugin_loader,
					      GS_PLUGIN_REFINE_FLAGS_DEFAULT |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
					      GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
					      self->cancellable,
					      gs_shell_installed_get_installed_cb,
					      self);
	gs_start_spinner (GTK_SPINNER (self->spinner_install));
	gtk_stack_set_visible_child_name (GTK_STACK (self->stack_install), "spinner");
}

/**
 * gs_shell_installed_reload:
 */
void
gs_shell_installed_reload (GsShellInstalled *self)
{
	gs_shell_installed_invalidate (self);
	gs_shell_installed_load (self);
}

/**
 * gs_shell_installed_switch_to:
 **/
void
gs_shell_installed_switch_to (GsShellInstalled *self, gboolean scroll_up)
{
	GtkWidget *widget;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_INSTALLED) {
		g_warning ("Called switch_to(installed) when in mode %s",
			   gs_shell_get_mode_string (self->shell));
		return;
	}

	set_selection_mode (self, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "buttonbox_main"));
	gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_select"));
	gtk_widget_show (widget);

	if (scroll_up) {
		GtkAdjustment *adj;
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_install));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}
	if (gs_shell_get_mode (self->shell) == GS_SHELL_MODE_INSTALLED) {
		gs_grab_focus_when_mapped (self->scrolledwindow_install);
	}

	/* no need to refresh */
	if (self->cache_valid)
		return;

	gs_shell_installed_load (self);
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
	g_autofree gchar *casefolded_name = NULL;

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
 * gs_shell_installed_sort_func:
 **/
static gint
gs_shell_installed_sort_func (GtkListBoxRow *a,
			      GtkListBoxRow *b,
			      gpointer user_data)
{
	GsApp *a1, *a2;
	g_autofree gchar *key1 = NULL;
	g_autofree gchar *key2 = NULL;

	/* check valid */
	if (!GTK_IS_BIN(a) || !GTK_IS_BIN(b)) {
		g_warning ("GtkListBoxRow not valid");
		return 0;
	}

	a1 = gs_app_row_get_app (GS_APP_ROW (a));
	a2 = gs_app_row_get_app (GS_APP_ROW (b));
	key1 = gs_shell_installed_get_app_sort_key (a1);
	key2 = gs_shell_installed_get_app_sort_key (a2);

	/* compare the keys according to the algorithm above */
	return g_strcmp0 (key1, key2);
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
gs_shell_installed_has_app (GsShellInstalled *self,
			    GsApp *app)
{
	GList *l;
	gboolean ret = FALSE;
	g_autoptr(GList) children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
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
					    GsShellInstalled *self)
{
	GsApp *app;
	GtkWidget *widget;
	guint i;
	g_autoptr(GPtrArray) pending = NULL;

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder,
						     "button_installed_counter"));
	pending = gs_plugin_loader_get_pending (plugin_loader);
	if (pending->len == 0) {
		gtk_widget_hide (widget);
	} else {
		g_autofree gchar *label = NULL;
		gtk_widget_show (widget);
		label = g_strdup_printf ("%d", pending->len);
		gtk_label_set_label (GTK_LABEL (widget), label);
	}
	for (i = 0; i < pending->len; i++) {
		app = GS_APP (g_ptr_array_index (pending, i));
		/* Be careful not to add pending apps more than once. */
		if (gs_shell_installed_has_app (self, app) == FALSE)
			gs_shell_installed_add_app (self, app);
	}
}

static void
set_selection_mode (GsShellInstalled *self, gboolean selection_mode)
{
	GList *l;
	GtkWidget *header;
	GtkWidget *widget;
	GtkStyleContext *context;
	g_autoptr(GList) children = NULL;
	
	if (self->selection_mode == selection_mode)
		return;

	self->selection_mode = selection_mode;

	header = GTK_WIDGET (gtk_builder_get_object (self->builder, "header"));
	context = gtk_widget_get_style_context (header);
	if (self->selection_mode) {
		gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header), FALSE);
		gtk_style_context_add_class (context, "selection-mode");
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_select"));
		gtk_button_set_image (GTK_BUTTON (widget), NULL);
		gtk_button_set_label (GTK_BUTTON (widget), _("_Cancel"));
		gtk_button_set_use_underline (GTK_BUTTON (widget), TRUE);
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "buttonbox_main"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "header_selection_menu_button"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "header_selection_label"));
		gtk_label_set_label (GTK_LABEL (widget), _("Click on items to select them"));
	} else {
		gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header), TRUE);
		gtk_style_context_remove_class (context, "selection-mode");
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_select"));
		gtk_button_set_image (GTK_BUTTON (widget), gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU));
		gtk_button_set_label (GTK_BUTTON (widget), NULL);
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "buttonbox_main"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "header_selection_menu_button"));
		gtk_widget_hide (widget);

		gtk_widget_hide (self->button_folder_add);
		gtk_widget_hide (self->button_folder_move);
		gtk_widget_hide (self->button_folder_remove);
	}

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		gs_app_row_set_selectable (app_row,
					   self->selection_mode);
	}

	gtk_revealer_set_reveal_child (GTK_REVEALER (self->bottom_install), self->selection_mode);
}

static void
selection_mode_cb (GtkButton *button, GsShellInstalled *self)
{
	set_selection_mode (self, !self->selection_mode);
}

static GList *
get_selected_apps (GsShellInstalled *self)
{
	GList *l, *list;
	g_autoptr(GList) children = NULL;

	list = NULL;
	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		if (gs_app_row_get_selected (app_row)) {
			list = g_list_prepend (list, gs_app_row_get_app (app_row));
		}
	}
	return list;
}

static void
selection_changed (GsShellInstalled *self)
{
	GList *l;
	GsApp *app;
	gboolean has_folders, has_nonfolders;
	g_autoptr(GList) apps = NULL;
	g_autoptr(GsFolders) folders = NULL;

	folders = gs_folders_get ();
	has_folders = has_nonfolders = FALSE;
	apps = get_selected_apps (self);
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

	gtk_widget_set_visible (self->button_folder_add, has_nonfolders);
	gtk_widget_set_visible (self->button_folder_move, has_folders && !has_nonfolders);
	gtk_widget_set_visible (self->button_folder_remove, has_folders);
}

static gboolean
folder_dialog_done (GsShellInstalled *self)
{
	set_selection_mode (self, FALSE);
	return FALSE;
}

static void
show_folder_dialog (GtkButton *button, GsShellInstalled *self)
{
	GtkWidget *toplevel;
	GtkWidget *dialog;
	g_autoptr(GList) apps = NULL;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
	apps = get_selected_apps (self);
	dialog = gs_app_folder_dialog_new (GTK_WINDOW (toplevel), apps);
	gtk_window_present (GTK_WINDOW (dialog));
	g_signal_connect_swapped (dialog, "delete-event",
				  G_CALLBACK (folder_dialog_done), self);
}

static void
remove_folders (GtkButton *button, GsShellInstalled *self)
{
	GList *l;
	GsApp *app;
	g_autoptr(GList) apps = NULL;
	g_autoptr(GsFolders) folders = NULL;

	folders = gs_folders_get ();
	apps = get_selected_apps (self);
	for (l = apps; l; l = l->next) {
		app = l->data;
		gs_folders_set_app_folder (folders,
					   gs_app_get_id (app),
					   gs_app_get_categories (app),
					   NULL);
	}

	gs_folders_save (folders);

	set_selection_mode (self, FALSE);
}

static void
select_all_cb (GtkMenuItem *item, GsShellInstalled *self)
{
	GList *l;
	g_autoptr(GList) children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		gs_app_row_set_selected (app_row, TRUE);
	}
}

static void
select_none_cb (GtkMenuItem *item, GsShellInstalled *self)
{
	GList *l;
	g_autoptr(GList) children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		gs_app_row_set_selected (app_row, FALSE);
	}
}

/**
 * gs_shell_installed_setup:
 */
void
gs_shell_installed_setup (GsShellInstalled *self,
			  GsShell *shell,
			  GsPluginLoader *plugin_loader,
			  GtkBuilder *builder,
			  GCancellable *cancellable)
{
	GtkWidget *widget;

	g_return_if_fail (GS_IS_SHELL_INSTALLED (self));

	self->shell = shell;
	self->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (self->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_shell_installed_pending_apps_changed_cb),
			  self);

	self->builder = g_object_ref (builder);
	self->cancellable = g_object_ref (cancellable);

	/* setup installed */
	g_signal_connect (self->list_box_install, "row-activated",
			  G_CALLBACK (gs_shell_installed_app_row_activated_cb), self);
	gtk_list_box_set_header_func (GTK_LIST_BOX (self->list_box_install),
				      gs_shell_installed_list_header_func,
				      self, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->list_box_install),
				    gs_shell_installed_sort_func,
				    self, NULL);

	g_signal_connect (self->button_folder_add, "clicked",
			  G_CALLBACK (show_folder_dialog), self);
	
	g_signal_connect (self->button_folder_move, "clicked",
			  G_CALLBACK (show_folder_dialog), self);
	
	g_signal_connect (self->button_folder_remove, "clicked",
			  G_CALLBACK (remove_folders), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_select"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (selection_mode_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_select"));
	gtk_button_set_image (GTK_BUTTON (widget), gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU));
	gtk_button_set_label (GTK_BUTTON (widget), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "select_all_menuitem"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (select_all_cb), self);
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "select_none_menuitem"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (select_none_cb), self);

	/* chain up */
	gs_page_setup (GS_PAGE (self),
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
	GsShellInstalled *self = GS_SHELL_INSTALLED (object);

	g_clear_object (&self->sizegroup_image);
	g_clear_object (&self->sizegroup_name);

	g_clear_object (&self->builder);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);

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

	gtk_widget_class_bind_template_child (widget_class, GsShellInstalled, bottom_install);
	gtk_widget_class_bind_template_child (widget_class, GsShellInstalled, button_folder_add);
	gtk_widget_class_bind_template_child (widget_class, GsShellInstalled, button_folder_move);
	gtk_widget_class_bind_template_child (widget_class, GsShellInstalled, button_folder_remove);
	gtk_widget_class_bind_template_child (widget_class, GsShellInstalled, list_box_install);
	gtk_widget_class_bind_template_child (widget_class, GsShellInstalled, scrolledwindow_install);
	gtk_widget_class_bind_template_child (widget_class, GsShellInstalled, spinner_install);
	gtk_widget_class_bind_template_child (widget_class, GsShellInstalled, stack_install);
}

/**
 * gs_shell_installed_init:
 **/
static void
gs_shell_installed_init (GsShellInstalled *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	self->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
}

/**
 * gs_shell_installed_new:
 **/
GsShellInstalled *
gs_shell_installed_new (void)
{
	GsShellInstalled *self;
	self = g_object_new (GS_TYPE_SHELL_INSTALLED, NULL);
	return GS_SHELL_INSTALLED (self);
}

/* vim: set noexpandtab: */
