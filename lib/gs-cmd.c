/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>

#include "gnome-software-private.h"

#include "gs-debug.h"

typedef struct {
	GsPluginLoader	*plugin_loader;
	guint64		 require_flags;
	guint		 max_results;
	gboolean	 interactive;
	gboolean	 only_freely_licensed;
} GsCmdSelf;

static void
gs_cmd_show_results_apps (GsAppList *list)
{
	for (guint j = 0; j < gs_app_list_length (list); j++) {
		GsApp *app = gs_app_list_index (list, j);
		GsAppList *related = gs_app_get_related (app);
		g_autofree gchar *tmp = gs_app_to_string (app);
		g_print ("%s\n", tmp);
		for (guint i = 0; i < gs_app_list_length (related); i++) {
			g_autofree gchar *tmp_rel = NULL;
			GsApp *app_rel = GS_APP (gs_app_list_index (related, i));
			tmp_rel = gs_app_to_string (app_rel);
			g_print ("\t%s\n", tmp_rel);
		}
	}
}

static gchar *
gs_cmd_pad_spaces (const gchar *text, guint length)
{
	gsize i;
	GString *str;
	str = g_string_sized_new (length + 1);
	g_string_append (str, text);
	for (i = strlen (text); i < length; i++)
		g_string_append_c (str, ' ');
	return g_string_free (str, FALSE);
}

static void
gs_cmd_show_results_categories (GPtrArray *list)
{
	for (guint i = 0; i < list->len; i++) {
		GsCategory *cat = GS_CATEGORY (g_ptr_array_index (list, i));
		GsCategory *parent = gs_category_get_parent (cat);
		g_autofree gchar *tmp = NULL;
		if (parent != NULL){
			g_autofree gchar *id = NULL;
			id = g_strdup_printf ("%s/%s [%u]",
					      gs_category_get_id (parent),
					      gs_category_get_id (cat),
					      gs_category_get_size (cat));
			tmp = gs_cmd_pad_spaces (id, 32);
			g_print ("%s : %s\n",
				 tmp, gs_category_get_name (cat));
		} else {
			GPtrArray *subcats = gs_category_get_children (cat);
			tmp = gs_cmd_pad_spaces (gs_category_get_id (cat), 32);
			g_print ("%s : %s\n",
				 tmp, gs_category_get_name (cat));
			gs_cmd_show_results_categories (subcats);
		}
	}
}

static GsPluginRefineRequireFlags
gs_cmd_refine_require_flag_from_string (const gchar *flag, GError **error)
{
	if (g_strcmp0 (flag, "all") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_MASK;
	if (g_strcmp0 (flag, "license") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_LICENSE;
	if (g_strcmp0 (flag, "url") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_URL;
	if (g_strcmp0 (flag, "description") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_DESCRIPTION;
	if (g_strcmp0 (flag, "size") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE;
	if (g_strcmp0 (flag, "rating") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_RATING;
	if (g_strcmp0 (flag, "version") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_VERSION;
	if (g_strcmp0 (flag, "history") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_HISTORY;
	if (g_strcmp0 (flag, "setup-action") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION;
	if (g_strcmp0 (flag, "update-details") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_DETAILS;
	if (g_strcmp0 (flag, "origin") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN;
	if (g_strcmp0 (flag, "related") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_RELATED;
	if (g_strcmp0 (flag, "menu-path") == 0)
		/* no longer supported by itself; categories are largely equivalent */
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_CATEGORIES;
	if (g_strcmp0 (flag, "upgrade-removed") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPGRADE_REMOVED;
	if (g_strcmp0 (flag, "provenance") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_PROVENANCE;
	if (g_strcmp0 (flag, "reviews") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEWS;
	if (g_strcmp0 (flag, "review-ratings") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEW_RATINGS;
	if (g_strcmp0 (flag, "key-colors") == 0)
		/* no longer supported by itself; derived automatically from the icon */
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_ICON;
	if (g_strcmp0 (flag, "icon") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_ICON;
	if (g_strcmp0 (flag, "permissions") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_PERMISSIONS;
	if (g_strcmp0 (flag, "origin-hostname") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN_HOSTNAME;
	if (g_strcmp0 (flag, "origin-ui") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN_UI;
	if (g_strcmp0 (flag, "runtime") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_RUNTIME;
	if (g_strcmp0 (flag, "categories") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_CATEGORIES;
	if (g_strcmp0 (flag, "project-group") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_PROJECT_GROUP;
	if (g_strcmp0 (flag, "developer-name") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_DEVELOPER_NAME;
	if (g_strcmp0 (flag, "kudos") == 0)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_KUDOS;
	g_set_error (error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_NOT_SUPPORTED,
		     "GsPluginRefineFlag '%s' not recognised", flag);
	return 0;
}

static guint64
gs_cmd_parse_refine_require_flags (const gchar *extra, GError **error)
{
	GsPluginRefineRequireFlags tmp;
	guint i;
	guint64 require_flags = GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE;
	g_auto(GStrv) split = NULL;

	if (extra == NULL)
		return GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE;

	split = g_strsplit (extra, ",", -1);
	for (i = 0; split[i] != NULL; i++) {
		tmp = gs_cmd_refine_require_flag_from_string (split[i], error);
		if (tmp == 0)
			return G_MAXUINT64;
		require_flags |= tmp;
	}
	return require_flags;
}

static GsPluginListAppsFlags
get_list_apps_flags (GsCmdSelf *self)
{
	GsPluginListAppsFlags flags = GS_PLUGIN_LIST_APPS_FLAGS_NONE;

	if (self->interactive)
		flags |= GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE;

	return flags;
}

static GsAppQueryLicenseType
get_query_license_type (GsCmdSelf *self)
{
	if (self->only_freely_licensed)
		return GS_APP_QUERY_LICENSE_FOSS;
	return GS_APP_QUERY_LICENSE_ANY;
}

static gboolean
gs_cmd_install_remove_exec (GsCmdSelf *self, gboolean is_install, const gchar *name, GError **error)
{
	g_autoptr(GsApp) app = NULL;
	GsAppList *list;
	g_autoptr(GsAppList) list_filtered = NULL;
	g_autoptr(GsAppQuery) query = NULL;
	g_autoptr(GsPluginJob) plugin_job2 = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	gboolean show_installed = TRUE;
	const gchar * const keywords[] = { name, NULL };

	/* ensure set */
	self->require_flags |= GS_PLUGIN_REFINE_REQUIRE_FLAGS_ICON;
	self->require_flags |= GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION;

	/* do search */
	query = gs_app_query_new ("keywords", keywords,
				  "refine-require-flags", self->require_flags,
				  "max-results", self->max_results,
				  "dedupe-flags", GS_APP_QUERY_DEDUPE_FLAGS_DEFAULT,
				  "sort-func", gs_utils_app_sort_match_value,
				  "license-type", get_query_license_type (self),
				  NULL);

	plugin_job = gs_plugin_job_list_apps_new (query, get_list_apps_flags (self));
	if (!gs_plugin_loader_job_process (self->plugin_loader, plugin_job, NULL, error))
		return FALSE;
	list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
	if (gs_app_list_length (list) == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "no components matched '%s'",
			     name);
		return FALSE;
	}

	/* filter */
	if (is_install)
		show_installed = FALSE;
	list_filtered = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app_tmp = gs_app_list_index (list, i);
		if (gs_app_is_installed (app_tmp) == show_installed)
			gs_app_list_add (list_filtered, app_tmp);
	}

	/* nothing */
	if (gs_app_list_length (list_filtered) == 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "no components were in the correct state for '%s %s'",
			     is_install ? "install" : "remove", name);
		return FALSE;
	}

	/* install */
	if (is_install) {
		plugin_job2 = gs_plugin_job_install_apps_new (list_filtered,
							      self->interactive ? GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE : GS_PLUGIN_INSTALL_APPS_FLAGS_NONE);
	} else {
		plugin_job2 = gs_plugin_job_uninstall_apps_new (list_filtered,
								self->interactive ? GS_PLUGIN_UNINSTALL_APPS_FLAGS_INTERACTIVE : GS_PLUGIN_UNINSTALL_APPS_FLAGS_NONE);
	}

	return gs_plugin_loader_job_process (self->plugin_loader, plugin_job2,
					     NULL, error);
}

static void
gs_cmd_self_free (GsCmdSelf *self)
{
	if (self->plugin_loader != NULL)
		g_object_unref (self->plugin_loader);
	g_free (self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsCmdSelf, gs_cmd_self_free)

static gint
app_sort_kind_cb (GsApp *app1, GsApp *app2, gpointer user_data)
{
	if (gs_app_get_kind (app1) == AS_COMPONENT_KIND_DESKTOP_APP)
		return -1;
	if (gs_app_get_kind (app2) == AS_COMPONENT_KIND_DESKTOP_APP)
		return 1;
	return 0;
}

int
main (int argc, char **argv)
{
	g_autoptr(GOptionContext) context = NULL;
	gboolean prefer_local = FALSE;
	gboolean ret;
	gboolean show_results = FALSE;
	gboolean verbose = FALSE;
	gint i;
	guint64 cache_age_secs = 0;
	gint repeat = 1;
	g_auto(GStrv) plugin_blocklist = NULL;
	g_auto(GStrv) plugin_allowlist = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsDebug) debug = gs_debug_new_from_environment ();
	g_autofree gchar *plugin_blocklist_str = NULL;
	g_autofree gchar *plugin_allowlist_str = NULL;
	g_autofree gchar *refine_flags_str = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GsCmdSelf) self = g_new0 (GsCmdSelf, 1);
	const GOptionEntry options[] = {
		{ "show-results", '\0', 0, G_OPTION_ARG_NONE, &show_results,
		  "Show the results for the action", NULL },
		{ "refine-flags", '\0', 0, G_OPTION_ARG_STRING, &refine_flags_str,
		  "Set any refine flags required for the action", NULL },
		{ "repeat", '\0', 0, G_OPTION_ARG_INT, &repeat,
		  "Repeat the action this number of times", NULL },
		{ "cache-age", '\0', 0, G_OPTION_ARG_INT64, &cache_age_secs,
		  "Use this maximum cache age in seconds", NULL },
		{ "max-results", '\0', 0, G_OPTION_ARG_INT, &self->max_results,
		  "Return a maximum number of results", NULL },
		{ "prefer-local", '\0', 0, G_OPTION_ARG_NONE, &prefer_local,
		  "Prefer local file sources to AppStream", NULL },
		{ "plugin-blocklist", '\0', 0, G_OPTION_ARG_STRING, &plugin_blocklist_str,
		  "Do not load specific plugins", NULL },
		{ "plugin-allowlist", '\0', 0, G_OPTION_ARG_STRING, &plugin_allowlist_str,
		  "Only load specific plugins", NULL },
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show verbose debugging information", NULL },
		{ "interactive", 'i', 0, G_OPTION_ARG_NONE, &self->interactive,
		  "Allow interactive authentication", NULL },
		{ "only-freely-licensed", '\0', 0, G_OPTION_ARG_NONE, &self->only_freely_licensed,
		  "Filter results to include only freely licensed apps", NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init ();

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, "GNOME Software Test Program");
	g_option_context_add_main_entries (context, options, NULL);
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		g_print ("Failed to parse options: %s\n", error->message);
		return EXIT_FAILURE;
	}
	gs_debug_set_verbose (debug, verbose);

	/* prefer local sources */
	if (prefer_local)
		g_setenv ("GNOME_SOFTWARE_PREFER_LOCAL", "true", TRUE);

	/* parse any refine flags */
	self->require_flags = gs_cmd_parse_refine_require_flags (refine_flags_str, &error);
	if (self->require_flags == G_MAXUINT64) {
		g_print ("Flag unknown: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* load plugins */
	self->plugin_loader = gs_plugin_loader_new (NULL, NULL);
	if (g_file_test (LOCALPLUGINDIR, G_FILE_TEST_EXISTS))
		gs_plugin_loader_add_location (self->plugin_loader, LOCALPLUGINDIR);
	if (plugin_allowlist_str != NULL)
		plugin_allowlist = g_strsplit (plugin_allowlist_str, ",", -1);
	if (plugin_blocklist_str != NULL)
		plugin_blocklist = g_strsplit (plugin_blocklist_str, ",", -1);
	ret = gs_plugin_loader_setup (self->plugin_loader,
				      (const gchar * const *) plugin_allowlist,
				      (const gchar * const *) plugin_blocklist,
				      NULL,
				      &error);
	if (!ret) {
		g_print ("Failed to setup plugins: %s\n", error->message);
		return EXIT_FAILURE;
	}
	gs_plugin_loader_dump_state (self->plugin_loader);

	/* ensure that at least some metadata of any age is present, and also
	 * spin up the plugins enough as to prime caches */
	if (g_getenv ("GS_CMD_NO_INITIAL_REFRESH") == NULL) {
		g_autoptr(GsPluginJob) plugin_job = NULL;
		GsPluginRefreshMetadataFlags refresh_metadata_flags = GS_PLUGIN_REFRESH_METADATA_FLAGS_NONE;

		if (self->interactive)
			refresh_metadata_flags |= GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE;

		plugin_job = gs_plugin_job_refresh_metadata_new (G_MAXUINT64, refresh_metadata_flags);
		ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
						    NULL, &error);
		if (!ret) {
			g_print ("Failed to refresh plugins: %s\n", error->message);
			return EXIT_FAILURE;
		}
	}

	/* do action */
	if (argc == 2 && g_strcmp0 (argv[1], "installed") == 0) {
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsAppQuery) query = NULL;
			g_autoptr(GsPluginJob) plugin_job = NULL;
			GsAppList *list;

			query = gs_app_query_new ("is-installed", GS_APP_QUERY_TRISTATE_TRUE,
						  "refine-require-flags", self->require_flags,
						  "max-results", self->max_results,
						  "dedupe-flags", GS_APP_QUERY_DEDUPE_FLAGS_DEFAULT,
						  "license-type", get_query_license_type (self),
						  NULL);

			plugin_job = gs_plugin_job_list_apps_new (query, get_list_apps_flags (self));
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job, NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc == 3 && g_strcmp0 (argv[1], "search") == 0) {
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsAppQuery) query = NULL;
			g_autoptr(GsPluginJob) plugin_job = NULL;
			const gchar *keywords[2] = { argv[2], NULL };
			GsAppList *list;

			query = gs_app_query_new ("keywords", keywords,
						  "refine-require-flags", self->require_flags,
						  "max-results", self->max_results,
						  "dedupe-flags", GS_APP_QUERY_DEDUPE_FLAGS_DEFAULT,
						  "sort-func", gs_utils_app_sort_match_value,
						  "license-type", get_query_license_type (self),
						  NULL);

			plugin_job = gs_plugin_job_list_apps_new (query, get_list_apps_flags (self));
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job, NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc == 3 && g_strcmp0 (argv[1], "get-alternates") == 0) {
		app = gs_app_new (argv[2]);
		gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsAppQuery) query = NULL;
			g_autoptr(GsPluginJob) plugin_job = NULL;
			GsAppList *list;

			query = gs_app_query_new ("alternate-of", app,
						  "refine-require-flags", self->require_flags,
						  "max-results", self->max_results,
						  "dedupe-flags", GS_APP_QUERY_DEDUPE_FLAGS_DEFAULT,
						  "sort-func", gs_utils_app_sort_priority,
						  "license-type", get_query_license_type (self),
						  NULL);

			plugin_job = gs_plugin_job_list_apps_new (query, get_list_apps_flags (self));
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job, NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc == 3 && g_strcmp0 (argv[1], "install") == 0) {
		ret = gs_cmd_install_remove_exec (self, TRUE, argv[2], &error);
	} else if (argc == 3 && g_strcmp0 (argv[1], "remove") == 0) {
		ret = gs_cmd_install_remove_exec (self, FALSE, argv[2], &error);
	} else if (argc == 3 && g_strcmp0 (argv[1], "action-upgrade-download") == 0) {
		g_autoptr(GsPluginJob) plugin_job = NULL;
		app = gs_app_new (argv[2]);
		gs_app_set_kind (app, AS_COMPONENT_KIND_OPERATING_SYSTEM);
		plugin_job = gs_plugin_job_download_upgrade_new (app,
								 self->interactive ? GS_PLUGIN_DOWNLOAD_UPGRADE_FLAGS_INTERACTIVE :
								 GS_PLUGIN_DOWNLOAD_UPGRADE_FLAGS_NONE);
		ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
						    NULL, &error);

		if (show_results && ret) {
			g_autoptr(GsAppList) list = gs_app_list_new ();
			gs_app_list_add (list, app);
			gs_cmd_show_results_apps (list);
		}
	} else if (argc == 3 && g_strcmp0 (argv[1], "refine") == 0) {
		app = gs_app_new (argv[2]);
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsPluginJob) plugin_job = NULL;
			plugin_job = gs_plugin_job_refine_new_for_app (app,
								       self->interactive ? GS_PLUGIN_REFINE_FLAGS_INTERACTIVE :
								       GS_PLUGIN_REFINE_FLAGS_NONE,
								       self->require_flags);
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
							    NULL, &error);
			if (!ret)
				break;
		}

		if (show_results) {
			g_autoptr(GsAppList) list = gs_app_list_new ();
			gs_app_list_add (list, app);
			gs_cmd_show_results_apps (list);
		}
	} else if (argc == 3 && g_strcmp0 (argv[1], "launch") == 0) {
		app = gs_app_new (argv[2]);
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsPluginJob) plugin_job = NULL;
			plugin_job = gs_plugin_job_launch_new (app,
							       self->interactive ? GS_PLUGIN_LAUNCH_FLAGS_INTERACTIVE :
							       GS_PLUGIN_LAUNCH_FLAGS_NONE);
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
							    NULL, &error);
			if (!ret)
				break;
		}
	} else if (argc == 3 && g_strcmp0 (argv[1], "filename-to-app") == 0) {
		g_autoptr(GsPluginJob) plugin_job = NULL;
		GsAppList *list;

		file = g_file_new_for_path (argv[2]);
		plugin_job = gs_plugin_job_file_to_app_new (file,
							    self->interactive ? GS_PLUGIN_FILE_TO_APP_FLAGS_INTERACTIVE :
							    GS_PLUGIN_FILE_TO_APP_FLAGS_NONE,
							    self->require_flags);
		ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job, NULL, &error);
		list = gs_plugin_job_file_to_app_get_result_list (GS_PLUGIN_JOB_FILE_TO_APP (plugin_job));

		if (show_results && list != NULL)
			gs_cmd_show_results_apps (list);
	} else if (argc == 3 && g_strcmp0 (argv[1], "url-to-app") == 0) {
		g_autoptr(GsPluginJob) plugin_job = NULL;
		GsAppList *list;

		plugin_job = gs_plugin_job_url_to_app_new (argv[2],
							   self->interactive ? GS_PLUGIN_URL_TO_APP_FLAGS_INTERACTIVE :
							   GS_PLUGIN_URL_TO_APP_FLAGS_NONE,
							   self->require_flags);
		ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
						    NULL, &error);
		list = gs_plugin_job_url_to_app_get_result_list (GS_PLUGIN_JOB_URL_TO_APP (plugin_job));

		if (show_results && list != NULL)
			gs_cmd_show_results_apps (list);
	} else if (argc == 2 && g_strcmp0 (argv[1], "updates") == 0) {
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsAppQuery) query = NULL;
			g_autoptr(GsPluginJob) plugin_job = NULL;
			GsAppList *list;

			query = gs_app_query_new ("is-for-update", GS_APP_QUERY_TRISTATE_TRUE,
						  "refine-require-flags", self->require_flags,
						  "max-results", self->max_results,
						  NULL);
			plugin_job = gs_plugin_job_list_apps_new (query, self->interactive ?
								  GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE :
								  GS_PLUGIN_LIST_APPS_FLAGS_NONE);
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
							     NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc == 2 && g_strcmp0 (argv[1], "upgrades") == 0) {
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsPluginJob) plugin_job = NULL;
			GsPluginListDistroUpgradesFlags upgrades_flags = GS_PLUGIN_LIST_DISTRO_UPGRADES_FLAGS_NONE;
			GsAppList *list;

			if (self->interactive)
				upgrades_flags |= GS_PLUGIN_LIST_DISTRO_UPGRADES_FLAGS_INTERACTIVE;

			plugin_job = gs_plugin_job_list_distro_upgrades_new (upgrades_flags, self->require_flags);
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
							    NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_distro_upgrades_get_result_list (GS_PLUGIN_JOB_LIST_DISTRO_UPGRADES (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc == 2 && g_strcmp0 (argv[1], "sources") == 0) {
		g_autoptr(GsAppQuery) query = NULL;
		g_autoptr(GsPluginJob) plugin_job = NULL;
		GsAppList *list;

		query = gs_app_query_new ("component-kinds", (AsComponentKind[]) { AS_COMPONENT_KIND_REPOSITORY, 0 },
					  "refine-require-flags", self->require_flags,
					  "max-results", self->max_results,
					  NULL);
		plugin_job = gs_plugin_job_list_apps_new (query, self->interactive ? GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE : GS_PLUGIN_LIST_APPS_FLAGS_NONE);
		ret = gs_plugin_loader_job_process (self->plugin_loader,
						    plugin_job,
						    NULL,
						    &error);
		list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));

		if (show_results && list != NULL)
			gs_cmd_show_results_apps (list);
	} else if (argc == 2 && g_strcmp0 (argv[1], "popular") == 0) {
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsPluginJob) plugin_job = NULL;
			g_autoptr(GsAppQuery) query = NULL;
			GsAppList *list;

			query = gs_app_query_new ("is-curated", GS_APP_QUERY_TRISTATE_TRUE,
						  "refine-require-flags", self->require_flags,
						  "max-results", self->max_results,
						  "sort-func", app_sort_kind_cb,
						  "license-type", get_query_license_type (self),
						  NULL);

			plugin_job = gs_plugin_job_list_apps_new (query, get_list_apps_flags (self));
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
							    NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc == 2 && g_strcmp0 (argv[1], "featured") == 0) {
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsPluginJob) plugin_job = NULL;
			g_autoptr(GsAppQuery) query = NULL;
			GsAppList *list;

			query = gs_app_query_new ("is-featured", GS_APP_QUERY_TRISTATE_TRUE,
						  "refine-require-flags", self->require_flags,
						  "max-results", self->max_results,
						  "license-type", get_query_license_type (self),
						  NULL);

			plugin_job = gs_plugin_job_list_apps_new (query, get_list_apps_flags (self));
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
							    NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc == 3 && g_strcmp0 (argv[1], "deployment-featured") == 0) {
		g_auto(GStrv) split = g_strsplit (argv[2], ",", -1);
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsPluginJob) plugin_job = NULL;
			g_autoptr(GsAppQuery) query = NULL;
			GsAppList *list;

			query = gs_app_query_new ("deployment-featured", split,
						  "refine-require-flags", self->require_flags,
						  "dedupe-flags", GS_APP_LIST_FILTER_FLAG_KEY_ID,
						  "max-results", self->max_results,
						  "license-type", get_query_license_type (self),
						  NULL);

			plugin_job = gs_plugin_job_list_apps_new (query, get_list_apps_flags (self));
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
							    NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc == 2 && g_strcmp0 (argv[1], "recent") == 0) {
		if (cache_age_secs == 0)
			cache_age_secs = 60 * 60 * 24 * 60;
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsPluginJob) plugin_job = NULL;
			g_autoptr(GDateTime) now = NULL;
			g_autoptr(GDateTime) released_since = NULL;
			g_autoptr(GsAppQuery) query = NULL;
			GsAppList *list;

			now = g_date_time_new_now_local ();
			released_since = g_date_time_add_seconds (now, -cache_age_secs);
			query = gs_app_query_new ("released-since", released_since,
						  "refine-require-flags", self->require_flags,
						  "dedupe-flags", GS_APP_LIST_FILTER_FLAG_KEY_ID,
						  "max-results", self->max_results,
						  "sort-func", app_sort_kind_cb,
						  "license-type", get_query_license_type (self),
						  NULL);

			plugin_job = gs_plugin_job_list_apps_new (query, get_list_apps_flags (self));
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
							    NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc == 2 && g_strcmp0 (argv[1], "get-categories") == 0) {
		for (i = 0; i < repeat; i++) {
			g_autoptr(GsPluginJob) plugin_job = NULL;
			GPtrArray *categories;
			GsPluginRefineCategoriesFlags flags = GS_PLUGIN_REFINE_CATEGORIES_FLAGS_SIZE;

			if (self->interactive)
				flags |= GS_PLUGIN_REFINE_CATEGORIES_FLAGS_INTERACTIVE;

			plugin_job = gs_plugin_job_list_categories_new (flags);
			if (!gs_plugin_loader_job_process (self->plugin_loader, plugin_job, NULL, &error)) {
				ret = FALSE;
				break;
			}

			categories = gs_plugin_job_list_categories_get_result_list (GS_PLUGIN_JOB_LIST_CATEGORIES (plugin_job));

			if (show_results && i == repeat - 1 && categories != NULL)
				gs_cmd_show_results_categories (categories);
		}
	} else if (argc == 3 && g_strcmp0 (argv[1], "get-category-apps") == 0) {
		g_autoptr(GsCategory) category_owned = NULL;
		GsCategory *category = NULL;
		g_auto(GStrv) split = NULL;
		GsCategoryManager *manager = gs_plugin_loader_get_category_manager (self->plugin_loader);

		split = g_strsplit (argv[2], "/", 2);
		if (g_strv_length (split) == 1) {
			category_owned = gs_category_manager_lookup (manager, split[0]);
			category = category_owned;
		} else {
			g_autoptr(GsCategory) parent = gs_category_manager_lookup (manager, split[0]);
			if (parent != NULL)
				category = gs_category_find_child (parent, split[1]);
		}

		if (category == NULL) {
			g_printerr ("Error: Could not find category ‘%s’\n", argv[2]);
			return EXIT_FAILURE;
		}

		for (i = 0; i < repeat; i++) {
			g_autoptr(GsPluginJob) plugin_job = NULL;
			GsAppList *list;
			g_autoptr(GsAppQuery) query = NULL;

			query = gs_app_query_new ("category", category,
						  "refine-require-flags", self->require_flags,
						  "max-results", self->max_results,
						  "sort-func", gs_utils_app_sort_name,
						  "license-type", get_query_license_type (self),
						  NULL);

			plugin_job = gs_plugin_job_list_apps_new (query, get_list_apps_flags (self));
			ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job, NULL, &error);
			if (!ret)
				break;

			list = gs_plugin_job_list_apps_get_result_list (GS_PLUGIN_JOB_LIST_APPS (plugin_job));
			if (show_results && i == repeat - 1 && list != NULL)
				gs_cmd_show_results_apps (list);
		}
	} else if (argc >= 2 && g_strcmp0 (argv[1], "refresh") == 0) {
		g_autoptr(GsPluginJob) plugin_job = NULL;
		GsPluginRefreshMetadataFlags refresh_metadata_flags = GS_PLUGIN_REFRESH_METADATA_FLAGS_NONE;

		if (self->interactive)
			refresh_metadata_flags |= GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE;

		plugin_job = gs_plugin_job_refresh_metadata_new (cache_age_secs, refresh_metadata_flags);
		ret = gs_plugin_loader_job_process (self->plugin_loader, plugin_job,
						    NULL, &error);
	} else if (argc >= 1 && g_strcmp0 (argv[1], "user-hash") == 0) {
		g_autofree gchar *user_hash = gs_utils_get_user_hash (&error);
		if (user_hash == NULL) {
			ret = FALSE;
		} else {
			g_print ("%s\n", user_hash);
			ret = TRUE;
		}
	} else {
		ret = FALSE;
		g_set_error_literal (&error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "Did not recognise option, use 'installed', "
				     "'updates', 'popular', 'get-categories', "
				     "'get-category-apps', 'get-alternates', 'filename-to-app', "
				     "'install', 'remove', "
				     "'sources', 'refresh', 'launch' or 'search'");
	}
	if (!ret) {
		g_print ("Failed: %s\n", error->message);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
