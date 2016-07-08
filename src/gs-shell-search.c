/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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
#include "gs-common.h"
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
	guint			 waiting_id;
	GtkWidget		*search_button;

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

static void
gs_shell_search_app_row_clicked_cb (GsAppRow *app_row,
				    GsShellSearch *self)
{
	GsApp *app;
	app = gs_app_row_get_app (app_row);
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE)
		gs_page_install_app (GS_PAGE (self), app, self->cancellable);
	else if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
		gs_page_remove_app (GS_PAGE (self), app, self->cancellable);
	else if (gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		if (gs_app_get_url (app, AS_URL_KIND_MISSING) == NULL) {
			gs_page_install_app (GS_PAGE (self), app, self->cancellable);
			return;
		}
		gs_app_show_url (app, AS_URL_KIND_MISSING);
	}
}

static void
gs_shell_search_waiting_cancel (GsShellSearch *self)
{
	if (self->waiting_id > 0)
		g_source_remove (self->waiting_id);
	self->waiting_id = 0;
}

static void
gs_shell_search_get_search_cb (GObject *source_object,
			       GAsyncResult *res,
			       gpointer user_data)
{
	guint i;
	GsApp *app;
	GsShellSearch *self = GS_SHELL_SEARCH (user_data);
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *app_row;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* don't do the delayed spinner */
	gs_shell_search_waiting_cancel (self);

	list = gs_plugin_loader_search_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_debug ("search cancelled");
			return;
		}
		g_warning ("failed to get search apps: %s", error->message);
		gs_stop_spinner (GTK_SPINNER (self->spinner_search));
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_search), "no-results");
		return;
	}

	/* no results */
	if (gs_app_list_length (list) == 0) {
		g_debug ("no search results to show");
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack_search), "no-results");
		return;
	}

	/* remove old entries */
	gs_container_remove_all (GTK_CONTAINER (self->list_box_search));

	gs_stop_spinner (GTK_SPINNER (self->spinner_search));
	gtk_stack_set_visible_child_name (GTK_STACK (self->stack_search), "results");
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
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
		g_autoptr (GsApp) a = NULL;
		a = gs_app_new (self->appid_to_show);
		gs_shell_show_app (self->shell, a);
		g_clear_pointer (&self->appid_to_show, g_free);
	}
}

static gboolean
gs_shell_search_waiting_show_cb (gpointer user_data)
{
	GsShellSearch *self = GS_SHELL_SEARCH (user_data);

	/* show spinner */
	gtk_stack_set_visible_child_name (GTK_STACK (self->stack_search), "spinner");
	gs_start_spinner (GTK_SPINNER (self->spinner_search));
	gs_shell_search_waiting_cancel (self);
	return FALSE;
}

static void
gs_shell_search_load (GsShellSearch *self)
{
	/* cancel any pending searches */
	if (self->search_cancellable != NULL) {
		g_cancellable_cancel (self->search_cancellable);
		g_object_unref (self->search_cancellable);
	}
	self->search_cancellable = g_cancellable_new ();

	/* search for apps */
	gs_shell_search_waiting_cancel (self);
	self->waiting_id = g_timeout_add (250, gs_shell_search_waiting_show_cb, self);

	gs_plugin_loader_search_async (self->plugin_loader,
				       self->value,
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
				       self->search_cancellable,
				       gs_shell_search_get_search_cb,
				       self);
}

static void
gs_shell_search_reload (GsPage *page)
{
	GsShellSearch *self = GS_SHELL_SEARCH (page);
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

const gchar *
gs_shell_search_get_text (GsShellSearch *self)
{
	return self->value;
}

void
gs_shell_search_set_text (GsShellSearch *self, const gchar *value)
{
	if (value == self->value)
		return;
	g_free (self->value);
	self->value = g_strdup (value);
}

static void
gs_shell_search_switch_to (GsPage *page, gboolean scroll_up)
{
	GsShellSearch *self = GS_SHELL_SEARCH (page);
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

	/* hardcode */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->search_button), TRUE);

	if (scroll_up) {
		GtkAdjustment *adj;
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolledwindow_search));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

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
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_UNAVAILABLE:
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
	g_string_append_printf (key, "%05x:", gs_app_get_match_value (app));

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
	gs_shell_search_reload (page);
}

static void
gs_shell_search_app_removed (GsPage *page, GsApp *app)
{
	gs_shell_search_reload (page);
}

static void
gs_shell_search_search_button_cb (GtkButton *button, GsShellSearch *self)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
		return;
	gs_shell_change_mode (self->shell, GS_SHELL_MODE_OVERVIEW, NULL, TRUE);
}

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

	/* search button */
	self->search_button = gs_search_button_new (NULL);
	gs_page_set_header_end_widget (GS_PAGE (self), self->search_button);
	g_signal_connect (self->search_button, "clicked",
			  G_CALLBACK (gs_shell_search_search_button_cb), self);

	/* chain up */
	gs_page_setup (GS_PAGE (self),
	               shell,
	               plugin_loader,
	               cancellable);
}

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

	G_OBJECT_CLASS (gs_shell_search_parent_class)->dispose (object);
}

static void
gs_shell_search_finalize (GObject *object)
{
	GsShellSearch *self = GS_SHELL_SEARCH (object);

	g_free (self->appid_to_show);
	g_free (self->value);

	G_OBJECT_CLASS (gs_shell_search_parent_class)->finalize (object);
}

static void
gs_shell_search_class_init (GsShellSearchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_search_dispose;
	object_class->finalize = gs_shell_search_finalize;
	page_class->app_installed = gs_shell_search_app_installed;
	page_class->app_removed = gs_shell_search_app_removed;
	page_class->switch_to = gs_shell_search_switch_to;
	page_class->reload = gs_shell_search_reload;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-search.ui");

	gtk_widget_class_bind_template_child (widget_class, GsShellSearch, list_box_search);
	gtk_widget_class_bind_template_child (widget_class, GsShellSearch, scrolledwindow_search);
	gtk_widget_class_bind_template_child (widget_class, GsShellSearch, spinner_search);
	gtk_widget_class_bind_template_child (widget_class, GsShellSearch, stack_search);
}

static void
gs_shell_search_init (GsShellSearch *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	self->sizegroup_image = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	self->sizegroup_name = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
}

GsShellSearch *
gs_shell_search_new (void)
{
	GsShellSearch *self;
	self = g_object_new (GS_TYPE_SHELL_SEARCH, NULL);
	return GS_SHELL_SEARCH (self);
}

/* vim: set noexpandtab: */
