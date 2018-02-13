/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
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

#include "gnome-software-private.h"
#include "gs-repos-dialog.h"
#include "gs-os-release.h"
#include "gs-repos-dialog-row.h"
#include "gs-common.h"

struct _GsReposDialog
{
	GtkDialog	 parent_instance;
	GSettings	*settings;
	GsApp		*third_party_repo;

	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
	GtkWidget	*button_back;
	GtkWidget	*frame_third_party;
	GtkWidget	*grid_noresults;
	GtkWidget	*label2;
	GtkWidget	*label_empty;
	GtkWidget	*label_header;
	GtkWidget	*listbox;
	GtkWidget	*listbox_apps;
	GtkWidget	*row_third_party;
	GtkWidget	*scrolledwindow_apps;
	GtkWidget	*spinner;
	GtkWidget	*stack;
};

G_DEFINE_TYPE (GsReposDialog, gs_repos_dialog, GTK_TYPE_DIALOG)

typedef struct {
	GsReposDialog	*dialog;
	GsPluginAction	 action;
} InstallData;

static void
install_data_free (InstallData *install_data)
{
	g_clear_object (&install_data->dialog);
	g_slice_free (InstallData, install_data);
}

static void reload_sources (GsReposDialog *dialog);
static void reload_third_party_repo (GsReposDialog *dialog);
static void refresh_third_party_repo (GsReposDialog *dialog);

static gchar *
get_source_installed_text (GPtrArray *sources)
{
	guint cnt_addon = 0;
	guint cnt_apps = 0;
	g_autofree gchar *addons_text = NULL;
	g_autofree gchar *apps_text = NULL;

	/* split up the types */
	for (guint j = 0; j < sources->len; j++) {
		GsApp *app = g_ptr_array_index (sources, j);
		GPtrArray *related = gs_app_get_related (app);
		for (guint i = 0; i < related->len; i++) {
			GsApp *app_tmp = g_ptr_array_index (related, i);
			switch (gs_app_get_kind (app_tmp)) {
			case AS_APP_KIND_WEB_APP:
			case AS_APP_KIND_DESKTOP:
				cnt_apps++;
				break;
			case AS_APP_KIND_FONT:
			case AS_APP_KIND_CODEC:
			case AS_APP_KIND_INPUT_METHOD:
			case AS_APP_KIND_ADDON:
				cnt_addon++;
				break;
			default:
				break;
			}
		}
	}

	/* nothing! */
	if (cnt_apps == 0 && cnt_addon == 0) {
		/* TRANSLATORS: This string describes a software repository that
		   has no software installed from it. */
		return g_strdup (_("No applications or addons installed; other software might still be"));
	}
	if (cnt_addon == 0) {
		/* TRANSLATORS: This string is used to construct the 'X applications
		   installed' sentence, describing a software repository. */
		return g_strdup_printf (ngettext ("%u application installed",
						  "%u applications installed",
						  cnt_apps), cnt_apps);
	}
	if (cnt_apps == 0) {
		/* TRANSLATORS: This string is used to construct the 'X add-ons
		   installed' sentence, describing a software repository. */
		return g_strdup_printf (ngettext ("%u add-on installed",
						  "%u add-ons installed",
						  cnt_addon), cnt_addon);
	}

	/* TRANSLATORS: This string is used to construct the 'X applications
	   and y add-ons installed' sentence, describing a software repository.
	   The correct form here depends on the number of applications. */
	apps_text = g_strdup_printf (ngettext ("%u application",
					       "%u applications",
					       cnt_apps), cnt_apps);
	/* TRANSLATORS: This string is used to construct the 'X applications
	   and y add-ons installed' sentence, describing a software repository.
	   The correct form here depends on the number of add-ons. */
	addons_text = g_strdup_printf (ngettext ("%u add-on",
						 "%u add-ons",
						 cnt_addon), cnt_addon);
	/* TRANSLATORS: This string is used to construct the 'X applications
	   and y add-ons installed' sentence, describing a software repository.
	   The correct form here depends on the total number of
	   applications and add-ons. */
	return g_strdup_printf (ngettext ("%s and %s installed",
					  "%s and %s installed",
					  cnt_apps + cnt_addon),
					  apps_text, addons_text);
}

static void
add_source (GtkListBox *listbox, GsApp *app)
{
	GtkWidget *row;
	g_autofree gchar *text = NULL;
	g_autoptr(GPtrArray) sources = g_ptr_array_new ();

	row = gs_repos_dialog_row_new ();
	gs_repos_dialog_row_set_name (GS_REPOS_DIALOG_ROW (row),
	                              gs_app_get_name (app));
	g_ptr_array_add (sources, app);
	text = get_source_installed_text (sources);
	gs_repos_dialog_row_set_description (GS_REPOS_DIALOG_ROW (row),
	                                     text);

	g_object_set_data_full (G_OBJECT (row), "GsShell::app",
				g_object_ref (app),
				(GDestroyNotify) g_object_unref);

	g_object_set_data_full (G_OBJECT (row),
	                        "sort",
	                        g_utf8_casefold (gs_app_get_name (app), -1),
	                        g_free);

	gtk_list_box_prepend (listbox, row);
	gtk_widget_show (row);
}

static void
third_party_repo_installed_cb (GObject *source,
                               GAsyncResult *res,
                               gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	InstallData *install_data = (InstallData *) user_data;
	g_autoptr(GError) error = NULL;

	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		const gchar *action_str = gs_plugin_action_to_string (install_data->action);

		if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED)) {
			g_debug ("third party repo %s cancelled", action_str);
			goto out;
		}

		g_warning ("failed to %s third party repo: %s", action_str, error->message);
		refresh_third_party_repo (install_data->dialog);
	} else {
		reload_sources (install_data->dialog);
		reload_third_party_repo (install_data->dialog);
	}

out:
	install_data_free (install_data);
}

static void
install_third_party_repo (GsReposDialog *dialog, gboolean install)
{
	GsPluginAction action;
	InstallData *install_data;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (install && gs_app_get_state (dialog->third_party_repo) == AS_APP_STATE_AVAILABLE) {
		action = GS_PLUGIN_ACTION_INSTALL;
	} else if (!install && gs_app_get_state (dialog->third_party_repo) == AS_APP_STATE_INSTALLED) {
		action = GS_PLUGIN_ACTION_REMOVE;
	} else {
		g_debug ("third party repo package in state %s when %s, skipping",
		         as_app_state_to_string (gs_app_get_state (dialog->third_party_repo)),
		         install ? "installing" : "removing");
		return;
	}

	install_data = g_slice_new0 (InstallData);
	install_data->dialog = g_object_ref (dialog);
	install_data->action = action;

	plugin_job = gs_plugin_job_newv (action,
	                                 "app", dialog->third_party_repo,
	                                 NULL);
	gs_plugin_loader_job_process_async (dialog->plugin_loader,
	                                    plugin_job,
	                                    dialog->cancellable,
	                                    third_party_repo_installed_cb,
	                                    install_data);
}

static void
third_party_switch_switch_active_cb (GsReposDialogRow *row,
                                     GParamSpec *pspec,
                                     GsReposDialog *dialog)
{
	gboolean active;

	active = gs_repos_dialog_row_get_switch_active (GS_REPOS_DIALOG_ROW (dialog->row_third_party));
	install_third_party_repo (dialog, active);
	g_settings_set_boolean (dialog->settings, "show-nonfree-prompt", FALSE);
}

static void
refresh_third_party_repo (GsReposDialog *dialog)
{
	gboolean switch_active;

	if (dialog->third_party_repo == NULL) {
		gtk_widget_hide (dialog->frame_third_party);
		return;
	}

	/* if the third party repo package is installed, show the switch as active */
	switch_active = (gs_app_get_state (dialog->third_party_repo) == AS_APP_STATE_INSTALLED);
	gs_repos_dialog_row_set_switch_active (GS_REPOS_DIALOG_ROW (dialog->row_third_party),
	                                       switch_active);

	gtk_widget_show (dialog->frame_third_party);
}

static void
get_sources_cb (GsPluginLoader *plugin_loader,
		GAsyncResult *res,
		GsReposDialog *dialog)
{
	GsApp *app;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get the results */
	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_CANCELLED)) {
			g_debug ("get sources cancelled");
			return;
		} else {
			g_warning ("failed to get sources: %s", error->message);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "empty");
		gtk_style_context_add_class (gtk_widget_get_style_context (dialog->label_header),
		                             "dim-label");
		return;
	}

	/* stop the spinner */
	gs_stop_spinner (GTK_SPINNER (dialog->spinner));

	/* no results */
	if (gs_app_list_length (list) == 0) {
		g_debug ("no sources to show");
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "empty");
		gtk_style_context_add_class (gtk_widget_get_style_context (dialog->label_header), "dim-label");
		return;
	}

	gtk_style_context_remove_class (gtk_widget_get_style_context (dialog->label_header),
	                                "dim-label");

	/* add each */
	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "sources");
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		if (gs_app_get_state (app) != AS_APP_STATE_INSTALLED)
			continue;
		add_source (GTK_LIST_BOX (dialog->listbox), app);
	}
}

static void
resolve_third_party_repo_cb (GsPluginLoader *plugin_loader,
                             GAsyncResult *res,
                             GsReposDialog *dialog)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get the results */
	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_CANCELLED)) {
			g_debug ("resolve third party repo cancelled");
			return;
		} else {
			g_warning ("failed to resolve third party repo: %s", error->message);
			return;
		}
	}

	/* save results for later */
	g_clear_object (&dialog->third_party_repo);
	if (gs_app_list_length (list) > 0)
		dialog->third_party_repo = g_object_ref (gs_app_list_index (list, 0));

	/* refresh widget */
	refresh_third_party_repo (dialog);
}

static void
reload_sources (GsReposDialog *dialog)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;

	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "waiting");
	gs_start_spinner (GTK_SPINNER (dialog->spinner));
	gtk_widget_hide (dialog->button_back);
	gs_container_remove_all (GTK_CONTAINER (dialog->listbox));

	/* get the list of non-core software repositories */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_SOURCES,
					 "failure-flags", GS_PLUGIN_FAILURE_FLAGS_NONE,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED,
					 NULL);
	gs_plugin_loader_job_process_async (dialog->plugin_loader, plugin_job,
					    dialog->cancellable,
					    (GAsyncReadyCallback) get_sources_cb,
					    dialog);
}

static gboolean
is_fedora (void)
{
	const gchar *id = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;

	os_release = gs_os_release_new (NULL);
	if (os_release == NULL)
		return FALSE;

	id = gs_os_release_get_id (os_release);
	if (g_strcmp0 (id, "fedora") == 0)
		return TRUE;

	return FALSE;
}

static void
reload_third_party_repo (GsReposDialog *dialog)
{
	const gchar *third_party_repo_package = "fedora-workstation-repositories";
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* Fedora-specific functionality */
	if (!is_fedora ())
		return;

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH_PROVIDES,
	                                 "search", third_party_repo_package,
	                                 "failure-flags", GS_PLUGIN_FAILURE_FLAGS_NONE,
	                                 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
	                                                 GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES,
	                                 NULL);
	gs_plugin_loader_job_process_async (dialog->plugin_loader, plugin_job,
	                                    dialog->cancellable,
	                                    (GAsyncReadyCallback) resolve_third_party_repo_cb,
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
	const gchar *key1 = g_object_get_data (G_OBJECT (a), "sort");
	const gchar *key2 = g_object_get_data (G_OBJECT (b), "sort");
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
	gtk_label_set_ellipsize (GTK_LABEL (widget), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);

	g_object_set_data_full (G_OBJECT (box),
	                        "sort",
	                        g_utf8_casefold (gs_app_get_name (app), -1),
	                        g_free);

	gtk_list_box_prepend (listbox, box);
	gtk_widget_show (widget);
	gtk_widget_show (box);

	row = gtk_widget_get_parent (box);
	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
}

static void
list_row_activated_cb (GtkListBox *list_box,
		       GtkListBoxRow *row,
		       GsReposDialog *dialog)
{
	GPtrArray *related;
	GsApp *app;
	guint cnt_apps = 0;

	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "details");

	gtk_widget_show (dialog->button_back);

	gs_container_remove_all (GTK_CONTAINER (dialog->listbox_apps));
	app = GS_APP (g_object_get_data (G_OBJECT (row),
					 "GsShell::app"));
	related = gs_app_get_related (app);
	for (guint i = 0; i < related->len; i++) {
		GsApp *app_tmp = g_ptr_array_index (related, i);
		switch (gs_app_get_kind (app_tmp)) {
		case AS_APP_KIND_DESKTOP:
			add_app (GTK_LIST_BOX (dialog->listbox_apps), app_tmp);
			cnt_apps++;
			break;
		default:
			break;
		}
	}

	/* save this */
	g_object_set_data_full (G_OBJECT (dialog->stack), "GsShell::app",
				g_object_ref (app),
				(GDestroyNotify) g_object_unref);

	gtk_widget_set_visible (dialog->scrolledwindow_apps, cnt_apps != 0);
	gtk_widget_set_visible (dialog->label2, cnt_apps != 0);
	gtk_widget_set_visible (dialog->grid_noresults, cnt_apps == 0);
}

static void
back_button_cb (GtkWidget *widget, GsReposDialog *dialog)
{
	gtk_widget_hide (dialog->button_back);
	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "sources");
}

static gboolean
key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	GsReposDialog *dialog = (GsReposDialog *) widget;
	GdkKeymap *keymap;
	GdkModifierType state;
	gboolean is_rtl;

	if (!gtk_widget_is_visible (dialog->button_back) || !gtk_widget_is_sensitive (dialog->button_back))
		return GDK_EVENT_PROPAGATE;

	state = event->state;
	keymap = gdk_keymap_get_for_display (gtk_widget_get_display (widget));
	gdk_keymap_add_virtual_modifiers (keymap, &state);
	state = state & gtk_accelerator_get_default_mod_mask ();
	is_rtl = gtk_widget_get_direction (dialog->button_back) == GTK_TEXT_DIR_RTL;

	if ((!is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Left) ||
	    (is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Right) ||
	    event->keyval == GDK_KEY_Back) {
		gtk_widget_activate (dialog->button_back);
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static gboolean
button_press_event (GsReposDialog *dialog, GdkEventButton *event)
{
	/* Mouse hardware back button is 8 */
	if (event->button != 8)
		return GDK_EVENT_PROPAGATE;

	if (!gtk_widget_is_visible (dialog->button_back) || !gtk_widget_is_sensitive (dialog->button_back))
		return GDK_EVENT_PROPAGATE;

	gtk_widget_activate (dialog->button_back);
	return GDK_EVENT_STOP;
}

static gchar *
get_os_name (void)
{
	gchar *name = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;

	os_release = gs_os_release_new (NULL);
	if (os_release != NULL)
		name = g_strdup (gs_os_release_get_name (os_release));
	if (name == NULL) {
		/* TRANSLATORS: this is the fallback text we use if we can't
		   figure out the name of the operating system */
		name = g_strdup (_("the operating system"));
	}

	return name;
}

static void
updates_changed_cb (GsPluginLoader *plugin_loader,
                    GsReposDialog *dialog)
{
	reload_sources (dialog);
	reload_third_party_repo (dialog);
}

static void
set_plugin_loader (GsReposDialog *dialog, GsPluginLoader *plugin_loader)
{
	dialog->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (dialog->plugin_loader, "updates-changed",
	                  G_CALLBACK (updates_changed_cb), dialog);
}

static void
gs_repos_dialog_dispose (GObject *object)
{
	GsReposDialog *dialog = GS_REPOS_DIALOG (object);

	if (dialog->plugin_loader != NULL) {
		g_signal_handlers_disconnect_by_func (dialog->plugin_loader, updates_changed_cb, dialog);
		g_clear_object (&dialog->plugin_loader);
	}

	if (dialog->cancellable != NULL) {
		g_cancellable_cancel (dialog->cancellable);
		g_clear_object (&dialog->cancellable);
	}
	g_clear_object (&dialog->settings);
	g_clear_object (&dialog->third_party_repo);

	G_OBJECT_CLASS (gs_repos_dialog_parent_class)->dispose (object);
}

static void
gs_repos_dialog_init (GsReposDialog *dialog)
{
	g_autofree gchar *label_text = NULL;
	g_autofree gchar *os_name = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	gtk_widget_init_template (GTK_WIDGET (dialog));

	dialog->cancellable = g_cancellable_new ();
	dialog->settings = g_settings_new ("org.gnome.software");

	os_name = get_os_name ();

	gtk_list_box_set_header_func (GTK_LIST_BOX (dialog->listbox),
				      list_header_func,
				      dialog,
				      NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (dialog->listbox),
				    list_sort_func,
				    dialog, NULL);
	g_signal_connect (dialog->listbox, "row-activated",
			  G_CALLBACK (list_row_activated_cb), dialog);

	gtk_list_box_set_header_func (GTK_LIST_BOX (dialog->listbox_apps),
				      list_header_func,
				      dialog,
				      NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (dialog->listbox_apps),
				    list_sort_func,
				    dialog, NULL);

	/* set up third party repository row */
	g_signal_connect (dialog->row_third_party, "notify::switch-active",
	                  G_CALLBACK (third_party_switch_switch_active_cb),
	                  dialog);
	gs_repos_dialog_row_set_switch_enabled (GS_REPOS_DIALOG_ROW (dialog->row_third_party), TRUE);
	gs_repos_dialog_row_set_name (GS_REPOS_DIALOG_ROW (dialog->row_third_party),
	                              /* TRANSLATORS: info bar title in the software repositories dialog */
	                              _("Third Party Repositories"));
	g_string_printf (str,
	                 /* TRANSLATORS: this is the third party repositories info bar.
	                    %s gets replaced by the distro name, e.g. Fedora */
	                 _("Access additional software that is not supplied by %s through select third party repositories."),
	                 os_name);
	g_string_append (str, " ");
	g_string_append (str,
	                 /* TRANSLATORS: this is the third party repositories info bar. */
	                 _("Some of this software is proprietary and therefore has restrictions on use and access to source code."));
	/* optional URL */
	uri = g_settings_get_string (dialog->settings, "nonfree-software-uri");
	if (uri != NULL && uri[0] != '\0') {
		g_string_append_printf (str, " <a href=\"%s\">%s</a>", uri,
					/* TRANSLATORS: this is the clickable
					 * link on the third party repositories info bar */
					_("Find out more…"));
	}
	gs_repos_dialog_row_set_comment (GS_REPOS_DIALOG_ROW (dialog->row_third_party), str->str);
	gs_repos_dialog_row_set_description (GS_REPOS_DIALOG_ROW (dialog->row_third_party), NULL);
	refresh_third_party_repo (dialog);

	/* TRANSLATORS: This is the text displayed in the Software Repositories
	   dialog when no OS-provided software repositories are enabled. %s gets
	   replaced by the name of the actual distro, e.g. Fedora. */
	label_text = g_strdup_printf (_("Software repositories can be downloaded from the internet. They give you access to additional software that is not provided by %s."),
	                              os_name);
	gtk_label_set_text (GTK_LABEL (dialog->label_empty), label_text);

	g_signal_connect (dialog->button_back, "clicked",
			  G_CALLBACK (back_button_cb), dialog);

	/* global keynav and mouse back button */
	g_signal_connect (dialog, "key-press-event",
			  G_CALLBACK (key_press_event), NULL);
	g_signal_connect (dialog, "button-press-event",
			  G_CALLBACK (button_press_event), NULL);
}

static void
gs_repos_dialog_class_init (GsReposDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_repos_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-repos-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, button_back);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, frame_third_party);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, grid_noresults);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, label2);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, label_empty);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, label_header);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, listbox);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, listbox_apps);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, row_third_party);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, scrolledwindow_apps);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, spinner);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, stack);
}

GtkWidget *
gs_repos_dialog_new (GtkWindow *parent, GsPluginLoader *plugin_loader)
{
	GsReposDialog *dialog;

	dialog = g_object_new (GS_TYPE_REPOS_DIALOG,
			       "use-header-bar", TRUE,
			       "transient-for", parent,
			       "modal", TRUE,
			       NULL);
	set_plugin_loader (dialog, plugin_loader);
	reload_sources (dialog);
	reload_third_party_repo (dialog);

	return GTK_WIDGET (dialog);
}

/* vim: set noexpandtab: */
