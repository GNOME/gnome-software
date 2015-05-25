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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-cleanup.h"
#include "gs-sources-dialog.h"
#include "gs-utils.h"

struct _GsSourcesDialogPrivate
{
	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
	GtkWidget	*button_back;
	GtkWidget	*button_remove;
	GtkWidget	*grid_noresults;
	GtkWidget	*label2;
	GtkWidget	*label_empty;
	GtkWidget	*label_header;
	GtkWidget	*listbox;
	GtkWidget	*listbox_apps;
	GtkWidget	*scrolledwindow_apps;
	GtkWidget	*spinner;
	GtkWidget	*stack;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsSourcesDialog, gs_sources_dialog, GTK_TYPE_DIALOG)

static void
add_source (GtkListBox *listbox, GsApp *app)
{
	GtkWidget *widget;
	GtkWidget *box;
	GtkStyleContext *context;
	GPtrArray *related;
	guint cnt_addon = 0;
	guint cnt_apps = 0;
	guint i;
	_cleanup_free_ gchar *addons_text = NULL;
	_cleanup_free_ gchar *apps_text = NULL;
	_cleanup_free_ gchar *text = NULL;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_top (box, 12);
	gtk_widget_set_margin_start (box, 12);
	gtk_widget_set_margin_bottom (box, 12);
	gtk_widget_set_margin_end (box, 12);

	widget = gtk_label_new (gs_app_get_name (app));
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);
	related = gs_app_get_related (app);

	/* split up the types */
	for (i = 0; i < related->len; i++) {
		GsApp *app_tmp = g_ptr_array_index (related, i);
		switch (gs_app_get_id_kind (app_tmp)) {
		case AS_ID_KIND_WEB_APP:
		case AS_ID_KIND_DESKTOP:
			cnt_apps++;
			break;
		case AS_ID_KIND_FONT:
		case AS_ID_KIND_CODEC:
		case AS_ID_KIND_INPUT_METHOD:
		case AS_ID_KIND_ADDON:
			cnt_addon++;
			break;
		default:
			break;
		}
	}

	if (cnt_apps == 0 && cnt_addon == 0) {
		/* TRANSLATORS: This string describes a software source that
		   has no software installed from it. */
		text = g_strdup (_("No software installed"));
	} else if (cnt_addon == 0) {
		/* TRANSLATORS: This string is used to construct the 'X applications
		   installed' sentence, describing a software source. */
		text = g_strdup_printf (ngettext ("%i application installed",
						  "%i applications installed",
						  cnt_apps), cnt_apps);
	} else if (cnt_apps == 0) {
		/* TRANSLATORS: This string is used to construct the 'X add-ons
		   installed' sentence, describing a software source. */
		text = g_strdup_printf (ngettext ("%i add-on installed",
						  "%i add-ons installed",
						  cnt_addon), cnt_addon);
	} else {
		/* TRANSLATORS: This string is used to construct the 'X applications
		   and y add-ons installed' sentence, describing a software source.
		   The correct form here depends on the number of applications. */
		apps_text = g_strdup_printf (ngettext ("%i application",
						       "%i applications",
						       cnt_apps), cnt_apps);
		/* TRANSLATORS: This string is used to construct the 'X applications
		   and y add-ons installed' sentence, describing a software source.
		   The correct form here depends on the number of add-ons. */
		addons_text = g_strdup_printf (ngettext ("%i add-on",
		                                         "%i add-ons",
		                                         cnt_addon), cnt_addon);
		/* TRANSLATORS: This string is used to construct the 'X applications
		   and y add-ons installed' sentence, describing a software source.
		   The correct form here depends on the total number of
		   applications and add-ons. */
		text = g_strdup_printf (ngettext ("%s and %s installed",
		                                  "%s and %s installed",
		                                  cnt_apps + cnt_addon),
		                                  apps_text, addons_text);
	}
	widget = gtk_label_new (text);
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

	context = gtk_widget_get_style_context (widget);
	gtk_style_context_add_class (context, "dim-label");
	g_object_set_data_full (G_OBJECT (box), "GsShell::app",
				g_object_ref (app),
				(GDestroyNotify) g_object_unref);

	g_object_set_data_full (G_OBJECT (box),
	                        "sort",
	                        g_strdup (gs_app_get_name (app)),
	                        g_free);

	gtk_list_box_prepend (listbox, box);
	gtk_widget_show_all (box);
}

static void
get_sources_cb (GsPluginLoader *plugin_loader,
		GAsyncResult *res,
		GsSourcesDialog *dialog)
{
	GList *l;
	GList *list;
	GsApp *app;
	GsSourcesDialogPrivate *priv = gs_sources_dialog_get_instance_private (dialog);
	_cleanup_error_free_ GError *error = NULL;

	/* show results */
	gs_stop_spinner (GTK_SPINNER (priv->spinner));

	/* get the results */
	list = gs_plugin_loader_get_sources_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_NO_RESULTS)) {
			g_debug ("no sources to show");
		} else if (g_error_matches (error,
					    G_IO_ERROR,
					    G_IO_ERROR_CANCELLED)) {
			g_debug ("get sources cancelled");
		} else {
			g_warning ("failed to get sources: %s", error->message);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "empty");
		gtk_style_context_add_class (gtk_widget_get_style_context (priv->label_header),
		                             "dim-label");
		goto out;
	}

	gtk_style_context_remove_class (gtk_widget_get_style_context (priv->label_header),
	                                "dim-label");

	/* add each */
	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "sources");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		add_source (GTK_LIST_BOX (priv->listbox), app);
	}
out:
	if (list != NULL)
		gs_plugin_list_free (list);
}

static void
reload_sources (GsSourcesDialog *dialog)
{
	GsSourcesDialogPrivate *priv = gs_sources_dialog_get_instance_private (dialog);

	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "waiting");
	gs_start_spinner (GTK_SPINNER (priv->spinner));
	gtk_widget_hide (priv->button_back);
	gs_container_remove_all (GTK_CONTAINER (priv->listbox));

	/* get the list of non-core software sources */
	gs_plugin_loader_get_sources_async (priv->plugin_loader,
					    GS_PLUGIN_REFINE_FLAGS_DEFAULT |
					    GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED,
					    priv->cancellable,
					    (GAsyncReadyCallback) get_sources_cb,
					    dialog);
}

static void
list_header_func (GtkListBoxRow *row,
		  GtkListBoxRow *before,
		  gpointer user_data)
{
	GtkWidget *header = NULL;
	if (before != NULL)
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

static gint
list_sort_func (GtkListBoxRow *a,
		GtkListBoxRow *b,
		gpointer user_data)
{
	GObject *o1 = G_OBJECT (gtk_bin_get_child (GTK_BIN (a)));
	GObject *o2 = G_OBJECT (gtk_bin_get_child (GTK_BIN (b)));
	const gchar *key1 = g_object_get_data (o1, "sort");
	const gchar *key2 = g_object_get_data (o2, "sort");
	return g_strcmp0 (key1, key2);
}

static void
add_app (GtkListBox *listbox, GsApp *app)
{
	GtkWidget *box;
	GtkWidget *widget;
	GtkWidget *row;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_top (box, 12);
	gtk_widget_set_margin_start (box, 12);
	gtk_widget_set_margin_bottom (box, 12);
	gtk_widget_set_margin_end (box, 12);

	widget = gtk_label_new (gs_app_get_name (app));
	gtk_widget_set_halign (widget, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

	gtk_list_box_prepend (listbox, box);
	gtk_widget_show (widget);
	gtk_widget_show (box);

	g_object_set_data_full (G_OBJECT (box),
	                        "sort",
	                        g_strdup (gs_app_get_name (app)),
	                        g_free);

	row = gtk_widget_get_parent (box);
	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
}

static void
list_row_activated_cb (GtkListBox *list_box,
		       GtkListBoxRow *row,
		       GsSourcesDialog *dialog)
{
	GPtrArray *related;
	GsApp *app;
	GsSourcesDialogPrivate *priv = gs_sources_dialog_get_instance_private (dialog);
	guint cnt_apps = 0;
	guint i;

	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "details");

	gtk_widget_show (priv->button_back);

	gs_container_remove_all (GTK_CONTAINER (priv->listbox_apps));
	app = GS_APP (g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), 
					 "GsShell::app"));
	related = gs_app_get_related (app);
	for (i = 0; i < related->len; i++) {
		GsApp *app_tmp = g_ptr_array_index (related, i);
		switch (gs_app_get_kind (app_tmp)) {
		case GS_APP_KIND_NORMAL:
		case GS_APP_KIND_SYSTEM:
			add_app (GTK_LIST_BOX (priv->listbox_apps), app_tmp);
			cnt_apps++;
			break;
		default:
			break;
		}
	}

	/* save this */
	g_object_set_data_full (G_OBJECT (priv->stack), "GsShell::app",
				g_object_ref (app),
				(GDestroyNotify) g_object_unref);

	gtk_widget_set_visible (priv->scrolledwindow_apps, cnt_apps != 0);
	gtk_widget_set_visible (priv->label2, cnt_apps != 0);
	gtk_widget_set_visible (priv->grid_noresults, cnt_apps == 0);
}

static void
back_button_cb (GtkWidget *widget, GsSourcesDialog *dialog)
{
	GsSourcesDialogPrivate *priv = gs_sources_dialog_get_instance_private (dialog);
	gtk_widget_hide (priv->button_back);
	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "sources");
}

static void
app_removed_cb (GObject *source,
		GAsyncResult *res,
		gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsSourcesDialog *dialog = GS_SOURCES_DIALOG (user_data);
	GsSourcesDialogPrivate *priv = gs_sources_dialog_get_instance_private (dialog);
	_cleanup_error_free_ GError *error = NULL;

	if (!gs_plugin_loader_app_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to remove: %s", error->message);
	} else {
		reload_sources (dialog);
	}

	/* enable button */
	gtk_widget_set_sensitive (priv->button_remove, TRUE);
	gtk_button_set_label (GTK_BUTTON (priv->button_remove), _("Remove Source"));

	/* allow going back */
	gtk_widget_set_sensitive (priv->button_back, TRUE);
	gtk_widget_set_sensitive (priv->listbox_apps, TRUE);
}

static void
remove_button_cb (GtkWidget *widget, GsSourcesDialog *dialog)
{
	GsApp *app;
	GsSourcesDialogPrivate *priv = gs_sources_dialog_get_instance_private (dialog);

	/* disable button */
	gtk_widget_set_sensitive (priv->button_remove, FALSE);
	gtk_button_set_label (GTK_BUTTON (priv->button_remove), _("Removing…"));

	/* disallow going back */
	gtk_widget_set_sensitive (priv->button_back, FALSE);
	gtk_widget_set_sensitive (priv->listbox_apps, FALSE);

	/* remove source */
	app = GS_APP (g_object_get_data (G_OBJECT (priv->stack), "GsShell::app"));
	g_debug ("removing source '%s'", gs_app_get_name (app));
	gs_plugin_loader_app_action_async (priv->plugin_loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_REMOVE,
					   priv->cancellable,
					   app_removed_cb,
					   dialog);
}

static gchar *
get_os_name (void)
{
	gchar *name = NULL;
	_cleanup_free_ gchar *buffer = NULL;

	if (g_file_get_contents ("/etc/os-release", &buffer, NULL, NULL)) {
		gchar *start, *end;

		start = end = NULL;
		if ((start = strstr (buffer, "NAME=")) != NULL) {
			start += strlen ("NAME=");
			end = strchr (start, '\n');
		}

		if (start != NULL && end != NULL) {
			name = g_strndup (start, end - start);
		}
	}

	if (name == NULL) {
		/* TRANSLATORS: this is the fallback text we use if we can't
		   figure out the name of the operating system */
		name = g_strdup (_("the operating system"));
	}

	return name;
}

static void
set_plugin_loader (GsSourcesDialog *dialog, GsPluginLoader *plugin_loader)
{
	GsSourcesDialogPrivate *priv = gs_sources_dialog_get_instance_private (dialog);

	priv->plugin_loader = g_object_ref (plugin_loader);
}

static void
gs_sources_dialog_dispose (GObject *object)
{
	GsSourcesDialog *dialog = GS_SOURCES_DIALOG (object);
	GsSourcesDialogPrivate *priv = gs_sources_dialog_get_instance_private (dialog);

	g_clear_object (&priv->plugin_loader);

	if (priv->cancellable != NULL) {
		g_cancellable_cancel (priv->cancellable);
		g_clear_object (&priv->cancellable);
	}

	G_OBJECT_CLASS (gs_sources_dialog_parent_class)->dispose (object);
}

static void
gs_sources_dialog_init (GsSourcesDialog *dialog)
{
	GsSourcesDialogPrivate *priv = gs_sources_dialog_get_instance_private (dialog);
	_cleanup_free_ gchar *label_text = NULL;
	_cleanup_free_ gchar *os_name = NULL;

	gtk_widget_init_template (GTK_WIDGET (dialog));

	priv->cancellable = g_cancellable_new ();

	gtk_list_box_set_header_func (GTK_LIST_BOX (priv->listbox),
				      list_header_func,
				      dialog,
				      NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->listbox),
				    list_sort_func,
				    dialog, NULL);
	g_signal_connect (priv->listbox, "row-activated",
			  G_CALLBACK (list_row_activated_cb), dialog);

	gtk_list_box_set_header_func (GTK_LIST_BOX (priv->listbox_apps),
				      list_header_func,
				      dialog,
				      NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->listbox_apps),
				    list_sort_func,
				    dialog, NULL);

	os_name = get_os_name ();
	/* TRANSLATORS: This is the text displayed in the Software Sources
	   dialog when no OS-provided software sources are enabled. %s gets
	   replaced by the name of the actual distro, e.g. Fedora. */
	label_text = g_strdup_printf (_("Software sources can be downloaded from the internet. They give you access to additional software that is not provided by %s."),
	                              os_name);
	gtk_label_set_text (GTK_LABEL (priv->label_empty), label_text);

	g_signal_connect (priv->button_back, "clicked",
			  G_CALLBACK (back_button_cb), dialog);
	g_signal_connect (priv->button_remove, "clicked",
			  G_CALLBACK (remove_button_cb), dialog);
}

static void
gs_sources_dialog_class_init (GsSourcesDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_sources_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-sources-dialog.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, button_back);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, button_remove);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, grid_noresults);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, label2);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, label_empty);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, label_header);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, listbox);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, listbox_apps);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, scrolledwindow_apps);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, spinner);
	gtk_widget_class_bind_template_child_private (widget_class, GsSourcesDialog, stack);
}

GtkWidget *
gs_sources_dialog_new (GtkWindow *parent, GsPluginLoader *plugin_loader)
{
	GsSourcesDialog *dialog;

	dialog = g_object_new (GS_TYPE_SOURCES_DIALOG,
			       "use-header-bar", TRUE,
			       "transient-for", parent,
			       "modal", TRUE,
			       NULL);
	set_plugin_loader (dialog, plugin_loader);
	reload_sources (dialog);

	return GTK_WIDGET (dialog);
}

/* vim: set noexpandtab: */
