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
#include "gs-app-row.h"

struct _GsShellSearch
{
	GsPage			 parent_instance;

	GsPluginLoader		*plugin_loader;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	GCancellable		*search_cancellable;
	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	GsShell			*shell;
	gchar			*appid_to_show;
	gchar			*value;

	GtkWidget		*list_box_search;
	GtkWidget		*scrolledwindow_search;
	GtkWidget		*spinner_search;
	GtkWidget		*stack_search;
};

G_DEFINE_TYPE (GsShellSearch, gs_shell_search, GS_TYPE_PAGE)

static void
gs_shell_search_app_row_activated_cb (GtkListBox *list_box,
				      GtkListBoxRow *row,
				      GsShellSearch *self)
{
	GsApp *app;
	app = gs_app_row_get_app (GS_APP_ROW (row));
	gs_shell_show_app (self->shell, app);
}

/**
 * gs_shell_search_app_row_clicked_cb:
 **/
static void
gs_shell_search_app_row_clicked_cb (GsAppRow *app_row,
				    GsShellSearch *self)
{
	GsApp *app;
	app = gs_app_row_get_app (app_row);
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE)
		gs_page_install_app (GS_PAGE (self), app);
	else if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
		gs_page_remove_app (GS_PAGE (self), app);
	else if (gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		if (gs_app_get_url (app, AS_URL_KIND_MISSING) == NULL) {
			gs_page_install_app (GS_PAGE (self), app);
			return;
		}
		gs_app_show_url (app, AS_URL_KIND_MISSING);
	}
}

/**
 * gs_shell_search_get_search_cb:
 **/
static void
gs_shell_search_get_search_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GList *l;
	GsApp *app;
	GsShellSearch *self = GS_SHELL_SEARCH (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *app_row;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	list = gs_plugin_loader_search_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_debug ("search cancelled");
			return;
		}
		if (g_error_matches (error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_NO_RESULTS)) {
			g_debug ("no search results to show");
		} else {
			g_warning ("failed to get search apps: %s", error->message);
		}
		gs_stop_spinner (GTK_SPINNER (self->spinner_search));
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_search), "no-results");
		return;
	}

	gs_stop_spinner (GTK_SPINNER (self->spinner_search));
	gtk_stack_set_visible_child_name (GTK_STACK (self->stack_search), "results");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		app_row = gs_app_row_new (app);
		g_signal_connect (app_row, "button-clicked",
				  G_CALLBACK (gs_shell_search_app_row_clicked_cb),
				  self);
		gtk_container_add (GTK_CONTAINER (self->list_box_search), app_row);
		gs_app_row_set_size_groups (GS_APP_ROW (app_row),
					    self->sizegroup_image,
					    self->sizegroup_name);
		gtk_widget_show (app_row);
	}

	if (self->appid_to_show != NULL) {
		gs_shell_show_details (self->shell, self->appid_to_show);
		g_clear_pointer (&self->appid_to_show, g_free);
	}
}

/**
 * gs_shell_search_load:
 */
static void
gs_shell_search_load (GsShellSearch *self)
{
	/* remove old entries */
	gs_container_remove_all (GTK_CONTAINER (self->list_box_search));

	/* cancel any pending searches */
	if (self->search_cancellable != NULL) {
		g_cancellable_cancel (self->search_cancellable);
		g_object_unref (self->search_cancellable);
	}
	self->search_cancellable = g_cancellable_new ();

	/* search for apps */
	gs_plugin_loader_search_async (self->plugin_loader,
				       self->value,
				       GS_PLUGIN_REFINE_FLAGS_DEFAULT |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENCE |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
				       self->search_cancellable,
				       gs_shell_search_get_search_cb,
				       self);

	gtk_stack_set_visible_child_name (GTK_STACK (self->stack_search), "spinner");
	gs_start_spinner (GTK_SPINNER (self->spinner_search));
}

/**
 * gs_shell_search_reload:
 */
void
gs_shell_search_reload (GsShellSearch *self)
{
	if (self->value != NULL)
		gs_shell_search_load (self);
}


/**
 * gs_shell_search_set_appid_to_show:
 *
 * Switch to the specified app id after loading the search results.
 **/
void
gs_shell_search_set_appid_to_show (GsShellSearch *self, const gchar *appid)
{
	g_free (self->appid_to_show);
	self->appid_to_show = g_strdup (appid);
}

/**
 * gs_shell_search_switch_to:
 **/
void
gs_shell_search_switch_to (GsShellSearch *self, const gchar *value, gboolean scroll_up)
{
	GtkWidget *widget;

	if (gs_shell_get_mode (self->shell) != GS_SHELL_MODE_SEARCH) {
		g_warning ("Called switch_to(search) when in mode %s",
			   gs_shell_get_mode_string (self->shell));
		return;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "buttonbox_main"));
	gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "search_bar"));
	gtk_widget_show (widget);

	if (scroll_up) {
		GtkAdjustment *adj;
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_search));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	g_free (self->value);
	self->value = g_strdup (value);

	gs_shell_search_load (self);
}

/**
 * gs_shell_installed_sort_func:
 *
 * Get a sort key to achive this:
 *
 * 1. Application rating
 * 2. Length of the long description
 * 3. Number of screenshots
 * 4. Install date
 * 5. Name
 **/
static gchar *
gs_shell_search_get_app_sort_key (GsApp *app)
{
	GPtrArray *ss;
	GString *key;
	const gchar *desc;

	/* sort installed, removing, other */
	key = g_string_sized_new (64);

	/* sort missing codecs before applications */
	switch (gs_app_get_kind (app)) {
	case GS_APP_KIND_MISSING:
		g_string_append (key, "9:");
		break;
	default:
		g_string_append (key, "1:");
		break;
	}

	/* artificially cut the rating of applications with no description */
	desc = gs_app_get_description (app);
	g_string_append_printf (key, "%c:", desc != NULL ? '2' : '1');

	/* sort by the search key */
	g_string_append_printf (key, "%s:", gs_app_get_search_sort_key (app));

	/* sort by kudos */
	g_string_append_printf (key, "%03i:", gs_app_get_kudos_percentage (app));

	/* sort by length of description */
	g_string_append_printf (key, "%03" G_GSIZE_FORMAT ":",
				desc != NULL ? strlen (desc) : 0);

	/* sort by number of screenshots */
	ss = gs_app_get_screenshots (app);
	g_string_append_printf (key, "%02i:", ss->len);

	/* sort by install date */
	g_string_append_printf (key, "%09" G_GUINT64_FORMAT ":",
				G_MAXUINT64 - gs_app_get_install_date (app));

	/* finally, sort by short name */
	g_string_append (key, gs_app_get_name (app));

	return g_string_free (key, FALSE);
}

/**
 * gs_shell_search_sort_func:
 **/
static gint
gs_shell_search_sort_func (GtkListBoxRow *a,
			   GtkListBoxRow *b,
			   gpointer user_data)
{
	GsApp *a1 = gs_app_row_get_app (GS_APP_ROW (a));
	GsApp *a2 = gs_app_row_get_app (GS_APP_ROW (b));
	g_autofree gchar *key1 = gs_shell_search_get_app_sort_key (a1);
	g_autofree gchar *key2 = gs_shell_search_get_app_sort_key (a2);

	/* compare the keys according to the algorithm above */
	return g_strcmp0 (key2, key1);
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
 * gs_shell_search_cancel_cb:
 */
static void
gs_shell_search_cancel_cb (GCancellable *cancellable,
			   GsShellSearch *self)
{
	if (self->search_cancellable != NULL)
		g_cancellable_cancel (self->search_cancellable);
}

static void
gs_shell_search_app_installed (GsPage *page, GsApp *app)
{
	gs_shell_search_reload (GS_SHELL_SEARCH (page));
}

static void
gs_shell_search_app_removed (GsPage *page, GsApp *app)
{
	gs_shell_search_reload (GS_SHELL_SEARCH (page));
}

/**
 * gs_shell_search_setup:
 */
void
gs_shell_search_setup (GsShellSearch *self,
		       GsShell *shell,
			  GsPluginLoader *plugin_loader,
			  GtkBuilder *builder,
			  GCancellable *cancellable)
{
	g_return_if_fail (GS_IS_SHELL_SEARCH (self));

	self->plugin_loader = g_object_ref (plugin_loader);
	self->builder = g_object_ref (builder);
	self->cancellable = g_object_ref (cancellable);
	self->shell = shell;

	/* connect the cancellables */
	g_cancellable_connect (self->cancellable,
			       G_CALLBACK (gs_shell_search_cancel_cb),
			       self, NULL);

	/* setup search */
	g_signal_connect (self->list_box_search, "row-activated",
			  G_CALLBACK (gs_shell_search_app_row_activated_cb), self);
	gtk_list_box_set_header_func (GTK_LIST_BOX (self->list_box_search),
				      gs_shell_search_list_header_func,
				      self, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->list_box_search),
				    gs_shell_search_sort_func,
				    self, NULL);

	/* chain up */
	gs_page_setup (GS_PAGE (self),
	               shell,
	               plugin_loader,
	               cancellable);
}

/**
 * gs_shell_search_dispose:
 **/
static void
gs_shell_search_dispose (GObject *object)
{
	GsShellSearch *self = GS_SHELL_SEARCH (object);

	g_clear_object (&self->sizegroup_image);
	g_clear_object (&self->sizegroup_name);

	g_clear_object (&self->builder);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);
	g_clear_object (&self->search_cancellable);

	g_free (self->appid_to_show);
	g_free (self->value);

	G_OBJECT_CLASS (gs_shell_search_parent_class)->dispose (object);
}

/**
 * gs_shell_search_class_init:
 **/
static void
gs_shell_search_class_init (GsShellSearchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_search_dispose;
	page_class->app_installed = gs_shell_search_app_installed;
	page_class->app_removed = gs_shell_search_app_removed;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-search.ui");

	gtk_widget_class_bind_template_child (widget_class, GsShellSearch, list_box_search);
	gtk_widget_class_bind_template_child (widget_class, GsShellSearch, scrolledwindow_search);
	gtk_widget_class_bind_template_child (widget_class, GsShellSearch, spinner_search);
	gtk_widget_class_bind_template_child (widget_class, GsShellSearch, stack_search);
}

/**
 * gs_shell_search_init:
 **/
static void
gs_shell_search_init (GsShellSearch *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	self->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
}

/**
 * gs_shell_search_new:
 **/
GsShellSearch *
gs_shell_search_new (void)
{
	GsShellSearch *self;
	self = g_object_new (GS_TYPE_SHELL_SEARCH, NULL);
	return GS_SHELL_SEARCH (self);
}

/* vim: set noexpandtab: */
