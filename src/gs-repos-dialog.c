/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-repos-dialog.h"

#include "gnome-software-private.h"
#include "gs-common.h"
#include "gs-os-release.h"
#include "gs-repo-row.h"
#include "gs-third-party-repo-row.h"
#include <glib/gi18n.h>

struct _GsReposDialog
{
	GtkDialog	 parent_instance;
	GSettings	*settings;
	GsApp		*third_party_repo;

	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
	GtkWidget	*frame_third_party;
	GtkWidget	*label_description;
	GtkWidget	*label_empty;
	GtkWidget	*label_header;
	GtkWidget	*listbox;
	GtkWidget	*listbox_third_party;
	GtkWidget	*row_third_party;
	GtkWidget	*spinner;
	GtkWidget	*stack;
};

G_DEFINE_TYPE (GsReposDialog, gs_repos_dialog, GTK_TYPE_DIALOG)

typedef struct {
	GsReposDialog	*dialog;
	GsApp		*repo;
	GsPluginAction	 action;
} InstallRemoveData;

static void
install_remove_data_free (InstallRemoveData *data)
{
	g_clear_object (&data->dialog);
	g_clear_object (&data->repo);
	g_slice_free (InstallRemoveData, data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(InstallRemoveData, install_remove_data_free);

static void reload_sources (GsReposDialog *dialog);
static void reload_third_party_repo (GsReposDialog *dialog);

static gchar *
get_repo_installed_text (GsApp *repo)
{
	GsAppList *related;
	guint cnt_addon = 0;
	guint cnt_apps = 0;
	g_autofree gchar *addons_text = NULL;
	g_autofree gchar *apps_text = NULL;

	related = gs_app_get_related (repo);
	for (guint i = 0; i < gs_app_list_length (related); i++) {
		GsApp *app_tmp = gs_app_list_index (related, i);
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

	if (cnt_apps == 0 && cnt_addon == 0) {
		/* nothing! */
		return NULL;
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

static gboolean
repo_supports_removal (GsApp *repo)
{
	const gchar *management_plugin = gs_app_get_management_plugin (repo);

	/* can't remove a repo, only enable/disable existing ones */
	if (g_strcmp0 (management_plugin, "fwupd") == 0 ||
	    g_strcmp0 (management_plugin, "packagekit") == 0 ||
	    g_strcmp0 (management_plugin, "shell-extensions") == 0)
		return FALSE;

	return TRUE;
}

static void
repo_enabled_cb (GObject *source,
                 GAsyncResult *res,
                 gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	g_autoptr(InstallRemoveData) install_remove_data = (InstallRemoveData *) user_data;
	g_autoptr(GError) error = NULL;
	const gchar *action_str;

	action_str = gs_plugin_action_to_string (install_remove_data->action);

	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED)) {
			g_debug ("repo %s cancelled", action_str);
			return;
		}

		g_warning ("failed to %s repo: %s", action_str, error->message);
		return;
	}

	g_debug ("finished %s repo %s", action_str, gs_app_get_id (install_remove_data->repo));
}

static void
_enable_repo (InstallRemoveData *install_data)
{
	GsReposDialog *dialog = install_data->dialog;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_debug ("enabling repo %s", gs_app_get_id (install_data->repo));
	plugin_job = gs_plugin_job_newv (install_data->action,
					 "interactive", TRUE,
	                                 "app", install_data->repo,
	                                 NULL);
	gs_plugin_loader_job_process_async (dialog->plugin_loader, plugin_job,
	                                    dialog->cancellable,
	                                    repo_enabled_cb,
	                                    install_data);
}

static void
enable_repo_response_cb (GtkDialog *confirm_dialog,
			 gint response,
			 gpointer user_data)
{
	g_autoptr(InstallRemoveData) install_data = (InstallRemoveData *) user_data;

	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (confirm_dialog));

	/* not agreed */
	if (response != GTK_RESPONSE_OK)
		return;

	_enable_repo (g_steal_pointer (&install_data));
}

static void
enable_repo (GsReposDialog *dialog, GsApp *repo)
{
	g_autoptr(InstallRemoveData) install_data = NULL;

	install_data = g_slice_new0 (InstallRemoveData);
	install_data->action = GS_PLUGIN_ACTION_INSTALL;
	install_data->repo = g_object_ref (repo);
	install_data->dialog = g_object_ref (dialog);

	/* user needs to confirm acceptance of an agreement */
	if (gs_app_get_agreement (repo) != NULL) {
		GtkWidget *confirm_dialog;
		g_autofree gchar *message = NULL;
		g_autoptr(GError) error = NULL;

		/* convert from AppStream markup */
		message = as_markup_convert_simple (gs_app_get_agreement (repo), &error);
		if (message == NULL) {
			/* failed, so just try and show the original markup */
			message = g_strdup (gs_app_get_agreement (repo));
			g_warning ("Failed to process AppStream markup: %s",
				   error->message);
		}

		/* ask for confirmation */
		confirm_dialog = gtk_message_dialog_new (GTK_WINDOW (dialog),
							 GTK_DIALOG_MODAL,
							 GTK_MESSAGE_QUESTION,
							 GTK_BUTTONS_CANCEL,
							 /* TRANSLATORS: window title */
							 "%s", _("Enable Third-Party Software Repository?"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (confirm_dialog),
							  "%s", message);

		/* TRANSLATORS: button to accept the agreement */
		gtk_dialog_add_button (GTK_DIALOG (confirm_dialog), _("Enable"),
				       GTK_RESPONSE_OK);

		/* handle this async */
		g_signal_connect (confirm_dialog, "response",
				  G_CALLBACK (enable_repo_response_cb),
				  g_steal_pointer (&install_data));

		gtk_window_set_modal (GTK_WINDOW (confirm_dialog), TRUE);
		gtk_window_present (GTK_WINDOW (confirm_dialog));
		return;
	}

	/* no prompt required */
	_enable_repo (g_steal_pointer (&install_data));
}

static void
remove_repo_response_cb (GtkDialog *confirm_dialog,
                         gint response,
                         gpointer user_data)
{
	g_autoptr(InstallRemoveData) remove_data = (InstallRemoveData *) user_data;
	GsReposDialog *dialog = remove_data->dialog;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* unmap the dialog */
	gtk_widget_destroy (GTK_WIDGET (confirm_dialog));

	/* not agreed */
	if (response != GTK_RESPONSE_OK)
		return;

	g_debug ("removing repo %s", gs_app_get_id (remove_data->repo));
	plugin_job = gs_plugin_job_newv (remove_data->action,
					 "interactive", TRUE,
					 "app", remove_data->repo,
					 NULL);
	gs_plugin_loader_job_process_async (dialog->plugin_loader, plugin_job,
					    dialog->cancellable,
					    repo_enabled_cb,
					    g_steal_pointer (&remove_data));
}

static void
remove_confirm_repo (GsReposDialog *dialog, GsApp *repo)
{
	InstallRemoveData *remove_data;
	GtkWidget *confirm_dialog;
	g_autofree gchar *message = NULL;
	g_autofree gchar *title = NULL;

	remove_data = g_slice_new0 (InstallRemoveData);
	remove_data->action = GS_PLUGIN_ACTION_REMOVE;
	remove_data->repo = g_object_ref (repo);
	remove_data->dialog = g_object_ref (dialog);

	if (repo_supports_removal (repo)) {
		/* TRANSLATORS: this is a prompt message, and '%s' is a
		 * repository name, e.g. 'GNOME Nightly' */
		title = g_strdup_printf (_("Remove “%s”?"),
		                         gs_app_get_name (repo));
	} else {
		/* TRANSLATORS: this is a prompt message, and '%s' is a
		 * repository name, e.g. 'GNOME Nightly' */
		title = g_strdup_printf (_("Disable “%s”?"),
		                         gs_app_get_name (repo));
	}
	/* TRANSLATORS: longer dialog text */
	message = g_strdup (_("Software that has been installed from this "
	                      "repository will no longer receive updates, "
	                      "including security fixes."));

	/* ask for confirmation */
	confirm_dialog = gtk_message_dialog_new (GTK_WINDOW (dialog),
	                                         GTK_DIALOG_MODAL,
	                                         GTK_MESSAGE_QUESTION,
	                                         GTK_BUTTONS_CANCEL,
	                                         "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (confirm_dialog),
						  "%s", message);

	if (repo_supports_removal (repo)) {
		/* TRANSLATORS: this is button text to remove the repo */
		gtk_dialog_add_button (GTK_DIALOG (confirm_dialog), _("Remove"), GTK_RESPONSE_OK);
	} else {
		/* TRANSLATORS: this is button text to remove the repo */
		gtk_dialog_add_button (GTK_DIALOG (confirm_dialog), _("Disable"), GTK_RESPONSE_OK);
	}

	/* handle this async */
	g_signal_connect (confirm_dialog, "response",
			  G_CALLBACK (remove_repo_response_cb), remove_data);

	gtk_window_set_modal (GTK_WINDOW (confirm_dialog), TRUE);
	gtk_window_present (GTK_WINDOW (confirm_dialog));
}

static void
repo_button_clicked_cb (GsRepoRow *row,
                        GsReposDialog *dialog)
{
	GsApp *repo;

	repo = gs_repo_row_get_repo (row);

	switch (gs_app_get_state (repo)) {
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
	        enable_repo (dialog, repo);
		break;
	case AS_APP_STATE_INSTALLED:
	        remove_confirm_repo (dialog, repo);
		break;
	default:
		g_warning ("repo %s button clicked in unexpected state %s",
		           gs_app_get_id (repo),
		           as_app_state_to_string (gs_app_get_state (repo)));
		break;
	}
}

static GtkListBox *
get_list_box_for_repo (GsReposDialog *dialog, GsApp *repo)
{
	if (dialog->third_party_repo != NULL) {
		const gchar *source_repo;
		const gchar *source_third_party_package;

		source_repo = gs_app_get_source_id_default (repo);
		source_third_party_package = gs_app_get_source_id_default (dialog->third_party_repo);

		/* group repos from the same repo-release package together */
		if (g_strcmp0 (source_repo, source_third_party_package) == 0)
			return GTK_LIST_BOX (dialog->listbox_third_party);
	}

	return GTK_LIST_BOX (dialog->listbox);
}

static void
add_repo (GsReposDialog *dialog, GsApp *repo)
{
	GtkWidget *row;
	g_autofree gchar *text = NULL;
	AsAppState state;

	state = gs_app_get_state (repo);
	if (!(state == AS_APP_STATE_AVAILABLE ||
	      state == AS_APP_STATE_AVAILABLE_LOCAL ||
	      state == AS_APP_STATE_INSTALLED ||
	      state == AS_APP_STATE_INSTALLING ||
	      state == AS_APP_STATE_REMOVING)) {
		g_warning ("repo %s in invalid state %s",
		           gs_app_get_id (repo),
		           as_app_state_to_string (state));
		return;
	}

	row = gs_repo_row_new ();
	gs_repo_row_set_name (GS_REPO_ROW (row),
	                      gs_app_get_name (repo));
	text = get_repo_installed_text (repo);
	gs_repo_row_set_comment (GS_REPO_ROW (row), text);
	gs_repo_row_set_url (GS_REPO_ROW (row),
	                     gs_app_get_url (repo, AS_URL_KIND_HOMEPAGE));
	gs_repo_row_show_status (GS_REPO_ROW (row));
	gs_repo_row_set_repo (GS_REPO_ROW (row), repo);

	g_signal_connect (row, "button-clicked",
	                  G_CALLBACK (repo_button_clicked_cb), dialog);

	gtk_list_box_prepend (get_list_box_for_repo (dialog, repo), row);
	gtk_widget_show (row);
}

static void
third_party_repo_installed_cb (GObject *source,
                               GAsyncResult *res,
                               gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	g_autoptr(InstallRemoveData) install_data = (InstallRemoveData *) user_data;
	g_autoptr(GError) error = NULL;

	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		const gchar *action_str = gs_plugin_action_to_string (install_data->action);

		if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED)) {
			g_debug ("third party repo %s cancelled", action_str);
			return;
		}

		g_warning ("failed to %s third party repo: %s", action_str, error->message);
		return;
	}

	reload_sources (install_data->dialog);
}

static void
install_third_party_repo (GsReposDialog *dialog, gboolean install)
{
	InstallRemoveData *install_data;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	install_data = g_slice_new0 (InstallRemoveData);
	install_data->dialog = g_object_ref (dialog);
	install_data->action = install ? GS_PLUGIN_ACTION_INSTALL : GS_PLUGIN_ACTION_REMOVE;

	plugin_job = gs_plugin_job_newv (install_data->action,
					 "interactive", TRUE,
	                                 "app", dialog->third_party_repo,
	                                 NULL);
	gs_plugin_loader_job_process_async (dialog->plugin_loader,
	                                    plugin_job,
	                                    dialog->cancellable,
	                                    third_party_repo_installed_cb,
	                                    install_data);
}

static void
third_party_repo_button_clicked_cb (GsThirdPartyRepoRow *row,
                                    gpointer user_data)
{
	GsReposDialog *dialog = (GsReposDialog *) user_data;
	GsApp *app;

	app = gs_third_party_repo_row_get_app (row);

	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_UNAVAILABLE:
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
		install_third_party_repo (dialog, TRUE);
		break;
	case AS_APP_STATE_UPDATABLE_LIVE:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_INSTALLED:
		install_third_party_repo (dialog, FALSE);
		break;
	default:
		g_warning ("third party repo %s button clicked in unexpected state %s",
		           gs_app_get_id (app),
		           as_app_state_to_string (gs_app_get_state (app)));
		break;
	}

	g_settings_set_boolean (dialog->settings, "show-nonfree-prompt", FALSE);
}

static void
refresh_third_party_repo (GsReposDialog *dialog)
{
	if (dialog->third_party_repo == NULL) {
		gtk_widget_hide (dialog->frame_third_party);
		return;
	}

	gtk_widget_show (dialog->frame_third_party);
}

static void
remove_all_repo_rows_cb (GtkWidget *widget, gpointer user_data)
{
	GtkContainer *container = GTK_CONTAINER (user_data);

	if (GS_IS_REPO_ROW (widget))
		gtk_container_remove (container, widget);
}

static void
container_remove_all_repo_rows (GtkContainer *container)
{
	gtk_container_foreach (container, remove_all_repo_rows_cb, container);
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

	/* remove previous */
	gs_container_remove_all (GTK_CONTAINER (dialog->listbox));
	container_remove_all_repo_rows (GTK_CONTAINER (dialog->listbox_third_party));

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
		add_repo (dialog, app);
	}
}

static void
resolve_third_party_repo_cb (GsPluginLoader *plugin_loader,
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
			g_debug ("resolve third party repo cancelled");
			return;
		} else {
			g_warning ("failed to resolve third party repo: %s", error->message);
			return;
		}
	}

	/* we should only get one result */
	if (gs_app_list_length (list) > 0)
		app = gs_app_list_index (list, 0);
	else
		app = NULL;

	g_set_object (&dialog->third_party_repo, app);
	gs_third_party_repo_row_set_app (GS_THIRD_PARTY_REPO_ROW (dialog->row_third_party), app);

	/* refresh widget */
	refresh_third_party_repo (dialog);
}

static void
reload_sources (GsReposDialog *dialog)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get the list of non-core software repositories */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_SOURCES,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED |
					                 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME,
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

static gchar *
get_row_sort_key (GtkListBoxRow *row)
{
	GsApp *app;
	guint sort_order;
	g_autofree gchar *sort_key = NULL;

	/* sort third party repo rows first */
	if (GS_IS_THIRD_PARTY_REPO_ROW (row)) {
		sort_order = 1;
		app = gs_third_party_repo_row_get_app (GS_THIRD_PARTY_REPO_ROW (row));
	} else {
		sort_order = 2;
		app = gs_repo_row_get_repo (GS_REPO_ROW (row));
	}

	sort_key = g_utf8_casefold (gs_app_get_name (app), -1);
	return g_strdup_printf ("%u:%s", sort_order, sort_key);
}

static gint
list_sort_func (GtkListBoxRow *a,
		GtkListBoxRow *b,
		gpointer user_data)
{
	g_autofree gchar *key1 = get_row_sort_key (a);
	g_autofree gchar *key2 = get_row_sort_key (b);

	/* compare the keys according to the algorithm above */
	return g_strcmp0 (key1, key2);
}

static void
list_row_activated_cb (GtkListBox *list_box,
		       GtkListBoxRow *row,
		       GsReposDialog *dialog)
{
	GtkListBoxRow *other_row;

	if (!GS_IS_REPO_ROW (row))
		return;

	gs_repo_row_show_details (GS_REPO_ROW (row));

	for (guint i = 0; (other_row = gtk_list_box_get_row_at_index (list_box, i)) != NULL; i++) {
		if (!GS_IS_REPO_ROW (other_row))
			continue;
		if (other_row == row)
			continue;

		gs_repo_row_hide_details (GS_REPO_ROW (other_row));
	}
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
reload_cb (GsPluginLoader *plugin_loader, GsReposDialog *dialog)
{
	reload_sources (dialog);
	reload_third_party_repo (dialog);
}

static void
set_plugin_loader (GsReposDialog *dialog, GsPluginLoader *plugin_loader)
{
	dialog->plugin_loader = g_object_ref (plugin_loader);
	g_signal_connect (dialog->plugin_loader, "reload",
	                  G_CALLBACK (reload_cb), dialog);
}

static void
gs_repos_dialog_dispose (GObject *object)
{
	GsReposDialog *dialog = GS_REPOS_DIALOG (object);

	if (dialog->plugin_loader != NULL) {
		g_signal_handlers_disconnect_by_func (dialog->plugin_loader, reload_cb, dialog);
		g_clear_object (&dialog->plugin_loader);
	}

	g_cancellable_cancel (dialog->cancellable);
	g_clear_object (&dialog->cancellable);
	g_clear_object (&dialog->settings);
	g_clear_object (&dialog->third_party_repo);

	G_OBJECT_CLASS (gs_repos_dialog_parent_class)->dispose (object);
}

static void
gs_repos_dialog_init (GsReposDialog *dialog)
{
	g_autofree gchar *label_description_text = NULL;
	g_autofree gchar *label_empty_text = NULL;
	g_autofree gchar *os_name = NULL;
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

	/* TRANSLATORS: This is the description text displayed in the Software Repositories dialog.
	   %s gets replaced by the name of the actual distro, e.g. Fedora. */
	label_description_text = g_strdup_printf (_("These repositories supplement the default software provided by %s."),
	                                          os_name);
	gtk_label_set_text (GTK_LABEL (dialog->label_description), label_description_text);

	/* set up third party repository row */
	gtk_list_box_set_header_func (GTK_LIST_BOX (dialog->listbox_third_party),
	                              list_header_func,
	                              dialog,
	                              NULL);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (dialog->listbox_third_party),
	                            list_sort_func,
	                            dialog, NULL);
	g_signal_connect (dialog->listbox_third_party, "row-activated",
	                  G_CALLBACK (list_row_activated_cb), dialog);
	g_signal_connect (dialog->row_third_party, "button-clicked",
	                  G_CALLBACK (third_party_repo_button_clicked_cb), dialog);
	gs_third_party_repo_row_set_name (GS_THIRD_PARTY_REPO_ROW (dialog->row_third_party),
	                                  /* TRANSLATORS: info bar title in the software repositories dialog */
	                                  _("Third Party Repositories"));
	g_string_append (str,
	                 /* TRANSLATORS: this is the third party repositories info bar. */
	                 _("Access additional software from selected third party sources."));
	g_string_append (str, " ");
	g_string_append (str,
	                 /* TRANSLATORS: this is the third party repositories info bar. */
	                 _("Some of this software is proprietary and therefore has restrictions on use, sharing, and access to source code."));
	g_string_append_printf (str, " <a href=\"%s\">%s</a>",
	                        "https://fedoraproject.org/wiki/Workstation/Third_Party_Software_Repositories",
	                        /* TRANSLATORS: this is the clickable
	                         * link on the third party repositories info bar */
	                        _("Find out more…"));
	gs_third_party_repo_row_set_comment (GS_THIRD_PARTY_REPO_ROW (dialog->row_third_party), str->str);
	refresh_third_party_repo (dialog);

	/* TRANSLATORS: This is the description text displayed in the Software Repositories dialog.
	   %s gets replaced by the name of the actual distro, e.g. Fedora. */
	label_empty_text = g_strdup_printf (_("These repositories supplement the default software provided by %s."),
	                                    os_name);
	gtk_label_set_text (GTK_LABEL (dialog->label_empty), label_empty_text);
}

static void
gs_repos_dialog_class_init (GsReposDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_repos_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-repos-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, frame_third_party);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, label_description);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, label_empty);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, label_header);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, listbox);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, listbox_third_party);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, row_third_party);
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
	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "waiting");
	gs_start_spinner (GTK_SPINNER (dialog->spinner));
	reload_sources (dialog);
	reload_third_party_repo (dialog);

	return GTK_WIDGET (dialog);
}
