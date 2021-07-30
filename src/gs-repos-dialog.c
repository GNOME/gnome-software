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
#include "gs-third-party-repo-row.h"
#include "gs-utils.h"
#include <glib/gi18n.h>

struct _GsReposDialog
{
	GtkDialog	 parent_instance;
	GSettings	*settings;
	GsApp		*third_party_repo;
	GHashTable	*sections; /* gchar * ~> GsReposSection * */

	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
	GtkWidget	*label_empty;
	GtkWidget	*label_header;
	GtkWidget	*content_box;
	GtkWidget	*spinner;
	GtkWidget	*stack;
};

G_DEFINE_TYPE (GsReposDialog, gs_repos_dialog, GTK_TYPE_DIALOG)

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
	gtk_widget_destroy (GTK_WIDGET (confirm_dialog));

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
	gtk_widget_destroy (GTK_WIDGET (confirm_dialog));

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
	message = g_strdup_printf (_("Software that has been installed from “%s” will cease receive updates."),
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

static gboolean
is_third_party_repo (GsReposDialog *dialog,
		     GsApp *repo)
{
	if (dialog->third_party_repo != NULL) {
		const gchar *source_repo;
		const gchar *source_third_party_package;

		source_repo = gs_app_get_source_id_default (repo);
		source_third_party_package = gs_app_get_source_id_default (dialog->third_party_repo);

		/* group repos from the same repo-release package together */
		return g_strcmp0 (source_repo, source_third_party_package) == 0;
	}

	return FALSE;
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
		section = gs_repos_section_new (dialog->plugin_loader);
		hdy_preferences_group_set_title (HDY_PREFERENCES_GROUP (section),
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

	title_sort_key_a = gs_utils_sort_key (hdy_preferences_group_get_title (HDY_PREFERENCES_GROUP (section_a)));
	title_sort_key_b = gs_utils_sort_key (hdy_preferences_group_get_title (HDY_PREFERENCES_GROUP (section_b)));

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
	g_hash_table_remove_all (dialog->sections);
	gs_container_remove_all (GTK_CONTAINER (dialog->content_box));

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
		add_repo (dialog, app, &other_repos);
	}

	sections = g_hash_table_get_values (dialog->sections);
	sections = g_list_sort (sections, repos_dialog_compare_sections_cb);
	for (GList *link = sections; link; link = g_list_next (link)) {
		GtkWidget *section = link->data;
		gtk_container_add (GTK_CONTAINER (dialog->content_box), section);
	}

	gtk_widget_set_visible (dialog->content_box, sections != NULL);

	if (other_repos) {
		GsReposSection *section;
		GtkWidget *label;
		GtkStyleContext *style;
		g_autofree gchar *anchor = NULL;
		g_autofree gchar *hint = NULL;

		section = GS_REPOS_SECTION (gs_repos_section_new (dialog->plugin_loader));
		hdy_preferences_group_set_title (HDY_PREFERENCES_GROUP (section),
						 _("Fedora Third Party Repositories"));
		gs_repos_section_set_sort_key (section, "900");
		g_signal_connect_object (section, "switch-clicked",
					 G_CALLBACK (repo_section_switch_clicked_cb), dialog, 0);
		gtk_widget_show (GTK_WIDGET (section));

		anchor = g_strdup_printf ("<a href=\"%s\">%s</a>",
	                        "https://fedoraproject.org/wiki/Workstation/Third_Party_Software_Repositories",
	                        /* TRANSLATORS: this is the clickable
	                         * link on the third party repositories info bar */
	                        _("more information"));
		hint = g_strdup_printf (
				/* TRANSLATORS: this is the third party repositories info bar. The '%s' is replaced
				   with a link consisting a text "more information", which constructs a sentence:
				   "Additional repositories from selected third parties - more information."*/
				_("Additional repositories from selected third parties — %s."),
				anchor);

		label = gtk_label_new ("");
		g_object_set (G_OBJECT (label),
			      "halign", GTK_ALIGN_START,
			      "hexpand", TRUE,
			      "label", hint,
			      "use-markup", TRUE,
			      "visible", TRUE,
			      "wrap", TRUE,
			      "xalign", 0.0,
			      NULL);
		style = gtk_widget_get_style_context (label);
		gtk_style_context_add_class (style, "dim-label");

		gtk_box_pack_start (GTK_BOX (section), label, FALSE, TRUE, 0);
		gtk_box_reorder_child (GTK_BOX (section), label, 1);

		for (GSList *link = other_repos; link; link = g_slist_next (link)) {
			GsApp *repo = link->data;
			gs_repos_section_add_repo (section, repo);
		}

		gtk_container_add (GTK_CONTAINER (dialog->content_box), GTK_WIDGET (section));
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
			reload_sources (dialog);
			return;
		}
	}

	/* we should only get one result */
	if (gs_app_list_length (list) > 0)
		app = gs_app_list_index (list, 0);
	else
		app = NULL;

	g_set_object (&dialog->third_party_repo, app);
	reload_sources (dialog);
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
	if (!is_fedora ()) {
		reload_sources (dialog);
		return;
	}

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH_PROVIDES,
	                                 "search", third_party_repo_package,
	                                 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
	                                                 GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE,
	                                 NULL);
	gs_plugin_loader_job_process_async (dialog->plugin_loader, plugin_job,
	                                    dialog->cancellable,
	                                    (GAsyncReadyCallback) resolve_third_party_repo_cb,
	                                    dialog);
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
	g_clear_pointer (&dialog->sections, g_hash_table_unref);
	g_clear_object (&dialog->cancellable);
	g_clear_object (&dialog->settings);
	g_clear_object (&dialog->third_party_repo);

	G_OBJECT_CLASS (gs_repos_dialog_parent_class)->dispose (object);
}

static void
gs_repos_dialog_init (GsReposDialog *dialog)
{
	g_autofree gchar *label_empty_text = NULL;
	g_autofree gchar *os_name = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	gtk_widget_init_template (GTK_WIDGET (dialog));

	dialog->cancellable = g_cancellable_new ();
	dialog->settings = g_settings_new ("org.gnome.software");
	dialog->sections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	os_name = get_os_name ();

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

	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, label_empty);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, label_header);
	gtk_widget_class_bind_template_child (widget_class, GsReposDialog, content_box);
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
	reload_third_party_repo (dialog);

	return GTK_WIDGET (dialog);
}
