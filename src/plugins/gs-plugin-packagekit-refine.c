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

#include <config.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include <gs-plugin.h>
#include <gs-utils.h>
#include <glib/gi18n.h>

#include "packagekit-common.h"

struct GsPluginPrivate {
	PkClient		*client;
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
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->client = pk_client_new ();
	pk_client_set_background (plugin->priv->client, FALSE);
	pk_client_set_interactive (plugin->priv->client, FALSE);
	pk_client_set_cache_age (plugin->priv->client, G_MAXUINT);
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 150.0f;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->client);
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
		gs_profile_start_full (plugin->profile,
				       "packagekit-refine::transaction");
	} else if (status == PK_STATUS_ENUM_FINISHED) {
		gs_profile_stop_full (plugin->profile,
				      "packagekit-refine::transaction");
	}

	plugin_status = packagekit_status_enum_to_plugin_status (status);
	if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
		gs_plugin_status_update (plugin, NULL, plugin_status);
}

/**
 * gs_plugin_packagekit_resolve_packages_app:
 **/
static void
gs_plugin_packagekit_resolve_packages_app (GPtrArray *packages,
					   GsApp *app)
{
	GPtrArray *sources;
	PkPackage *package;
	const gchar *pkgname;
	gchar *tmp;
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
				//FIXME: this isn't going to work
				gs_app_set_metadata (app, "PackageKit::package-id",
						     pk_package_get_id (package));
				switch (pk_package_get_info (package)) {
				case GS_APP_STATE_INSTALLED:
					number_installed++;
					break;
				case GS_APP_STATE_AVAILABLE:
					number_available++;
					break;
				default:
					/* should we expect anything else? */
					break;
				}
				if (gs_app_get_version (app) == NULL)
					gs_app_set_version (app,
						pk_package_get_version (package));
			}
		}
	}

	/* if *all* the source packages for the app are installed then the
	 * application is considered completely installed */
	if (number_installed == sources->len && number_available == 0) {
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, GS_APP_STATE_INSTALLED);
	} else if (number_installed + number_available == sources->len) {
		/* if all the source packages are installed and all the rest
		 * of the packages are available then the app is available */
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
	} else if (number_installed + number_available > sources->len) {
		/* we have more packages returned than source packages */
		gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
		gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
	} else if (number_installed + number_available < sources->len) {
		/* we have less packages returned than source packages */
		tmp = gs_app_to_string (app);
		g_debug ("Failed to find all packages for:\n%s", tmp);
		g_free (tmp);
		gs_app_set_kind (app, GS_APP_KIND_UNKNOWN);
		gs_app_set_state (app, GS_APP_STATE_UNAVAILABLE);
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
	GPtrArray *array = NULL;
	GPtrArray *package_ids;
	GPtrArray *packages = NULL;
	GPtrArray *sources;
	GsApp *app;
	PkError *error_code = NULL;
	PkResults *results = NULL;
	const gchar *pkgname;
	gboolean ret = TRUE;
	guint i;

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
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to resolve: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		gs_plugin_packagekit_resolve_packages_app (packages, app);
	}
out:
	g_ptr_array_unref (package_ids);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

static gboolean
gs_plugin_packagekit_refine_from_desktop (GsPlugin *plugin,
					  GsApp	 *app,
					  const gchar *filename,
					  GCancellable *cancellable,
					  GError **error)
{
	const gchar *to_array[] = { NULL, NULL };
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
	GPtrArray *packages = NULL;
	PkError *error_code = NULL;
	PkPackage *package;
	PkResults *results = NULL;

	to_array[0] = filename;
	results = pk_client_search_files (plugin->priv->client,
					  pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1),
					  (gchar **) to_array,
					  cancellable,
					  gs_plugin_packagekit_progress_cb, plugin,
					  error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to search files: %s, %s",
			     pk_error_enum_to_string (pk_error_get_code (error_code)),
			     pk_error_get_details (error_code));
		goto out;
	}

	/* get results */
	packages = pk_results_get_package_array (results);
	if (packages->len == 1) {
		package = g_ptr_array_index (packages, 0);
		gs_app_set_metadata (app, "PackageKit::package-id", pk_package_get_id (package));
		gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		gs_app_set_management_plugin (app, "PackageKit");
	} else {
		g_warning ("Failed to find one package for %s, %s, [%d]",
			   gs_app_get_id (app), filename, packages->len);
	}
out:
	if (packages != NULL)
		g_ptr_array_unref (packages);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return ret;
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
	gboolean ret = TRUE;
	const gchar **package_ids;
	GList *l;
	GPtrArray *array = NULL;
	GsApp *app;
	guint i = 0;
	guint size;
	PkResults *results = NULL;
	PkUpdateDetail *update_detail;

	size = g_list_length (list);
	package_ids = g_new0 (const gchar *, size + 1);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		package_id = gs_app_get_metadata_item (app, "PackageKit::package-id");
		package_ids[i++] = package_id;
	}

	/* get any update details */
	results = pk_client_get_update_detail (plugin->priv->client,
					       (gchar **) package_ids,
					       cancellable,
					       gs_plugin_packagekit_progress_cb, plugin,
					       error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* set the update details for the update */
	array = pk_results_get_update_detail_array (results);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		package_id = gs_app_get_metadata_item (app, "PackageKit::package-id");
		for (i = 0; i < array->len; i++) {
			/* right package? */
			update_detail = g_ptr_array_index (array, i);
			if (g_strcmp0 (package_id, pk_update_detail_get_package_id (update_detail)) != 0)
				continue;
			gs_app_set_update_details (app, pk_update_detail_get_update_text (update_detail));
			break;
		}
		if (gs_app_get_update_details (app) == NULL) {
			/* TRANSLATORS: this is where update details either are
			 * no longer available or were never provided in the first place */
			gs_app_set_update_details (app, _("No update details were provided"));
		}
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	g_free (package_ids);
	return ret;
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
	gchar **split1;
	gchar **split2;
	gboolean ret;

	split1 = pk_package_id_split (package_id1);
	split2 = pk_package_id_split (package_id2);
	ret = (g_strcmp0 (split1[PK_PACKAGE_ID_NAME],
			  split2[PK_PACKAGE_ID_NAME]) == 0 &&
	       g_strcmp0 (split1[PK_PACKAGE_ID_VERSION],
			  split2[PK_PACKAGE_ID_VERSION]) == 0 &&
	       g_strcmp0 (split1[PK_PACKAGE_ID_ARCH],
			  split2[PK_PACKAGE_ID_ARCH]) == 0);
	g_strfreev (split1);
	g_strfreev (split2);
	return ret;
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
	GPtrArray *array = NULL;
	GsApp *app;
	PkDetails *details;
	PkResults *results = NULL;
	const gchar **package_ids;
	const gchar *package_id;
	gboolean ret = TRUE;
	gchar *desc;
	guint i = 0;
	guint size;
#if !PK_CHECK_VERSION(0,8,12)
	gchar *tmp;
	guint64 size_tmp;
	gboolean matches;
#endif

	size = g_list_length (list);
	package_ids = g_new0 (const gchar *, size + 1);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		package_id = gs_app_get_metadata_item (app, "PackageKit::package-id");
		package_ids[i++] = package_id;
	}

	/* get any details */
	results = pk_client_get_details (plugin->priv->client,
					 (gchar **) package_ids,
					 cancellable,
					 gs_plugin_packagekit_progress_cb, plugin,
					 error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* set the update details for the update */
	array = pk_results_get_details_array (results);
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		package_id = gs_app_get_metadata_item (app, "PackageKit::package-id");
		for (i = 0; i < array->len; i++) {
			/* right package? */
			details = g_ptr_array_index (array, i);
#if PK_CHECK_VERSION(0,8,12)
			if (!gs_pk_compare_ids (package_id,
						pk_details_get_package_id (details)) != 0) {
				continue;
			}
			if (gs_app_get_licence (app) == NULL)
				gs_app_set_licence (app, pk_details_get_license (details));
			if (gs_app_get_url (app, GS_APP_URL_KIND_HOMEPAGE) == NULL) {
				gs_app_set_url (app,
						GS_APP_URL_KIND_HOMEPAGE,
						pk_details_get_url (details));
			}
			if (gs_app_get_size (app) == 0)
				gs_app_set_size (app, pk_details_get_size (details));
			if (gs_app_get_description (app) == NULL &&
			    g_getenv ("GNOME_SOFTWARE_USE_PKG_DESCRIPTIONS") != NULL) {
				desc = gs_pk_format_desc (pk_details_get_description (details));
				gs_app_set_description (app, desc);
				g_free (desc);
			}
#else
			g_object_get (details, "package-id", &tmp, NULL);
			matches = gs_pk_compare_ids (package_id, tmp);
			g_free (tmp);
			if (!matches)
				continue;
			if (gs_app_get_licence (app) == NULL) {
				g_object_get (details, "license", &tmp, NULL);
				gs_app_set_licence (app, tmp);
				g_free (tmp);
			}
			if (gs_app_get_url (app, GS_APP_URL_KIND_HOMEPAGE) == NULL) {
				g_object_get (details, "url", &tmp, NULL);
				gs_app_set_url (app, GS_APP_URL_KIND_HOMEPAGE, tmp);
				g_free (tmp);
			}
			if (gs_app_get_size (app) == 0) {
				g_object_get (details, "size", &size_tmp, NULL);
				gs_app_set_size (app, size_tmp);
			}
			if (gs_app_get_description (app) == NULL &&
			    g_getenv ("GNOME_SOFTWARE_USE_PKG_DESCRIPTIONS") != NULL) {
				g_object_get (details, "description", &tmp, NULL);
				desc = gs_pk_format_desc (tmp);
				g_free (tmp);
				gs_app_set_description (app, desc);
				g_free (desc);
			}
#endif
			break;
		}
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	g_free (package_ids);
	return ret;
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
	GList *list_tmp = NULL;
	GsApp *app;
	gboolean ret = TRUE;

	gs_profile_start_full (plugin->profile, "packagekit-refine[source->licence]");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_licence (app) != NULL &&
		    gs_app_get_url (app, GS_APP_URL_KIND_HOMEPAGE) != NULL &&
		    gs_app_get_size (app) != 0 &&
		    (gs_app_get_description (app) != NULL ||
		     g_getenv ("GNOME_SOFTWARE_USE_PKG_DESCRIPTIONS") == NULL))
			continue;
		if (gs_app_get_metadata_item (app, "PackageKit::package-id") == NULL)
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
	gs_profile_stop_full (plugin->profile, "packagekit-refine[source->licence]");
	g_list_free (list_tmp);
	return ret;
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
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	GList *l;
	GList *resolve_all = NULL;
	GList *updatedetails_all = NULL;
	GPtrArray *sources;
	GsApp *app;
	const gchar *tmp;
	gboolean ret = TRUE;

	/* can we resolve in one go? */
	gs_profile_start_full (plugin->profile, "packagekit-refine[name->id]");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_metadata_item (app, "PackageKit::package-id") != NULL)
			continue;
		if (gs_app_get_id_kind (app) == GS_APP_ID_KIND_WEBAPP)
			continue;
		sources = gs_app_get_sources (app);
		if (sources->len == 0)
			continue;
		if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN ||
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
	gs_profile_stop_full (plugin->profile, "packagekit-refine[name->id]");

	/* set the package-id for an installed desktop file */
	gs_profile_start_full (plugin->profile, "packagekit-refine[desktop-filename->id]");
	for (l = list; l != NULL; l = l->next) {
		if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION) == 0)
			continue;
		app = GS_APP (l->data);
		if (gs_app_get_metadata_item (app, "PackageKit::package-id") != NULL)
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
	gs_profile_stop_full (plugin->profile, "packagekit-refine[desktop-filename->id]");

	/* any update details missing? */
	gs_profile_start_full (plugin->profile, "packagekit-refine[id->update-details]");
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_state (app) != GS_APP_STATE_UPDATABLE)
			continue;
		if (gs_app_get_update_details (app) != NULL)
			continue;
		if (gs_app_get_metadata_item (app, "PackageKit::package-id") == NULL)
			continue;
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
	gs_profile_stop_full (plugin->profile, "packagekit-refine[id->update-details]");

	/* any important details missing? */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENCE) > 0 ||
	    (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL) > 0 ||
	    (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) > 0 ||
	    (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION) > 0) {
		ret = gs_plugin_refine_require_details (plugin,
							list,
							cancellable,
							error);
		if (!ret)
			goto out;
	}
out:
	g_list_free (resolve_all);
	g_list_free (updatedetails_all);
	return ret;
}
