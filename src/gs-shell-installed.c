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

#include "gs-shell.h"
#include "gs-shell-installed.h"
#include "gs-app.h"
#include "gs-utils.h"
#include "gs-app-widget.h"

#define INSTALL_DATE_QUEUED     (G_MAXUINT - 1)
#define INSTALL_DATE_INSTALLING (G_MAXUINT - 2)

static void	gs_shell_installed_finalize	(GObject	*object);
static void	gs_shell_installed_remove_row	(GtkListBox	*list_box,
						 GtkWidget	*child);

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
	GsShell			*shell;
};

G_DEFINE_TYPE (GsShellInstalled, gs_shell_installed, G_TYPE_OBJECT)

static void gs_shell_installed_pending_apps_changed_cb (GsPluginLoader *plugin_loader,
							GsShellInstalled *shell_installed);

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
gs_shell_installed_remove_row (GtkListBox *list_box, GtkWidget *child)
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
 * gs_shell_installed_app_removed_cb:
 **/
static void
gs_shell_installed_app_removed_cb (GObject *source,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GError *error = NULL;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsShellInstalledHelper *helper = (GsShellInstalledHelper *) user_data;
	GsShellInstalledPrivate *priv = helper->shell_installed->priv;
	GsApp *app;
	gboolean ret;

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		app = gs_app_widget_get_app (helper->app_widget);
		g_warning ("failed to remove %s: %s",
			   gs_app_get_id (app),
			   error->message);
		gs_app_notify_failed_modal (priv->builder,
					    app,
					    GS_PLUGIN_LOADER_ACTION_REMOVE,
					    error);
		g_error_free (error);
	} else {
		/* remove from the list */
		app = gs_app_widget_get_app (helper->app_widget);
		g_debug ("removed %s", gs_app_get_id (app));
		gs_shell_installed_remove_row (GTK_LIST_BOX (priv->list_box_installed),
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
				/* TRANSLATORS: this is a prompt message, and
				 * '%s' is an application summary, e.g. 'GNOME Clocks' */
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
						    /* TRANSLATORS: longer dialog text */
						    _("%s will be removed, and you will have to install it to use it again."),
						    gs_app_get_name (app));
	/* TRANSLATORS: this is button text to remove the application */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Remove"), GTK_RESPONSE_OK);
	if (gs_app_get_state (app) == GS_APP_STATE_QUEUED)
		response = GTK_RESPONSE_OK; /* pending install */
	else
		response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_OK) {
		g_debug ("removing %s", gs_app_get_id (app));
		helper = g_new0 (GsShellInstalledHelper, 1);
		helper->shell_installed = g_object_ref (shell_installed);
		helper->app_widget = g_object_ref (app_widget);
		gs_plugin_loader_app_action_async (priv->plugin_loader,
						   app,
						   GS_PLUGIN_LOADER_ACTION_REMOVE,
						   priv->cancellable,
						   gs_shell_installed_app_removed_cb,
						   helper);
	}
	g_string_free (markup, TRUE);
	gtk_widget_destroy (dialog);
}

static void
gs_shell_installed_app_state_changed_cb (GsApp *app, GsShellInstalled *shell)
{
	gtk_list_box_invalidate_sort (shell->priv->list_box_installed);
}

static void
gs_shell_installed_add_app (GsShellInstalled *shell, GsApp *app)
{
	GsShellInstalledPrivate *priv = shell->priv;
	GtkWidget *widget;

	widget = gs_app_widget_new ();
	gs_app_widget_set_colorful (GS_APP_WIDGET (widget), FALSE);
	g_signal_connect (widget, "button-clicked",
			  G_CALLBACK (gs_shell_installed_app_remove_cb), shell);
	g_signal_connect_object (app, "state-changed",
				 G_CALLBACK (gs_shell_installed_app_state_changed_cb), shell, 0);
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
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_install"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), "view");

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
out:
	gs_plugin_list_free (list);
	gs_shell_installed_pending_apps_changed_cb (plugin_loader, shell_installed);
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

	gtk_list_box_invalidate_sort (priv->list_box_installed);

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
					      GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					      priv->cancellable,
					      gs_shell_installed_get_installed_cb,
					      shell_installed);

	spinner = GTK_SPINNER (gtk_builder_get_object (shell_installed->priv->builder, "spinner_install"));
	gs_start_spinner (spinner);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "stack_install"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), "spinner");

	priv->waiting = TRUE;
}

/**
 * gs_shell_installed_sort_func:
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

	key = g_string_sized_new (64);

	/* sort installed, removing, other */
	switch (gs_app_get_state (app)) {
	case GS_APP_STATE_INSTALLING:
		g_string_append (key, "1:");
		break;
	case GS_APP_STATE_REMOVING:
		g_string_append (key, "2:");
		break;
	default:
		g_string_append (key, "3:");
		break;
	}

	/* sort desktop files, then addons */
	switch (gs_app_get_id_kind (app)) {
	case GS_APP_ID_KIND_DESKTOP:
	case GS_APP_ID_KIND_WEBAPP:
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

	/* sort by install date */
	g_string_append_printf (key, "%09" G_GUINT64_FORMAT ":",
				G_MAXUINT64 - gs_app_get_install_date (app));

	/* finally, sort by short name */
	g_string_append (key, gs_app_get_name (app));
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
	GsAppWidget *aw1 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (a)));
	GsAppWidget *aw2 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (b)));
	GsApp *a1 = gs_app_widget_get_app (aw1);
	GsApp *a2 = gs_app_widget_get_app (aw2);
	gchar *key1 = gs_shell_installed_get_app_sort_key (a1);
	gchar *key2 = gs_shell_installed_get_app_sort_key (a2);
	gint retval;

	/* compare the keys according to the algorithm above */
	retval = g_strcmp0 (key1, key2);

	g_free (key1);
	g_free (key2);

	return retval;
}

/**
 * gs_shell_installed_is_addon_id_kind
 **/
static gboolean
gs_shell_installed_is_addon_id_kind (GsApp *app)
{
	GsAppIdKind id_kind;
	id_kind = gs_app_get_id_kind (app);
	if (id_kind == GS_APP_ID_KIND_DESKTOP)
		return FALSE;
	if (id_kind == GS_APP_ID_KIND_WEBAPP)
		return FALSE;
	return TRUE;
}

/**
 * gs_shell_installed_list_header_func
 **/
static void
gs_shell_installed_list_header_func (GtkListBoxRow *row,
				     GtkListBoxRow *before,
				     gpointer user_data)
{
	GsAppWidget *aw1;
	GsAppWidget *aw2;
	GtkStyleContext *context;
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

	/* desktop -> addons */
	aw1 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (before)));
	aw2 = GS_APP_WIDGET (gtk_bin_get_child (GTK_BIN (row)));
	if (!gs_shell_installed_is_addon_id_kind (gs_app_widget_get_app (aw1)) &&
	    gs_shell_installed_is_addon_id_kind (gs_app_widget_get_app (aw2))) {
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
						     "button_installed"));
	pending = gs_plugin_loader_get_pending (plugin_loader);
	if (pending->len == 0) {
		/* TRANSLATORS: this is tab button to show the list of
		 * installed software */
		label = g_strdup (_("_Installed"));
	} else {
		/* TRANSLATORS: this is tab button to show the list of
		 * installed software. The '%d' refers to the number of
		 * applications either installing or erasing */
		label = g_strdup_printf (_("_Installed (%d)"), pending->len);
	}
	for (i = 0; i < pending->len; i++) {
		app = GS_APP (g_ptr_array_index (pending, i));
		/* Sort installing apps above removing and
		 * installed apps. Be careful not to add
		 * pending apps more than once.
		 */
		if (gs_app_get_state (app) == GS_APP_STATE_QUEUED) {
			if (gs_app_get_install_date (app) != INSTALL_DATE_QUEUED) {
				gs_app_set_install_date (app, INSTALL_DATE_QUEUED);
				gs_shell_installed_add_app (shell_installed, app);
			}
		} else if (gs_app_get_state (app) == GS_APP_STATE_INSTALLING) {
			if (gs_app_get_install_date (app) != INSTALL_DATE_INSTALLING) {
				gs_app_set_install_date (app, INSTALL_DATE_INSTALLING);
				gs_shell_installed_add_app (shell_installed, app);
			}
		}
	}

	gtk_button_set_label (GTK_BUTTON (widget), label);
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

	g_return_if_fail (GS_IS_SHELL_INSTALLED (shell_installed));

	priv->shell = shell;
	priv->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (priv->plugin_loader, "pending-apps-changed",
			  G_CALLBACK (gs_shell_installed_pending_apps_changed_cb),
			  shell_installed);

	priv->builder = g_object_ref (builder);
	priv->cancellable = g_object_ref (cancellable);

	/* setup installed */
	priv->list_box_installed = GTK_LIST_BOX (gtk_builder_get_object (priv->builder, "list_box_install"));
	g_signal_connect (priv->list_box_installed, "row-activated",
			  G_CALLBACK (gs_shell_installed_app_widget_activated_cb), shell_installed);
	gtk_list_box_set_header_func (priv->list_box_installed,
				      gs_shell_installed_list_header_func,
				      shell_installed, NULL);
	gtk_list_box_set_sort_func (priv->list_box_installed,
				    gs_shell_installed_sort_func,
				    shell_installed, NULL);
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

	g_object_unref (priv->sizegroup_image);
	g_object_unref (priv->sizegroup_name);

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

/* vim: set noexpandtab: */
