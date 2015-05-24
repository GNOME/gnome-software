/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include "gs-cleanup.h"
#include <gs-plugin.h>
#include <gs-utils.h>
#include <glib/gi18n.h>

#include "packagekit-common.h"

struct GsPluginPrivate {
	PkControl		*control;
	PkClient		*client;
	GHashTable		*sources;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "packagekit-refine";
}

/**
 * gs_plugin_initialize:
 */
static void
gs_plugin_packagekit_cache_invalid_cb (PkControl *control, GsPlugin *plugin)
{
	gs_plugin_updates_changed (plugin);
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->client = pk_client_new ();
	plugin->priv->control = pk_control_new ();
	g_signal_connect (plugin->priv->control, "updates-changed",
			  G_CALLBACK (gs_plugin_packagekit_cache_invalid_cb), plugin);
	g_signal_connect (plugin->priv->control, "repo-list-changed",
			  G_CALLBACK (gs_plugin_packagekit_cache_invalid_cb), plugin);
	pk_client_set_background (plugin->priv->client, FALSE);
	pk_client_set_interactive (plugin->priv->client, FALSE);
	pk_client_set_cache_age (plugin->priv->client, G_MAXUINT);
	plugin->priv->sources = g_hash_table_new_full (g_str_hash,
						       g_str_equal,
						       g_free,
						       g_free);
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",		/* need pkgname */
		"packagekit",		/* need package_id */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_hash_table_unref (plugin->priv->sources);
	g_object_unref (plugin->priv->client);
	g_object_unref (plugin->priv->control);
}

/**
 * gs_plugin_packagekit_progress_cb:
 **/
static void
gs_plugin_packagekit_progress_cb (PkProgress *progress,
				  PkProgressType type,
				  gpointer user_data)
{
	GsPluginStatus plugin_status;
	PkStatusEnum status;
	GsPlugin *plugin = GS_PLUGIN (user_data);

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;
	g_object_get (progress,
		      "status", &status,
		      NULL);

	/* profile */
	if (status == PK_STATUS_ENUM_SETUP) {
		gs_profile_start (plugin->profile,
				  "packagekit-refine::transaction");
	} else if (status == PK_STATUS_ENUM_FINISHED) {
		gs_profile_stop (plugin->profile,
				 "packagekit-refine::transaction");
	}

	plugin_status = packagekit_status_enum_to_plugin_status (status);
	if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
		gs_plugin_status_update (plugin, NULL, plugin_status);
}

/**
 * gs_plugin_packagekit_set_origin:
 **/
static void
gs_plugin_packagekit_set_origin (GsPlugin *plugin,
				 GsApp *app,
				 const gchar *id)
{
	const gchar *name;
	name = g_hash_table_lookup (plugin->priv->sources, id);
	if (name != NULL)
		gs_app_set_origin (app, name);
	else
		gs_app_set_origin (app, id);
}

/**
 * gs_plugin_packagekit_resolve_packages_app:
 **/
static void
gs_plugin_packagekit_resolve_packages_app (GsPlugin *plugin,
					   GPtrArray *packages,
					   GsApp *app)
{
	GPtrArray *sources;
	PkPackage *package;
	const gchar *data;
	const gchar *pkgname;
	guint i, j;
	guint number_available = 0;
	guint number_installed = 0;

	/* find any packages that match the package name */
	number_installed = 0;
	number_available = 0;
	sources = gs_app_get_sources (app);
	for (j = 0; j < sources->len; j++) {
		pkgname = g_ptr_array_index (sources, j);
		for (i = 0; i < packages->len; i++) {
			package = g_ptr_array_index (packages, i);
			if (g_strcmp0 (pk_package_get_name (package), pkgname) == 0) {
				gs_app_set_management_plugin (app, "PackageKit");
				gs_app_add_source_id (app, pk_package_get_id (package));
				switch (pk_package_get_info (package)) {
				case PK_INFO_ENUM_INSTALLED:
					number_installed++;
					data = pk_package_get_data (package);
					if (g_str_has_prefix (data, "installed:")) {
						gs_plugin_packagekit_set_origin (plugin,
										 app,
										 data + 10);
					}
					break;
				case PK_INFO_ENUM_AVAILABLE:
					number_available++;
					break;
#if PK_CHECK_VERSION(1,0,4)
				case PK_INFO_ENUM_UNAVAILABLE:
					data = pk_package_get_data (package);
					gs_plugin_packagekit_set_origin (plugin, app, data);
					gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);
					gs_app_set_size (app, GS_APP_SIZE_MISSING);
					number_available++;
					break;
#endif
				default:
					/* should we expect anything else? */
					break;
				}
				if (gs_app_get_version (app) == NULL)
					gs_app_set_version (app,
						pk_package_get_version (package));
				gs_app_set_name (app,
						 GS_APP_QUALITY_LOWEST,
						 pk_package_get_name (package));
				gs_app_set_summary (app,
						    GS_APP_QUALITY_LOWEST,
						    pk_package_get_summary (package));
			}
		}
	}

	/* if *all* the source packages for the app are installed then the
	 * application is considered completely installed */
	if (number_installed == sources->len && number_available == 0) {
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	} else if (number_installed + number_available == sources->len) {
		/* if all the source packages are installed and all the rest
		 * of the packages are available then the app is available */
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	} else if (number_installed + number_available > sources->len) {
		/* we have more packages returned than source packages */
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	} else if (number_installed + number_available < sources->len) {
		_cleanup_free_ gchar *tmp = NULL;
		/* we have less packages returned than source packages */
		tmp = gs_app_to_string (app);
		g_debug ("Failed to find all packages for:\n%s", tmp);
		gs_app_set_kind (app, GS_APP_KIND_UNKNOWN);
		gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);
	}
}

/**
 * gs_plugin_packagekit_resolve_packages:
 **/
static gboolean
gs_plugin_packagekit_resolve_packages (GsPlugin *plugin,
				       GList *list,
				       GCancellable *cancellable,
				       GError **error)
{
	GList *l;
	GPtrArray *sources;
	GsApp *app;
	const gchar *pkgname;
	guint i;
	_cleanup_object_unref_ PkError *error_code = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *package_ids = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *packages = NULL;

	package_ids = g_ptr_array_new_with_free_func (g_free);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		sources = gs_app_get_sources (app);
		for (i = 0; i < sources->len; i++) {
			pkgname = g_ptr_array_index (sources, i);
			g_ptr_array_add (package_ids, g_strdup (pkgname));
		}
	}
	g_ptr_array_add (package_ids, NULL);

	/* resolve them all at once */
	results = pk_client_resolve (plugin->priv->client,
				     pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_ARCH, -1),
				     (gchar **) package_ids->pdata,
				     cancellable,
				     gs_plugin_packagekit_progress_cb, plugin,
				     error);
	if (results == NULL)
		return FALSE;

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to resolve: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		gs_plugin_packagekit_resolve_packages_app (plugin, packages, app);
	}
	return TRUE;
}

static gboolean
gs_plugin_packagekit_refine_from_desktop (GsPlugin *plugin,
					  GsApp	 *app,
					  const gchar *filename,
					  GCancellable *cancellable,
					  GError **error)
{
	const gchar *to_array[] = { NULL, NULL };
	_cleanup_object_unref_ PkError *error_code = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *packages = NULL;

	to_array[0] = filename;
	results = pk_client_search_files (plugin->priv->client,
					  pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
					  (gchar **) to_array,
					  cancellable,
					  gs_plugin_packagekit_progress_cb, plugin,
					  error);
	if (results == NULL)
		return FALSE;

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to search files: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		return FALSE;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (packages->len == 1) {
		PkPackage *package;
		package = g_ptr_array_index (packages, 0);
		gs_app_add_source_id (app, pk_package_get_id (package));
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_set_management_plugin (app, "PackageKit");
	} else {
		g_warning ("Failed to find one package for %s, %s, [%d]",
			   gs_app_get_id (app), filename, packages->len);
	}
	return TRUE;
}

/**
 * gs_plugin_packagekit_refine_updatedetails:
 */
static gboolean
gs_plugin_packagekit_refine_updatedetails (GsPlugin *plugin,
					   GList *list,
					   GCancellable *cancellable,
					   GError **error)
{
	const gchar *package_id;
	GList *l;
	GsApp *app;
	guint i = 0;
	guint size;
	PkUpdateDetail *update_detail;
	_cleanup_free_ const gchar **package_ids = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

	size = g_list_length (list);
	package_ids = g_new0 (const gchar *, size + 1);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		package_id = gs_app_get_source_id_default (app);
		package_ids[i++] = package_id;
	}

	/* get any update details */
	results = pk_client_get_update_detail (plugin->priv->client,
					       (gchar **) package_ids,
					       cancellable,
					       gs_plugin_packagekit_progress_cb, plugin,
					       error);
	if (results == NULL)
		return FALSE;

	/* set the update details for the update */
	array = pk_results_get_update_detail_array (results);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		package_id = gs_app_get_source_id_default (app);
		for (i = 0; i < array->len; i++) {
			/* right package? */
			update_detail = g_ptr_array_index (array, i);
			if (g_strcmp0 (package_id, pk_update_detail_get_package_id (update_detail)) != 0)
				continue;
			gs_app_set_update_details (app, pk_update_detail_get_update_text (update_detail));
			break;
		}
	}
	return TRUE;
}

/**
 * gs_pk_format_desc:
 */
static gchar *
gs_pk_format_desc (const gchar *text)
{
	GString *str;
	str = g_string_new (text);
	gs_string_replace (str, "\n", " ");
	gs_string_replace (str, ".  ", ".\n\n");
	return g_string_free (str, FALSE);
}

/**
 * gs_pk_compare_ids:
 *
 * Do not compare the repo. Some backends do not append the origin.
 */
static gboolean
gs_pk_compare_ids (const gchar *package_id1, const gchar *package_id2)
{
	gboolean ret;
	_cleanup_strv_free_ gchar **split1 = NULL;
	_cleanup_strv_free_ gchar **split2 = NULL;

	split1 = pk_package_id_split (package_id1);
	split2 = pk_package_id_split (package_id2);
	ret = (g_strcmp0 (split1[PK_PACKAGE_ID_NAME],
			  split2[PK_PACKAGE_ID_NAME]) == 0 &&
	       g_strcmp0 (split1[PK_PACKAGE_ID_VERSION],
			  split2[PK_PACKAGE_ID_VERSION]) == 0 &&
	       g_strcmp0 (split1[PK_PACKAGE_ID_ARCH],
			  split2[PK_PACKAGE_ID_ARCH]) == 0);
	return ret;
}

/**
 * gs_plugin_packagekit_refine_details_app:
 */
static void
gs_plugin_packagekit_refine_details_app (GsPlugin *plugin,
					 GPtrArray *array,
					 GsApp *app)
{
	GPtrArray *source_ids;
	PkDetails *details;
	const gchar *package_id;
	guint i;
	guint j;
	guint64 size = 0;

	source_ids = gs_app_get_source_ids (app);
	for (j = 0; j < source_ids->len; j++) {
		package_id = g_ptr_array_index (source_ids, j);
		for (i = 0; i < array->len; i++) {
			_cleanup_free_ gchar *desc = NULL;
			/* right package? */
			details = g_ptr_array_index (array, i);
			if (!gs_pk_compare_ids (package_id,
						pk_details_get_package_id (details))) {
				continue;
			}
			if (gs_app_get_licence (app) == NULL)
				gs_app_set_licence (app, pk_details_get_license (details));
			if (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE) == NULL) {
				gs_app_set_url (app,
						AS_URL_KIND_HOMEPAGE,
						pk_details_get_url (details));
			}
			size += pk_details_get_size (details);
			desc = gs_pk_format_desc (pk_details_get_description (details));
			gs_app_set_description (app,
						GS_APP_QUALITY_LOWEST,
						desc);
			gs_app_set_summary (app,
					    GS_APP_QUALITY_LOWEST,
					    pk_details_get_summary (details));
			break;
		}
	}

	/* the size is the size of all sources */
	if (size > 0 && gs_app_get_size (app) == 0)
		gs_app_set_size (app, size);
}

/**
 * gs_plugin_packagekit_refine_details:
 */
static gboolean
gs_plugin_packagekit_refine_details (GsPlugin *plugin,
				     GList *list,
				     GCancellable *cancellable,
				     GError **error)
{
	GList *l;
	GPtrArray *source_ids;
	GsApp *app;
	const gchar *package_id;
	guint i;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *package_ids = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

	package_ids = g_ptr_array_new_with_free_func (g_free);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		source_ids = gs_app_get_source_ids (app);
		for (i = 0; i < source_ids->len; i++) {
			package_id = g_ptr_array_index (source_ids, i);
			g_ptr_array_add (package_ids, g_strdup (package_id));
		}
	}
	g_ptr_array_add (package_ids, NULL);

	/* get any details */
	results = pk_client_get_details (plugin->priv->client,
					 (gchar **) package_ids->pdata,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, plugin,
					 error);
	if (results == NULL)
		return FALSE;

	/* set the update details for the update */
	array = pk_results_get_details_array (results);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		gs_plugin_packagekit_refine_details_app (plugin, array, app);
	}
	return TRUE;
}

/**
 * gs_plugin_refine_app_needs_details:
 */
static gboolean
gs_plugin_refine_app_needs_details (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_licence (app) == NULL)
		return TRUE;
	if (gs_app_get_url (app, AS_URL_KIND_HOMEPAGE) == NULL)
		return TRUE;
	if (gs_app_get_size (app) == GS_APP_SIZE_UNKNOWN)
		return TRUE;
	if (gs_app_get_description (app) == NULL && plugin->use_pkg_descriptions)
		return TRUE;
	return FALSE;
}

/**
 * gs_plugin_refine_require_details:
 */
static gboolean
gs_plugin_refine_require_details (GsPlugin *plugin,
				  GList *list,
				  GCancellable *cancellable,
				  GError **error)
{
	GList *l;
	GsApp *app;
	gboolean ret = TRUE;
	_cleanup_list_free_ GList *list_tmp = NULL;

	gs_profile_start (plugin->profile, "packagekit-refine[source->licence]");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_id_kind (app) == AS_ID_KIND_WEB_APP)
			continue;
		if (gs_app_get_source_id_default (app) == NULL)
			continue;
		if (!gs_plugin_refine_app_needs_details (plugin, app))
			continue;
		list_tmp = g_list_prepend (list_tmp, app);
	}
	if (list_tmp == NULL)
		goto out;
	ret = gs_plugin_packagekit_refine_details (plugin,
						   list_tmp,
						   cancellable,
						   error);
	if (!ret)
		goto out;
out:
	gs_profile_stop (plugin->profile, "packagekit-refine[source->licence]");
	return ret;
}

/**
 * gs_plugin_packagekit_get_source_list:
 **/
static gboolean
gs_plugin_packagekit_get_source_list (GsPlugin *plugin,
				      GCancellable *cancellable,
				      GError **error)
{
	PkRepoDetail *rd;
	guint i;
	_cleanup_object_unref_ PkResults *results = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

	/* ask PK for the repo details */
	results = pk_client_get_repo_list (plugin->priv->client,
					   pk_bitfield_from_enums (PK_FILTER_ENUM_NONE, -1),
					   cancellable,
					   gs_plugin_packagekit_progress_cb, plugin,
					   error);
	if (results == NULL)
		return FALSE;
	array = pk_results_get_repo_detail_array (results);
	for (i = 0; i < array->len; i++) {
		rd = g_ptr_array_index (array, i);
		g_hash_table_insert (plugin->priv->sources,
				     g_strdup (pk_repo_detail_get_id (rd)),
				     g_strdup (pk_repo_detail_get_description (rd)));
	}
	return TRUE;
}

/**
 * gs_plugin_refine_requires_version:
 */
static gboolean
gs_plugin_refine_requires_version (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_version (app);
	if (tmp != NULL)
		return FALSE;
	return (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) > 0;
}

/**
 * gs_plugin_refine_requires_update_details:
 */
static gboolean
gs_plugin_refine_requires_update_details (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_update_details (app);
	if (tmp != NULL)
		return FALSE;
	return (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS) > 0;
}

/**
 * gs_plugin_refine_requires_package_id:
 */
static gboolean
gs_plugin_refine_requires_package_id (GsApp *app, GsPluginRefineFlags flags)
{
	const gchar *tmp;
	tmp = gs_app_get_source_id_default (app);
	if (tmp != NULL)
		return FALSE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) > 0)
		return TRUE;
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENCE) > 0)
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
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) > 0)
		return TRUE;
	return FALSE;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList **list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	GList *l;
	GPtrArray *sources;
	GsApp *app;
	const gchar *profile_id = NULL;
	const gchar *tmp;
	gboolean ret = TRUE;
	_cleanup_list_free_ GList *resolve_all = NULL;
	_cleanup_list_free_ GList *updatedetails_all = NULL;

	/* get the repo_id -> repo_name mapping set up */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) > 0 &&
	     g_hash_table_size (plugin->priv->sources) == 0) {
		ret = gs_plugin_packagekit_get_source_list (plugin,
							    cancellable,
							    error);
		if (!ret)
			goto out;
	}

	/* can we resolve in one go? */
	profile_id = "packagekit-refine[name->id]";
	gs_profile_start (plugin->profile, profile_id);
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_id_kind (app) == AS_ID_KIND_WEB_APP)
			continue;
		sources = gs_app_get_sources (app);
		if (sources->len == 0)
			continue;
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN ||
		    gs_plugin_refine_requires_package_id (app, flags) ||
		    gs_plugin_refine_requires_version (app, flags)) {
			resolve_all = g_list_prepend (resolve_all, app);
		}
	}
	if (resolve_all != NULL) {
		ret = gs_plugin_packagekit_resolve_packages (plugin,
							     resolve_all,
							     cancellable,
							     error);
		if (!ret)
			goto out;
	}
	gs_profile_stop (plugin->profile, profile_id);
	profile_id = NULL;

	/* set the package-id for an installed desktop file */
	profile_id = "packagekit-refine[desktop-filename->id]";
	gs_profile_start (plugin->profile, profile_id);
	for (l = *list; l != NULL; l = l->next) {
		if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION) == 0)
			continue;
		app = GS_APP (l->data);
		if (gs_app_get_source_id_default (app) != NULL)
			continue;
		tmp = gs_app_get_metadata_item (app, "DataDir::desktop-filename");
		if (tmp == NULL)
			continue;
		ret = gs_plugin_packagekit_refine_from_desktop (plugin,
								app,
								tmp,
								cancellable,
								error);
		if (!ret)
			goto out;
	}
	gs_profile_stop (plugin->profile, profile_id);
	profile_id = NULL;

	/* any update details missing? */
	profile_id = "packagekit-refine[id->update-details]";
	gs_profile_start (plugin->profile, profile_id);
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE)
			continue;
		if (gs_plugin_refine_requires_update_details (app, flags))
			updatedetails_all = g_list_prepend (updatedetails_all, app);
	}
	if (updatedetails_all != NULL) {
		ret = gs_plugin_packagekit_refine_updatedetails (plugin,
								 updatedetails_all,
								 cancellable,
								 error);
		if (!ret)
			goto out;
	}
	gs_profile_stop (plugin->profile, profile_id);
	profile_id = NULL;

	/* any important details missing? */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENCE) > 0 ||
	    (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL) > 0 ||
	    (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) > 0 ||
	    (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION) > 0) {
		ret = gs_plugin_refine_require_details (plugin,
							*list,
							cancellable,
							error);
		if (!ret)
			goto out;
	}
out:
	if (profile_id != NULL)
		gs_profile_stop (plugin->profile, profile_id);
	return ret;
}
