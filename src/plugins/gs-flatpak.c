/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Joaquim Rocha <jrocha@endlessm.com>
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

/* Notes:
 *
 * All GsApp's created have management-plugin set to flatpak
 * Some GsApp's created have have flatpak::kind of app or runtime
 * The GsApp:origin is the remote name, e.g. test-repo
 */

#include <config.h>

#include <flatpak.h>
#include <gnome-software.h>

#include "gs-appstream.h"
#include "gs-flatpak.h"

static void
gs_plugin_flatpak_changed_cb (GFileMonitor *monitor,
			      GFile *child,
			      GFile *other_file,
			      GFileMonitorEvent event_type,
			      GsPlugin *plugin)
{
	gs_plugin_updates_changed (plugin);
}

gboolean
gs_flatpak_setup (GsPlugin *plugin,
		  GsFlatpakType type,
		  FlatpakInstallation **installation,
		  GFileMonitor **monitor,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *destdir;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* we use a permissions helper to elevate privs */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "flatpak::ensure-origin");
	destdir = g_getenv ("GS_SELF_TEST_FLATPACK_DATADIR");
	if (destdir != NULL) {
		g_autofree gchar *full_path = g_build_filename (destdir,
								"flatpak",
								NULL);
		g_autoptr(GFile) file = g_file_new_for_path (full_path);
		g_debug ("using custom flatpak path %s", full_path);
		*installation = flatpak_installation_new_for_path (file, TRUE,
								   cancellable,
								   error);
	} else if (type == GS_FLATPAK_TYPE_SYSTEM) {
		*installation = flatpak_installation_new_system (cancellable,
								 error);
	} else if (type == GS_FLATPAK_TYPE_USER) {
		*installation = flatpak_installation_new_user (cancellable,
							       error);
	}

	if (*installation == NULL)
		return FALSE;

	/* watch for changes */
	*monitor = flatpak_installation_create_monitor (*installation,
							cancellable,
							error);
	if (*monitor == NULL)
		return FALSE;
	g_signal_connect (*monitor, "changed",
			  G_CALLBACK (gs_plugin_flatpak_changed_cb), plugin);

	/* success */
	return TRUE;
}

static gboolean
gs_flatpak_refresh_appstream (GsPlugin *plugin,
			      FlatpakInstallation *installation,
			      guint cache_age,
			      GCancellable *cancellable,
			      GError **error)
{
	gboolean ret;
	guint i;
	g_autoptr(GPtrArray) xremotes = NULL;

	xremotes = flatpak_installation_list_remotes (installation, cancellable,
						      error);
	if (xremotes == NULL)
		return FALSE;
	for (i = 0; i < xremotes->len; i++) {
		guint tmp;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GFile) file = NULL;
		g_autoptr(GFile) file_timestamp = NULL;
		g_autofree gchar *appstream_fn = NULL;
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);

		/* not enabled */
		if (flatpak_remote_get_disabled (xremote))
			continue;

		/* skip known-broken repos */
		if (g_strcmp0 (flatpak_remote_get_name (xremote), "gnome-sdk") == 0)
			continue;
		if (g_strcmp0 (flatpak_remote_get_name (xremote), "test-apps") == 0)
			continue;

		/* is the timestamp new enough */
		file_timestamp = flatpak_remote_get_appstream_timestamp (xremote, NULL);
		tmp = gs_utils_get_file_age (file_timestamp);
		if (tmp < cache_age) {
			g_autofree gchar *fn = g_file_get_path (file_timestamp);
			g_debug ("%s is only %i seconds old, so ignoring refresh",
				 fn, tmp);
			continue;
		}

		/* download new data */
		g_debug ("%s is %i seconds old, so downloading new data",
			 flatpak_remote_get_name (xremote), tmp);
		ret = flatpak_installation_update_appstream_sync (installation,
								  flatpak_remote_get_name (xremote),
								  NULL, /* arch */
								  NULL, /* out_changed */
								  cancellable,
								  &error_local);
		if (!ret) {
			if (g_error_matches (error_local,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED)) {
				g_debug ("Failed to get AppStream metadata: %s",
					 error_local->message);
				continue;
			}
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "Failed to get AppStream metadata: %s",
				     error_local->message);
			return FALSE;
		}

		/* add the new AppStream repo to the shared store */
		file = flatpak_remote_get_appstream_dir (xremote, NULL);
		appstream_fn = g_file_get_path (file);
		g_debug ("using AppStream metadata found at: %s", appstream_fn);
	}
	return TRUE;
}

static void
gs_app_set_flatpak_kind (GsApp *app, FlatpakRefKind kind)
{
	if (kind == FLATPAK_REF_KIND_APP)
		gs_app_set_metadata (app, "flatpak::kind", "app");
	else if (kind == FLATPAK_REF_KIND_RUNTIME)
		gs_app_set_metadata (app, "flatpak::kind", "runtime");
	else
		g_assert_not_reached ();
}

static void
gs_flatpak_set_metadata (GsPlugin *plugin, GsApp *app, FlatpakRef *xref)
{
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_set_flatpak_kind (app, flatpak_ref_get_kind (xref));
	gs_app_set_flatpak_name (app, flatpak_ref_get_name (xref));
	gs_app_set_flatpak_arch (app, flatpak_ref_get_arch (xref));
	gs_app_set_flatpak_branch (app, flatpak_ref_get_branch (xref));
	gs_app_set_flatpak_commit (app, flatpak_ref_get_commit (xref));
}

static void
gs_flatpak_set_metadata_installed (GsPlugin *plugin,
				   GsApp *app,
				   FlatpakInstalledRef *xref)
{
	guint64 mtime;
	guint64 size_installed;
	g_autofree gchar *metadata_fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	/* for all types */
	gs_flatpak_set_metadata (plugin, app, FLATPAK_REF (xref));
	gs_app_set_metadata (app, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));

	/* get the last time the app was updated */
	metadata_fn = g_build_filename (flatpak_installed_ref_get_deploy_dir (xref),
					"..",
					"active",
					NULL);
	file = g_file_new_for_path (metadata_fn);
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_TIME_MODIFIED,
				  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				  NULL, NULL);
	if (info != NULL) {
		mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
		gs_app_set_install_date (app, mtime);
	}

	/* this is faster than resolving */
	gs_app_set_origin (app, flatpak_installed_ref_get_origin (xref));

	/* this is faster than flatpak_installation_fetch_remote_size_sync() */
	size_installed = flatpak_installed_ref_get_installed_size (xref);
	if (size_installed != 0)
		gs_app_set_size_installed (app, size_installed);
}

static gchar *
gs_flatpak_build_id (FlatpakInstallation *installation, FlatpakRef *xref)
{
	const gchar *prefix = GS_FLATPAK_SYSTEM_PREFIX;

	/* use a different prefix if we're somehow running this as per-user */
	if (flatpak_installation_get_is_user (installation))
		prefix = GS_FLATPAK_USER_PREFIX;

	/* flatpak doesn't use a suffix; AppStream does */
	if (flatpak_ref_get_kind (xref) == FLATPAK_REF_KIND_APP) {
		return g_strdup_printf ("%s:%s.desktop",
					prefix,
					flatpak_ref_get_name (xref));
	}
	return g_strdup_printf ("%s:%s.runtime",
				prefix,
				flatpak_ref_get_name (xref));
}

static GsApp *
gs_flatpak_create_installed (GsPlugin *plugin,
			     FlatpakInstallation *installation,
			     FlatpakInstalledRef *xref,
			     GError **error)
{
	g_autofree gchar *id = NULL;
	g_autoptr(AsIcon) icon = NULL;
	g_autoptr(GsApp) app = NULL;

	g_return_val_if_fail (xref != NULL, NULL);

	/*
	 * Only show the current application in GNOME Software
	 *
	 * You can have multiple versions/branches of a particular app-id
	 * installed but only one of them is "current" where this means:
	 *  1) the default to launch unless you specify a version
	 *  2) The one that gets its exported files exported
	 */
	if (!flatpak_installed_ref_get_is_current (xref) &&
	    flatpak_ref_get_kind (FLATPAK_REF(xref)) == FLATPAK_REF_KIND_APP) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "%s not current, ignoring",
			     flatpak_ref_get_name (FLATPAK_REF (xref)));
		return NULL;
	}

	/* create new object */
	id = gs_flatpak_build_id (installation, FLATPAK_REF (xref));
	app = gs_plugin_cache_lookup (plugin, id);
	if (app == NULL) {
		app = gs_app_new (id);
		gs_plugin_cache_add (plugin, id, app);
	}
	gs_flatpak_set_metadata_installed (plugin, app, xref);

	switch (flatpak_ref_get_kind (FLATPAK_REF(xref))) {
	case FLATPAK_REF_KIND_APP:
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
		break;
	case FLATPAK_REF_KIND_RUNTIME:
		gs_app_set_flatpak_kind (app, FLATPAK_REF_KIND_RUNTIME);
		gs_app_set_kind (app, AS_APP_KIND_RUNTIME);
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
				 flatpak_ref_get_name (FLATPAK_REF (xref)));
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
				    "Framework for applications");
		gs_app_set_version (app, flatpak_ref_get_branch (FLATPAK_REF (xref)));
		icon = as_icon_new ();
		as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
		as_icon_set_name (icon, "system-run-symbolic");
		gs_app_add_icon (app, icon);
		break;
	default:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "FlatpakRefKind not known");
		return NULL;
	}
	return g_object_ref (app);
}

gboolean
gs_flatpak_add_installed (GsPlugin *plugin,
			  FlatpakInstallation *installation,
			  GsAppList *list,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autoptr(GError) error_md = NULL;
	g_autoptr(GPtrArray) xrefs = NULL;
	guint i;

	/* if we've never ever run before, get the AppStream data */
	if (!gs_flatpak_refresh_appstream (plugin,
					   installation,
					   G_MAXUINT,
					   cancellable,
					   &error_md)) {
		g_warning ("failed to get initial available data: %s",
			   error_md->message);
	}

	/* get apps and runtimes */
	xrefs = flatpak_installation_list_installed_refs (installation,
							  cancellable, error);
	if (xrefs == NULL)
		return FALSE;
	for (i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GsApp) app = NULL;

		/* only apps */
		if (flatpak_ref_get_kind (FLATPAK_REF (xref)) != FLATPAK_REF_KIND_APP)
			continue;

		app = gs_flatpak_create_installed (plugin,
						   installation,
						   xref,
						   &error_local);
		if (app == NULL) {
			g_warning ("failed to add flatpak: %s", error_local->message);
			continue;
		}
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_flatpak_add_sources (GsPlugin *plugin,
			FlatpakInstallation *installation,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(GPtrArray) xrefs = NULL;
	g_autoptr(GPtrArray) xremotes = NULL;
	guint i;
	guint j;

	/* get installed apps and runtimes */
	xrefs = flatpak_installation_list_installed_refs (installation,
							  cancellable,
							  error);
	if (xrefs == NULL)
		return FALSE;

	/* get available remotes */
	xremotes = flatpak_installation_list_remotes (installation,
						      cancellable,
						      error);
	if (xremotes == NULL)
		return FALSE;
	for (i = 0; i < xremotes->len; i++) {
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		g_autoptr(GsApp) app = NULL;

		/* apps installed from bundles add their own remote that only
		 * can be used for updating that app only -- so hide them */
		if (flatpak_remote_get_noenumerate (xremote))
			continue;

		/* not enabled */
		if (flatpak_remote_get_disabled (xremote))
			continue;

		app = gs_app_new (flatpak_remote_get_name (xremote));
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		gs_app_set_kind (app, AS_APP_KIND_SOURCE);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_set_name (app,
				 GS_APP_QUALITY_LOWEST,
				 flatpak_remote_get_name (xremote));
		gs_app_set_summary (app,
				    GS_APP_QUALITY_LOWEST,
				    flatpak_remote_get_title (xremote));
		gs_app_set_url (app,
				AS_URL_KIND_HOMEPAGE,
				flatpak_remote_get_url (xremote));
		gs_app_list_add (list, app);

		/* add related apps, i.e. what was installed from there */
		for (j = 0; j < xrefs->len; j++) {
			FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, j);
			g_autoptr(GError) error_local = NULL;
			g_autoptr(GsApp) related = NULL;

			/* only apps */
			if (flatpak_ref_get_kind (FLATPAK_REF (xref)) != FLATPAK_REF_KIND_APP)
				continue;
			if (g_strcmp0 (flatpak_installed_ref_get_origin (xref),
				       flatpak_remote_get_name (xremote)) != 0)
				continue;
			related = gs_flatpak_create_installed (plugin,
							       installation,
							       xref,
							       &error_local);
			if (related == NULL) {
				g_warning ("failed to add flatpak: %s",
					   error_local->message);
				continue;
			}
			gs_app_set_state (related, AS_APP_STATE_INSTALLED);
			gs_app_add_related (app, related);
		}
	}
	return TRUE;
}

gboolean
gs_flatpak_add_source (GsPlugin *plugin,
		       FlatpakInstallation *installation,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	const gchar *gpg_key;
	g_autoptr(FlatpakRemote) xremote = NULL;

	/* only process this source if was created for this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* create a new remote */
	xremote = flatpak_remote_new (gs_app_get_id (app));
	flatpak_remote_set_url (xremote, gs_app_get_url (app, AS_URL_KIND_HOMEPAGE));
	if (gs_app_get_summary (app) != NULL)
		flatpak_remote_set_title (xremote, gs_app_get_summary (app));

	/* decode GPG key if set */
	gpg_key = gs_app_get_metadata_item (app, "flatpak::gpg-key");
	if (gpg_key != NULL) {
		gsize data_len = 0;
		g_autofree guchar *data = NULL;
		g_autoptr(GBytes) bytes = NULL;
		data = g_base64_decode (gpg_key, &data_len);
		bytes = g_bytes_new (data, data_len);
		flatpak_remote_set_gpg_verify (xremote, TRUE);
		flatpak_remote_set_gpg_key (xremote, bytes);
	} else {
		flatpak_remote_set_gpg_verify (xremote, FALSE);
	}

	/* install it */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!flatpak_installation_modify_remote (installation,
						 xremote,
						 cancellable,
						 error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* success */
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

gboolean
gs_flatpak_add_updates (GsPlugin *plugin,
			FlatpakInstallation *installation,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	guint i;
	g_autoptr(GPtrArray) xrefs = NULL;

	/* manually drop the cache */
	if (0&&!flatpak_installation_drop_caches (installation,
						  cancellable,
						  error)) {
		return FALSE;
	}

	/* get all the installed apps (no network I/O) */
	xrefs = flatpak_installation_list_installed_refs (installation,
							  cancellable,
							  error);
	if (xrefs == NULL)
		return FALSE;
	for (i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);
		const gchar *commit;
		const gchar *latest_commit;
		g_autoptr(GsApp) app = NULL;
		g_autoptr(GError) error_local = NULL;

		/* check the application has already been downloaded */
		commit = flatpak_ref_get_commit (FLATPAK_REF (xref));
		latest_commit = flatpak_installed_ref_get_latest_commit (xref);
		if (g_strcmp0 (commit, latest_commit) == 0) {
			g_debug ("no downloaded update for %s",
				 flatpak_ref_get_name (FLATPAK_REF (xref)));
			continue;
		}

		/* we have an update to show */
		g_debug ("%s has a downloaded update %s->%s",
			 flatpak_ref_get_name (FLATPAK_REF (xref)),
			 commit, latest_commit);
		app = gs_flatpak_create_installed (plugin, installation,
						   xref, &error_local);
		if (app == NULL) {
			g_warning ("failed to add flatpak: %s", error_local->message);
			continue;
		}
		if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
			gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

static void
gs_flatpak_progress_cb (const gchar *status,
			guint progress,
			gboolean estimating,
			gpointer user_data)
{
	GsApp *app = GS_APP (user_data);
	gs_app_set_progress (app, progress);
}

gboolean
gs_flatpak_refresh (GsPlugin *plugin,
		    FlatpakInstallation *installation,
		    guint cache_age,
		    GsPluginRefreshFlags flags,
		    GCancellable *cancellable,
		    GError **error)
{
	guint i;
	g_autoptr(GPtrArray) xrefs = NULL;

	/* update AppStream metadata */
	if (flags & GS_PLUGIN_REFRESH_FLAGS_METADATA) {
		if (!gs_flatpak_refresh_appstream (plugin, installation,
						   cache_age, cancellable,
						   error))
			return FALSE;
	}

	/* no longer interesting */
	if ((flags & GS_PLUGIN_REFRESH_FLAGS_PAYLOAD) == 0)
		return TRUE;

	/* get all the updates available from all remotes */
	xrefs = flatpak_installation_list_installed_refs_for_update (installation,
								     cancellable,
								     error);
	if (xrefs == NULL)
		return FALSE;
	for (i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);
		g_autoptr(GsApp) app = NULL;
		g_autoptr(FlatpakInstalledRef) xref2 = NULL;

		/* try to create a GsApp so we can do progress reporting */
		app = gs_flatpak_create_installed (plugin, installation, xref,
						   NULL);

		/* fetch but do not deploy */
		g_debug ("pulling update for %s",
			 flatpak_ref_get_name (FLATPAK_REF (xref)));
		xref2 = flatpak_installation_update (installation,
						     FLATPAK_UPDATE_FLAGS_NO_DEPLOY,
						     flatpak_ref_get_kind (FLATPAK_REF (xref)),
						     flatpak_ref_get_name (FLATPAK_REF (xref)),
						     flatpak_ref_get_arch (FLATPAK_REF (xref)),
						     flatpak_ref_get_branch (FLATPAK_REF (xref)),
						     gs_flatpak_progress_cb, app,
						     cancellable, error);
		if (xref2 == NULL)
			return FALSE;
	}

	return TRUE;
}

static gboolean
gs_plugin_refine_item_origin_ui (GsPlugin *plugin,
				 FlatpakInstallation *installation,
				 GsApp *app,
				 GCancellable *cancellable,
				 GError **error)
{
	const gchar *origin;
	guint i;
	g_autoptr(GPtrArray) xremotes = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* already set */
	origin = gs_app_get_origin_ui (app);
	if (origin != NULL)
		return TRUE;

	/* find list of remotes */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "flatpak::refine-origin-ui");
	xremotes = flatpak_installation_list_remotes (installation,
						      cancellable,
						      error);
	if (xremotes == NULL)
		return FALSE;
	for (i = 0; i < xremotes->len; i++) {
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		if (flatpak_remote_get_disabled (xremote))
			continue;
		if (g_strcmp0 (gs_app_get_origin (app),
			       flatpak_remote_get_name (xremote)) == 0) {
			gs_app_set_origin_ui (app, flatpak_remote_get_title (xremote));
			break;
		}
	}

	return TRUE;
}

static gboolean
gs_refine_item_metadata (GsPlugin *plugin,
			 GsApp *app,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autoptr(FlatpakRef) xref = NULL;

	/* already set */
	if (gs_app_get_metadata_item (app, "flatpak::kind") != NULL)
		return TRUE;

	/* not a valid type */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
		return TRUE;

	/* AppStream sets the source to appname/arch/branch, if this isn't set
	 * we can't break out the fields */
	if (gs_app_get_source_default (app) == NULL) {
		g_autofree gchar *tmp = gs_app_to_string (app);
		g_warning ("no source set by appstream for %s: %s",
			   gs_plugin_get_name (plugin), tmp);
		return TRUE;
	}

	/* parse the ref */
	xref = flatpak_ref_parse (gs_app_get_source_default (app), error);
	if (xref == NULL) {
		g_prefix_error (error, "failed to parse '%s': ",
				gs_app_get_source_default (app));
		return FALSE;
	}
	gs_flatpak_set_metadata (plugin, app, xref);

	/* success */
	return TRUE;
}

static FlatpakRefKind
gs_app_get_flatpak_kind (GsApp *app)
{
	const gchar *kind = gs_app_get_metadata_item (app, "flatpak::kind");
	if (g_strcmp0 (kind, "app") == 0)
		return FLATPAK_REF_KIND_APP;
	if (g_strcmp0 (kind, "runtime") == 0)
		return FLATPAK_REF_KIND_RUNTIME;
	g_warning ("unknown flatpak kind: %s", kind);
	return FLATPAK_REF_KIND_APP;
}

static gboolean
gs_plugin_refine_item_origin (GsPlugin *plugin,
			      FlatpakInstallation *installation,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	guint i;
	g_autoptr(GPtrArray) xremotes = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* already set */
	if (gs_app_get_origin (app) != NULL)
		return TRUE;

	/* ensure metadata exists */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "flatpak::refine-origin");
	if (!gs_refine_item_metadata (plugin, app, cancellable, error))
		return FALSE;

	/* find list of remotes */
	g_debug ("looking for a remote for %s/%s/%s",
		 gs_app_get_flatpak_name (app),
		 gs_app_get_flatpak_arch (app),
		 gs_app_get_flatpak_branch (app));
	xremotes = flatpak_installation_list_remotes (installation, cancellable,
						      error);
	if (xremotes == NULL)
		return FALSE;
	for (i = 0; i < xremotes->len; i++) {
		const gchar *remote_name;
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		g_autoptr(FlatpakRemoteRef) xref = NULL;

		/* not enabled */
		if (flatpak_remote_get_disabled (xremote))
			continue;

		/* sync */
		remote_name = flatpak_remote_get_name (xremote);
		g_debug ("looking at remote %s", remote_name);
		xref = flatpak_installation_fetch_remote_ref_sync (installation,
								   remote_name,
								   gs_app_get_flatpak_kind (app),
								   gs_app_get_flatpak_name (app),
								   gs_app_get_flatpak_arch (app),
								   gs_app_get_flatpak_branch (app),
								   cancellable,
								   NULL);
		if (xref != NULL) {
			g_debug ("found remote %s", remote_name);
			gs_app_set_origin (app, remote_name);
			return TRUE;
		}
	}
	g_set_error (error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_NOT_SUPPORTED,
		     "Not found %s/%s/%s",
		     gs_app_get_flatpak_name (app),
		     gs_app_get_flatpak_arch (app),
		     gs_app_get_flatpak_branch (app));
	return FALSE;
}

static gboolean
gs_flatpak_app_matches_xref (GsPlugin *plugin,
			     FlatpakInstallation *installation,
			     GsApp *app,
			     FlatpakRef *xref)
{
	g_autofree gchar *id = NULL;

	/* check ID */
	id = gs_flatpak_build_id (installation, xref);
	if (g_strcmp0 (id, gs_app_get_id (app)) == 0)
		return TRUE;

	/* do all the metadata items match? */
	if (g_strcmp0 (gs_app_get_flatpak_name (app),
		       flatpak_ref_get_name (xref)) == 0 &&
	    g_strcmp0 (gs_app_get_flatpak_arch (app),
		       flatpak_ref_get_arch (xref)) == 0 &&
	    g_strcmp0 (gs_app_get_flatpak_branch (app),
		       flatpak_ref_get_branch (xref)) == 0)
		return TRUE;

	/* sad panda */
	return FALSE;
}

static FlatpakRef *
gs_flatpak_create_fake_ref (GsApp *app, GError **error)
{
	g_autofree gchar *id = NULL;
	id = g_strdup_printf ("%s/%s/%s/%s",
			      gs_app_get_flatpak_kind_as_str (app),
			      gs_app_get_flatpak_name (app),
			      gs_app_get_flatpak_arch (app),
			      gs_app_get_flatpak_branch (app));
	return flatpak_ref_parse (id, error);
}

static gboolean
gs_plugin_refine_item_state (GsPlugin *plugin,
			     FlatpakInstallation *installation,
			     GsApp *app,
			     GCancellable *cancellable,
			     GError **error)
{
	guint i;
	g_autoptr(GPtrArray) xrefs = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* already found */
	if (gs_app_get_state (app) != AS_APP_STATE_UNKNOWN)
		return TRUE;

	/* need broken out metadata */
	if (!gs_refine_item_metadata (plugin, app, cancellable, error))
		return FALSE;

	/* get apps and runtimes */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "flatpak::refine-action");
	xrefs = flatpak_installation_list_installed_refs (installation,
							  cancellable, error);
	if (xrefs == NULL)
		return FALSE;
	for (i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);

		/* check xref is app */
		if (!gs_flatpak_app_matches_xref (plugin, installation, app,
						  FLATPAK_REF(xref)))
			continue;

		/* mark as installed */
		g_debug ("marking %s as installed with flatpak",
			 gs_app_get_id (app));
		gs_flatpak_set_metadata_installed (plugin, app, xref);
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	}

	/* ensure origin set */
	if (!gs_plugin_refine_item_origin (plugin, installation, app,
					   cancellable, error))
		return FALSE;

	/* anything not installed just check the remote is still present */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN &&
	    gs_app_get_origin (app) != NULL) {
		g_autoptr(FlatpakRemote) xremote = NULL;
		xremote = flatpak_installation_get_remote_by_name (installation,
								   gs_app_get_origin (app),
								   cancellable, NULL);
		if (xremote != NULL) {
			if (flatpak_remote_get_disabled (xremote)) {
				g_debug ("%s is available with flatpak "
					 "but %s is disabled",
					 gs_app_get_id (app),
					 flatpak_remote_get_name (xremote));
				gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);
			} else {
				g_debug ("marking %s as available with flatpak",
					 gs_app_get_id (app));
				gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
			}
		} else {
			g_warning ("failed to find flatpak user remote %s for %s",
				   gs_app_get_origin (app),
				   gs_app_get_id (app));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
gs_flatpak_set_app_metadata (GsPlugin *plugin,
			     GsApp *app,
			     const gchar *data,
			     gsize length,
			     GError **error)
{
	g_autofree gchar *name = NULL;
	g_autofree gchar *runtime = NULL;
	g_autofree gchar *source = NULL;
	g_autoptr(GKeyFile) kf = NULL;
	g_autoptr(GsApp) app_runtime = NULL;

	kf = g_key_file_new ();
	if (!g_key_file_load_from_data (kf, data, length, G_KEY_FILE_NONE, error))
		return FALSE;
	name = g_key_file_get_string (kf, "Application", "name", error);
	if (name == NULL)
		return FALSE;
	gs_app_set_flatpak_name (app, name);
	runtime = g_key_file_get_string (kf, "Application", "runtime", error);
	if (runtime == NULL)
		return FALSE;
	g_debug ("runtime for %s is %s", name, runtime);

	/* create runtime */
	app_runtime = gs_appstream_create_runtime (plugin, app, runtime);
	if (app_runtime != NULL)
		gs_app_set_runtime (app, app_runtime);

	return TRUE;
}

static gboolean
gs_plugin_refine_item_runtime (GsPlugin *plugin,
			       FlatpakInstallation *installation,
			       GsApp *app,
			       GCancellable *cancellable,
			       GError **error)
{
	const gchar *str;
	gsize len = -1;
	g_autofree gchar *contents = NULL;
	g_autofree gchar *installation_path_str = NULL;
	g_autofree gchar *install_path = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GFile) installation_path = NULL;

	/* not applicable */
	if (gs_app_get_flatpak_kind (app) != FLATPAK_REF_KIND_APP)
		return TRUE;

	/* already exists */
	if (gs_app_get_runtime (app) != NULL)
		return TRUE;

	/* this is quicker than doing network IO */
	installation_path = flatpak_installation_get_path (installation);
	installation_path_str = g_file_get_path (installation_path);
	install_path = g_build_filename (installation_path_str,
					 gs_app_get_flatpak_kind_as_str (app),
					 gs_app_get_flatpak_name (app),
					 gs_app_get_flatpak_arch (app),
					 gs_app_get_flatpak_branch (app),
					 "active",
					 "metadata",
					 NULL);
	if (g_file_test (install_path, G_FILE_TEST_EXISTS)) {
		if (!g_file_get_contents (install_path, &contents, &len, error))
			return FALSE;
		str = contents;
	} else {
		g_autoptr(FlatpakRef) xref = NULL;

		/* fetch from the server */
		xref = gs_flatpak_create_fake_ref (app, error);
		if (xref == NULL)
			return FALSE;
		data = flatpak_installation_fetch_remote_metadata_sync (installation,
									gs_app_get_origin (app),
									xref,
									cancellable,
									error);
		if (data == NULL)
			return FALSE;
		str = g_bytes_get_data (data, &len);
	}

	/* parse key file */
	if (!gs_flatpak_set_app_metadata (plugin, app, str, len, error))
		return FALSE;
	return TRUE;
}

static gboolean
gs_plugin_refine_item_size (GsPlugin *plugin,
			    FlatpakInstallation *installation,
			    GsApp *app,
			    GCancellable *cancellable,
			    GError **error)
{
	gboolean ret;
	guint64 download_size;
	guint64 installed_size;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FlatpakRef) xref = NULL;
	g_autoptr(GError) error_local = NULL;

	/* already set */
	if (gs_app_get_size_installed (app) > 0 &&
	    gs_app_get_size_download (app) > 0)
		return TRUE;

	/* need runtime */
	if (!gs_plugin_refine_item_runtime (plugin, installation, app,
					    cancellable, error))
		return FALSE;

	/* calculate the platform size too if the app is not installed */
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE &&
	    gs_app_get_flatpak_kind (app) == FLATPAK_REF_KIND_APP) {
		GsApp *app_runtime;

		/* find out what runtime the application depends on */
		if (!gs_plugin_refine_item_runtime (plugin,
						    installation,
						    app,
						    cancellable,
						    error))
			return FALSE;

		/* is the app_runtime already installed? */
		app_runtime = gs_app_get_runtime (app);
		if (!gs_plugin_refine_item_state (plugin,
						  installation,
						  app_runtime,
						  cancellable,
						  error))
			return FALSE;
		if (gs_app_get_state (app_runtime) == AS_APP_STATE_INSTALLED) {
			g_debug ("runtime %s is already installed, so not adding size",
				 gs_app_get_id (app_runtime));
		} else {
			if (!gs_plugin_refine_item_size (plugin,
							 installation,
							 app_runtime,
							 cancellable,
							 error))
				return FALSE;
		}
	}

	/* just get the size of the app */
	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "flatpak::refine-size");
	if (!gs_plugin_refine_item_origin (plugin, installation, app,
					   cancellable, error))
		return FALSE;
	xref = gs_flatpak_create_fake_ref (app, error);
	if (xref == NULL)
		return FALSE;
	ret = flatpak_installation_fetch_remote_size_sync (installation,
							   gs_app_get_origin (app),
							   xref,
							   &download_size,
							   &installed_size,
							   cancellable, &error_local);
	if (!ret) {
		g_warning ("libflatpak failed to return application size: %s",
			   error_local->message);
		gs_app_set_size_installed (app, GS_APP_SIZE_UNKNOWABLE);
		gs_app_set_size_download (app, GS_APP_SIZE_UNKNOWABLE);
	} else {
		gs_app_set_size_installed (app, installed_size);
		gs_app_set_size_download (app, download_size);
	}
	return TRUE;
}

gboolean
gs_flatpak_refine_app (GsPlugin *plugin,
		       FlatpakInstallation *installation,
		       GsApp *app,
		       GsPluginRefineFlags flags,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(AsProfileTask) ptask = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (plugin),
				  "flatpak::refine{%s}",
				  gs_app_get_id (app));

	/* flatpak apps can always be removed */
	gs_app_remove_quirk (app, AS_APP_QUIRK_COMPULSORY);

	/* AppStream sets the source to appname/arch/branch */
	if (!gs_refine_item_metadata (plugin, app, cancellable, error))
		return FALSE;

	/* check the installed state */
	if (!gs_plugin_refine_item_state (plugin, installation, app,
					  cancellable, error))
		return FALSE;

	/* version fallback */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) {
		if (gs_app_get_version (app) == NULL) {
			const gchar *branch;
			branch = gs_app_get_flatpak_branch (app);
			gs_app_set_version (app, branch);
		}
	}

	/* size */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) {
		if (!gs_plugin_refine_item_size (plugin, installation, app,
						 cancellable, error))
			return FALSE;
	}

	/* origin */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) {
		if (!gs_plugin_refine_item_origin_ui (plugin, installation, app,
						      cancellable, error))
			return FALSE;
	}

	return TRUE;
}

gboolean
gs_flatpak_launch (GsPlugin *plugin,
		   FlatpakInstallation *installation,
		   GsApp *app,
		   GCancellable *cancellable,
		   GError **error)
{
	const gchar *branch = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	branch = gs_app_get_flatpak_branch (app);
	if (branch == NULL)
		branch = "master";
	return flatpak_installation_launch (installation,
					    gs_app_get_flatpak_name (app),
					    NULL,
					    branch,
					    NULL,
					    cancellable,
					    error);
}

gboolean
gs_flatpak_app_remove (GsPlugin *plugin,
		       FlatpakInstallation *installation,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* refine to get basics */
	if (!gs_flatpak_refine_app (plugin, installation, app,
				    GS_PLUGIN_REFINE_FLAGS_DEFAULT,
				    cancellable, error))
		return FALSE;

	/* remove */
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	if (!flatpak_installation_uninstall (installation,
					     FLATPAK_REF_KIND_APP,
					     gs_app_get_flatpak_name (app),
					     gs_app_get_flatpak_arch (app),
					     gs_app_get_flatpak_branch (app),
					     gs_flatpak_progress_cb, app,
					     cancellable, error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is not known: we don't know if we can re-install this app */
	gs_app_set_state (app, AS_APP_STATE_UNKNOWN);

	/* refresh the state */
	if (!gs_plugin_refine_item_state (plugin, installation, app,
					  cancellable, error))
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
gs_flatpak_app_install (GsPlugin *plugin,
			FlatpakInstallation *installation,
			GsApp *app,
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(FlatpakInstalledRef) xref = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* ensure we have metadata and state */
	if (!gs_flatpak_refine_app (plugin, installation, app, 0, cancellable,
				    error))
		return FALSE;

	/* install */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);

	/* install required runtime if not already installed */
	if (gs_app_get_kind (app) == AS_APP_KIND_DESKTOP) {
		GsApp *runtime;
		runtime = gs_app_get_runtime (app);

		/* the runtime could come from a different remote to the app */
		if (!gs_refine_item_metadata (plugin, runtime, cancellable,
					      error))
			return FALSE;
		if (!gs_plugin_refine_item_origin (plugin, installation,
						   runtime, cancellable, error))
			return FALSE;
		if (!gs_plugin_refine_item_state (plugin, installation, runtime,
						  cancellable, error))
			return FALSE;
		if (gs_app_get_state (runtime) == AS_APP_STATE_UNKNOWN) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "Failed to find runtime %s",
				     gs_app_get_source_default (runtime));
			return FALSE;
		}

		/* not installed */
		if (gs_app_get_state (runtime) == AS_APP_STATE_AVAILABLE) {
			g_debug ("%s is not already installed, so installing",
				 gs_app_get_id (runtime));
			gs_app_set_state (runtime, AS_APP_STATE_INSTALLING);
			xref = flatpak_installation_install (installation,
							     gs_app_get_origin (runtime),
							     gs_app_get_flatpak_kind (runtime),
							     gs_app_get_flatpak_name (runtime),
							     gs_app_get_flatpak_arch (runtime),
							     gs_app_get_flatpak_branch (runtime),
							     gs_flatpak_progress_cb, app,
							     cancellable, error);
			if (xref == NULL) {
				gs_app_set_state_recover (runtime);
				return FALSE;
			}
			gs_app_set_state (runtime, AS_APP_STATE_INSTALLED);
		} else {
			g_debug ("%s is already installed, so skipping",
				 gs_app_get_id (runtime));
		}
	}

	/* use the source for local apps */
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE_LOCAL) {
		xref = flatpak_installation_install_bundle (installation,
							    gs_app_get_local_file (app),
							    gs_flatpak_progress_cb,
							    app,
							    cancellable, error);
	} else {
		g_debug ("installing %s", gs_app_get_id (app));
		xref = flatpak_installation_install (installation,
						     gs_app_get_origin (app),
						     gs_app_get_flatpak_kind (app),
						     gs_app_get_flatpak_name (app),
						     gs_app_get_flatpak_arch (app),
						     gs_app_get_flatpak_branch (app),
						     gs_flatpak_progress_cb, app,
						     cancellable, error);
	}
	if (xref == NULL) {
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

gboolean
gs_flatpak_update_app (GsPlugin *plugin,
		       FlatpakInstallation *installation,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(FlatpakInstalledRef) xref = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* install */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	xref = flatpak_installation_update (installation,
					    FLATPAK_UPDATE_FLAGS_NONE,
					    gs_app_get_flatpak_kind (app),
					    gs_app_get_flatpak_name (app),
					    gs_app_get_flatpak_arch (app),
					    gs_app_get_flatpak_branch (app),
					    gs_flatpak_progress_cb, app,
					    cancellable, error);
	if (xref == NULL) {
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

static gboolean
gs_flatpak_file_to_app_bundle (GsPlugin *plugin,
			       FlatpakInstallation *installation,
			       GsAppList *list,
			       GFile *file,
			       GCancellable *cancellable,
			       GError **error)
{
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *id_prefixed = NULL;
	g_autoptr(GBytes) appstream_gz = NULL;
	g_autoptr(GBytes) icon_data = NULL;
	g_autoptr(GBytes) metadata = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(FlatpakBundleRef) xref_bundle = NULL;

	/* load bundle */
	xref_bundle = flatpak_bundle_ref_new (file, error);
	if (xref_bundle == NULL) {
		g_prefix_error (error, "error loading bundle: ");
		return FALSE;
	}

	/* create a virtual ID */
	id_prefixed = gs_flatpak_build_id (installation,
					   FLATPAK_REF (xref_bundle));

	/* load metadata */
	app = gs_app_new (id_prefixed);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_set_size_installed (app, flatpak_bundle_ref_get_installed_size (xref_bundle));
	gs_flatpak_set_metadata (plugin, app, FLATPAK_REF (xref_bundle));
	metadata = flatpak_bundle_ref_get_metadata (xref_bundle);
	if (!gs_flatpak_set_app_metadata (plugin, app,
					  g_bytes_get_data (metadata, NULL),
					  g_bytes_get_size (metadata),
					  error))
		return FALSE;

	/* load AppStream */
	appstream_gz = flatpak_bundle_ref_get_appstream (xref_bundle);
	if (appstream_gz != NULL) {
		g_autoptr(GZlibDecompressor) decompressor = NULL;
		g_autoptr(GInputStream) stream_gz = NULL;
		g_autoptr(GInputStream) stream_data = NULL;
		g_autoptr(GBytes) appstream = NULL;
		g_autoptr(AsStore) store = NULL;
		g_autofree gchar *id = NULL;
		AsApp *item;

		/* decompress data */
		decompressor = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
		stream_gz = g_memory_input_stream_new_from_bytes (appstream_gz);
		if (stream_gz == NULL)
			return FALSE;
		stream_data = g_converter_input_stream_new (stream_gz,
							    G_CONVERTER (decompressor));

		appstream = g_input_stream_read_bytes (stream_data,
						       0x100000, /* 1Mb */
						       cancellable,
						       error);
		if (appstream == NULL)
			return FALSE;
		store = as_store_new ();
		if (!as_store_from_bytes (store, appstream, cancellable, error))
			return FALSE;

		/* find app */
		id = g_strdup_printf ("%s.desktop", gs_app_get_flatpak_name (app));
		item = as_store_get_app_by_id (store, id);
		if (item == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "application %s not found",
				     id);
			return FALSE;
		}

		/* copy details from AppStream to app */
		if (!gs_appstream_refine_app (plugin, app, item, error))
			return FALSE;
	}

	/* load icon */
	icon_data = flatpak_bundle_ref_get_icon (xref_bundle,
						 64 * gs_plugin_get_scale (plugin));
	if (icon_data == NULL)
		icon_data = flatpak_bundle_ref_get_icon (xref_bundle, 64);
	if (icon_data != NULL) {
		g_autoptr(GInputStream) stream_icon = NULL;
		g_autoptr(GdkPixbuf) pixbuf = NULL;
		stream_icon = g_memory_input_stream_new_from_bytes (icon_data);
		pixbuf = gdk_pixbuf_new_from_stream (stream_icon, cancellable, error);
		if (pixbuf == NULL)
			return FALSE;
		gs_app_set_pixbuf (app, pixbuf);
	} else {
		g_autoptr(AsIcon) icon = NULL;
		icon = as_icon_new ();
		as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
		as_icon_set_name (icon, "application-x-executable");
		gs_app_add_icon (app, icon);
	}

	/* not quite true: this just means we can update this specific app */
	if (flatpak_bundle_ref_get_origin (xref_bundle))
		gs_app_add_quirk (app, AS_APP_QUIRK_HAS_SOURCE);

	g_debug ("created local app: %s", gs_app_to_string (app));
	gs_app_list_add (list, app);
	return TRUE;
}

static gboolean
gs_flatpak_file_to_app_repo (GsPlugin *plugin,
			     FlatpakInstallation *installation,
			     GsAppList *list,
			     GFile *file,
			     GCancellable *cancellable,
			     GError **error)
{
	gchar *tmp;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *repo_comment = NULL;
	g_autofree gchar *repo_description = NULL;
	g_autofree gchar *repo_gpgkey = NULL;
	g_autofree gchar *repo_homepage = NULL;
	g_autofree gchar *repo_icon = NULL;
	g_autofree gchar *repo_title = NULL;
	g_autofree gchar *repo_url = NULL;
	g_autofree gchar *repo_id = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GKeyFile) kf = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(FlatpakRemote) xremote = NULL;

	/* read the file */
	kf = g_key_file_new ();
	filename = g_file_get_path (file);
	if (!g_key_file_load_from_file (kf, filename,
					G_KEY_FILE_NONE,
					&error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "failed to load flatpakrepo: %s",
			     error_local->message);
		return FALSE;
	}

	/* get the ID from the basename */
	repo_id = g_file_get_basename (file);
	tmp = g_strrstr (repo_id, ".");
	if (tmp != NULL)
		*tmp = '\0';

	/* create source */
	repo_title = g_key_file_get_string (kf, "Flatpak Repo", "Title", NULL);
	repo_url = g_key_file_get_string (kf, "Flatpak Repo", "Url", NULL);
	repo_gpgkey = g_key_file_get_string (kf, "Flatpak Repo", "GPGKey", NULL);
	if (repo_title == NULL || repo_url == NULL || repo_gpgkey == NULL ||
	    repo_title[0] == '\0' || repo_url[0] == '\0' || repo_gpgkey[0] == '\0') {
		g_set_error_literal (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "not enough data in file, expected Title, Url, GPGKey");
		return FALSE;
	}
	app = gs_app_new (repo_id);
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, repo_title);
	gs_app_set_metadata (app, "flatpak::gpg-key", repo_gpgkey);

	/* optional data */
	repo_homepage = g_key_file_get_string (kf, "Flatpak Repo", "Homepage", NULL);
	if (repo_homepage != NULL)
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, repo_homepage);
	repo_comment = g_key_file_get_string (kf, "Flatpak Repo", "Comment", NULL);
	if (repo_comment != NULL)
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, repo_comment);
	repo_description = g_key_file_get_string (kf, "Flatpak Repo", "Description", NULL);
	if (repo_description != NULL)
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, repo_description);
	repo_icon = g_key_file_get_string (kf, "Flatpak Repo", "Icon", NULL);
	if (repo_icon != NULL) {
		g_autoptr(AsIcon) ic = as_icon_new ();
		as_icon_set_kind (ic, AS_ICON_KIND_REMOTE);
		as_icon_set_url (ic, repo_icon);
		gs_app_add_icon (app, ic);
	}

	/* check to see if the repo ID already exists */
	xremote = flatpak_installation_get_remote_by_name (installation,
							   repo_id,
							   cancellable, NULL);
	if (xremote != NULL) {
		g_debug ("repo %s already exists", repo_id);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	} else {
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	}
	return TRUE;
}

gboolean
gs_flatpak_file_to_app (GsPlugin *plugin,
			FlatpakInstallation *installation,
			GsAppList *list,
			GFile *file,
			GCancellable *cancellable,
			GError **error)
{
	g_autofree gchar *content_type = NULL;
	const gchar *mimetypes_bundle[] = {
		"application/vnd.flatpak",
		NULL };
	const gchar *mimetypes_repo[] = {
		"application/vnd.flatpak.repo",
		NULL };

	/* does this match any of the mimetypes_bundle we support */
	content_type = gs_utils_get_content_type (file, cancellable, error);
	if (content_type == NULL)
		return FALSE;
	if (g_strv_contains (mimetypes_bundle, content_type)) {
		return gs_flatpak_file_to_app_bundle (plugin,
						      installation,
						      list,
						      file,
						      cancellable,
						      error);
	}
	if (g_strv_contains (mimetypes_repo, content_type)) {
		return gs_flatpak_file_to_app_repo (plugin,
						    installation,
						    list,
						    file,
						    cancellable,
						    error);
	}
	return TRUE;
}
