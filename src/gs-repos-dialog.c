/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
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
#include "gs-repos-section.h"
#include "gs-utils.h"
#include <glib/gi18n.h>

struct _GsReposDialog
{
	AdwWindow	 parent_instance;
	GSettings	*settings;
	GsFedoraThirdParty *third_party;
	gboolean	 third_party_enabled;
	GHashTable	*third_party_repos; /* (nullable) (owned), mapping from owned repo ID → owned plugin name */
	GHashTable	*sections; /* gchar * ~> GsReposSection * */

	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
	GtkWidget	*status_empty;
	GtkWidget	*content_page;
	GtkWidget	*spinner;
	GtkWidget	*stack;
};

G_DEFINE_TYPE (GsReposDialog, gs_repos_dialog, ADW_TYPE_WINDOW)

static void reload_third_party_repos (GsReposDialog *dialog);

typedef struct {
	GsReposDialog	*dialog;
	GsApp		*repo;
	GWeakRef	 row_weakref;
	GsPluginAction	 action;
} InstallRemoveData;

static void
install_remove_data_free (InstallRemoveData *data)
{
	g_clear_object (&data->dialog);
	g_clear_object (&data->repo);
	g_weak_ref_clear (&data->row_weakref);
	g_slice_free (InstallRemoveData, data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(InstallRemoveData, install_remove_data_free);

static void
repo_enabled_cb (GObject *source,
                 GAsyncResult *res,
                 gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	g_autoptr(InstallRemoveData) install_remove_data = (InstallRemoveData *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsRepoRow) row = NULL;
	const gchar *action_str;

	action_str = gs_plugin_action_to_string (install_remove_data->action);
	row = g_weak_ref_get (&install_remove_data->row_weakref);
	if (row)
		gs_repo_row_unmark_busy (row);

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
	gtk_window_destroy (GTK_WINDOW (confirm_dialog));

	/* not agreed */
	if (response != GTK_RESPONSE_OK) {
		g_autoptr(GsRepoRow) row = g_weak_ref_get (&install_data->row_weakref);
		if (row)
			gs_repo_row_unmark_busy (row);
		return;
	}

	_enable_repo (g_steal_pointer (&install_data));
}

static void
enable_repo (GsReposDialog *dialog,
	     GsRepoRow *row,
	     GsApp *repo)
{
	g_autoptr(InstallRemoveData) install_data = NULL;

	install_data = g_slice_new0 (InstallRemoveData);
	install_data->action = GS_PLUGIN_ACTION_ENABLE_REPO;
	install_data->dialog = g_object_ref (dialog);
	install_data->repo = g_object_ref (repo);
	g_weak_ref_init (&install_data->row_weakref, row);

	gs_repo_row_mark_busy (row);

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
	gtk_window_destroy (GTK_WINDOW (confirm_dialog));

	/* not agreed */
	if (response != GTK_RESPONSE_OK) {
		g_autoptr(GsRepoRow) row = g_weak_ref_get (&remove_data->row_weakref);
		if (row)
			gs_repo_row_unmark_busy (row);
		return;
	}

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
remove_confirm_repo (GsReposDialog *dialog,
		     GsRepoRow *row,
		     GsApp *repo,
		     GsPluginAction action)
{
	InstallRemoveData *remove_data;
	GtkWidget *confirm_dialog;
	g_autofree gchar *message = NULL;
	GtkWidget *button;
	GtkStyleContext *context;

	remove_data = g_slice_new0 (InstallRemoveData);
	remove_data->action = action;
	remove_data->dialog = g_object_ref (dialog);
	remove_data->repo = g_object_ref (repo);
	g_weak_ref_init (&remove_data->row_weakref, row);

	/* TRANSLATORS: The '%s' is replaced with a repository name, like "Fedora Modular - x86_64" */
	message = g_strdup_printf (_("Software that has been installed from “%s” will cease to receive updates."),
			gs_app_get_name (repo));

	/* ask for confirmation */
	confirm_dialog = gtk_message_dialog_new (GTK_WINDOW (dialog),
	                                         GTK_DIALOG_MODAL,
	                                         GTK_MESSAGE_QUESTION,
	                                         GTK_BUTTONS_CANCEL,
	                                         "%s",
						 action == GS_PLUGIN_ACTION_DISABLE_REPO ? _("Disable Repository?") : _("Remove Repository?"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (confirm_dialog),
						  "%s", message);

	if (action == GS_PLUGIN_ACTION_DISABLE_REPO) {
		/* TRANSLATORS: this is button text to disable a repo */
		button = gtk_dialog_add_button (GTK_DIALOG (confirm_dialog), _("_Disable"), GTK_RESPONSE_OK);
	} else {
		/* TRANSLATORS: this is button text to remove a repo */
		button = gtk_dialog_add_button (GTK_DIALOG (confirm_dialog), _("_Remove"), GTK_RESPONSE_OK);
	}
	context = gtk_widget_get_style_context (button);
	gtk_style_context_add_class (context, "destructive-action");

	/* handle this async */
	g_signal_connect (confirm_dialog, "response",
			  G_CALLBACK (remove_repo_response_cb), remove_data);

	gtk_window_set_modal (GTK_WINDOW (confirm_dialog), TRUE);
	gtk_window_present (GTK_WINDOW (confirm_dialog));

	gs_repo_row_mark_busy (row);
}

static void
repo_section_switch_clicked_cb (GsReposSection *section,
				GsRepoRow *row,
				GsReposDialog *dialog)
{
	GsApp *repo;

	repo = gs_repo_row_get_repo (row);

	switch (gs_app_get_state (repo)) {
	case GS_APP_STATE_AVAILABLE:
	case GS_APP_STATE_AVAILABLE_LOCAL:
		enable_repo (dialog, row, repo);
		break;
	case GS_APP_STATE_INSTALLED:
		remove_confirm_repo (dialog, row, repo, GS_PLUGIN_ACTION_DISABLE_REPO);
		break;
	default:
		g_warning ("repo %s button clicked in unexpected state %s",
		           gs_app_get_id (repo),
		           gs_app_state_to_string (gs_app_get_state (repo)));
		break;
	}
}

static void
repo_section_remove_clicked_cb (GsReposSection *section,
				GsRepoRow *row,
				GsReposDialog *dialog)
{
	GsApp *repo = gs_repo_row_get_repo (row);
	remove_confirm_repo (dialog, row, repo, GS_PLUGIN_ACTION_REMOVE_REPO);
}

static void
fedora_third_party_switch_done_cb (GObject *source_object,
				   GAsyncResult *result,
				   gpointer user_data)
{
	g_autoptr(GsReposDialog) self = user_data;
	g_autoptr(GError) error = NULL;

	if (!gs_fedora_third_party_switch_finish (GS_FEDORA_THIRD_PARTY (source_object), result, &error)) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			return;
		g_warning ("Failed to switch 'fedora-third-party' config: %s", error->message);
	}

	/* Reload the state, because the user could dismiss the authentication prompt
	   or the repos could change their state. */
	reload_third_party_repos (self);
}

static void
fedora_third_party_repos_switch_notify_cb (GObject *object,
					   GParamSpec *param,
					   gpointer user_data)
{
	GsReposDialog *self = user_data;

	gs_fedora_third_party_switch (self->third_party,
				      gtk_switch_get_active (GTK_SWITCH (object)),
				      TRUE,
				      self->cancellable,
				      fedora_third_party_switch_done_cb,
				      g_object_ref (self));
}

static gboolean
is_third_party_repo (GsReposDialog *dialog,
		     GsApp *repo)
{
	return gs_app_get_scope (repo) == AS_COMPONENT_SCOPE_SYSTEM &&
	       gs_fedora_third_party_util_is_third_party_repo (dialog->third_party_repos,
							       gs_app_get_id (repo),
							       gs_app_get_management_plugin (repo));
}

static void
add_repo (GsReposDialog *dialog,
	  GsApp *repo,
	  GSList **third_party_repos)
{
	GsAppState state;
	GtkWidget *section;
	g_autofree gchar *origin_ui = NULL;

	state = gs_app_get_state (repo);
	if (!(state == GS_APP_STATE_AVAILABLE ||
	      state == GS_APP_STATE_AVAILABLE_LOCAL ||
	      state == GS_APP_STATE_INSTALLED ||
	      state == GS_APP_STATE_INSTALLING ||
	      state == GS_APP_STATE_REMOVING)) {
		g_warning ("repo %s in invalid state %s",
		           gs_app_get_id (repo),
		           gs_app_state_to_string (state));
		return;
	}

	if (third_party_repos && is_third_party_repo (dialog, repo)) {
		*third_party_repos = g_slist_prepend (*third_party_repos, repo);
		return;
	}

	origin_ui = gs_app_get_origin_ui (repo);
	if (!origin_ui)
		origin_ui = gs_app_get_packaging_format (repo);
	if (!origin_ui)
		origin_ui = g_strdup (gs_app_get_management_plugin (repo));
	section = g_hash_table_lookup (dialog->sections, origin_ui);
	if (section == NULL) {
		section = gs_repos_section_new (dialog->plugin_loader, FALSE);
		adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (section),
						 origin_ui);
		g_signal_connect_object (section, "remove-clicked",
					 G_CALLBACK (repo_section_remove_clicked_cb), dialog, 0);
		g_signal_connect_object (section, "switch-clicked",
					 G_CALLBACK (repo_section_switch_clicked_cb), dialog, 0);
		g_hash_table_insert (dialog->sections, g_steal_pointer (&origin_ui), section);
		gtk_widget_show (section);
	}

	gs_repos_section_add_repo (GS_REPOS_SECTION (section), repo);
}

static gint
repos_dialog_compare_sections_cb (gconstpointer aa,
				  gconstpointer bb)
{
	GsReposSection *section_a = (GsReposSection *) aa;
	GsReposSection *section_b = (GsReposSection *) bb;
	const gchar *section_sort_key_a;
	const gchar *section_sort_key_b;
	g_autofree gchar *title_sort_key_a = NULL;
	g_autofree gchar *title_sort_key_b = NULL;
	gint res;

	section_sort_key_a = gs_repos_section_get_sort_key (section_a);
	section_sort_key_b = gs_repos_section_get_sort_key (section_b);

	res = g_strcmp0 (section_sort_key_a, section_sort_key_b);
	if (res != 0)
		return res;

	title_sort_key_a = gs_utils_sort_key (adw_preferences_group_get_title (ADW_PREFERENCES_GROUP (section_a)));
	title_sort_key_b = gs_utils_sort_key (adw_preferences_group_get_title (ADW_PREFERENCES_GROUP (section_b)));

	return g_strcmp0 (title_sort_key_a, title_sort_key_b);
}

static void
get_sources_cb (GsPluginLoader *plugin_loader,
		GAsyncResult *res,
		GsReposDialog *dialog)
{
	GsApp *app;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GSList) other_repos = NULL;
	g_autoptr(GList) sections = NULL;
	AdwPreferencesGroup *added_section;
	GHashTableIter iter;

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
		return;
	}

	/* remove previous */
	g_hash_table_iter_init (&iter, dialog->sections);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&added_section)) {
		adw_preferences_page_remove (ADW_PREFERENCES_PAGE (dialog->content_page),
					     added_section);
		g_hash_table_iter_remove (&iter);
	}

	/* stop the spinner */
	gs_stop_spinner (GTK_SPINNER (dialog->spinner));

	/* no results */
	if (gs_app_list_length (list) == 0) {
		g_debug ("no sources to show");
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "empty");
		return;
	}

	/* add each */
	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "sources");
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		add_repo (dialog, app, &other_repos);
	}

	sections = g_hash_table_get_values (dialog->sections);
	sections = g_list_sort (sections, repos_dialog_compare_sections_cb);
	for (GList *link = sections; link; link = g_list_next (link)) {
		AdwPreferencesGroup *section = link->data;
		adw_preferences_page_add (ADW_PREFERENCES_PAGE (dialog->content_page), section);
	}

	gtk_widget_set_visible (dialog->content_page, sections != NULL);

	if (other_repos) {
		GsReposSection *section;
		GtkWidget *widget;
		GtkWidget *row;
		g_autofree gchar *anchor = NULL;
		g_autofree gchar *hint = NULL;
		g_autofree gchar *section_id = NULL;

		widget = gtk_switch_new ();
		gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
		gtk_switch_set_active (GTK_SWITCH (widget), dialog->third_party_enabled);
		g_signal_connect_object (widget, "notify::active",
					 G_CALLBACK (fedora_third_party_repos_switch_notify_cb), dialog, 0);
		gtk_widget_show (widget);

		row = adw_action_row_new ();
		adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _("Enable New Repositories"));
		adw_action_row_set_subtitle (ADW_ACTION_ROW (row), _("Turn on new repositories when they are added."));
		adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), widget);
		adw_action_row_add_suffix (ADW_ACTION_ROW (row), widget);
		gtk_widget_show (row);

		anchor = g_strdup_printf ("<a href=\"%s\">%s</a>",
	                        "https://docs.fedoraproject.org/en-US/workstation-working-group/third-party-repos/",
	                        /* TRANSLATORS: this is the clickable
	                         * link on the third party repositories info bar */
	                        _("more information"));
		hint = g_strdup_printf (
				/* TRANSLATORS: this is the third party repositories info bar. The '%s' is replaced
				   with a link consisting a text "more information", which constructs a sentence:
				   "Additional repositories from selected third parties - more information."*/
				_("Additional repositories from selected third parties — %s."),
				anchor);

		widget = adw_preferences_group_new ();
		adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (widget),
						 _("Fedora Third Party Repositories"));
/* AdwPreferencesGroup:use-markup doesn't exist before 1.3.90, configurations
 * where GNOME 41 will be used and Libhandy 1.3.90 won't be available are
 * unlikely, so let's just ignore the description in such cases. */
#if ADW_CHECK_VERSION(1, 3, 90)
		adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (widget), hint);
		adw_preferences_group_set_use_markup (ADW_PREFERENCES_GROUP (widget), TRUE);
#endif

		gtk_widget_show (widget);
		adw_preferences_group_add (ADW_PREFERENCES_GROUP (widget), row);
		adw_preferences_page_add (ADW_PREFERENCES_PAGE (dialog->content_page),
					  ADW_PREFERENCES_GROUP (widget));

		/* use something unique, not clashing with the other section names */
		section_id = g_strdup_printf ("fedora-third-party::1::%p", widget);
		g_hash_table_insert (dialog->sections, g_steal_pointer (&section_id), widget);

		section = GS_REPOS_SECTION (gs_repos_section_new (dialog->plugin_loader, TRUE));
		gs_repos_section_set_sort_key (section, "900");
		g_signal_connect_object (section, "switch-clicked",
					 G_CALLBACK (repo_section_switch_clicked_cb), dialog, 0);
		gtk_widget_show (GTK_WIDGET (section));

		for (GSList *link = other_repos; link; link = g_slist_next (link)) {
			GsApp *repo = link->data;
			gs_repos_section_add_repo (section, repo);
		}

		/* use something unique, not clashing with the other section names */
		section_id = g_strdup_printf ("fedora-third-party::2::%p", section);
		g_hash_table_insert (dialog->sections, g_steal_pointer (&section_id), section);

		adw_preferences_page_add (ADW_PREFERENCES_PAGE (dialog->content_page),
					  ADW_PREFERENCES_GROUP (section));
	}
}

static void
reload_sources (GsReposDialog *dialog)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* get the list of non-core software repositories */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_SOURCES,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED |
					                 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE,
					 "dedupe-flags", GS_APP_LIST_FILTER_FLAG_NONE,
					 NULL);
	gs_plugin_loader_job_process_async (dialog->plugin_loader, plugin_job,
					    dialog->cancellable,
					    (GAsyncReadyCallback) get_sources_cb,
					    dialog);
}

static void
fedora_third_party_list_repos_done_cb (GObject *source_object,
				       GAsyncResult *result,
				       gpointer user_data)
{
	g_autoptr(GsReposDialog) self = user_data;
	g_autoptr(GHashTable) repos = NULL;
	g_autoptr(GError) error = NULL;

	if (!gs_fedora_third_party_list_finish (GS_FEDORA_THIRD_PARTY (source_object), result, &repos, &error)) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			return;
		g_warning ("Failed to list 'fedora-third-party' repos: %s", error->message);
	} else {
		self->third_party_repos = g_steal_pointer (&repos);
	}

	reload_sources (self);
}

static void
fedora_third_party_query_done_cb (GObject *source_object,
				  GAsyncResult *result,
				  gpointer user_data)
{
	GsFedoraThirdPartyState state = GS_FEDORA_THIRD_PARTY_STATE_UNKNOWN;
	g_autoptr(GsReposDialog) self = user_data;
	g_autoptr(GError) error = NULL;

	if (!gs_fedora_third_party_query_finish (GS_FEDORA_THIRD_PARTY (source_object), result, &state, &error)) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			return;
		g_warning ("Failed to query 'fedora-third-party': %s", error->message);
	} else {
		self->third_party_enabled = state == GS_FEDORA_THIRD_PARTY_STATE_ENABLED;
	}

	gs_fedora_third_party_list (self->third_party, self->cancellable,
				    fedora_third_party_list_repos_done_cb, g_object_ref (self));
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
reload_third_party_repos (GsReposDialog *dialog)
{
	/* Fedora-specific functionality */
	if (!is_fedora ()) {
		reload_sources (dialog);
		return;
	}

	gs_fedora_third_party_invalidate (dialog->third_party);

	if (!gs_fedora_third_party_is_available (dialog->third_party)) {
		reload_sources (dialog);
		return;
	}

	g_clear_pointer (&dialog->third_party_repos, g_hash_table_unref);

	gs_fedora_third_party_query (dialog->third_party, dialog->cancellable, fedora_third_party_query_done_cb, g_object_ref (dialog));
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
	reload_third_party_repos (dialog);
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
	g_clear_pointer (&dialog->third_party_repos, g_hash_table_unref);
	g_clear_pointer (&dialog->sections, g_hash_table_unref);
	g_clear_object (&dialog->third_party);
	g_clear_object (&dialog->cancellable);
	g_clear_object (&dialog->settings);

	G_OBJECT_CLASS (gs_repos_dialog_parent_class)->dispose (object);
}

static void
gs_repos_dialog_init (GsReposDialog *dialog)
{
	g_autofree gchar *label_empty_text = NULL;
	g_autofree gchar *os_name = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	gtk_widget_init_template (GTK_WIDGET (dialog));

	dialog->third_party = gs_fedora_third_party_new ();
	dialog->cancellable = g_cancellable_new ();
	dialog->settings = g_settings_new ("org.gnome.software");
	dialog->sections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	os_name = get_os_name ();

	/* TRANSLATORS: This is the description text displayed in the Software Repositories dialog.
	   %s gets replaced by the name of the actual distro, e.g. Fedora. */
	label_empty_text = g_strdup_printf (_("These repositories supplement the default software provided by %s."),
	                                    os_name);
	adw_status_page_set_description (ADW_STATUS_PAGE (dialog->status_empty), label_empty_text);
}

static void
gs_repos_dialog_class_init (GsReposDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_repos_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-repos-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, status_empty);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, content_page);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, spinner);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, stack);

	gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "window.close", NULL);
}

GtkWidget *
gs_repos_dialog_new (GtkWindow *parent, GsPluginLoader *plugin_loader)
{
	GsReposDialog *dialog;

	dialog = g_object_new (GS_TYPE_REPOS_DIALOG,
			       "transient-for", parent,
			       "modal", TRUE,
			       NULL);
	set_plugin_loader (dialog, plugin_loader);
	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "waiting");
	gs_start_spinner (GTK_SPINNER (dialog->spinner));
	reload_third_party_repos (dialog);

	return GTK_WIDGET (dialog);
}
