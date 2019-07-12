/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#include "gs-shell.h"
#include "gs-installed-page.h"
#include "gs-common.h"
#include "gs-app-row.h"
#include "gs-app-folder-dialog.h"
#include "gs-folders.h"

struct _GsInstalledPage
{
	GsPage			 parent_instance;

	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	GtkSizeGroup		*sizegroup_desc;
	GtkSizeGroup		*sizegroup_button;
	gboolean		 cache_valid;
	gboolean		 waiting;
	GsShell			*shell;
	GtkWidget		*button_select;
	gboolean 		 selection_mode;
	GSettings		*settings;

	GtkWidget		*bottom_install;
	GtkWidget		*button_folder_add;
	GtkWidget		*button_folder_move;
	GtkWidget		*button_folder_remove;
	GtkWidget		*list_box_install;
	GtkWidget		*scrolledwindow_install;
	GtkWidget		*spinner_install;
	GtkWidget		*stack_install;
};

G_DEFINE_TYPE (GsInstalledPage, gs_installed_page, GS_TYPE_PAGE)

static void gs_installed_page_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
                                                       GsInstalledPage *self);
static void set_selection_mode (GsInstalledPage *self, gboolean selection_mode);

static void
gs_installed_page_invalidate (GsInstalledPage *self)
{
	self->cache_valid = FALSE;
}

static void
gs_installed_page_app_row_activated_cb (GtkListBox *list_box,
                                        GtkListBoxRow *row,
                                        GsInstalledPage *self)
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
	if (list == NULL)
		return;
	gtk_container_remove (GTK_CONTAINER (list), GTK_WIDGET (row));
}

static void
gs_installed_page_unreveal_row (GsAppRow *app_row)
{
	gs_app_row_unreveal (app_row);
	g_signal_connect (app_row, "unrevealed",
			  G_CALLBACK (row_unrevealed), NULL);
}

static void
gs_installed_page_app_removed (GsPage *page, GsApp *app)
{
	GsInstalledPage *self = GS_INSTALLED_PAGE (page);
	g_autoptr(GList) children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (GList *l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		if (gs_app_row_get_app (app_row) == app) {
			gs_installed_page_unreveal_row (app_row);
		}
	}
}

static void
gs_installed_page_app_remove_cb (GsAppRow *app_row,
                                 GsInstalledPage *self)
{
	GsApp *app;

	app = gs_app_row_get_app (app_row);
	gs_page_remove_app (GS_PAGE (self), app, self->cancellable);
}

static gboolean
gs_installed_page_invalidate_sort_idle (gpointer user_data)
{
	GsAppRow *app_row = user_data;
	GsApp *app = gs_app_row_get_app (app_row);
	AsAppState state = gs_app_get_state (app);

	gtk_list_box_row_changed (GTK_LIST_BOX_ROW (app_row));

	/* if the app has been uninstalled (which can happen from another view)
	 * we should removed it from the installed view */
	if (state == AS_APP_STATE_AVAILABLE || state == AS_APP_STATE_UNKNOWN)
		gs_installed_page_unreveal_row (app_row);

	g_object_unref (app_row);
	return G_SOURCE_REMOVE;
}

static void
gs_installed_page_notify_state_changed_cb (GsApp *app,
                                           GParamSpec *pspec,
                                           GsAppRow *app_row)
{
	g_idle_add (gs_installed_page_invalidate_sort_idle, g_object_ref (app_row));
}

static void selection_changed (GsInstalledPage *self);

static gboolean
should_show_installed_size (GsInstalledPage *self)
{
	return g_settings_get_boolean (self->settings,
				       "installed-page-show-size");
}

static gboolean
gs_installed_page_is_actual_app (GsApp *app)
{
	if (gs_app_get_description (app) != NULL)
		return TRUE;
	/* special snowflake */
	if (g_strcmp0 (gs_app_get_id (app), "google-chrome.desktop") == 0)
		return TRUE;
	g_debug ("%s is not an actual app", gs_app_get_unique_id (app));
	return FALSE;
}

static void
gs_installed_page_add_app (GsInstalledPage *self, GsAppList *list, GsApp *app)
{
	GtkWidget *app_row;

	app_row = gs_app_row_new (app);
	gs_app_row_set_show_folders (GS_APP_ROW (app_row), FALSE);
	gs_app_row_set_show_buttons (GS_APP_ROW (app_row), TRUE);
	if (gs_utils_list_has_app_fuzzy (list, app))
		gs_app_row_set_show_source (GS_APP_ROW (app_row), TRUE);
	g_signal_connect (app_row, "button-clicked",
			  G_CALLBACK (gs_installed_page_app_remove_cb), self);
	g_signal_connect_object (app, "notify::state",
				 G_CALLBACK (gs_installed_page_notify_state_changed_cb),
				 app_row, 0);
	g_signal_connect_swapped (app_row, "notify::selected",
				  G_CALLBACK (selection_changed), self);
	gtk_container_add (GTK_CONTAINER (self->list_box_install), app_row);
	gs_app_row_set_size_groups (GS_APP_ROW (app_row),
				    self->sizegroup_image,
				    self->sizegroup_name,
				    self->sizegroup_desc,
				    self->sizegroup_button);

	if (!gs_app_has_quirk (app, GS_APP_QUIRK_COMPULSORY)) {
		gs_app_row_set_show_installed_size (GS_APP_ROW (app_row),
						    should_show_installed_size (self));
	}
	gs_app_row_set_selectable (GS_APP_ROW (app_row),
				   self->selection_mode);

	/* only show if is an actual application */
	gtk_widget_set_visible (app_row, gs_installed_page_is_actual_app (app));
}

static void
gs_installed_page_get_installed_cb (GObject *source_object,
                                    GAsyncResult *res,
                                    gpointer user_data)
{
	guint i;
	GsApp *app;
	GsInstalledPage *self = GS_INSTALLED_PAGE (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	gs_stop_spinner (GTK_SPINNER (self->spinner_install));
	gtk_stack_set_visible_child_name (GTK_STACK (self->stack_install), "view");

	self->waiting = FALSE;
	self->cache_valid = TRUE;

	list = gs_plugin_loader_job_process_finish (plugin_loader,
						    res,
						    &error);
	if (list == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to get installed apps: %s", error->message);
		goto out;
	}
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		gs_installed_page_add_app (self, list, app);
	}
out:
	gs_installed_page_pending_apps_changed_cb (plugin_loader, self);
}

static void
gs_installed_page_load (GsInstalledPage *self)
{
	GsPluginRefineFlags flags;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (self->waiting)
		return;
	self->waiting = TRUE;

	/* remove old entries */
	gs_container_remove_all (GTK_CONTAINER (self->list_box_install));

	flags = GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_CATEGORIES |
		GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING;

	if (should_show_installed_size (self))
		flags |= GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE;

	/* get installed apps */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_INSTALLED,
					 "refine-flags", flags,
					 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_NONE,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader,
					    plugin_job,
					    self->cancellable,
					    gs_installed_page_get_installed_cb,
					    self);
	gs_start_spinner (GTK_SPINNER (self->spinner_install));
	gtk_stack_set_visible_child_name (GTK_STACK (self->stack_install), "spinner");
}

static void
gs_installed_page_reload (GsPage *page)
{
	GsInstalledPage *self = GS_INSTALLED_PAGE (page);
	gs_installed_page_invalidate (self);
	gs_installed_page_load (self);
}

static void
gs_shell_update_button_select_visibility (GsInstalledPage *self)
{
	gboolean show_button_select;
	if (gs_utils_is_current_desktop ("GNOME")) {
		show_button_select = g_settings_get_boolean (self->settings,
							     "show-folder-management");
	} else {
		show_button_select = FALSE;
	}
	gtk_widget_set_visible (self->button_select, show_button_select);
}

static void
gs_installed_page_switch_to (GsPage *page, gboolean scroll_up)
{
	GsInstalledPage *self = GS_INSTALLED_PAGE (page);
	GtkWidget *widget;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_INSTALLED) {
		g_warning ("Called switch_to(installed) when in mode %s",
			   gs_shell_get_mode_string (self->shell));
		return;
	}

	set_selection_mode (self, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "buttonbox_main"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "menu_button"));
	gtk_widget_show (widget);

	gs_shell_update_button_select_visibility (self);

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

	gs_installed_page_load (self);
}

/**
 * gs_installed_page_get_app_sort_key:
 *
 * Get a sort key to achive this:
 *
 * 1. state:installing applications
 * 2. state: applications queued for installing
 * 3. state:removing applications
 * 4. kind:normal applications
 * 5. kind:system applications
 *
 * Within each of these groups, they are sorted by the install date and then
 * by name.
 **/
static gchar *
gs_installed_page_get_app_sort_key (GsApp *app)
{
	GString *key;
	g_autofree gchar *casefolded_name = NULL;

	key = g_string_sized_new (64);

	/* sort installed, removing, other */
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_INSTALLING:
		g_string_append (key, "1:");
		break;
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		g_string_append (key, "2:");
		break;
	case AS_APP_STATE_REMOVING:
		g_string_append (key, "3:");
		break;
	default:
		g_string_append (key, "4:");
		break;
	}

	/* sort apps by kind */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_OS_UPDATE:
		g_string_append (key, "1:");
		break;
	case AS_APP_KIND_DESKTOP:
		g_string_append (key, "2:");
		break;
	case AS_APP_KIND_WEB_APP:
		g_string_append (key, "3:");
		break;
	case AS_APP_KIND_RUNTIME:
		g_string_append (key, "4:");
		break;
	case AS_APP_KIND_ADDON:
		g_string_append (key, "5:");
		break;
	case AS_APP_KIND_CODEC:
		g_string_append (key, "6:");
		break;
	case AS_APP_KIND_FONT:
		g_string_append (key, "6:");
		break;
	case AS_APP_KIND_INPUT_METHOD:
		g_string_append (key, "7:");
		break;
	case AS_APP_KIND_SHELL_EXTENSION:
		g_string_append (key, "8:");
		break;
	default:
		g_string_append (key, "9:");
		break;
	}

	/* sort normal, compulsory */
	if (!gs_app_has_quirk (app, GS_APP_QUIRK_COMPULSORY))
		g_string_append (key, "1:");
	else
		g_string_append (key, "2:");

	/* finally, sort by short name */
	if (gs_app_get_name (app) != NULL) {
		casefolded_name = g_utf8_casefold (gs_app_get_name (app), -1);
		g_string_append (key, casefolded_name);
	}
	return g_string_free (key, FALSE);
}

static gint
gs_installed_page_sort_func (GtkListBoxRow *a,
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
	key1 = gs_installed_page_get_app_sort_key (a1);
	key2 = gs_installed_page_get_app_sort_key (a2);

	/* compare the keys according to the algorithm above */
	return g_strcmp0 (key1, key2);
}

typedef enum {
	GS_UPDATE_LIST_SECTION_REMOVABLE_APPS,
	GS_UPDATE_LIST_SECTION_SYSTEM_APPS,
	GS_UPDATE_LIST_SECTION_ADDONS,
	GS_UPDATE_LIST_SECTION_LAST
} GsInstalledPageSection;

static GsInstalledPageSection
gs_installed_page_get_app_section (GsApp *app)
{
	if (gs_app_get_kind (app) == AS_APP_KIND_DESKTOP ||
	    gs_app_get_kind (app) == AS_APP_KIND_WEB_APP) {
		if (gs_app_has_quirk (app, GS_APP_QUIRK_COMPULSORY))
			return GS_UPDATE_LIST_SECTION_SYSTEM_APPS;
		return GS_UPDATE_LIST_SECTION_REMOVABLE_APPS;
	}
	return GS_UPDATE_LIST_SECTION_ADDONS;
}

static GtkWidget *
gs_installed_page_get_section_header (GsInstalledPageSection section)
{
	GtkWidget *header = NULL;

	if (section == GS_UPDATE_LIST_SECTION_SYSTEM_APPS) {
		/* TRANSLATORS: This is the header dividing the normal
		 * applications and the system ones */
		header = gtk_label_new (_("System Applications"));
	} else if (section == GS_UPDATE_LIST_SECTION_ADDONS) {
		/* TRANSLATORS: This is the header dividing the normal
		 * applications and the addons */
		header = gtk_label_new (_("Add-ons"));
	}

	/* fix header style */
	if (header != NULL) {
		GtkStyleContext *context = gtk_widget_get_style_context (header);
		gtk_label_set_xalign (GTK_LABEL (header), 0.0);
		gtk_style_context_add_class (context, "app-listbox-header");
		gtk_style_context_add_class (context, "app-listbox-header-title");
	}
	return header;
}

static void
gs_installed_page_list_header_func (GtkListBoxRow *row,
                                    GtkListBoxRow *before,
                                    gpointer user_data)
{
	GsApp *app = gs_app_row_get_app (GS_APP_ROW (row));
	GsInstalledPageSection before_section = GS_UPDATE_LIST_SECTION_LAST;
	GsInstalledPageSection section;
	GtkWidget *header;

	/* first entry */
	gtk_list_box_row_set_header (row, NULL);
	if (before != NULL) {
		GsApp *before_app = gs_app_row_get_app (GS_APP_ROW (before));
		before_section = gs_installed_page_get_app_section (before_app);
	}

	/* section changed or forced to have headers */
	section = gs_installed_page_get_app_section (app);
	if (before_section != section) {
		header = gs_installed_page_get_section_header (section);
		if (header == NULL)
			return;
	} else {
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	}
	gtk_list_box_row_set_header (row, header);
}

static gboolean
gs_installed_page_has_app (GsInstalledPage *self,
                           GsApp *app)
{
	gboolean ret = FALSE;
	g_autoptr(GList) children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (GList *l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		if (gs_app_row_get_app (app_row) == app) {
			ret = TRUE;
			break;
		}
	}
	return ret;
}

static void
gs_installed_page_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
                                           GsInstalledPage *self)
{
	GsApp *app;
	GtkWidget *widget;
	guint i;
	guint cnt = 0;
	g_autoptr(GsAppList) pending = NULL;

	/* add new apps to the list */
	pending = gs_plugin_loader_get_pending (plugin_loader);
	for (i = 0; i < gs_app_list_length (pending); i++) {
		app = gs_app_list_index (pending, i);

		/* never show OS upgrades, we handle the scheduling and
		 * cancellation in GsUpgradeBanner */
		if (gs_app_get_kind (app) == AS_APP_KIND_OS_UPGRADE)
			continue;

		/* do not to add pending apps more than once. */
		if (gs_installed_page_has_app (self, app) == FALSE)
			gs_installed_page_add_app (self, pending, app);

		/* incremement the label */
		cnt++;
	}

	/* show a label with the number of on-going operations */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder,
						     "button_installed_counter"));
	if (cnt == 0) {
		gtk_widget_hide (widget);
	} else {
		g_autofree gchar *label = NULL;
		label = g_strdup_printf ("%u", cnt);
		gtk_label_set_label (GTK_LABEL (widget), label);
		gtk_widget_show (widget);
	}
}

static void
set_selection_mode (GsInstalledPage *self, gboolean selection_mode)
{
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
		gtk_button_set_image (GTK_BUTTON (self->button_select), NULL);
		gtk_button_set_label (GTK_BUTTON (self->button_select), _("_Cancel"));
		gtk_button_set_use_underline (GTK_BUTTON (self->button_select), TRUE);
		gtk_widget_show (self->button_select);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "buttonbox_main"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "menu_button"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "header_selection_menu_button"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "header_selection_label"));
		gtk_label_set_label (GTK_LABEL (widget), _("Click on items to select them"));
	} else {
		gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header), TRUE);
		gtk_style_context_remove_class (context, "selection-mode");
		gtk_button_set_image (GTK_BUTTON (self->button_select), gtk_image_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU));
		gtk_button_set_label (GTK_BUTTON (self->button_select), NULL);
		gtk_widget_show (self->button_select);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "buttonbox_main"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "menu_button"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "header_selection_menu_button"));
		gtk_widget_hide (widget);
	}

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (GList *l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		GsApp *app = gs_app_row_get_app (app_row);
		gs_app_row_set_selectable (app_row,
					   self->selection_mode);
		gtk_widget_set_visible (GTK_WIDGET (app_row),
					self->selection_mode ||
					gs_installed_page_is_actual_app (app));
	}

	gtk_revealer_set_reveal_child (GTK_REVEALER (self->bottom_install), self->selection_mode);
}

static void
selection_mode_cb (GtkButton *button, GsInstalledPage *self)
{
	set_selection_mode (self, !self->selection_mode);
}

static GList *
get_selected_apps (GsInstalledPage *self)
{
	GList *list;
	g_autoptr(GList) children = NULL;

	list = NULL;
	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (GList *l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		if (gs_app_row_get_selected (app_row)) {
			list = g_list_prepend (list, gs_app_row_get_app (app_row));
		}
	}
	return list;
}

static void
selection_changed (GsInstalledPage *self)
{
	GsApp *app;
	gboolean has_folders, has_nonfolders;
	g_autoptr(GList) apps = NULL;
	g_autoptr(GsFolders) folders = NULL;

	folders = gs_folders_get ();
	has_folders = has_nonfolders = FALSE;
	apps = get_selected_apps (self);
	for (GList *l = apps; l; l = l->next) {
		app = l->data;
		if (gs_folders_get_app_folder (folders,
					       gs_app_get_id (app),
					       gs_app_get_categories (app))) {
			has_folders = TRUE;
		} else {
			has_nonfolders = TRUE;
		}
	}

	gtk_widget_set_sensitive (self->button_folder_add, has_nonfolders);
	gtk_widget_set_sensitive (self->button_folder_move, has_folders && !has_nonfolders);
	gtk_widget_set_sensitive (self->button_folder_remove, has_folders);
}

static gboolean
folder_dialog_done (GsInstalledPage *self)
{
	set_selection_mode (self, FALSE);
	return FALSE;
}

static void
show_folder_dialog (GtkButton *button, GsInstalledPage *self)
{
	GtkWidget *toplevel;
	GtkWidget *dialog;
	g_autoptr(GList) apps = NULL;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
	apps = get_selected_apps (self);
	dialog = gs_app_folder_dialog_new (GTK_WINDOW (toplevel), apps);
	g_signal_connect_swapped (dialog, "delete-event",
				  G_CALLBACK (folder_dialog_done), self);
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);
	gs_shell_modal_dialog_present (self->shell, GTK_DIALOG (dialog));
}

static void
remove_folders (GtkButton *button, GsInstalledPage *self)
{
	GsApp *app;
	g_autoptr(GList) apps = NULL;
	g_autoptr(GsFolders) folders = NULL;

	folders = gs_folders_get ();
	apps = get_selected_apps (self);
	for (GList *l = apps; l; l = l->next) {
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
select_all_cb (GtkMenuItem *item, GsInstalledPage *self)
{
	g_autoptr(GList) children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (GList *l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		gs_app_row_set_selected (app_row, TRUE);
	}
}

static void
select_none_cb (GtkMenuItem *item, GsInstalledPage *self)
{
	g_autoptr(GList) children = NULL;

	children = gtk_container_get_children (GTK_CONTAINER (self->list_box_install));
	for (GList *l = children; l; l = l->next) {
		GsAppRow *app_row = GS_APP_ROW (l->data);
		gs_app_row_set_selected (app_row, FALSE);
	}
}

static void
gs_shell_settings_changed_cb (GsInstalledPage *self,
                              const gchar *key,
                              gpointer data)
{
	if (g_strcmp0 (key, "show-folder-management") == 0) {
		gs_shell_update_button_select_visibility (self);
	}
}

static gboolean
gs_installed_page_setup (GsPage *page,
                         GsShell *shell,
                         GsPluginLoader *plugin_loader,
                         GtkBuilder *builder,
                         GCancellable *cancellable,
                         GError **error)
{
	GsInstalledPage *self = GS_INSTALLED_PAGE (page);
	AtkObject *accessible;
	GtkWidget *widget;

	g_return_val_if_fail (GS_IS_INSTALLED_PAGE (self), TRUE);

	self->shell = shell;
	self->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (self->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_installed_page_pending_apps_changed_cb),
			  self);

	self->builder = g_object_ref (builder);
	self->cancellable = g_object_ref (cancellable);

	/* setup installed */
	g_signal_connect (self->list_box_install, "row-activated",
			  G_CALLBACK (gs_installed_page_app_row_activated_cb), self);
	gtk_list_box_set_header_func (GTK_LIST_BOX (self->list_box_install),
				      gs_installed_page_list_header_func,
				      self, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->list_box_install),
				    gs_installed_page_sort_func,
				    self, NULL);

	g_signal_connect (self->button_folder_add, "clicked",
			  G_CALLBACK (show_folder_dialog), self);
	
	g_signal_connect (self->button_folder_move, "clicked",
			  G_CALLBACK (show_folder_dialog), self);
	
	g_signal_connect (self->button_folder_remove, "clicked",
			  G_CALLBACK (remove_folders), self);

	self->button_select = gtk_button_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU);
	accessible = gtk_widget_get_accessible (self->button_select);
	if (accessible != NULL)
		atk_object_set_name (accessible, _("Select"));
	gs_page_set_header_end_widget (GS_PAGE (self), self->button_select);
	g_signal_connect (self->button_select, "clicked",
			  G_CALLBACK (selection_mode_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "select_all_menuitem"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (select_all_cb), self);
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "select_none_menuitem"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (select_none_cb), self);
	return TRUE;
}

static void
gs_installed_page_dispose (GObject *object)
{
	GsInstalledPage *self = GS_INSTALLED_PAGE (object);

	g_clear_object (&self->sizegroup_image);
	g_clear_object (&self->sizegroup_name);
	g_clear_object (&self->sizegroup_desc);
	g_clear_object (&self->sizegroup_button);

	g_clear_object (&self->builder);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);
	g_clear_object (&self->settings);

	G_OBJECT_CLASS (gs_installed_page_parent_class)->dispose (object);
}

static void
gs_installed_page_class_init (GsInstalledPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_installed_page_dispose;
	page_class->app_removed = gs_installed_page_app_removed;
	page_class->switch_to = gs_installed_page_switch_to;
	page_class->reload = gs_installed_page_reload;
	page_class->setup = gs_installed_page_setup;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-installed-page.ui");

	gtk_widget_class_bind_template_child (widget_class, GsInstalledPage, bottom_install);
	gtk_widget_class_bind_template_child (widget_class, GsInstalledPage, button_folder_add);
	gtk_widget_class_bind_template_child (widget_class, GsInstalledPage, button_folder_move);
	gtk_widget_class_bind_template_child (widget_class, GsInstalledPage, button_folder_remove);
	gtk_widget_class_bind_template_child (widget_class, GsInstalledPage, list_box_install);
	gtk_widget_class_bind_template_child (widget_class, GsInstalledPage, scrolledwindow_install);
	gtk_widget_class_bind_template_child (widget_class, GsInstalledPage, spinner_install);
	gtk_widget_class_bind_template_child (widget_class, GsInstalledPage, stack_install);
}

static void
gs_installed_page_init (GsInstalledPage *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	self->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_desc = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_button = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	self->settings = g_settings_new ("org.gnome.software");
	g_signal_connect_swapped (self->settings, "changed",
				  G_CALLBACK (gs_shell_settings_changed_cb),
				  self);
}

GsInstalledPage *
gs_installed_page_new (void)
{
	GsInstalledPage *self;
	self = g_object_new (GS_TYPE_INSTALLED_PAGE, NULL);
	return GS_INSTALLED_PAGE (self);
}
