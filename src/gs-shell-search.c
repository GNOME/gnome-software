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

#include "gs-cleanup.h"
#include "gs-shell-search.h"
#include "gs-shell.h"
#include "gs-app.h"
#include "gs-utils.h"
#include "gs-app-row.h"

static void	gs_shell_search_finalize	(GObject	*object);

struct GsShellSearchPrivate
{
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

G_DEFINE_TYPE_WITH_PRIVATE (GsShellSearch, gs_shell_search, GTK_TYPE_BIN)

static void
gs_shell_search_app_row_activated_cb (GtkListBox *list_box,
				      GtkListBoxRow *row,
				      GsShellSearch *shell_search)
{
	GsApp *app;
	app = gs_app_row_get_app (GS_APP_ROW (row));
	gs_shell_show_app (shell_search->priv->shell, app);
}

typedef struct {
	GsApp			*app;
	GsShellSearch		*shell_search;
} GsShellSearchHelper;

static void
gs_shell_search_app_installed_cb (GObject *source,
				  GAsyncResult *res,
				  gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellSearchHelper *helper = (GsShellSearchHelper *) user_data;
	gboolean ret;
	_cleanup_error_free_ GError *error = NULL;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to install %s: %s",
			   gs_app_get_id (helper->app),
			   error->message);
		gs_app_notify_failed_modal (helper->app,
					    gs_shell_get_window (helper->shell_search->priv->shell),
					    GS_PLUGIN_LOADER_ACTION_INSTALL,
					    error);
	} else {
		/* only show this if the window is not active */
		if (!gs_shell_is_active (helper->shell_search->priv->shell))
			gs_app_notify_installed (helper->app);
	}
	gs_shell_search_reload (helper->shell_search);
	g_object_unref (helper->app);
	g_object_unref (helper->shell_search);
	g_free (helper);
}

/**
 * gs_shell_search_finished_func:
 **/
static void
gs_shell_search_app_removed_cb (GObject *source,
				GAsyncResult *res,
				gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellSearchHelper *helper = (GsShellSearchHelper *) user_data;
	gboolean ret;
	_cleanup_error_free_ GError *error = NULL;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		g_warning ("failed to remove: %s", error->message);
		gs_app_notify_failed_modal (helper->app,
					    gs_shell_get_window (helper->shell_search->priv->shell),
					    GS_PLUGIN_LOADER_ACTION_REMOVE,
					    error);
	}
	gs_shell_search_reload (helper->shell_search);
	g_object_unref (helper->app);
	g_object_unref (helper->shell_search);
	g_free (helper);
}

/**
 * gs_shell_search_app_remove:
 **/
static void
gs_shell_search_app_remove (GsShellSearch *shell_search, GsApp *app)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GtkResponseType response;
	GtkWidget *dialog;
	_cleanup_string_free_ GString *markup = NULL;

	markup = g_string_new ("");
	g_string_append_printf (markup,
				/* TRANSLATORS: this is a prompt message, and
				 * '%s' is an application summary, e.g. 'GNOME Clocks' */
				_("Are you sure you want to remove %s?"),
				gs_app_get_name (app));
	g_string_prepend (markup, "<b>");
	g_string_append (markup, "</b>");
	dialog = gtk_message_dialog_new (gs_shell_get_window (priv->shell),
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CANCEL,
					 NULL);
	gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), markup->str);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog),
						    /* TRANSLATORS: longer dialog text */
						    _("%s will be removed, and you will have to install it to use it again."),
						    gs_app_get_name (app));
	/* TRANSLATORS: this is button text to remove the application */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Remove"), GTK_RESPONSE_OK);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_OK) {
		GsShellSearchHelper *helper;
		g_debug ("remove %s", gs_app_get_id (app));
		helper = g_new0 (GsShellSearchHelper, 1);
		helper->app = g_object_ref (app);
		helper->shell_search = g_object_ref (shell_search);
		gs_plugin_loader_app_action_async (priv->plugin_loader,
						   app,
						   GS_PLUGIN_LOADER_ACTION_REMOVE,
						   priv->cancellable,
						   gs_shell_search_app_removed_cb,
						   helper);
	}
	gtk_widget_destroy (dialog);
}

/**
 * gs_shell_search_app_install:
 **/
static void
gs_shell_search_app_install (GsShellSearch *shell_search, GsApp *app)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GsShellSearchHelper *helper;
	helper = g_new0 (GsShellSearchHelper, 1);
	helper->app = g_object_ref (app);
	helper->shell_search = g_object_ref (shell_search);
	gs_plugin_loader_app_action_async (priv->plugin_loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_INSTALL,
					   priv->cancellable,
					   gs_shell_search_app_installed_cb,
					   helper);
}

/**
 * gs_shell_search_show_missing_url:
 **/
static void
gs_shell_search_show_missing_url (GsApp *app)
{
	const gchar *url;
	_cleanup_error_free_ GError *error = NULL;

	url = gs_app_get_url (app, AS_URL_KIND_MISSING);
	if (!gtk_show_uri (NULL, url, GDK_CURRENT_TIME, &error))
		g_warning ("spawn of '%s' failed", url);
}

/**
 * gs_shell_search_install_unavailable_app:
 **/
static void
gs_shell_search_install_unavailable_app (GsShellSearch *shell_search, GsApp *app)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GtkResponseType response;
	GsShellSearchHelper *helper;

	/* get confirmation */
	response = gs_app_notify_unavailable (app, gs_shell_get_window (priv->shell));
	if (response == GTK_RESPONSE_OK) {
		g_debug ("installing %s", gs_app_get_id (app));
		helper = g_new0 (GsShellSearchHelper, 1);
		helper->shell_search = g_object_ref (shell_search);
		helper->app = g_object_ref (app);
		gs_plugin_loader_app_action_async (priv->plugin_loader,
						   app,
						   GS_PLUGIN_LOADER_ACTION_INSTALL,
						   priv->cancellable,
						   gs_shell_search_app_installed_cb,
						   helper);
	}
}

/**
 * gs_shell_search_app_row_clicked_cb:
 **/
static void
gs_shell_search_app_row_clicked_cb (GsAppRow *app_row,
				    GsShellSearch *shell_search)
{
	GsApp *app;
	app = gs_app_row_get_app (app_row);
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE)
		gs_shell_search_app_install (shell_search, app);
	else if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
		gs_shell_search_app_remove (shell_search, app);
	else if (gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		if (gs_app_get_url (app, AS_URL_KIND_MISSING) == NULL) {
			gs_shell_search_install_unavailable_app (shell_search, app);
			return;
		}
		gs_shell_search_show_missing_url (app);
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
	GList *list;
	GsApp *app;
	GsShellSearch *shell_search = GS_SHELL_SEARCH (user_data);
	GsShellSearchPrivate *priv = shell_search->priv;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GtkWidget *app_row;
	_cleanup_error_free_ GError *error = NULL;

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
		gs_stop_spinner (GTK_SPINNER (priv->spinner_search));
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_search), "no-results");
		return;
	}

	gs_stop_spinner (GTK_SPINNER (priv->spinner_search));
	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_search), "results");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		app_row = gs_app_row_new ();
		g_signal_connect (app_row, "button-clicked",
				  G_CALLBACK (gs_shell_search_app_row_clicked_cb),
				  shell_search);
		gs_app_row_set_app (GS_APP_ROW (app_row), app);
		gtk_container_add (GTK_CONTAINER (priv->list_box_search), app_row);
		gs_app_row_set_size_groups (GS_APP_ROW (app_row),
					    priv->sizegroup_image,
					    priv->sizegroup_name);
		gtk_widget_show (app_row);
	}

	if (priv->appid_to_show != NULL) {
		gs_shell_show_details (priv->shell, priv->appid_to_show);
		g_clear_pointer (&priv->appid_to_show, g_free);
	}
}

/**
 * gs_shell_search_load:
 */
static void
gs_shell_search_load (GsShellSearch *shell_search)
{
	GsShellSearchPrivate *priv = shell_search->priv;

	/* remove old entries */
	gs_container_remove_all (GTK_CONTAINER (priv->list_box_search));

	/* cancel any pending searches */
	if (priv->search_cancellable != NULL) {
		g_cancellable_cancel (priv->search_cancellable);
		g_object_unref (priv->search_cancellable);
	}
	priv->search_cancellable = g_cancellable_new ();

	/* search for apps */
	gs_plugin_loader_search_async (priv->plugin_loader,
				       priv->value,
				       GS_PLUGIN_REFINE_FLAGS_DEFAULT |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
				       GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING,
				       priv->search_cancellable,
				       gs_shell_search_get_search_cb,
				       shell_search);

	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack_search), "spinner");
	gs_start_spinner (GTK_SPINNER (priv->spinner_search));
}

/**
 * gs_shell_search_reload:
 */
void
gs_shell_search_reload (GsShellSearch *shell_search)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	if (priv->value != NULL)
		gs_shell_search_load (shell_search);
}


/**
 * gs_shell_search_set_appid_to_show:
 *
 * Switch to the specified app id after loading the search results.
 **/
void
gs_shell_search_set_appid_to_show (GsShellSearch *shell_search, const gchar *appid)
{
	GsShellSearchPrivate *priv = shell_search->priv;

	g_free (priv->appid_to_show);
	priv->appid_to_show = g_strdup (appid);
}

/**
 * gs_shell_search_switch_to:
 **/
void
gs_shell_search_switch_to (GsShellSearch *shell_search, const gchar *value, gboolean scroll_up)
{
	GsShellSearchPrivate *priv = shell_search->priv;
	GtkWidget *widget;

	if (gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_SEARCH) {
		g_warning ("Called switch_to(search) when in mode %s",
			   gs_shell_get_mode_string (priv->shell));
		return;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "buttonbox_main"));
	gtk_widget_show (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "search_bar"));
	gtk_widget_show (widget);

	if (scroll_up) {
		GtkAdjustment *adj;
		adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolledwindow_search));
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
	}

	g_free (priv->value);
	priv->value = g_strdup (value);

	gs_shell_search_load (shell_search);
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
	gint rating;

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
	rating = gs_app_get_kudos_weight (app);
	g_string_append_printf (key, "%03i:", rating > 0 ? rating : 0);

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
	_cleanup_free_ gchar *key1 = gs_shell_search_get_app_sort_key (a1);
	_cleanup_free_ gchar *key2 = gs_shell_search_get_app_sort_key (a2);

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
			   GsShellSearch *shell_search)
{
	GsShellSearchPrivate *priv = shell_search->priv;

	if (priv->search_cancellable != NULL)
		g_cancellable_cancel (priv->search_cancellable);
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

	g_return_if_fail (GS_IS_SHELL_SEARCH (shell_search));

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);
	priv->shell = shell;

	/* connect the cancellables */
	g_cancellable_connect (priv->cancellable,
			       G_CALLBACK (gs_shell_search_cancel_cb),
			       shell_search, NULL);

	/* setup search */
	g_signal_connect (priv->list_box_search, "row-activated",
			  G_CALLBACK (gs_shell_search_app_row_activated_cb), shell_search);
	gtk_list_box_set_header_func (GTK_LIST_BOX (priv->list_box_search),
				      gs_shell_search_list_header_func,
				      shell_search, NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->list_box_search),
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
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = gs_shell_search_finalize;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-search.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsShellSearch, list_box_search);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellSearch, scrolledwindow_search);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellSearch, spinner_search);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellSearch, stack_search);
}

/**
 * gs_shell_search_init:
 **/
static void
gs_shell_search_init (GsShellSearch *shell_search)
{
	gtk_widget_init_template (GTK_WIDGET (shell_search));

	shell_search->priv = gs_shell_search_get_instance_private (shell_search);
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

	g_object_unref (priv->sizegroup_image);
	g_object_unref (priv->sizegroup_name);

	g_object_unref (priv->builder);
	g_object_unref (priv->plugin_loader);
	g_object_unref (priv->cancellable);

	if (priv->search_cancellable != NULL)
		g_object_unref (priv->search_cancellable);

	g_free (priv->appid_to_show);
	g_free (priv->value);

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

/* vim: set noexpandtab: */
