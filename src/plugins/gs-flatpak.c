/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Joaquim Rocha <jrocha@endlessm.com>
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include "gs-appstream.h"
#include "gs-flatpak.h"
#include "gs-flatpak-symlinks.h"

struct _GsFlatpak {
	GObject			 parent_instance;
	FlatpakInstallation	*installation;
	GHashTable		*broken_remotes;
	GFileMonitor		*monitor;
	AsAppScope		 scope;
	GsPlugin		*plugin;
};

G_DEFINE_TYPE (GsFlatpak, gs_flatpak, G_TYPE_OBJECT)

static gboolean
gs_flatpak_refresh_appstream (GsFlatpak *self, guint cache_age,
			      GsPluginRefreshFlags flags,
			      GCancellable *cancellable, GError **error);

static void
gs_plugin_flatpak_error_convert (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return;

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gio (perror))
		return;

	/* custom to this plugin */
	if (error->domain == FLATPAK_ERROR) {
		switch (error->code) {
		case FLATPAK_ERROR_ALREADY_INSTALLED:
		case FLATPAK_ERROR_NOT_INSTALLED:
			error->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
			break;
		default:
			error->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}
	} else {
		g_warning ("can't reliably fixup error from domain %s",
			   g_quark_to_string (error->domain));
		error->code = GS_PLUGIN_ERROR_FAILED;
	}
	error->domain = GS_PLUGIN_ERROR;
}

static void
gs_plugin_flatpak_changed_cb (GFileMonitor *monitor,
			      GFile *child,
			      GFile *other_file,
			      GFileMonitorEvent event_type,
			      GsFlatpak *self)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error_md = NULL;

	/* manually drop the cache */
	if (!flatpak_installation_drop_caches (self->installation,
					       NULL, &error)) {
		g_warning ("failed to drop cache: %s", error->message);
		return;
	}

	/* if this is a new remote, get the AppStream data */
	if (!gs_flatpak_refresh_appstream (self, G_MAXUINT, 0, NULL, &error_md)) {
		g_warning ("failed to get initial available data: %s",
			   error_md->message);
	}

	/* ensure the AppStream symlink cache is up to date */
	if (!gs_flatpak_symlinks_rebuild (self->installation, NULL, &error))
		g_warning ("failed to check symlinks: %s", error->message);
}

gboolean
gs_flatpak_setup (GsFlatpak *self, GCancellable *cancellable, GError **error)
{
	const gchar *destdir;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* we use a permissions helper to elevate privs */
	ptask = as_profile_start_literal (gs_plugin_get_profile (self->plugin),
					  "flatpak::ensure-origin");
	g_assert (ptask != NULL);
	destdir = g_getenv ("GS_SELF_TEST_FLATPACK_DATADIR");
	if (destdir != NULL) {
		g_autofree gchar *full_path = g_build_filename (destdir,
								"flatpak",
								NULL);
		g_autoptr(GFile) file = g_file_new_for_path (full_path);
		g_debug ("using custom flatpak path %s", full_path);
		self->installation = flatpak_installation_new_for_path (file, TRUE,
									cancellable,
									error);
	} else if (self->scope == AS_APP_SCOPE_SYSTEM) {
		self->installation = flatpak_installation_new_system (cancellable,
								      error);
	} else if (self->scope == AS_APP_SCOPE_USER) {
		self->installation = flatpak_installation_new_user (cancellable,
								    error);
	}
	if (self->installation == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}

	/* watch for changes */
	self->monitor = flatpak_installation_create_monitor (self->installation,
							     cancellable,
							     error);
	if (self->monitor == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	g_signal_connect (self->monitor, "changed",
			  G_CALLBACK (gs_plugin_flatpak_changed_cb), self);

	/* ensure the AppStream symlink cache is up to date */
	if (!gs_flatpak_symlinks_rebuild (self->installation, cancellable, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
gs_flatpak_refresh_appstream (GsFlatpak *self, guint cache_age,
			      GsPluginRefreshFlags flags,
			      GCancellable *cancellable, GError **error)
{
	gboolean ret;
	gboolean something_changed = FALSE;
	guint i;
	g_autoptr(GPtrArray) xremotes = NULL;

	xremotes = flatpak_installation_list_remotes (self->installation, cancellable,
						      error);
	if (xremotes == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	for (i = 0; i < xremotes->len; i++) {
		const gchar *remote_name;
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
		remote_name = flatpak_remote_get_name (xremote);
		if (g_hash_table_lookup (self->broken_remotes, remote_name) != NULL) {
			g_debug ("skipping known broken remote: %s", remote_name);
			continue;
		}

		/* is the timestamp new enough */
		file_timestamp = flatpak_remote_get_appstream_timestamp (xremote, NULL);
		tmp = gs_utils_get_file_age (file_timestamp);
		if (tmp < cache_age) {
			g_autofree gchar *fn = g_file_get_path (file_timestamp);
			g_debug ("%s is only %u seconds old, so ignoring refresh",
				 fn, tmp);
			continue;
		}

		/* download new data */
		g_debug ("%s is %u seconds old, so downloading new data",
			 remote_name, tmp);
		ret = flatpak_installation_update_appstream_sync (self->installation,
								  remote_name,
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
				/* don't try to fetch this again until refresh() */
				g_hash_table_insert (self->broken_remotes,
						     g_strdup (remote_name),
						     GUINT_TO_POINTER (1));
				continue;
			}
			if ((flags & GS_PLUGIN_REFRESH_FLAGS_INTERACTIVE) == 0) {
				g_warning ("Failed to get AppStream metadata: %s",
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

		/* trigger the symlink rebuild */
		something_changed = TRUE;
	}

	/* ensure the AppStream symlink cache is up to date */
	if (something_changed) {
		if (!gs_flatpak_symlinks_rebuild (self->installation,
						  cancellable,
						  error))
			return FALSE;
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
gs_plugin_refine_item_scope (GsFlatpak *self, GsApp *app)
{
	if (gs_app_get_scope (app) == AS_APP_SCOPE_UNKNOWN) {
		gboolean is_user = flatpak_installation_get_is_user (self->installation);
		gs_app_set_scope (app, is_user ? AS_APP_SCOPE_USER : AS_APP_SCOPE_SYSTEM);
	}
}

static void
gs_flatpak_set_metadata (GsFlatpak *self, GsApp *app, FlatpakRef *xref)
{
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_FLATPAK);
	gs_app_set_management_plugin (app, gs_plugin_get_name (self->plugin));
	gs_app_set_flatpak_kind (app, flatpak_ref_get_kind (xref));
	gs_app_set_flatpak_name (app, flatpak_ref_get_name (xref));
	gs_app_set_flatpak_arch (app, flatpak_ref_get_arch (xref));
	gs_app_set_flatpak_branch (app, flatpak_ref_get_branch (xref));
	gs_app_set_flatpak_commit (app, flatpak_ref_get_commit (xref));
	gs_plugin_refine_item_scope (self, app);
}

static void
gs_flatpak_set_metadata_installed (GsFlatpak *self, GsApp *app,
				   FlatpakInstalledRef *xref)
{
	guint64 mtime;
	guint64 size_installed;
	g_autofree gchar *metadata_fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	/* for all types */
	gs_flatpak_set_metadata (self, app, FLATPAK_REF (xref));
	if (gs_app_get_metadata_item (app, "GnomeSoftware::Creator") == NULL) {
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (self->plugin));
	}

	/* get the last time the app was updated */
	metadata_fn = g_build_filename (flatpak_installed_ref_get_deploy_dir (xref),
					"..",
					"active",
					"metadata",
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
	if (gs_app_get_origin (app) == NULL)
		gs_app_set_origin (app, flatpak_installed_ref_get_origin (xref));

	/* this is faster than flatpak_installation_fetch_remote_size_sync() */
	size_installed = flatpak_installed_ref_get_installed_size (xref);
	if (size_installed != 0)
		gs_app_set_size_installed (app, size_installed);
}

static gchar *
gs_flatpak_build_id (FlatpakRef *xref)
{
	if (flatpak_ref_get_kind (xref) == FLATPAK_REF_KIND_APP) {
		return g_strdup_printf ("%s.desktop",
					flatpak_ref_get_name (xref));
	}
	return g_strdup_printf ("%s.runtime",
				flatpak_ref_get_name (xref));
}

static gchar *
gs_flatpak_build_unique_id (FlatpakInstallation *installation, FlatpakRef *xref)
{
	AsAppKind kind = AS_APP_KIND_DESKTOP;
	AsAppScope scope = AS_APP_SCOPE_SYSTEM;
	g_autofree gchar *id = NULL;

	/* use a different prefix if we're somehow running this as per-user */
	if (flatpak_installation_get_is_user (installation))
		scope = AS_APP_SCOPE_USER;
	if (flatpak_ref_get_kind (xref) == FLATPAK_REF_KIND_RUNTIME)
		kind = AS_APP_KIND_RUNTIME;
	id = gs_flatpak_build_id (xref);
	return as_utils_unique_id_build (scope,
					 AS_BUNDLE_KIND_FLATPAK,
					 NULL,	/* origin */
					 kind,
					 id,
					 flatpak_ref_get_branch (xref));
}

static GsApp *
gs_flatpak_create_installed (GsFlatpak *self,
			     FlatpakInstalledRef *xref,
			     GError **error)
{
	g_autofree gchar *unique_id = NULL;
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
	unique_id = gs_flatpak_build_unique_id (self->installation,
						FLATPAK_REF (xref));
	app = gs_plugin_cache_lookup (self->plugin, unique_id);
	if (app == NULL) {
		g_autofree gchar *id = gs_flatpak_build_id (FLATPAK_REF (xref));
		app = gs_app_new (id);
		gs_plugin_cache_add (self->plugin, unique_id, app);
	}
	gs_flatpak_set_metadata_installed (self, app, xref);

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
gs_flatpak_add_installed (GsFlatpak *self, GsAppList *list,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autoptr(GError) error_md = NULL;
	g_autoptr(GPtrArray) xrefs = NULL;
	guint i;

	/* if we've never ever run before, get the AppStream data */
	if (!gs_flatpak_refresh_appstream (self, G_MAXUINT, 0,
					   cancellable,
					   &error_md)) {
		g_warning ("failed to get initial available data: %s",
			   error_md->message);
	}

	/* get apps and runtimes */
	xrefs = flatpak_installation_list_installed_refs (self->installation,
							  cancellable, error);
	if (xrefs == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	for (i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GsApp) app = NULL;

		/* only apps */
		if (flatpak_ref_get_kind (FLATPAK_REF (xref)) != FLATPAK_REF_KIND_APP)
			continue;

		app = gs_flatpak_create_installed (self, xref, &error_local);
		if (app == NULL) {
			g_warning ("failed to add flatpak: %s", error_local->message);
			continue;
		}
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_flatpak_add_sources (GsFlatpak *self, GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(GPtrArray) xrefs = NULL;
	g_autoptr(GPtrArray) xremotes = NULL;
	guint i;
	guint j;

	/* get installed apps and runtimes */
	xrefs = flatpak_installation_list_installed_refs (self->installation,
							  cancellable,
							  error);
	if (xrefs == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}

	/* get available remotes */
	xremotes = flatpak_installation_list_remotes (self->installation,
						      cancellable,
						      error);
	if (xremotes == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	for (i = 0; i < xremotes->len; i++) {
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		g_autoptr(GsApp) app = NULL;
		g_autofree gchar *url = NULL;
		g_autofree gchar *title = NULL;;

		/* apps installed from bundles add their own remote that only
		 * can be used for updating that app only -- so hide them */
		if (flatpak_remote_get_noenumerate (xremote))
			continue;

		/* create both enabled and disabled and filter in the UI */
		app = gs_app_new (flatpak_remote_get_name (xremote));
		gs_app_set_management_plugin (app, gs_plugin_get_name (self->plugin));
		gs_app_set_kind (app, AS_APP_KIND_SOURCE);
		gs_app_set_state (app, flatpak_remote_get_disabled (xremote) ?
				  AS_APP_STATE_AVAILABLE : AS_APP_STATE_INSTALLED);
		gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
		gs_app_set_name (app,
				 GS_APP_QUALITY_LOWEST,
				 flatpak_remote_get_name (xremote));

		/* title */
		title = flatpak_remote_get_title (xremote);
		if (title != NULL)
			gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, title);

		/* url */
		url = flatpak_remote_get_url (xremote);
		if (url != NULL)
			gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, url);
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
			related = gs_flatpak_create_installed (self,
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

static gboolean
gs_flatpak_app_install_source (GsFlatpak *self, GsApp *app,
			       GCancellable *cancellable,
			       GError **error)
{
	const gchar *gpg_key;
	g_autoptr(FlatpakRemote) xremote = NULL;

	/* only process this source if was created for this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (self->plugin)) != 0)
		return TRUE;

	/* does the remote already exist and is disabled */
	xremote = flatpak_installation_get_remote_by_name (self->installation,
							   gs_app_get_id (app),
							   cancellable, NULL);
	if (xremote != NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "flatpak source %s already exists",
			     flatpak_remote_get_name (xremote));
		return FALSE;
	}

	/* create a new remote */
	xremote = flatpak_remote_new (gs_app_get_id (app));
	flatpak_remote_set_noenumerate (xremote, FALSE);
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
	if (!flatpak_installation_modify_remote (self->installation,
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
gs_flatpak_add_updates (GsFlatpak *self, GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	guint i;
	g_autoptr(GPtrArray) xrefs = NULL;

	/* manually drop the cache */
	if (0&&!flatpak_installation_drop_caches (self->installation,
						  cancellable,
						  error)) {
		return FALSE;
	}

	/* get all the installed apps (no network I/O) */
	xrefs = flatpak_installation_list_installed_refs (self->installation,
							  cancellable,
							  error);
	if (xrefs == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
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
		app = gs_flatpak_create_installed (self, xref, &error_local);
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
gs_flatpak_refresh (GsFlatpak *self,
		    guint cache_age,
		    GsPluginRefreshFlags flags,
		    GCancellable *cancellable,
		    GError **error)
{
	guint i;
	g_autoptr(GPtrArray) xrefs = NULL;

	/* give all the repos a second chance */
	g_hash_table_remove_all (self->broken_remotes);

	/* update AppStream metadata */
	if (flags & GS_PLUGIN_REFRESH_FLAGS_METADATA) {
		if (!gs_flatpak_refresh_appstream (self, cache_age, flags,
						   cancellable, error))
			return FALSE;
	}

	/* no longer interesting */
	if ((flags & GS_PLUGIN_REFRESH_FLAGS_PAYLOAD) == 0)
		return TRUE;

	/* get all the updates available from all remotes */
	xrefs = flatpak_installation_list_installed_refs_for_update (self->installation,
								     cancellable,
								     error);
	if (xrefs == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	for (i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);
		g_autoptr(GsApp) app = NULL;
		g_autoptr(FlatpakInstalledRef) xref2 = NULL;

		/* try to create a GsApp so we can do progress reporting */
		app = gs_flatpak_create_installed (self, xref,
						   NULL);

		/* fetch but do not deploy */
		g_debug ("pulling update for %s",
			 flatpak_ref_get_name (FLATPAK_REF (xref)));
		xref2 = flatpak_installation_update (self->installation,
						     FLATPAK_UPDATE_FLAGS_NO_DEPLOY,
						     flatpak_ref_get_kind (FLATPAK_REF (xref)),
						     flatpak_ref_get_name (FLATPAK_REF (xref)),
						     flatpak_ref_get_arch (FLATPAK_REF (xref)),
						     flatpak_ref_get_branch (FLATPAK_REF (xref)),
						     gs_flatpak_progress_cb, app,
						     cancellable, error);
		if (xref2 == NULL) {
			gs_plugin_flatpak_error_convert (error);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
gs_plugin_refine_item_origin_ui (GsFlatpak *self, GsApp *app,
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
	ptask = as_profile_start_literal (gs_plugin_get_profile (self->plugin),
					  "flatpak::refine-origin-ui");
	g_assert (ptask != NULL);
	xremotes = flatpak_installation_list_remotes (self->installation,
						      cancellable,
						      error);
	if (xremotes == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	for (i = 0; i < xremotes->len; i++) {
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		if (flatpak_remote_get_disabled (xremote))
			continue;
		if (g_strcmp0 (gs_app_get_origin (app),
			       flatpak_remote_get_name (xremote)) == 0) {
			g_autofree gchar *title = NULL;
			title = flatpak_remote_get_title (xremote);
			gs_app_set_origin_ui (app, title);
			break;
		}
	}

	return TRUE;
}

static gboolean
gs_plugin_refine_item_origin_hostname (GsFlatpak *self, GsApp *app,
				       GCancellable *cancellable,
				       GError **error)
{
	g_autoptr(FlatpakRemote) xremote = NULL;
	g_autofree gchar *url = NULL;

	/* already set */
	if (gs_app_get_origin_hostname (app) != NULL)
		return TRUE;

	/* get the remote  */
	xremote = flatpak_installation_get_remote_by_name (self->installation,
							   gs_app_get_origin (app),
							   cancellable,
							   error);
	if (xremote == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	url = flatpak_remote_get_url (xremote);
	if (url == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "no URL for remote %s",
			     flatpak_remote_get_name (xremote));
		return FALSE;
	}
	gs_app_set_origin_hostname (app, url);
	return TRUE;
}

static gboolean
gs_refine_item_metadata (GsFlatpak *self, GsApp *app,
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
			   gs_plugin_get_name (self->plugin), tmp);
		return TRUE;
	}

	/* parse the ref */
	xref = flatpak_ref_parse (gs_app_get_source_default (app), error);
	if (xref == NULL) {
		gs_plugin_flatpak_error_convert (error);
		g_prefix_error (error, "failed to parse '%s': ",
				gs_app_get_source_default (app));
		return FALSE;
	}
	gs_flatpak_set_metadata (self, app, xref);

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
refine_origin_from_installation (GsFlatpak *self,
				 FlatpakInstallation *installation,
				 GsApp *app,
				 GCancellable *cancellable,
				 GError **error)
{
	guint i;
	g_autoptr(GPtrArray) xremotes = NULL;

	xremotes = flatpak_installation_list_remotes (installation,
						      cancellable,
						      error);
	if (xremotes == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
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

static FlatpakInstallation *
gs_flatpak_get_installation_counterpart (GsFlatpak *self,
					 GCancellable *cancellable,
					 GError **error)
{
	FlatpakInstallation *installation;
	if (flatpak_installation_get_is_user (self->installation))
		installation = flatpak_installation_new_system (cancellable, error);
	else
		installation = flatpak_installation_new_user (cancellable, error);
	if (installation == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return NULL;
	}
	return installation;
}

static gboolean
gs_plugin_refine_item_origin (GsFlatpak *self,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GError) local_error = NULL;
	gboolean ignore_error = FALSE;

	/* already set */
	if (gs_app_get_origin (app) != NULL)
		return TRUE;

	/* ensure metadata exists */
	ptask = as_profile_start_literal (gs_plugin_get_profile (self->plugin),
					  "flatpak::refine-origin");
	g_assert (ptask != NULL);
	if (!gs_refine_item_metadata (self, app, cancellable, error))
		return FALSE;

	/* find list of remotes */
	g_debug ("looking for a remote for %s/%s/%s",
		 gs_app_get_flatpak_name (app),
		 gs_app_get_flatpak_arch (app),
		 gs_app_get_flatpak_branch (app));

	/* first check the plugin's own flatpak installation */
	if (refine_origin_from_installation (self, self->installation, app,
					     cancellable, &local_error)) {
		return TRUE;
	}

	ignore_error = g_error_matches (local_error, GS_PLUGIN_ERROR,
					GS_PLUGIN_ERROR_NOT_SUPPORTED);

	/* check the system installation if we're on a user one */
	if (ignore_error &&
	    gs_app_get_flatpak_kind (app) == FLATPAK_REF_KIND_RUNTIME) {
		g_autoptr(FlatpakInstallation) installation =
			gs_flatpak_get_installation_counterpart (self,
								 cancellable,
								 error);

		if (installation == NULL)
			return FALSE;

		if (refine_origin_from_installation (self, installation, app,
						     cancellable, error)) {
			return TRUE;
		}
	} else {
		g_propagate_error (error, local_error);
		/* safely handle the autoptr */
		local_error = NULL;
	}

	return FALSE;
}

static gboolean
gs_flatpak_app_matches_xref (GsFlatpak *self, GsApp *app, FlatpakRef *xref)
{
	g_autofree gchar *id = NULL;

	/* check ID */
	id = gs_flatpak_build_unique_id (self->installation, xref);
	if (g_strcmp0 (id, gs_app_get_unique_id (app)) == 0)
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
	FlatpakRef *xref;
	g_autofree gchar *id = NULL;
	id = g_strdup_printf ("%s/%s/%s/%s",
			      gs_app_get_flatpak_kind_as_str (app),
			      gs_app_get_flatpak_name (app),
			      gs_app_get_flatpak_arch (app),
			      gs_app_get_flatpak_branch (app));
	xref = flatpak_ref_parse (id, error);
	if (xref == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return NULL;
	}
	return xref;
}

static gboolean
gs_plugin_refine_item_state (GsFlatpak *self,
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
	if (!gs_refine_item_metadata (self, app, cancellable, error))
		return FALSE;

	/* get apps and runtimes */
	ptask = as_profile_start_literal (gs_plugin_get_profile (self->plugin),
					  "flatpak::refine-action");
	g_assert (ptask != NULL);
	xrefs = flatpak_installation_list_installed_refs (self->installation,
							  cancellable, error);
	if (xrefs == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	for (i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);

		/* check xref is app */
		if (!gs_flatpak_app_matches_xref (self, app, FLATPAK_REF(xref)))
			continue;

		/* mark as installed */
		g_debug ("marking %s as installed with flatpak",
			 gs_app_get_id (app));
		gs_flatpak_set_metadata_installed (self, app, xref);
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	}

	/* ensure origin set */
	if (!gs_plugin_refine_item_origin (self, app, cancellable, error))
		return FALSE;

	/* special case: if this is per-user instance and the runtime is
	 * available system-wide then mark it installed, and vice-versa */
	if (gs_app_get_flatpak_kind (app) == FLATPAK_REF_KIND_RUNTIME &&
	    gs_app_get_state (app) == AS_APP_STATE_UNKNOWN) {
		g_autoptr(FlatpakInstallation) installation =
			gs_flatpak_get_installation_counterpart (self,
								 cancellable,
								 error);
		if (installation == NULL)
			return FALSE;
		xrefs = flatpak_installation_list_installed_refs (installation,
								  cancellable, error);
		if (xrefs == NULL) {
			gs_plugin_flatpak_error_convert (error);
			return FALSE;
		}
		for (i = 0; i < xrefs->len; i++) {
			FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);
			if (!gs_flatpak_app_matches_xref (self, app, FLATPAK_REF(xref)))
				continue;
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		}
	}

	/* anything not installed just check the remote is still present */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN &&
	    gs_app_get_origin (app) != NULL) {
		g_autoptr(FlatpakRemote) xremote = NULL;
		xremote = flatpak_installation_get_remote_by_name (self->installation,
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
			g_warning ("failed to find flatpak %s remote %s for %s",
				   flatpak_installation_get_is_user (self->installation) ? "user" : "system",
				   gs_app_get_origin (app),
				   gs_app_get_unique_id (app));
			g_warning ("%s", gs_app_to_string (app));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
gs_flatpak_set_app_metadata (GsFlatpak *self,
			     GsApp *app,
			     const gchar *data,
			     gsize length,
			     GError **error)
{
	gboolean secure = TRUE;
	g_autofree gchar *name = NULL;
	g_autofree gchar *runtime = NULL;
	g_autofree gchar *source = NULL;
	g_autoptr(GKeyFile) kf = NULL;
	g_autoptr(GsApp) app_runtime = NULL;
	g_auto(GStrv) shared = NULL;
	g_auto(GStrv) sockets = NULL;
	g_auto(GStrv) filesystems = NULL;

	kf = g_key_file_new ();
	if (!g_key_file_load_from_data (kf, data, length, G_KEY_FILE_NONE, error)) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	name = g_key_file_get_string (kf, "Application", "name", error);
	if (name == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	gs_app_set_flatpak_name (app, name);
	runtime = g_key_file_get_string (kf, "Application", "runtime", error);
	if (runtime == NULL) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	g_debug ("runtime for %s is %s", name, runtime);

	/* we always get this, but it's a low bar... */
	gs_app_add_kudo (app, GS_APP_KUDO_SANDBOXED);
	shared = g_key_file_get_string_list (kf, "Context", "shared", NULL, NULL);
	if (shared != NULL) {
		/* SHM isn't secure enough */
		if (g_strv_contains ((const gchar * const *) shared, "ipc"))
			secure = FALSE;
	}
	sockets = g_key_file_get_string_list (kf, "Context", "sockets", NULL, NULL);
	if (sockets != NULL) {
		/* X11 isn't secure enough */
		if (g_strv_contains ((const gchar * const *) sockets, "x11"))
			secure = FALSE;
	}
	filesystems = g_key_file_get_string_list (kf, "Context", "filesystems", NULL, NULL);
	if (filesystems != NULL) {
		/* secure apps should be using portals */
		if (g_strv_contains ((const gchar * const *) filesystems, "home"))
			secure = FALSE;
	}

	/* this is actually quite hard to achieve */
	if (secure)
		gs_app_add_kudo (app, GS_APP_KUDO_SANDBOXED_SECURE);

	/* create runtime */
	if (gs_app_get_runtime (app) == NULL) {
		app_runtime = gs_appstream_create_runtime (self->plugin, app, runtime);
		if (app_runtime != NULL) {
			gs_plugin_refine_item_scope (self, app_runtime);
			gs_app_set_runtime (app, app_runtime);
		}
	}

	return TRUE;
}

static gboolean
gs_plugin_refine_item_metadata (GsFlatpak *self,
			       GsApp *app,
			       GCancellable *cancellable,
			       GError **error)
{
	const gchar *str;
	gsize len = 0;
	g_autofree gchar *contents = NULL;
	g_autofree gchar *installation_path_str = NULL;
	g_autofree gchar *install_path = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GFile) installation_path = NULL;

	/* not applicable */
	if (gs_app_get_flatpak_kind (app) != FLATPAK_REF_KIND_APP)
		return TRUE;

	/* this is quicker than doing network IO */
	installation_path = flatpak_installation_get_path (self->installation);
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
		data = flatpak_installation_fetch_remote_metadata_sync (self->installation,
									gs_app_get_origin (app),
									xref,
									cancellable,
									error);
		if (data == NULL) {
			gs_plugin_flatpak_error_convert (error);
			return FALSE;
		}
		str = g_bytes_get_data (data, &len);
	}

	/* parse key file */
	if (!gs_flatpak_set_app_metadata (self, app, str, len, error))
		return FALSE;
	return TRUE;
}

static FlatpakInstalledRef *
gs_flatpak_get_installed_ref (GsFlatpak *self,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	FlatpakInstalledRef *ref;
	ref = flatpak_installation_get_installed_ref (self->installation,
						      gs_app_get_flatpak_kind (app),
						      gs_app_get_flatpak_name (app),
						      gs_app_get_flatpak_arch (app),
						      gs_app_get_flatpak_branch (app),
						      cancellable,
						      error);
	if (ref == NULL)
		gs_plugin_flatpak_error_convert (error);
	return ref;
}

static gboolean
gs_plugin_refine_item_size (GsFlatpak *self,
			    GsApp *app,
			    GCancellable *cancellable,
			    GError **error)
{
	gboolean ret;
	guint64 download_size = GS_APP_SIZE_UNKNOWABLE;
	guint64 installed_size = GS_APP_SIZE_UNKNOWABLE;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* already set */
	if (gs_app_is_installed (app)) {
		/* only care about the installed size if the app is installed */
		if (gs_app_get_size_installed (app) > 0)
			return TRUE;
	} else {
		if (gs_app_get_size_installed (app) > 0 &&
		    gs_app_get_size_download (app) > 0)
		return TRUE;
	}

	/* need runtime */
	if (!gs_plugin_refine_item_metadata (self, app, cancellable, error))
		return FALSE;

	/* calculate the platform size too if the app is not installed */
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE &&
	    gs_app_get_flatpak_kind (app) == FLATPAK_REF_KIND_APP) {
		GsApp *app_runtime;

		/* is the app_runtime already installed? */
		app_runtime = gs_app_get_runtime (app);
		if (!gs_plugin_refine_item_state (self,
						  app_runtime,
						  cancellable,
						  error))
			return FALSE;
		if (gs_app_get_state (app_runtime) == AS_APP_STATE_INSTALLED) {
			g_debug ("runtime %s is already installed, so not adding size",
				 gs_app_get_id (app_runtime));
		} else {
			if (!gs_plugin_refine_item_size (self,
							 app_runtime,
							 cancellable,
							 error))
				return FALSE;
		}
	}

	/* just get the size of the app */
	ptask = as_profile_start_literal (gs_plugin_get_profile (self->plugin),
					  "flatpak::refine-size");
	g_assert (ptask != NULL);
	if (!gs_plugin_refine_item_origin (self, app,
					   cancellable, error))
		return FALSE;

	/* if the app is installed we use the ref to fetch the installed size
	 * and ignore the download size as this is faster */
	if (gs_app_is_installed (app)) {
		g_autoptr(FlatpakInstalledRef) xref = NULL;
		xref = gs_flatpak_get_installed_ref (self, app,
						     cancellable, error);
		installed_size = flatpak_installed_ref_get_installed_size (xref);
		if (installed_size == 0)
			installed_size = GS_APP_SIZE_UNKNOWABLE;
	} else {
		g_autoptr(FlatpakRef) xref = NULL;
		g_autoptr(GError) error_local = NULL;
		xref = gs_flatpak_create_fake_ref (app, error);
		if (xref == NULL)
			return FALSE;
		ret = flatpak_installation_fetch_remote_size_sync (self->installation,
								   gs_app_get_origin (app),
								   xref,
								   &download_size,
								   &installed_size,
								   cancellable,
								   &error_local);

		if (!ret) {
			g_warning ("libflatpak failed to return application "
				   "size: %s", error_local->message);
		}
	}

	gs_app_set_size_installed (app, installed_size);
	gs_app_set_size_download (app, download_size);

	return TRUE;
}

gboolean
gs_flatpak_refine_app (GsFlatpak *self,
		       GsApp *app,
		       GsPluginRefineFlags flags,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(AsProfileTask) ptask = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (self->plugin)) != 0)
		return TRUE;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "flatpak::refine{%s}",
				  gs_app_get_id (app));
	g_assert (ptask != NULL);

	/* flatpak apps can always be removed */
	gs_app_remove_quirk (app, AS_APP_QUIRK_COMPULSORY);

	/* scope is fast, do unconditionally */
	gs_plugin_refine_item_scope (self, app);

	/* AppStream sets the source to appname/arch/branch */
	if (!gs_refine_item_metadata (self, app, cancellable, error)) {
		g_prefix_error (error, "failed to get metadata: ");
		return FALSE;
	}

	/* check the installed state */
	if (!gs_plugin_refine_item_state (self, app, cancellable, error)) {
		g_prefix_error (error, "failed to get state: ");
		return FALSE;
	}

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
		if (!gs_plugin_refine_item_size (self, app,
						 cancellable, error)) {
			g_prefix_error (error, "failed to get size: ");
			return FALSE;
		}
	}

	/* origin */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) {
		if (!gs_plugin_refine_item_origin_ui (self, app,
						      cancellable, error)) {
			g_prefix_error (error, "failed to get origin: ");
			return FALSE;
		}
	}

	/* origin-hostname */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME) {
		if (!gs_plugin_refine_item_origin_hostname (self, app,
							    cancellable,
							    error)) {
			g_prefix_error (error, "failed to get origin-hostname: ");
			return FALSE;
		}
	}

	/* permissions */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS) {
		if (!gs_plugin_refine_item_metadata (self, app,
						     cancellable, error)) {
			g_prefix_error (error, "failed to get permissions: ");
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
gs_flatpak_launch (GsFlatpak *self,
		   GsApp *app,
		   GCancellable *cancellable,
		   GError **error)
{
	GsApp *runtime;
	const gchar *branch = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (self->plugin)) != 0)
		return TRUE;

	branch = gs_app_get_flatpak_branch (app);
	if (branch == NULL)
		branch = "master";

	/* check the runtime is installed */
	runtime = gs_app_get_runtime (app);
	if (runtime != NULL) {
		if (!gs_plugin_refine_item_state (self, runtime, cancellable, error))
			return FALSE;
		if (!gs_app_is_installed (runtime)) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "runtime is not installed");
			gs_utils_error_add_unique_id (error, runtime);
			gs_plugin_cache_add (self->plugin, NULL, runtime);
			return FALSE;
		}
	}

	/* launch the app */
	if (!flatpak_installation_launch (self->installation,
					  gs_app_get_flatpak_name (app),
					  NULL,
					  branch,
					  NULL,
					  cancellable,
					  error)) {
		gs_plugin_flatpak_error_convert (error);
		return FALSE;
	}
	return TRUE;
}

static gboolean
gs_flatpak_app_remove_source (GsFlatpak *self,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autoptr(FlatpakRemote) xremote = NULL;

	/* find the remote */
	xremote = flatpak_installation_get_remote_by_name (self->installation,
							   gs_app_get_id (app),
							   cancellable, error);
	if (xremote == NULL) {
		gs_plugin_flatpak_error_convert (error);
		g_prefix_error (error,
				"flatpak source %s not found: ",
				gs_app_get_id (app));
		return FALSE;
	}

	/* remove */
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	if (!flatpak_installation_remove_remote (self->installation,
						 gs_app_get_id (app),
						 cancellable,
						 error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	return TRUE;
}

gboolean
gs_flatpak_app_remove (GsFlatpak *self,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (self->plugin)) != 0)
		return TRUE;

	/* refine to get basics */
	if (!gs_flatpak_refine_app (self, app,
				    GS_PLUGIN_REFINE_FLAGS_DEFAULT,
				    cancellable, error))
		return FALSE;

	/* is a source */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE) {
		return gs_flatpak_app_remove_source (self,
						     app,
						     cancellable,
						     error);
	}

	/* remove */
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	if (!flatpak_installation_uninstall (self->installation,
					     gs_app_get_flatpak_kind (app),
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
	if (!gs_plugin_refine_item_state (self, app, cancellable, error))
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
gs_flatpak_app_install (GsFlatpak *self,
			GsApp *app,
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(FlatpakInstalledRef) xref = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (self->plugin)) != 0)
		return TRUE;

	/* ensure we have metadata and state */
	if (!gs_flatpak_refine_app (self, app, 0, cancellable,
				    error))
		return FALSE;

	/* install */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);

	/* add a source */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE) {
		return gs_flatpak_app_install_source (self,
						      app,
						      cancellable,
						      error);
	}

	/* install required runtime if not already installed */
	if (gs_app_get_kind (app) == AS_APP_KIND_DESKTOP) {
		GsApp *runtime;
		runtime = gs_app_get_runtime (app);

		/* the runtime could come from a different remote to the app */
		if (!gs_refine_item_metadata (self, runtime, cancellable,
					      error))
			return FALSE;
		if (!gs_plugin_refine_item_origin (self,
						   runtime, cancellable, error))
			return FALSE;
		if (!gs_plugin_refine_item_state (self, runtime,
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
			xref = flatpak_installation_install (self->installation,
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
		xref = flatpak_installation_install_bundle (self->installation,
							    gs_app_get_local_file (app),
							    gs_flatpak_progress_cb,
							    app,
							    cancellable, error);
	} else {
		g_debug ("installing %s", gs_app_get_id (app));
		xref = flatpak_installation_install (self->installation,
						     gs_app_get_origin (app),
						     gs_app_get_flatpak_kind (app),
						     gs_app_get_flatpak_name (app),
						     gs_app_get_flatpak_arch (app),
						     gs_app_get_flatpak_branch (app),
						     gs_flatpak_progress_cb, app,
						     cancellable, error);
	}
	if (xref == NULL) {
		gs_plugin_flatpak_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

gboolean
gs_flatpak_update_app (GsFlatpak *self,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(FlatpakInstalledRef) xref = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (self->plugin)) != 0)
		return TRUE;

	/* install */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	xref = flatpak_installation_update (self->installation,
					    FLATPAK_UPDATE_FLAGS_NO_PULL,
					    gs_app_get_flatpak_kind (app),
					    gs_app_get_flatpak_name (app),
					    gs_app_get_flatpak_arch (app),
					    gs_app_get_flatpak_branch (app),
					    gs_flatpak_progress_cb, app,
					    cancellable, error);
	if (xref == NULL) {
		gs_plugin_flatpak_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* update UI */
	gs_plugin_updates_changed (self->plugin);

	/* state is known */
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

static gboolean
gs_flatpak_file_to_app_bundle (GsFlatpak *self,
			       GsAppList *list,
			       GFile *file,
			       GCancellable *cancellable,
			       GError **error)
{
	gint size;
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *unique_id = NULL;
	g_autoptr(GBytes) appstream_gz = NULL;
	g_autoptr(GBytes) icon_data = NULL;
	g_autoptr(GBytes) metadata = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(FlatpakBundleRef) xref_bundle = NULL;

	/* load bundle */
	xref_bundle = flatpak_bundle_ref_new (file, error);
	if (xref_bundle == NULL) {
		gs_plugin_flatpak_error_convert (error);
		g_prefix_error (error, "error loading bundle: ");
		return FALSE;
	}

	/* create a virtual ID */
	unique_id = gs_flatpak_build_unique_id (self->installation,
						FLATPAK_REF (xref_bundle));
	app = gs_plugin_cache_lookup (self->plugin, unique_id);
	if (app == NULL) {
		g_autofree gchar *id = gs_flatpak_build_id (FLATPAK_REF (xref_bundle));
		app = gs_app_new (id);
		gs_plugin_cache_add (self->plugin, unique_id, app);
	}

	/* load metadata */
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_set_size_installed (app, flatpak_bundle_ref_get_installed_size (xref_bundle));
	gs_flatpak_set_metadata (self, app, FLATPAK_REF (xref_bundle));
	metadata = flatpak_bundle_ref_get_metadata (xref_bundle);
	if (!gs_flatpak_set_app_metadata (self, app,
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
		if (appstream == NULL) {
			gs_plugin_flatpak_error_convert (error);
			return FALSE;
		}
		store = as_store_new ();
		if (!as_store_from_bytes (store, appstream, cancellable, error)) {
			gs_plugin_flatpak_error_convert (error);
			return FALSE;
		}

		/* allow peeking into this for debugging */
		if (g_getenv ("GS_FLATPAK_DEBUG_APPSTREAM") != NULL) {
			g_autoptr(GString) str = NULL;
			str = as_store_to_xml (store,
					       AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE |
					       AS_NODE_TO_XML_FLAG_FORMAT_INDENT);
			g_debug ("showing AppStream data: %s", str->str);
		}

		/* check for sanity */
		if (as_store_get_size (store) == 0) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "no apps found in AppStream data");
			return FALSE;
		}
		g_debug ("%u applications found in AppStream data",
			 as_store_get_size (store));

		/* find app */
		id = g_strdup_printf ("%s.desktop", gs_app_get_flatpak_name (app));
		item = as_store_get_app_by_id (store, id);
		if (item == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "application %s not found",
				     id);
			return FALSE;
		}

		/* copy details from AppStream to app */
		if (!gs_appstream_refine_app (self->plugin, app, item, error))
			return FALSE;
	}

	/* load icon */
	size = 64 * (gint) gs_plugin_get_scale (self->plugin);
	icon_data = flatpak_bundle_ref_get_icon (xref_bundle, size);
	if (icon_data == NULL)
		icon_data = flatpak_bundle_ref_get_icon (xref_bundle, 64);
	if (icon_data != NULL) {
		g_autoptr(GInputStream) stream_icon = NULL;
		g_autoptr(GdkPixbuf) pixbuf = NULL;
		stream_icon = g_memory_input_stream_new_from_bytes (icon_data);
		pixbuf = gdk_pixbuf_new_from_stream (stream_icon, cancellable, error);
		if (pixbuf == NULL) {
			gs_utils_error_convert_gdk_pixbuf (error);
			return FALSE;
		}
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

	/* success */
	gs_app_list_add (list, app);
	return TRUE;
}

static gboolean
gs_flatpak_file_to_app_repo (GsFlatpak *self,
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
				     "not enough data in file, "
				     "expected Title, Url, GPGKey");
		return FALSE;
	}

	/* user specified a URL */
	if (g_str_has_prefix (repo_gpgkey, "http://") ||
	    g_str_has_prefix (repo_gpgkey, "https://")) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "Base64 encoded GPGKey required, not URL");
		return FALSE;
	}

	/* create source */
	app = gs_app_new (repo_id);
	gs_app_set_kind (app, AS_APP_KIND_SOURCE);
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_LAUNCHABLE);
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, repo_title);
	gs_app_set_metadata (app, "flatpak::gpg-key", repo_gpgkey);
	gs_app_set_origin_hostname (app, repo_url);
	gs_app_set_management_plugin (app, gs_plugin_get_name (self->plugin));

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
	xremote = flatpak_installation_get_remote_by_name (self->installation,
							   repo_id,
							   cancellable, NULL);
	if (xremote != NULL) {
		g_debug ("repo %s already exists", repo_id);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	} else {
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	}

	/* success */
	gs_app_list_add (list, app);
	return TRUE;
}

gboolean
gs_flatpak_file_to_app (GsFlatpak *self,
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
		return gs_flatpak_file_to_app_bundle (self,
						      list,
						      file,
						      cancellable,
						      error);
	}
	if (g_strv_contains (mimetypes_repo, content_type)) {
		return gs_flatpak_file_to_app_repo (self,
						    list,
						    file,
						    cancellable,
						    error);
	}
	return TRUE;
}

static void
gs_flatpak_finalize (GObject *object)
{
	GsFlatpak *self;
	g_return_if_fail (GS_IS_FLATPAK (object));
	self = GS_FLATPAK (object);

	g_object_unref (self->plugin);
	g_hash_table_unref (self->broken_remotes);

	G_OBJECT_CLASS (gs_flatpak_parent_class)->finalize (object);
}

static void
gs_flatpak_class_init (GsFlatpakClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_flatpak_finalize;
}

static void
gs_flatpak_init (GsFlatpak *self)
{
	self->broken_remotes = g_hash_table_new_full (g_str_hash, g_str_equal,
						      g_free, NULL);
}

GsFlatpak *
gs_flatpak_new (GsPlugin *plugin, AsAppScope scope)
{
	GsFlatpak *self;
	self = g_object_new (GS_TYPE_FLATPAK, NULL);
	self->scope = scope;
	self->plugin = g_object_ref (plugin);
	return GS_FLATPAK (self);
}
