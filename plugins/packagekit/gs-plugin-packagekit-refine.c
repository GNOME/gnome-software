/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>
#include <gnome-software.h>

#include "gs-markdown.h"
#include "gs-packagekit-helper.h"
#include "packagekit-common.h"

/*
 * SECTION:
 * Uses the system PackageKit instance to return convert filenames to
 * package-ids and to also discover update details about a package.
 *
 * Requires:    | [id]
 * Refines:     | [source-id], [installed]
 */

struct GsPluginData {
	PkControl		*control;
	PkClient		*client;
	GMutex			 client_mutex;
};

static void
gs_plugin_packagekit_updates_changed_cb (PkControl *control, GsPlugin *plugin)
{
	gs_plugin_updates_changed (plugin);
}

static void
gs_plugin_packagekit_repo_list_changed_cb (PkControl *control, GsPlugin *plugin)
{
	gs_plugin_reload (plugin);
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	g_mutex_init (&priv->client_mutex);
	priv->client = pk_client_new ();
	priv->control = pk_control_new ();
	g_signal_connect (priv->control, "updates-changed",
			  G_CALLBACK (gs_plugin_packagekit_updates_changed_cb), plugin);
	g_signal_connect (priv->control, "repo-list-changed",
			  G_CALLBACK (gs_plugin_packagekit_repo_list_changed_cb), plugin);
	pk_client_set_background (priv->client, FALSE);
	pk_client_set_cache_age (priv->client, G_MAXUINT);

	/* need pkgname and ID */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_mutex_clear (&priv->client_mutex);
	g_object_unref (priv->client);
	g_object_unref (priv->control);
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_PACKAGE &&
	    gs_app_get_scope (app) == AS_APP_SCOPE_SYSTEM) {
		gs_app_set_management_plugin (app, "packagekit");
		gs_plugin_packagekit_set_packaging_format (plugin, app);
		return;
	}
}

static gboolean
gs_plugin_packagekit_resolve_packages_with_filter (GsPlugin *plugin,
                                                   GsAppList *list,
                                                   PkBitfield filter,
                                                   GCancellable *cancellable,
                                                   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GPtrArray *sources;
	GsApp *app;
	const gchar *pkgname;
	guint i;
	guint j;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) package_ids = NULL;
	g_autoptr(GPtrArray) packages = NULL;

	package_ids = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		sources = gs_app_get_sources (app);
		for (j = 0; j < sources->len; j++) {
			pkgname = g_ptr_array_index (sources, j);
			if (pkgname == NULL || pkgname[0] == '\0') {
				g_warning ("invalid pkgname '%s' for %s",
					   pkgname,
					   gs_app_get_unique_id (app));
				continue;
			}
			g_ptr_array_add (package_ids, g_strdup (pkgname));
		}
	}
	if (package_ids->len == 0)
		return TRUE;
	g_ptr_array_add (package_ids, NULL);

	/* resolve them all at once */
	g_mutex_lock (&priv->client_mutex);
	results = pk_client_resolve (priv->client,
				     filter,
				     (gchar **) package_ids->pdata,
				     cancellable,
				     gs_packagekit_helper_cb, helper,
				     error);
	g_mutex_unlock (&priv->client_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to resolve package_ids: ");
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);

	/* if the user types more characters we'll get cancelled - don't go on
	 * to mark apps as unavailable because packages->len = 0 */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		if (gs_app_get_local_file (app) != NULL)
			continue;
		gs_plugin_packagekit_resolve_packages_app (plugin, packages, app);
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_resolve_packages (GsPlugin *plugin,
                                       GsAppList *list,
                                       GCancellable *cancellable,
                                       GError **error)
{
	PkBitfield filter;
	g_autoptr(GsAppList) resolve2_list = NULL;

	/* first, try to resolve packages with ARCH filter */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
	                                 PK_FILTER_ENUM_ARCH,
	                                 -1);
	if (!gs_plugin_packagekit_resolve_packages_with_filter (plugin,
	                                                        list,
	                                                        filter,
	                                                        cancellable,
	                                                        error)) {
		return FALSE;
	}

	/* if any packages remaining in UNKNOWN state, try to resolve them again,
	 * but this time without ARCH filter */
	resolve2_list = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_list_add (resolve2_list, app);
	}
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST,
	                                 PK_FILTER_ENUM_NOT_ARCH,
	                                 PK_FILTER_ENUM_NOT_SOURCE,
	                                 -1);
	if (!gs_plugin_packagekit_resolve_packages_with_filter (plugin,
	                                                        resolve2_list,
	                                                        filter,
	                                                        cancellable,
	                                                        error)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_from_desktop (GsPlugin *plugin,
					  GsApp *app,
					  const gchar *filename,
					  GCancellable *cancellable,
					  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *to_array[] = { NULL, NULL };
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) packages = NULL;

	to_array[0] = filename;
	gs_packagekit_helper_add_app (helper, app);
	g_mutex_lock (&priv->client_mutex);
	results = pk_client_search_files (priv->client,
					  pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
					  (gchar **) to_array,
					  cancellable,
					  gs_packagekit_helper_cb, helper,
					  error);
	g_mutex_unlock (&priv->client_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to search file %s: ", filename);
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (packages->len == 1) {
		PkPackage *package;
		package = g_ptr_array_index (packages, 0);
		gs_plugin_packagekit_set_metadata_from_package (plugin, app, package);
	} else {
		g_warning ("Failed to find one package for %s, %s, [%u]",
			   gs_app_get_id (app), filename, packages->len);
	}
	return TRUE;
}

/*
 * gs_plugin_packagekit_fixup_update_description:
 *
 * Lets assume Fedora is sending us valid markdown, but fall back to
 * plain text if this fails.
 */
static gchar *
gs_plugin_packagekit_fixup_update_description (const gchar *text)
{
	gchar *tmp;
	g_autoptr(GsMarkdown) markdown = NULL;

	/* nothing to do */
	if (text == NULL)
		return NULL;

	/* try to parse */
	markdown = gs_markdown_new (GS_MARKDOWN_OUTPUT_TEXT);
	gs_markdown_set_smart_quoting (markdown, FALSE);
	gs_markdown_set_autocode (markdown, FALSE);
	gs_markdown_set_autolinkify (markdown, FALSE);
	tmp = gs_markdown_parse (markdown, text);
	if (tmp != NULL)
		return tmp;
	return g_strdup (text);
}

static gboolean
gs_plugin_packagekit_refine_updatedetails (GsPlugin *plugin,
					   GsAppList *list,
					   GCancellable *cancellable,
					   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *package_id;
	guint j;
	GsApp *app;
	guint cnt = 0;
	PkUpdateDetail *update_detail;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autofree const gchar **package_ids = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	package_ids = g_new0 (const gchar *, gs_app_list_length (list) + 1);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		package_id = gs_app_get_source_id_default (app);
		if (package_id != NULL)
			package_ids[cnt++] = package_id;
	}

	/* nothing to do */
	if (cnt == 0)
		return TRUE;

	/* get any update details */
	g_mutex_lock (&priv->client_mutex);
	results = pk_client_get_update_detail (priv->client,
					       (gchar **) package_ids,
					       cancellable,
					       gs_packagekit_helper_cb, helper,
					       error);
	g_mutex_unlock (&priv->client_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to get update details for %s: ",
				package_ids[0]);
		return FALSE;
	}

	/* set the update details for the update */
	array = pk_results_get_update_detail_array (results);
	for (j = 0; j < gs_app_list_length (list); j++) {
		app = gs_app_list_index (list, j);
		package_id = gs_app_get_source_id_default (app);
		for (guint i = 0; i < array->len; i++) {
			const gchar *tmp;
			g_autofree gchar *desc = NULL;
			/* right package? */
			update_detail = g_ptr_array_index (array, i);
			if (g_strcmp0 (package_id, pk_update_detail_get_package_id (update_detail)) != 0)
				continue;
			tmp = pk_update_detail_get_update_text (update_detail);
			desc = gs_plugin_packagekit_fixup_update_description (tmp);
			if (desc != NULL)
				gs_app_set_update_details (app, desc);
			break;
		}
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_details2 (GsPlugin *plugin,
				      GsAppList *list,
				      GCancellable *cancellable,
				      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GPtrArray *source_ids;
	GsApp *app;
	const gchar *package_id;
	guint i, j;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GPtrArray) package_ids = NULL;
	g_autoptr(PkResults) results = NULL;

	package_ids = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		source_ids = gs_app_get_source_ids (app);
		for (j = 0; j < source_ids->len; j++) {
			package_id = g_ptr_array_index (source_ids, j);
			g_ptr_array_add (package_ids, g_strdup (package_id));
		}
	}
	if (package_ids->len == 0)
		return TRUE;
	g_ptr_array_add (package_ids, NULL);

	/* get any details */
	g_mutex_lock (&priv->client_mutex);
	results = pk_client_get_details (priv->client,
					 (gchar **) package_ids->pdata,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&priv->client_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_autofree gchar *package_ids_str = g_strjoinv (",", (gchar **) package_ids->pdata);
		g_prefix_error (error, "failed to get details for %s: ",
		                package_ids_str);
		return FALSE;
	}

	/* set the update details for the update */
	array = pk_results_get_details_array (results);
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		gs_plugin_packagekit_refine_details_app (plugin, array, app);
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_update_urgency (GsPlugin *plugin,
					    GsAppList *list,
					    GsPluginRefineFlags flags,
					    GCancellable *cancellable,
					    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	guint i;
	GsApp *app;
	const gchar *package_id;
	PkBitfield filter;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkPackageSack) sack = NULL;
	g_autoptr(PkResults) results = NULL;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_SEVERITY) == 0)
		return TRUE;

	/* get the list of updates */
	filter = pk_bitfield_value (PK_FILTER_ENUM_NONE);
	g_mutex_lock (&priv->client_mutex);
	results = pk_client_get_updates (priv->client,
					 filter,
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&priv->client_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to get updates for urgency: ");
		return FALSE;
	}

	/* set the update severity for the app */
	sack = pk_results_get_package_sack (results);
	for (i = 0; i < gs_app_list_length (list); i++) {
		g_autoptr (PkPackage) pkg = NULL;
		app = gs_app_list_index (list, i);
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		package_id = gs_app_get_source_id_default (app);
		if (package_id == NULL)
			continue;
		pkg = pk_package_sack_find_by_id (sack, package_id);
		if (pkg == NULL)
			continue;
		switch (pk_package_get_info (pkg)) {
		case PK_INFO_ENUM_AVAILABLE:
		case PK_INFO_ENUM_NORMAL:
		case PK_INFO_ENUM_LOW:
		case PK_INFO_ENUM_ENHANCEMENT:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_LOW);
			break;
		case PK_INFO_ENUM_BUGFIX:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_MEDIUM);
			break;
		case PK_INFO_ENUM_SECURITY:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_CRITICAL);
			break;
		case PK_INFO_ENUM_IMPORTANT:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_HIGH);
			break;
		default:
			gs_app_set_update_urgency (app, AS_URGENCY_KIND_UNKNOWN);
			g_warning ("unhandled info state %s",
				   pk_info_enum_to_string (pk_package_get_info (pkg)));
			break;
		}
	}
	return TRUE;
}

static gboolean
gs_plugin_refine_app_needs_details (GsPlugin *plugin, GsPluginRefineFlags flags, GsApp *app)
{
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) > 0 &&
	    gs_app_get_license (app) == NULL)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL) > 0 &&
	    gs_app_get_url (app, AS_URL_KIND_HOMEPAGE) == NULL)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) > 0 &&
	    gs_app_get_size_installed (app) == 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) > 0 &&
	    gs_app_get_size_download (app) == 0)
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_packagekit_refine_details (GsPlugin *plugin,
				     GsAppList *list,
				     GsPluginRefineFlags flags,
				     GCancellable *cancellable,
				     GError **error)
{
	gboolean ret = TRUE;
	g_autoptr(GsAppList) list_tmp = NULL;

	list_tmp = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
			continue;
		if (gs_app_get_source_id_default (app) == NULL)
			continue;
		if (!gs_plugin_refine_app_needs_details (plugin, flags, app))
			continue;
		gs_app_list_add (list_tmp, app);
	}
	if (gs_app_list_length (list_tmp) == 0)
		return TRUE;
	ret = gs_plugin_packagekit_refine_details2 (plugin,
						    list_tmp,
						    cancellable,
						    error);
	if (!ret)
		return FALSE;
	return TRUE;
}

static gboolean
gs_plugin_refine_requires_version (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_version (app);
	if (tmp != NULL)
		return FALSE;
	return (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) > 0;
}

static gboolean
gs_plugin_refine_requires_update_details (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_update_details (app);
	if (tmp != NULL)
		return FALSE;
	return (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS) > 0;
}

static gboolean
gs_plugin_refine_requires_origin (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_origin (app);
	if (tmp != NULL)
		return FALSE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) > 0)
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_refine_requires_package_id (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_source_id_default (app);
	if (tmp != NULL)
		return FALSE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION) > 0)
		return TRUE;
	return FALSE;
}

static gboolean
gs_plugin_packagekit_refine_distro_upgrade (GsPlugin *plugin,
					    GsApp *app,
					    GCancellable *cancellable,
					    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	guint i;
	GsApp *app2;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GsAppList) list = NULL;
	guint cache_age_save;

	gs_packagekit_helper_add_app (helper, app);

	/* ask PK to simulate upgrading the system */
	g_mutex_lock (&priv->client_mutex);
	cache_age_save = pk_client_get_cache_age (priv->client);
	pk_client_set_cache_age (priv->client, 60 * 60 * 24 * 7); /* once per week */
	results = pk_client_upgrade_system (priv->client,
					    pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_SIMULATE, -1),
					    gs_app_get_version (app),
					    PK_UPGRADE_KIND_ENUM_COMPLETE,
					    cancellable,
					    gs_packagekit_helper_cb, helper,
					    error);
	pk_client_set_cache_age (priv->client, cache_age_save);
	g_mutex_unlock (&priv->client_mutex);

	if (!gs_plugin_packagekit_results_valid (results, error)) {
		g_prefix_error (error, "failed to refine distro upgrade: ");
		return FALSE;
	}
	list = gs_app_list_new ();
	if (!gs_plugin_packagekit_add_results (plugin, list, results, error))
		return FALSE;

	/* add each of these as related applications */
	for (i = 0; i < gs_app_list_length (list); i++) {
		app2 = gs_app_list_index (list, i);
		if (gs_app_get_state (app2) != AS_APP_STATE_UNAVAILABLE)
			continue;
		gs_app_add_related (app, app2);
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_valid_package_name (const gchar *source)
{
	if (g_strstr_len (source, -1, "/") != NULL)
		return FALSE;
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_name_to_id (GsPlugin *plugin,
					GsAppList *list,
					GsPluginRefineFlags flags,
					GCancellable *cancellable,
					GError **error)
{
	g_autoptr(GsAppList) resolve_all = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GPtrArray *sources;
		GsApp *app = gs_app_list_index (list, i);
		const gchar *tmp;
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		tmp = gs_app_get_management_plugin (app);
		if (tmp != NULL && g_strcmp0 (tmp, "packagekit") != 0)
			continue;
		sources = gs_app_get_sources (app);
		if (sources->len == 0)
			continue;
		tmp = g_ptr_array_index (sources, 0);
		if (!gs_plugin_packagekit_refine_valid_package_name (tmp))
			continue;
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN ||
		    gs_plugin_refine_requires_package_id (app, flags) ||
		    gs_plugin_refine_requires_origin (app, flags) ||
		    gs_plugin_refine_requires_version (app, flags)) {
			gs_app_list_add (resolve_all, app);
		}
	}
	if (gs_app_list_length (resolve_all) > 0) {
		if (!gs_plugin_packagekit_resolve_packages (plugin,
							    resolve_all,
							    cancellable,
							    error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_filename_to_id (GsPlugin *plugin,
					    GsAppList *list,
					    GsPluginRefineFlags flags,
					    GCancellable *cancellable,
					    GError **error)
{
	/* not now */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION) == 0)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		g_autofree gchar *fn = NULL;
		GsApp *app = gs_app_list_index (list, i);
		const gchar *tmp;
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		if (gs_app_get_source_id_default (app) != NULL)
			continue;
		tmp = gs_app_get_management_plugin (app);
		if (tmp != NULL && g_strcmp0 (tmp, "packagekit") != 0)
			continue;
		tmp = gs_app_get_id (app);
		if (tmp == NULL)
			continue;
		switch (gs_app_get_kind (app)) {
		case AS_APP_KIND_DESKTOP:
			fn = g_strdup_printf ("/usr/share/applications/%s", tmp);
			break;
		case AS_APP_KIND_ADDON:
			fn = g_strdup_printf ("/usr/share/metainfo/%s.metainfo.xml", tmp);
			if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
				g_free (fn);
				fn = g_strdup_printf ("/usr/share/appdata/%s.metainfo.xml", tmp);
			}
			break;
		default:
			break;
		}
		if (fn == NULL)
			continue;
		if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
			g_debug ("ignoring %s as does not exist", fn);
			continue;
		}
		if (!gs_plugin_packagekit_refine_from_desktop (plugin,
								app,
								fn,
								cancellable,
								error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_update_details (GsPlugin *plugin,
					    GsAppList *list,
					    GsPluginRefineFlags flags,
					    GCancellable *cancellable,
					    GError **error)
{
	g_autoptr(GsAppList) updatedetails_all = gs_app_list_new ();
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		const gchar *tmp;
		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE)
			continue;
		if (gs_app_get_source_id_default (app) == NULL)
			continue;
		tmp = gs_app_get_management_plugin (app);
		if (tmp != NULL && g_strcmp0 (tmp, "packagekit") != 0)
			continue;
		if (gs_plugin_refine_requires_update_details (app, flags))
			gs_app_list_add (updatedetails_all, app);
	}
	if (gs_app_list_length (updatedetails_all) > 0) {
		if (!gs_plugin_packagekit_refine_updatedetails (plugin,
								updatedetails_all,
								cancellable,
								error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GsAppList *list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	/* when we need the cannot-be-upgraded applications, we implement this
	 * by doing a UpgradeSystem(SIMULATE) which adds the removed packages
	 * to the related-apps list with a state of %AS_APP_STATE_UNAVAILABLE */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED) {
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			if (gs_app_get_kind (app) != AS_APP_KIND_OS_UPGRADE)
				continue;
			if (!gs_plugin_packagekit_refine_distro_upgrade (plugin,
									 app,
									 cancellable,
									 error))
				return FALSE;
		}
	}

	/* can we resolve in one go? */
	if (!gs_plugin_packagekit_refine_name_to_id (plugin, list, flags, cancellable, error))
		return FALSE;

	/* set the package-id for an installed desktop file */
	if (!gs_plugin_packagekit_refine_filename_to_id (plugin, list, flags, cancellable, error))
		return FALSE;

	/* any update details missing? */
	if (!gs_plugin_packagekit_refine_update_details (plugin, list, flags, cancellable, error))
		return FALSE;

	/* any package details missing? */
	if (!gs_plugin_packagekit_refine_details (plugin, list, flags, cancellable, error))
		return FALSE;

	/* get the update severity */
	if (!gs_plugin_packagekit_refine_update_urgency (plugin, list, flags, cancellable, error))
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
		return TRUE;

	/* the scope is always system-wide */
	if (gs_app_get_scope (app) == AS_APP_SCOPE_UNKNOWN)
		gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_UNKNOWN)
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);

	return TRUE;
}
