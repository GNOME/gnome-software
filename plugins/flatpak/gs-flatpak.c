/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Joaquim Rocha <jrocha@endlessm.com>
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016-2018 Kalev Lember <klember@redhat.com>
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
 * The GsApp:origin is the remote name, e.g. test-repo
 */

#include <config.h>

#include <glib/gi18n.h>

#include "gs-appstream.h"
#include "gs-flatpak-app.h"
#include "gs-flatpak.h"
#include "gs-flatpak-utils.h"

struct _GsFlatpak {
	GObject			 parent_instance;
	GsFlatpakFlags		 flags;
	FlatpakInstallation	*installation;
	GHashTable		*broken_remotes;
	GFileMonitor		*monitor;
	AsAppScope		 scope;
	GsPlugin		*plugin;
	AsStore			*store;
	gchar			*id;
	guint			 changed_id;
};

G_DEFINE_TYPE (GsFlatpak, gs_flatpak, G_TYPE_OBJECT)

static gboolean
gs_flatpak_refresh_appstream (GsFlatpak *self, guint cache_age,
			      GCancellable *cancellable, GError **error);

static gchar *
gs_flatpak_build_id (FlatpakRef *xref)
{
	if (flatpak_ref_get_kind (xref) == FLATPAK_REF_KIND_APP) {
		return g_strdup_printf ("%s.desktop",
					flatpak_ref_get_name (xref));
	}
	return g_strdup (flatpak_ref_get_name (xref));
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
gs_flatpak_claim_app (GsFlatpak *self, GsApp *app)
{
	if (gs_app_get_management_plugin (app) != NULL)
		return;
	gs_app_set_management_plugin (app, gs_plugin_get_name (self->plugin));
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_FLATPAK);
	gs_app_set_scope (app, self->scope);

	/* ony when we have a non-temp object */
	if ((self->flags & GS_FLATPAK_FLAG_IS_TEMPORARY) == 0)
		gs_flatpak_app_set_object_id (app, gs_flatpak_get_id (self));
}

static void
gs_flatpak_claim_app_list (GsFlatpak *self, GsAppList *list)
{
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		gs_flatpak_claim_app (self, app);
	}
}

static void
gs_flatpak_set_metadata (GsFlatpak *self, GsApp *app, FlatpakRef *xref)
{
	/* core */
	gs_flatpak_claim_app (self, app);
	gs_app_set_branch (app, flatpak_ref_get_branch (xref));
	gs_plugin_refine_item_scope (self, app);

	/* flatpak specific */
	gs_flatpak_app_set_ref_kind (app, flatpak_ref_get_kind (xref));
	gs_flatpak_app_set_ref_name (app, flatpak_ref_get_name (xref));
	gs_flatpak_app_set_ref_arch (app, flatpak_ref_get_arch (xref));
	gs_flatpak_app_set_ref_branch (app, flatpak_ref_get_branch (xref));
	gs_flatpak_app_set_commit (app, flatpak_ref_get_commit (xref));

	/* map the flatpak kind to the gnome-software kind */
	if (flatpak_ref_get_kind (xref) == FLATPAK_REF_KIND_APP) {
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	} else if (flatpak_ref_get_kind (xref) == FLATPAK_REF_KIND_RUNTIME) {
		const gchar *id = gs_app_get_id (app);
		/* this is anything that's not an app, including locales
		 * sources and debuginfo */
		if (g_str_has_suffix (id, ".Locale")) {
			gs_app_set_kind (app, AS_APP_KIND_LOCALIZATION);
		} else if (g_str_has_suffix (id, ".Debug") ||
			   g_str_has_suffix (id, ".Sources") ||
			   g_str_has_prefix (id, "org.freedesktop.Platform.Icontheme.") ||
			   g_str_has_prefix (id, "org.gtk.Gtk3theme.")) {
			gs_app_set_kind (app, AS_APP_KIND_GENERIC);
		} else {
			gs_app_set_kind (app, AS_APP_KIND_RUNTIME);
		}
	}
}

static GsApp *
gs_flatpak_create_app (GsFlatpak *self, FlatpakRef *xref)
{
	GsApp *app_cached;
	g_autofree gchar *id = NULL;
	g_autoptr(GsApp) app = NULL;

	/* create a temp GsApp */
	id = gs_flatpak_build_id (xref);
	app = gs_app_new (id);
	gs_flatpak_set_metadata (self, app, xref);

	/* return the ref'd cached copy */
	app_cached = gs_plugin_cache_lookup (self->plugin, gs_app_get_unique_id (app));
	if (app_cached != NULL)
		return app_cached;

	/* fallback values */
	if (gs_app_get_kind (app) == AS_APP_KIND_RUNTIME) {
		g_autoptr(AsIcon) icon = NULL;
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
				 flatpak_ref_get_name (FLATPAK_REF (xref)));
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
				    "Framework for applications");
		gs_app_set_version (app, flatpak_ref_get_branch (FLATPAK_REF (xref)));
		icon = as_icon_new ();
		as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
		as_icon_set_name (icon, "system-run-symbolic");
		gs_app_add_icon (app, icon);
	}

	/* no existing match, just steal the temp object */
	gs_plugin_cache_add (self->plugin, NULL, app);
	return g_steal_pointer (&app);
}

static GsApp *
gs_flatpak_create_source (GsFlatpak *self, FlatpakRemote *xremote)
{
	GsApp *app_cached;
	g_autoptr(GsApp) app = NULL;

	/* create a temp GsApp */
	app = gs_flatpak_app_new_from_remote (xremote);
	gs_flatpak_claim_app (self, app);

	/* we already have one, returned the ref'd cached copy */
	app_cached = gs_plugin_cache_lookup (self->plugin, gs_app_get_unique_id (app));
	if (app_cached != NULL)
		return app_cached;

	/* no existing match, just steal the temp object */
	gs_plugin_cache_add (self->plugin, NULL, app);
	return g_steal_pointer (&app);
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

	/* don't refresh when it's us ourselves doing the change */
	if (gs_plugin_has_flags (self->plugin, GS_PLUGIN_FLAGS_RUNNING_SELF))
		return;

	/* manually drop the cache */
	if (!flatpak_installation_drop_caches (self->installation,
					       NULL, &error)) {
		g_warning ("failed to drop cache: %s", error->message);
		return;
	}

	/* if this is a new remote, get the AppStream data */
	if (!gs_flatpak_refresh_appstream (self, G_MAXUINT, NULL, &error_md)) {
		g_warning ("failed to get initial available data: %s",
			   error_md->message);
	}
}

static void
gs_flatpak_remove_prefixed_names (AsApp *app)
{
	GHashTable *names;
	g_autoptr(GList) keys = NULL;

	names = as_app_get_names (app);
	keys = g_hash_table_get_keys (names);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *locale = l->data;
		const gchar *value = g_hash_table_lookup (names, locale);
		if (value == NULL)
			continue;
		if (!g_str_has_prefix (value, "(Nightly) "))
			continue;
		as_app_set_name (app, locale, value + 10);
	}
}

static gboolean
gs_flatpak_add_apps_from_xremote (GsFlatpak *self,
				  FlatpakRemote *xremote,
				  GCancellable *cancellable,
				  GError **error)
{
	GPtrArray *apps;
	g_autofree gchar *appstream_dir_fn = NULL;
	g_autofree gchar *appstream_fn = NULL;
	g_autofree gchar *default_branch = NULL;
	g_autofree gchar *only_app_id = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(GFile) appstream_dir = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GSettings) settings = NULL;
	g_autoptr(GPtrArray) app_filtered = NULL;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::add-apps-from-remote{%s}",
				  gs_flatpak_get_id (self),
				  flatpak_remote_get_name (xremote));
	g_assert (ptask != NULL);

	/* get the AppStream data location */
	appstream_dir = flatpak_remote_get_appstream_dir (xremote, NULL);
	if (appstream_dir == NULL) {
		g_debug ("no appstream dir for %s, skipping",
			 flatpak_remote_get_name (xremote));
		return TRUE;
	}

	/* load the file into a temp store */
	appstream_dir_fn = g_file_get_path (appstream_dir);
	appstream_fn = g_build_filename (appstream_dir_fn,
					 "appstream.xml.gz", NULL);
	if (!g_file_test (appstream_fn, G_FILE_TEST_EXISTS)) {
		g_debug ("no %s appstream metadata found: %s",
			 flatpak_remote_get_name (xremote),
			 appstream_fn);
		return TRUE;
	}
	file = g_file_new_for_path (appstream_fn);
	store = as_store_new ();
	as_store_set_add_flags (store,
				AS_STORE_ADD_FLAG_USE_UNIQUE_ID |
				AS_STORE_ADD_FLAG_ONLY_NATIVE_LANGS);
	as_store_set_search_match (store,
				   AS_APP_SEARCH_MATCH_MIMETYPE |
				   AS_APP_SEARCH_MATCH_PKGNAME |
				   AS_APP_SEARCH_MATCH_COMMENT |
				   AS_APP_SEARCH_MATCH_NAME |
				   AS_APP_SEARCH_MATCH_KEYWORD |
				   AS_APP_SEARCH_MATCH_ORIGIN |
				   AS_APP_SEARCH_MATCH_ID);
	if (!as_store_from_file (store, file, NULL, cancellable, error)) {
		gs_utils_error_convert_appstream (error);
		return FALSE;
	}

	/* override the *AppStream* origin */
	apps = as_store_get_apps (store);
	for (guint i = 0; i < apps->len; i++) {
		AsApp *app = g_ptr_array_index (apps, i);
		as_app_set_origin (app, flatpak_remote_get_name (xremote));
	}

	/* only add the specific app for noenumerate=true */
	if (flatpak_remote_get_noenumerate (xremote)) {
		g_autofree gchar *tmp = NULL;
		tmp = g_strdup (flatpak_remote_get_name (xremote));
		g_strdelimit (tmp, "-", '\0');
		only_app_id = g_strdup_printf ("%s.desktop", tmp);
	}

	/* do we want to filter to the default branch */
	settings = g_settings_new ("org.gnome.software");
	if (g_settings_get_boolean (settings, "filter-default-branch"))
		default_branch = flatpak_remote_get_default_branch (xremote);

	/* get all the apps and fix them up */
	app_filtered = g_ptr_array_new ();
	for (guint i = 0; i < apps->len; i++) {
		AsApp *app = g_ptr_array_index (apps, i);

		/* filter to app */
		if (only_app_id != NULL &&
		    g_strcmp0 (as_app_get_id (app), only_app_id) != 0) {
			as_app_set_kind (app, AS_APP_KIND_UNKNOWN);
			continue;
		}

		/* filter by branch */
		if (default_branch != NULL &&
		    g_strcmp0 (as_app_get_branch (app), default_branch) != 0) {
			g_debug ("not adding app with branch %s as filtering to %s",
				 as_app_get_branch (app), default_branch);
			continue;
		}

		/* fix the names when using old versions of appstream-compose */
		gs_flatpak_remove_prefixed_names (app);

		/* add */
		as_app_set_scope (app, self->scope);
		as_app_set_origin (app, flatpak_remote_get_name (xremote));
		as_app_add_keyword (app, NULL, "flatpak");
		g_debug ("adding %s", as_app_get_unique_id (app));
		g_ptr_array_add (app_filtered, app);
	}

	/* add them to the main store */
	as_store_add_apps (self->store, app_filtered);

	/* ensure the token cache for all apps */
	as_store_load_search_cache (store);

	return TRUE;
}

static gchar *
gs_flatpak_discard_desktop_suffix (const gchar *app_id)
{
	const gchar *desktop_suffix = ".desktop";
	guint app_prefix_len;

	if (!g_str_has_suffix (app_id, desktop_suffix))
		return g_strdup (app_id);

	app_prefix_len = strlen (app_id) - strlen (desktop_suffix);
	return g_strndup (app_id, app_prefix_len);
}

static void
gs_flatpak_rescan_installed (GsFlatpak *self,
			     GCancellable *cancellable,
			     GError **error)
{
	GPtrArray *icons;
	const gchar *fn;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GFile) path = NULL;
	g_autoptr(GDir) dir = NULL;
	g_autofree gchar *path_str = NULL;
	g_autofree gchar *path_exports = NULL;
	g_autofree gchar *path_apps = NULL;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::rescan-installed",
				  gs_flatpak_get_id (self));
	g_assert (ptask != NULL);

	/* add all installed desktop files */
	path = flatpak_installation_get_path (self->installation);
	path_str = g_file_get_path (path);
	path_exports = g_build_filename (path_str, "exports", NULL);
	path_apps = g_build_filename (path_exports, "share", "applications", NULL);
	dir = g_dir_open (path_apps, 0, NULL);
	if (dir == NULL)
		return;
	while ((fn = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *fn_desktop = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(AsApp) app = NULL;
		g_autoptr(AsFormat) format = as_format_new ();
		g_autoptr(FlatpakInstalledRef) app_ref = NULL;
		g_autofree gchar *app_id = NULL;

		/* ignore */
		if (g_strcmp0 (fn, "mimeinfo.cache") == 0)
			continue;

		/* parse desktop files */
		app = as_app_new ();
		fn_desktop = g_build_filename (path_apps, fn, NULL);
		if (!as_app_parse_file (app, fn_desktop, 0, &error_local)) {
			g_warning ("failed to parse %s: %s",
				   fn_desktop, error_local->message);
			continue;
		}

		/* fix up icons */
		icons = as_app_get_icons (app);
		for (guint i = 0; i < icons->len; i++) {
			AsIcon *ic = g_ptr_array_index (icons, i);
			if (as_icon_get_kind (ic) == AS_ICON_KIND_UNKNOWN) {
				as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
				as_icon_set_prefix (ic, path_exports);
			}
		}

		/* fix the names when using old versions of appstream-compose */
		gs_flatpak_remove_prefixed_names (app);

		/* add */
		as_app_set_state (app, AS_APP_STATE_INSTALLED);
		as_app_set_scope (app, self->scope);
		as_format_set_kind (format, AS_FORMAT_KIND_DESKTOP);
		as_format_set_filename (format, fn_desktop);
		as_app_add_format (app, format);

		app_id = gs_flatpak_discard_desktop_suffix (fn);
		app_ref = flatpak_installation_get_current_installed_app (self->installation,
									  app_id,
									  cancellable,
									  &error_local);
		if (app_ref == NULL) {
			g_warning ("Could not get app (from ID '%s') for installed desktop "
				   "file %s: %s", app_id, fn_desktop, error_local->message);
			continue;
		}

		as_app_set_branch (app, flatpak_ref_get_branch (FLATPAK_REF (app_ref)));
		as_app_set_icon_path (app, path_exports);
		as_app_add_keyword (app, NULL, "flatpak");
		as_store_add_app (self->store, app);
	}
}

static gboolean
gs_flatpak_rescan_appstream_store (GsFlatpak *self,
				   GCancellable *cancellable,
				   GError **error)
{
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GPtrArray) xremotes = NULL;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::rescan-appstream",
				  gs_flatpak_get_id (self));
	g_assert (ptask != NULL);

	/* remove all components */
	as_store_remove_all (self->store);

	/* go through each remote adding metadata */
	xremotes = flatpak_installation_list_remotes (self->installation,
						      cancellable,
						      error);
	if (xremotes == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < xremotes->len; i++) {
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		if (flatpak_remote_get_disabled (xremote))
			continue;
		g_debug ("found remote %s",
			 flatpak_remote_get_name (xremote));
		if (!gs_flatpak_add_apps_from_xremote (self, xremote, cancellable, error))
			return FALSE;
	}

	/* add any installed files without AppStream info */
	gs_flatpak_rescan_installed (self, cancellable, error);

	return TRUE;
}

gboolean
gs_flatpak_setup (GsFlatpak *self, GCancellable *cancellable, GError **error)
{
	/* watch for changes */
	self->monitor = flatpak_installation_create_monitor (self->installation,
							     cancellable,
							     error);
	if (self->monitor == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	self->changed_id =
		g_signal_connect (self->monitor, "changed",
				  G_CALLBACK (gs_plugin_flatpak_changed_cb), self);

	/* success */
	return TRUE;
}

typedef struct {
	GsPlugin	*plugin;
	GsApp		*app;
	guint		 job_max;
	guint		 job_now;
} GsFlatpakProgressHelper;

static void
gs_flatpak_progress_helper_free (GsFlatpakProgressHelper *phelper)
{
	g_object_unref (phelper->plugin);
	if (phelper->app != NULL)
		g_object_unref (phelper->app);
	g_slice_free (GsFlatpakProgressHelper, phelper);
}

static GsFlatpakProgressHelper *
gs_flatpak_progress_helper_new (GsPlugin *plugin, GsApp *app)
{
	GsFlatpakProgressHelper *phelper;
	phelper = g_slice_new0 (GsFlatpakProgressHelper);
	phelper->plugin = g_object_ref (plugin);
	if (app != NULL)
		phelper->app = g_object_ref (app);
	return phelper;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsFlatpakProgressHelper, gs_flatpak_progress_helper_free)

static void
gs_flatpak_progress_cb (const gchar *status,
			guint progress,
			gboolean estimating,
			gpointer user_data)
{
	GsFlatpakProgressHelper *phelper = (GsFlatpakProgressHelper *) user_data;
	GsPluginStatus plugin_status = GS_PLUGIN_STATUS_DOWNLOADING;

	/* fix up */
	if (phelper->job_max == 0)
		phelper->job_max = 1;

	if (phelper->app != NULL) {
		gdouble job_factor = 1.f / phelper->job_max;
		gdouble offset = 100.f * job_factor * phelper->job_now;
		gs_app_set_progress (phelper->app, offset + (progress * job_factor));

		switch (gs_app_get_state (phelper->app)) {
		case AS_APP_STATE_INSTALLING:
		case AS_APP_STATE_PURCHASING:
			plugin_status = GS_PLUGIN_STATUS_INSTALLING;
			break;
		case AS_APP_STATE_REMOVING:
			plugin_status = GS_PLUGIN_STATUS_REMOVING;
			break;
		default:
			break;
		}
	}
	gs_plugin_status_update (phelper->plugin, phelper->app, plugin_status);
}

static gboolean
gs_flatpak_refresh_appstream_remote (GsFlatpak *self,
				     const gchar *remote_name,
				     GCancellable *cancellable,
				     GError **error)
{
	g_autofree gchar *str = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (self->plugin));
	g_autoptr(GsFlatpakProgressHelper) phelper = NULL;
	g_autoptr(GError) local_error = NULL;

	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::refresh-appstream{%s}",
				  gs_flatpak_get_id (self),
				  remote_name);
	g_assert (ptask != NULL);

	/* TRANSLATORS: status text when downloading new metadata */
	str = g_strdup_printf (_("Getting flatpak metadata for %s…"), remote_name);
	gs_app_set_summary_missing (app_dl, str);
	gs_plugin_status_update (self->plugin, app_dl, GS_PLUGIN_STATUS_DOWNLOADING);

	if (!flatpak_installation_update_remote_sync (self->installation,
						      remote_name,
						      cancellable,
						      &local_error)) {
		g_debug ("Failed to update metadata for remote %s: %s\n",
			 remote_name, local_error->message);
		gs_flatpak_error_convert (&local_error);
		g_propagate_error (error, g_steal_pointer (&local_error));
		return FALSE;
	}
	phelper = gs_flatpak_progress_helper_new (self->plugin, app_dl);
	if (!flatpak_installation_update_appstream_full_sync (self->installation,
							      remote_name,
							      NULL, /* arch */
							      gs_flatpak_progress_cb,
							      phelper,
							      NULL, /* out_changed */
							      cancellable,
							      error)) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	/* success */
	gs_app_set_progress (app_dl, 100);
	return TRUE;
}

static gboolean
gs_flatpak_refresh_appstream (GsFlatpak *self, guint cache_age,
			      GCancellable *cancellable, GError **error)
{
	gboolean ret;
	gboolean something_changed = FALSE;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GPtrArray) xremotes = NULL;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::refresh-appstream",
				  gs_flatpak_get_id (self));
	g_assert (ptask != NULL);

	/* get remotes */
	xremotes = flatpak_installation_list_remotes (self->installation,
						      cancellable,
						      error);
	if (xremotes == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < xremotes->len; i++) {
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
		ret = gs_flatpak_refresh_appstream_remote (self,
							   remote_name,
							   cancellable,
							   &error_local);
		if (!ret) {
			g_autoptr(GsPluginEvent) event = NULL;
			if (g_error_matches (error_local,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED)) {
				g_debug ("Failed to get AppStream metadata: %s",
					 error_local->message);
				/* don't try to fetch this again until refresh() */
				g_hash_table_insert (self->broken_remotes,
						     g_strdup (remote_name),
						     GUINT_TO_POINTER (1));
				continue;
			}

			/* allow the plugin loader to decide if this should be
			 * shown the user, possibly only for interactive jobs */
			event = gs_plugin_event_new ();
			gs_flatpak_error_convert (&error_local);
			gs_plugin_event_set_error (event, error_local);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			gs_plugin_report_event (self->plugin, event);
			continue;
		}

		/* add the new AppStream repo to the shared store */
		file = flatpak_remote_get_appstream_dir (xremote, NULL);
		appstream_fn = g_file_get_path (file);
		g_debug ("using AppStream metadata found at: %s", appstream_fn);

		/* trigger the symlink rebuild */
		something_changed = TRUE;
	}

	/* ensure the AppStream store is up to date */
	if (something_changed ||
	    as_store_get_size (self->store) == 0) {
		if (!gs_flatpak_rescan_appstream_store (self, cancellable, error))
			return FALSE;
	}

	return TRUE;
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

	/* if it's a runtime, check if the main-app info should be set */
	if (gs_app_get_kind (app) == AS_APP_KIND_RUNTIME &&
	    gs_flatpak_app_get_main_app_ref_name (app) == NULL) {
		g_autoptr(GError) error = NULL;
		g_autoptr(GKeyFile) metadata_file = NULL;
		metadata_file = g_key_file_new ();
		if (g_key_file_load_from_file (metadata_file, metadata_fn,
					       G_KEY_FILE_NONE, &error)) {
			g_autofree gchar *main_app = g_key_file_get_string (metadata_file,
									    "ExtensionOf",
									    "ref", NULL);
			if (main_app != NULL)
				gs_flatpak_app_set_main_app_ref_name (app, main_app);
		} else {
			g_warning ("Error loading the metadata file for '%s': %s",
				   gs_app_get_unique_id (app), error->message);
		}
	}

	/* this is faster than resolving */
	if (gs_app_get_origin (app) == NULL)
		gs_app_set_origin (app, flatpak_installed_ref_get_origin (xref));

	/* this is faster than flatpak_installation_fetch_remote_size_sync() */
	size_installed = flatpak_installed_ref_get_installed_size (xref);
	if (size_installed != 0)
		gs_app_set_size_installed (app, size_installed);
}

static GsApp *
gs_flatpak_create_installed (GsFlatpak *self,
			     FlatpakInstalledRef *xref,
			     GError **error)
{
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
	app = gs_flatpak_create_app (self, FLATPAK_REF (xref));
	gs_flatpak_set_metadata_installed (self, app, xref);
	return g_steal_pointer (&app);
}

gboolean
gs_flatpak_add_installed (GsFlatpak *self, GsAppList *list,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autoptr(GPtrArray) xrefs = NULL;

	/* get apps and runtimes */
	xrefs = flatpak_installation_list_installed_refs (self->installation,
							  cancellable, error);
	if (xrefs == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GsApp) app = gs_flatpak_create_installed (self, xref, &error_local);
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

	/* get installed apps and runtimes */
	xrefs = flatpak_installation_list_installed_refs (self->installation,
							  cancellable,
							  error);
	if (xrefs == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	/* get available remotes */
	xremotes = flatpak_installation_list_remotes (self->installation,
						      cancellable,
						      error);
	if (xremotes == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < xremotes->len; i++) {
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		g_autoptr(GsApp) app = NULL;

		/* apps installed from bundles add their own remote that only
		 * can be used for updating that app only -- so hide them */
		if (flatpak_remote_get_noenumerate (xremote))
			continue;

		/* create app */
		app = gs_flatpak_create_source (self, xremote);
		gs_app_list_add (list, app);

		/* add related apps, i.e. what was installed from there */
		for (guint j = 0; j < xrefs->len; j++) {
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
			if (gs_app_get_state (related) == AS_APP_STATE_UNKNOWN)
				gs_app_set_state (related, AS_APP_STATE_INSTALLED);
			gs_app_add_related (app, related);
		}
	}
	return TRUE;
}

GsApp *
gs_flatpak_find_source_by_url (GsFlatpak *self,
			       const gchar *url,
			       GCancellable *cancellable,
			       GError **error)
{
	g_autoptr(GPtrArray) xremotes = NULL;

	g_return_val_if_fail (url != NULL, NULL);

	xremotes = flatpak_installation_list_remotes (self->installation, cancellable, error);
	if (xremotes == NULL)
		return NULL;
	for (guint i = 0; i < xremotes->len; i++) {
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		g_autofree gchar *url_tmp = flatpak_remote_get_url (xremote);
		if (g_strcmp0 (url, url_tmp) == 0)
			return gs_flatpak_create_source (self, xremote);
	}
	g_set_error (error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_NOT_SUPPORTED,
		     "cannot find %s", url);
	return NULL;
}

/* transfer full */
GsApp *
gs_flatpak_ref_to_app (GsFlatpak *self, const gchar *ref,
		       GCancellable *cancellable, GError **error)
{
	g_autoptr(GPtrArray) xremotes = NULL;
	g_autoptr(GPtrArray) xrefs = NULL;

	g_return_val_if_fail (ref != NULL, NULL);

	/* get all the installed apps (no network I/O) */
	xrefs = flatpak_installation_list_installed_refs (self->installation,
							  cancellable,
							  error);
	if (xrefs == NULL) {
		gs_flatpak_error_convert (error);
		return NULL;
	}
	for (guint i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);
		g_autofree gchar *ref_tmp = flatpak_ref_format_ref (FLATPAK_REF (xref));
		if (g_strcmp0 (ref, ref_tmp) == 0)
			return gs_flatpak_create_installed (self, xref, error);
	}

	/* look at each remote xref */
	xremotes = flatpak_installation_list_remotes (self->installation,
						      cancellable, error);
	if (xremotes == NULL) {
		gs_flatpak_error_convert (error);
		return NULL;
	}
	for (guint i = 0; i < xremotes->len; i++) {
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) refs_remote = NULL;

		/* disabled */
		if (flatpak_remote_get_disabled (xremote))
			continue;
		refs_remote = flatpak_installation_list_remote_refs_sync (self->installation,
									  flatpak_remote_get_name (xremote),
									  cancellable,
									  &error_local);
		if (refs_remote == NULL) {
			g_debug ("failed to list refs in '%s': %s",
				 flatpak_remote_get_name (xremote),
				 error_local->message);
			continue;
		}
		for (guint j = 0; j < refs_remote->len; j++) {
			FlatpakRef *xref = g_ptr_array_index (refs_remote, j);
			g_autofree gchar *ref_tmp = flatpak_ref_format_ref (xref);
			if (g_strcmp0 (ref, ref_tmp) == 0) {
				GsApp *app;
				app = gs_flatpak_create_app (self, xref);
				gs_app_set_origin (app, flatpak_remote_get_name (xremote));
				return app;
			}
		}
	}

	/* nothing found */
	g_set_error (error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_NOT_SUPPORTED,
		     "cannot find %s", ref);
	return NULL;
}

gboolean
gs_flatpak_app_install_source (GsFlatpak *self, GsApp *app,
			       GCancellable *cancellable,
			       GError **error)
{
	const gchar *gpg_key;
	const gchar *branch;
	g_autoptr(FlatpakRemote) xremote = NULL;

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
	flatpak_remote_set_url (xremote, gs_flatpak_app_get_repo_url (app));
	flatpak_remote_set_noenumerate (xremote, FALSE);
	if (gs_app_get_summary (app) != NULL)
		flatpak_remote_set_title (xremote, gs_app_get_summary (app));

	/* decode GPG key if set */
	gpg_key = gs_flatpak_app_get_repo_gpgkey (app);
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

	/* default branch */
	branch = gs_app_get_branch (app);
	if (branch != NULL)
		flatpak_remote_set_default_branch (xremote, branch);

	/* install it */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!flatpak_installation_modify_remote (self->installation,
						 xremote,
						 cancellable,
						 error)) {
		gs_flatpak_error_convert (error);
		g_prefix_error (error, "cannot modify remote: ");
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* refresh the AppStream data manually */
	if (!gs_flatpak_add_apps_from_xremote (self, xremote, cancellable, error)) {
		g_prefix_error (error, "cannot refresh remote AppStream: ");
		return FALSE;
	}

	/* success */
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
}

static GsApp *
get_main_app_of_related (GsFlatpak *self,
			 GsApp *related_app,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autoptr(FlatpakInstalledRef) ref = NULL;
	const gchar *ref_name;
	g_auto(GStrv) app_tokens = NULL;

	ref_name = gs_flatpak_app_get_main_app_ref_name (related_app);
	if (ref_name == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			     "%s doesn't have a main app set to it.",
			     gs_app_get_unique_id (related_app));
		return NULL;
	}

	app_tokens = g_strsplit (ref_name, "/", -1);
	if (g_strv_length (app_tokens) != 4) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "The main app of %s has an invalid name: %s",
			     gs_app_get_unique_id (related_app), ref_name);
		return NULL;
	}

	/* this function only returns G_IO_ERROR_NOT_FOUND when the metadata file
	 * is missing, but if that's the case then things should have broken before
	 * this point */
	ref = flatpak_installation_get_installed_ref (self->installation,
						      FLATPAK_REF_KIND_APP,
						      app_tokens[1],
						      app_tokens[2],
						      app_tokens[3],
						      cancellable,
						      error);
	if (ref == NULL)
		return NULL;

	return gs_flatpak_create_installed (self, ref, error);
}

static GsApp *
get_real_app_for_update (GsFlatpak *self,
			 GsApp *app,
			 GCancellable *cancellable,
			 GError **error)
{
	GsApp *main_app = NULL;
	g_autoptr(GError) error_local = NULL;

	if (gs_app_get_kind (app) == AS_APP_KIND_RUNTIME)
		main_app = get_main_app_of_related (self, app, cancellable, &error_local);

	if (main_app == NULL) {
		/* not all runtimes are extensions, and in that case we get the
		 * not-found error, so we only report other types of errors */
		if (error_local != NULL &&
		    !g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_propagate_error (error, g_steal_pointer (&error_local));
			gs_flatpak_error_convert (error);
			return NULL;
		}

		main_app = g_object_ref (app);
	} else {
		g_debug ("Related extension app %s of main app %s is updatable, so "
			 "setting the latter's state instead.", gs_app_get_unique_id (app),
			 gs_app_get_unique_id (main_app));
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
	}

	return main_app;
}

gboolean
gs_flatpak_add_updates (GsFlatpak *self, GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(GPtrArray) xrefs = NULL;

	/* get all the updatable apps and runtimes */
	xrefs = flatpak_installation_list_installed_refs_for_update (self->installation,
								     cancellable,
								     error);
	if (xrefs == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	/* look at each installed xref */
	for (guint i = 0; i < xrefs->len; i++) {
		FlatpakInstalledRef *xref = g_ptr_array_index (xrefs, i);
		const gchar *commit;
		const gchar *latest_commit;
		g_autoptr(GsApp) app = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GsApp) main_app = NULL;

		/* check the application has already been downloaded */
		commit = flatpak_ref_get_commit (FLATPAK_REF (xref));
		latest_commit = flatpak_installed_ref_get_latest_commit (xref);
		if (latest_commit == NULL) {
			g_debug ("could not get latest commit for %s",
				 flatpak_ref_get_name (FLATPAK_REF (xref)));
			continue;
		}

		app = gs_flatpak_create_installed (self, xref, &error_local);
		if (app == NULL) {
			g_warning ("failed to add flatpak: %s", error_local->message);
			continue;
		}
		main_app = get_real_app_for_update (self, app, cancellable, &error_local);
		if (main_app == NULL) {
			g_debug ("Couldn't get the main app for updatable app extension %s: "
				 "%s; adding the app itself to the updates list...",
				 gs_app_get_unique_id (app), error_local->message);
			main_app = g_object_ref (app);
		}
		gs_app_set_state (main_app, AS_APP_STATE_UPDATABLE_LIVE);

		/* already downloaded */
		if (g_strcmp0 (commit, latest_commit) != 0) {
			g_debug ("%s has a downloaded update %s->%s",
				 flatpak_ref_get_name (FLATPAK_REF (xref)),
				 commit, latest_commit);
			gs_app_set_update_details (main_app, NULL);
			gs_app_set_update_version (main_app, NULL);
			gs_app_set_update_urgency (main_app, AS_URGENCY_KIND_UNKNOWN);
			gs_app_set_size_download (main_app, 0);
			gs_app_list_add (list, main_app);

		/* needs download */
		} else {
			guint64 download_size = 0;
			g_debug ("%s needs update",
				 flatpak_ref_get_name (FLATPAK_REF (xref)));

			/* get the current download size */
			if (gs_app_get_size_download (main_app) == 0) {
				if (!flatpak_installation_fetch_remote_size_sync (self->installation,
										  gs_app_get_origin (app),
										  FLATPAK_REF (xref),
										  &download_size,
										  NULL,
										  cancellable,
										  &error_local)) {
					g_warning ("failed to get download size: %s",
						   error_local->message);
					gs_app_set_size_download (main_app, GS_APP_SIZE_UNKNOWABLE);
				} else {
					gs_app_set_size_download (main_app, download_size);
				}
			}
		}
		gs_app_list_add (list, main_app);
	}

	/* success */
	return TRUE;
}

gboolean
gs_flatpak_refresh (GsFlatpak *self,
		    guint cache_age,
		    GCancellable *cancellable,
		    GError **error)
{
	/* give all the repos a second chance */
	g_hash_table_remove_all (self->broken_remotes);

	/* manually drop the cache */
	if (!flatpak_installation_drop_caches (self->installation,
					       cancellable,
					       error)) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	/* update AppStream metadata */
	return gs_flatpak_refresh_appstream (self, cache_age, cancellable, error);
}

static gboolean
gs_plugin_refine_item_origin_hostname (GsFlatpak *self, GsApp *app,
				       GCancellable *cancellable,
				       GError **error)
{
	g_autoptr(FlatpakRemote) xremote = NULL;
	g_autofree gchar *url = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GError) error_local = NULL;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::refine-origin-hostname{%s}",
				  gs_flatpak_get_id (self),
				  gs_app_get_id (app));
	g_assert (ptask != NULL);

	/* already set */
	if (gs_app_get_origin_hostname (app) != NULL)
		return TRUE;

	/* no origin */
	if (gs_app_get_origin (app) == NULL)
		return TRUE;

	/* get the remote  */
	xremote = flatpak_installation_get_remote_by_name (self->installation,
							   gs_app_get_origin (app),
							   cancellable,
							   &error_local);
	if (xremote == NULL) {
		if (g_error_matches (error_local,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND)) {
			/* if the user deletes the -origin remote for a locally
			 * installed flatpakref file then we should just show
			 * 'localhost' and not return an error */
			gs_app_set_origin_hostname (app, "");
			return TRUE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));
		gs_flatpak_error_convert (error);
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
	if (gs_flatpak_app_get_ref_name (app) != NULL)
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
		gs_flatpak_error_convert (error);
		g_prefix_error (error, "failed to parse '%s': ",
				gs_app_get_source_default (app));
		return FALSE;
	}
	gs_flatpak_set_metadata (self, app, xref);

	/* success */
	return TRUE;
}

static gboolean
gs_plugin_refine_item_origin (GsFlatpak *self,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autofree gchar *ref_display = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GPtrArray) xremotes = NULL;

	/* already set */
	if (gs_app_get_origin (app) != NULL)
		return TRUE;

	/* not applicable */
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE_LOCAL)
		return TRUE;

	/* ensure metadata exists */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::refine-origin",
				  gs_flatpak_get_id (self));
	g_assert (ptask != NULL);
	if (!gs_refine_item_metadata (self, app, cancellable, error))
		return FALSE;

	/* find list of remotes */
	ref_display = gs_flatpak_app_get_ref_display (app);
	g_debug ("looking for a remote for %s", ref_display);
	xremotes = flatpak_installation_list_remotes (self->installation,
						      cancellable, error);
	if (xremotes == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < xremotes->len; i++) {
		const gchar *remote_name;
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		g_autoptr(FlatpakRemoteRef) xref = NULL;
		g_autoptr(GError) error_local = NULL;

		/* not enabled */
		if (flatpak_remote_get_disabled (xremote))
			continue;

		/* sync */
		remote_name = flatpak_remote_get_name (xremote);
		g_debug ("looking at remote %s", remote_name);
		xref = flatpak_installation_fetch_remote_ref_sync (self->installation,
								   remote_name,
								   gs_flatpak_app_get_ref_kind (app),
								   gs_flatpak_app_get_ref_name (app),
								   gs_flatpak_app_get_ref_arch (app),
								   gs_flatpak_app_get_ref_branch (app),
								   cancellable,
								   &error_local);
		if (xref != NULL) {
			g_debug ("found remote %s", remote_name);
			gs_app_set_origin (app, remote_name);
			gs_flatpak_app_set_commit (app, flatpak_ref_get_commit (FLATPAK_REF (xref)));
			gs_plugin_refine_item_scope (self, app);
			return TRUE;
		}
		g_debug ("%s failed to find remote %s: %s",
			 ref_display, remote_name, error_local->message);
	}

	/* not found */
	g_set_error (error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_NOT_SUPPORTED,
		     "%s not found in any remote",
		     ref_display);
	return FALSE;
}

static FlatpakRef *
gs_flatpak_create_fake_ref (GsApp *app, GError **error)
{
	FlatpakRef *xref;
	g_autofree gchar *id = NULL;
	id = g_strdup_printf ("%s/%s/%s/%s",
			      gs_flatpak_app_get_ref_kind_as_str (app),
			      gs_flatpak_app_get_ref_name (app),
			      gs_flatpak_app_get_ref_arch (app),
			      gs_flatpak_app_get_ref_branch (app));
	xref = flatpak_ref_parse (id, error);
	if (xref == NULL) {
		gs_flatpak_error_convert (error);
		return NULL;
	}
	return xref;
}

gboolean
gs_flatpak_refine_app_state (GsFlatpak *self,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autoptr(GPtrArray) xrefs = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FlatpakInstalledRef) ref = NULL;
	g_autoptr(GError) error_local = NULL;

	/* already found */
	if (gs_app_get_state (app) != AS_APP_STATE_UNKNOWN)
		return TRUE;

	/* need broken out metadata */
	if (!gs_refine_item_metadata (self, app, cancellable, error))
		return FALSE;

	/* get apps and runtimes */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::refine-action",
				  gs_flatpak_get_id (self));
	g_assert (ptask != NULL);
	ref = flatpak_installation_get_installed_ref (self->installation,
						      gs_flatpak_app_get_ref_kind (app),
						      gs_flatpak_app_get_ref_name (app),
						      gs_flatpak_app_get_ref_arch (app),
						      gs_flatpak_app_get_ref_branch (app),
						      cancellable,
						      &error_local);
	if (ref != NULL) {
		g_debug ("marking %s as installed with flatpak",
			 gs_app_get_id (app));
		gs_flatpak_set_metadata_installed (self, app, ref);
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	} else if (!g_error_matches (error_local, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED)) {
		g_propagate_error (error, g_steal_pointer (&error_local));
		gs_flatpak_error_convert (error);
		return FALSE;
	}

	/* ensure origin set */
	if (!gs_plugin_refine_item_origin (self, app, cancellable, error))
		return FALSE;

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
			gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
			g_debug ("failed to find %s remote %s for %s",
				 self->id,
				 gs_app_get_origin (app),
				 gs_app_get_unique_id (app));
		}
	}

	/* success */
	return TRUE;
}

static GsApp *
gs_flatpak_create_runtime (GsFlatpak *self, GsApp *parent, const gchar *runtime)
{
	g_autofree gchar *source = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GsApp) app_cache = NULL;
	g_autoptr(GsApp) app = NULL;

	/* get the name/arch/branch */
	split = g_strsplit (runtime, "/", -1);
	if (g_strv_length (split) != 3)
		return NULL;

	/* create the complete GsApp from the single string */
	app = gs_app_new (split[0]);
	gs_flatpak_claim_app (self, app);
	source = g_strdup_printf ("runtime/%s", runtime);
	gs_app_add_source (app, source);
	gs_app_set_kind (app, AS_APP_KIND_RUNTIME);
	gs_app_set_branch (app, split[2]);

	/* search in the cache */
	app_cache = gs_plugin_cache_lookup (self->plugin, gs_app_get_unique_id (app));
	if (app_cache != NULL) {
		/* since the cached runtime can have been created somewhere else
		 * (we're using a global cache), we need to make sure that a
		 * source is set */
		if (gs_app_get_source_default (app_cache) == NULL)
			gs_app_add_source (app_cache, source);
		return g_steal_pointer (&app_cache);
	}

	/* set superclassed app properties */
	gs_flatpak_app_set_ref_kind (app, FLATPAK_REF_KIND_RUNTIME);
	gs_flatpak_app_set_ref_name (app, split[0]);
	gs_flatpak_app_set_ref_arch (app, split[1]);
	gs_flatpak_app_set_ref_branch (app, split[2]);

	/* save in the cache */
	gs_plugin_cache_add (self->plugin, NULL, app);
	return g_steal_pointer (&app);
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
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	name = g_key_file_get_string (kf, "Application", "name", error);
	if (name == NULL) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	gs_flatpak_app_set_ref_name (app, name);
	runtime = g_key_file_get_string (kf, "Application", "runtime", error);
	if (runtime == NULL) {
		gs_flatpak_error_convert (error);
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
	app_runtime = gs_flatpak_create_runtime (self, app, runtime);
	if (app_runtime != NULL) {
		gs_plugin_refine_item_scope (self, app_runtime);
		gs_app_set_runtime (app, app_runtime);
	}

	return TRUE;
}

static GBytes *
gs_flatpak_fetch_remote_metadata (GsFlatpak *self,
				  GsApp *app,
				  GCancellable *cancellable,
				  GError **error)
{
	g_autoptr(GBytes) data = NULL;
	g_autoptr(FlatpakRef) xref = NULL;

	/* no origin */
	if (gs_app_get_origin (app) == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "no origin set when getting metadata for %s",
			     gs_app_get_unique_id (app));
		return NULL;
	}

	/* fetch from the server */
	xref = gs_flatpak_create_fake_ref (app, error);
	if (xref == NULL)
		return NULL;
	data = flatpak_installation_fetch_remote_metadata_sync (self->installation,
								gs_app_get_origin (app),
								xref,
								cancellable,
								error);
	if (data == NULL) {
		gs_flatpak_error_convert (error);
		return NULL;
	}
	return g_steal_pointer (&data);
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
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GFile) installation_path = NULL;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::refine-metadata{%s}",
				  gs_flatpak_get_id (self),
				  gs_app_get_id (app));
	g_assert (ptask != NULL);

	/* not applicable */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
		return TRUE;
	if (gs_flatpak_app_get_ref_kind (app) != FLATPAK_REF_KIND_APP)
		return TRUE;

	/* already done */
	if (gs_app_has_kudo (app, GS_APP_KUDO_SANDBOXED)) {
		g_debug ("skipping reading metadata");
		return TRUE;
	}

	/* this is quicker than doing network IO */
	installation_path = flatpak_installation_get_path (self->installation);
	installation_path_str = g_file_get_path (installation_path);
	install_path = g_build_filename (installation_path_str,
					 gs_flatpak_app_get_ref_kind_as_str (app),
					 gs_flatpak_app_get_ref_name (app),
					 gs_flatpak_app_get_ref_arch (app),
					 gs_flatpak_app_get_ref_branch (app),
					 "active",
					 "metadata",
					 NULL);
	if (g_file_test (install_path, G_FILE_TEST_EXISTS)) {
		if (!g_file_get_contents (install_path, &contents, &len, error))
			return FALSE;
		str = contents;
	} else {
		data = gs_flatpak_fetch_remote_metadata (self, app, cancellable,
							 error);
		if (data == NULL)
			return FALSE;
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
						      gs_flatpak_app_get_ref_kind (app),
						      gs_flatpak_app_get_ref_name (app),
						      gs_flatpak_app_get_ref_arch (app),
						      gs_flatpak_app_get_ref_branch (app),
						      cancellable,
						      error);
	if (ref == NULL)
		gs_flatpak_error_convert (error);
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

	/* not applicable */
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE_LOCAL)
		return TRUE;
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
		return TRUE;

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
	    gs_flatpak_app_get_ref_kind (app) == FLATPAK_REF_KIND_APP) {
		GsApp *app_runtime;

		/* is the app_runtime already installed? */
		app_runtime = gs_app_get_runtime (app);
		if (!gs_flatpak_refine_app_state (self,
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
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::refine-size",
				  gs_flatpak_get_id (self));
	g_assert (ptask != NULL);
	if (!gs_plugin_refine_item_origin (self, app,
					   cancellable, error))
		return FALSE;

	/* if the app is installed we use the ref to fetch the installed size
	 * and ignore the download size as this is faster */
	if (gs_app_is_installed (app)) {
		g_autoptr(FlatpakInstalledRef) xref = NULL;
		xref = gs_flatpak_get_installed_ref (self, app, cancellable, error);
		if (xref == NULL)
			return FALSE;
		installed_size = flatpak_installed_ref_get_installed_size (xref);
		if (installed_size == 0)
			installed_size = GS_APP_SIZE_UNKNOWABLE;
	} else {
		g_autoptr(FlatpakRef) xref = NULL;
		g_autoptr(GError) error_local = NULL;

		/* no origin */
		if (gs_app_get_origin (app) == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no origin set for %s",
				     gs_app_get_unique_id (app));
			return FALSE;
		}
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

static void
gs_flatpak_refine_appstream_release (AsApp *item, GsApp *app)
{
	AsRelease *rel = as_app_get_release_default (item);
	if (rel == NULL)
		return;
	if (as_release_get_version (rel) == NULL)
		return;
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
		gs_app_set_version (app, as_release_get_version (rel));
		break;
	default:
		g_debug ("%s is not installed, so ignoring version of %s",
			 as_app_get_id (item), as_release_get_version (rel));
		break;
	}
}

static gboolean
gs_flatpak_refine_appstream (GsFlatpak *self, GsApp *app, GError **error)
{
	AsApp *item;
	const gchar *unique_id = gs_app_get_unique_id (app);
	g_autoptr(AsProfileTask) ptask = NULL;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::refine-appstream{%s}",
				  gs_flatpak_get_id (self),
				  gs_app_get_id (app));
	g_assert (ptask != NULL);

	if (unique_id == NULL)
		return TRUE;
	item = as_store_get_app_by_unique_id (self->store,
					      unique_id,
					      AS_STORE_SEARCH_FLAG_USE_WILDCARDS);
	if (item == NULL) {
		g_autoptr(GPtrArray) apps = NULL;
		apps = as_store_get_apps_by_id (self->store, gs_app_get_id (app));
		if (apps->len > 0) {
			g_debug ("potential matches for %s:", unique_id);
			for (guint i = 0; i < apps->len; i++) {
				AsApp *app_tmp = g_ptr_array_index (apps, i);
				g_debug ("- %s", as_app_get_unique_id (app_tmp));
			}
		}
		return TRUE;
	}

	if (!gs_appstream_refine_app (self->plugin, app, item, error))
		return FALSE;

	/* use the default release as the version number */
	gs_flatpak_refine_appstream_release (item, app);

	return TRUE;
}

gboolean
gs_flatpak_refine_app (GsFlatpak *self,
		       GsApp *app,
		       GsPluginRefineFlags flags,
		       GCancellable *cancellable,
		       GError **error)
{
	AsAppState old_state = gs_app_get_state (app);
	g_autoptr(AsProfileTask) ptask = NULL;

	/* profile */
	ptask = as_profile_start (gs_plugin_get_profile (self->plugin),
				  "%s::refine{%s}",
				  gs_flatpak_get_id (self),
				  gs_app_get_id (app));
	g_assert (ptask != NULL);

	/* always do AppStream properties */
	if (!gs_flatpak_refine_appstream (self, app, error))
		return FALSE;

	/* flatpak apps can always be removed */
	gs_app_remove_quirk (app, AS_APP_QUIRK_COMPULSORY);

	/* AppStream sets the source to appname/arch/branch */
	if (!gs_refine_item_metadata (self, app, cancellable, error)) {
		g_prefix_error (error, "failed to get metadata: ");
		return FALSE;
	}

	/* check the installed state */
	if (!gs_flatpak_refine_app_state (self, app, cancellable, error)) {
		g_prefix_error (error, "failed to get state: ");
		return FALSE;
	}

	/* scope is fast, do unconditionally */
	gs_plugin_refine_item_scope (self, app);

	/* if the state was changed, perhaps set the version from the release */
	if (old_state != gs_app_get_state (app)) {
		if (!gs_flatpak_refine_appstream (self, app, error))
			return FALSE;
	}

	/* version fallback */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) {
		if (gs_app_get_version (app) == NULL) {
			const gchar *branch;
			branch = gs_flatpak_app_get_ref_branch (app);
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
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME ||
	    flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS) {
		if (!gs_plugin_refine_item_metadata (self, app,
						     cancellable, error)) {
			g_prefix_error (error, "failed to get permissions: ");
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
gs_flatpak_refine_wildcard (GsFlatpak *self, GsApp *app,
			    GsAppList *list, GsPluginRefineFlags flags,
			    GCancellable *cancellable, GError **error)
{
	const gchar *id;
	guint i;
	g_autoptr(GPtrArray) items = NULL;

	/* not valid */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	/* find all apps when matching any prefixes */
	items = as_store_get_apps_by_id (self->store, id);
	for (i = 0; i < items->len; i++) {
		AsApp *item = g_ptr_array_index (items, i);
		g_autoptr(GsApp) new = NULL;

		/* is compatible */
		if (!as_utils_unique_id_equal (gs_app_get_unique_id (app),
					       as_app_get_unique_id (item))) {
			g_debug ("does not match unique ID constraints");
			continue;
		}

		/* does the app have an installation method */
		if (as_app_get_bundle_default (item) == NULL) {
			g_debug ("not using %s for wildcard as no bundle",
				 as_app_get_id (item));
			continue;
		}

		/* new app */
		g_debug ("found %s for wildcard %s",
			 as_app_get_unique_id (item), id);
		new = gs_appstream_create_app (self->plugin, item, NULL);
		if (new == NULL)
			return FALSE;
		gs_flatpak_claim_app (self, new);
		if (!gs_flatpak_refine_app (self, new, flags, cancellable, error))
			return FALSE;
		gs_app_list_add (list, new);
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

	/* check the runtime is installed */
	runtime = gs_app_get_runtime (app);
	if (runtime != NULL) {
		if (!gs_flatpak_refine_app_state (self, runtime, cancellable, error))
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
					  gs_flatpak_app_get_ref_name (app),
					  gs_flatpak_app_get_ref_arch (app),
					  gs_flatpak_app_get_ref_branch (app),
					  NULL,
					  cancellable,
					  error)) {
		gs_flatpak_error_convert (error);
		return FALSE;
	}
	return TRUE;
}

gboolean
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
		gs_flatpak_error_convert (error);
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
		gs_flatpak_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	return TRUE;
}

GsApp *
gs_flatpak_file_to_app_bundle (GsFlatpak *self,
			       GFile *file,
			       GCancellable *cancellable,
			       GError **error)
{
	gint size;
	g_autofree gchar *content_type = NULL;
	g_autoptr(GBytes) appstream_gz = NULL;
	g_autoptr(GBytes) icon_data = NULL;
	g_autoptr(GBytes) metadata = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(FlatpakBundleRef) xref_bundle = NULL;

	/* load bundle */
	xref_bundle = flatpak_bundle_ref_new (file, error);
	if (xref_bundle == NULL) {
		gs_flatpak_error_convert (error);
		g_prefix_error (error, "error loading bundle: ");
		return NULL;
	}

	/* load metadata */
	app = gs_flatpak_create_app (self, FLATPAK_REF (xref_bundle));
	if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED) {
		if (gs_flatpak_app_get_ref_name (app) == NULL)
			gs_flatpak_set_metadata (self, app, FLATPAK_REF (xref_bundle));
		return g_steal_pointer (&app);
	}
	gs_flatpak_app_set_file_kind (app, GS_FLATPAK_APP_FILE_KIND_BUNDLE);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_set_size_installed (app, flatpak_bundle_ref_get_installed_size (xref_bundle));
	gs_flatpak_set_metadata (self, app, FLATPAK_REF (xref_bundle));
	metadata = flatpak_bundle_ref_get_metadata (xref_bundle);
	if (!gs_flatpak_set_app_metadata (self, app,
					  g_bytes_get_data (metadata, NULL),
					  g_bytes_get_size (metadata),
					  error))
		return NULL;

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
			return NULL;
		stream_data = g_converter_input_stream_new (stream_gz,
							    G_CONVERTER (decompressor));

		appstream = g_input_stream_read_bytes (stream_data,
						       0x100000, /* 1Mb */
						       cancellable,
						       error);
		if (appstream == NULL) {
			gs_flatpak_error_convert (error);
			return NULL;
		}
		store = as_store_new ();
		if (!as_store_from_bytes (store, appstream, cancellable, error)) {
			gs_flatpak_error_convert (error);
			return NULL;
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
			return NULL;
		}
		g_debug ("%u applications found in AppStream data",
			 as_store_get_size (store));

		/* find app */
		id = g_strdup_printf ("%s.desktop", gs_flatpak_app_get_ref_name (app));
		item = as_store_get_app_by_id (store, id);
		if (item == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "application %s not found",
				     id);
			return NULL;
		}

		/* copy details from AppStream to app */
		if (!gs_appstream_refine_app (self->plugin, app, item, error))
			return NULL;
	} else {
		g_warning ("no appstream metadata in file");
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST,
				 gs_flatpak_app_get_ref_name (app));
		gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
				    "A flatpak application");
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
			return NULL;
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
	return g_steal_pointer (&app);
}

GsApp *
gs_flatpak_file_to_app_ref (GsFlatpak *self,
			    GFile *file,
			    GCancellable *cancellable,
			    GError **error)
{
	GsApp *runtime;
	const gchar *remote_name;
	gsize len = 0;
	g_autofree gchar *contents = NULL;
	g_autoptr(FlatpakRemoteRef) xref = NULL;
	g_autoptr(GBytes) ref_file_data = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(FlatpakRemote) xremote = NULL;
	g_autoptr(GKeyFile) kf = NULL;
	g_autofree gchar *origin_url = NULL;
	g_autofree gchar *ref_comment = NULL;
	g_autofree gchar *ref_description = NULL;
	g_autofree gchar *ref_homepage = NULL;
	g_autofree gchar *ref_icon = NULL;
	g_autofree gchar *ref_title = NULL;
	g_autofree gchar *ref_name = NULL;

	/* get file data */
	if (!g_file_load_contents (file,
				   cancellable,
				   &contents,
				   &len,
				   NULL,
				   error)) {
		gs_utils_error_convert_gio (error);
		return NULL;
	}

	/* load the file */
	kf = g_key_file_new ();
	if (!g_key_file_load_from_data (kf, contents, len, G_KEY_FILE_NONE, error)) {
		gs_utils_error_convert_gio (error);
		return NULL;
	}

	/* check version */
	if (g_key_file_has_key (kf, "Flatpak Ref", "Version", NULL)) {
		guint64 ver = g_key_file_get_uint64 (kf, "Flatpak Ref", "Version", NULL);
		if (ver != 1) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "unsupported version %" G_GUINT64_FORMAT, ver);
			return NULL;
		}
	}

	/* get name */
	ref_name = g_key_file_get_string (kf, "Flatpak Ref", "Name", error);
	if (ref_name == NULL) {
		gs_utils_error_convert_gio (error);
		return NULL;
	}

	/* install the remote, but not the app */
	ref_file_data = g_bytes_new (contents, len);
	xref = flatpak_installation_install_ref_file (self->installation,
						      ref_file_data,
						      cancellable,
						      error);
	if (xref == NULL) {
		gs_flatpak_error_convert (error);
		return NULL;
	}

	/* load metadata */
	app = gs_flatpak_create_app (self, FLATPAK_REF (xref));
	if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED) {
		if (gs_flatpak_app_get_ref_name (app) == NULL)
			gs_flatpak_set_metadata (self, app, FLATPAK_REF (xref));
		return g_steal_pointer (&app);
	}
	gs_app_add_quirk (app, AS_APP_QUIRK_HAS_SOURCE);
	gs_flatpak_app_set_file_kind (app, GS_FLATPAK_APP_FILE_KIND_REF);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
	gs_flatpak_set_metadata (self, app, FLATPAK_REF (xref));

	/* use the data from the flatpakref file as a fallback */
	ref_title = g_key_file_get_string (kf, "Flatpak Ref", "Title", NULL);
	if (ref_title != NULL)
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, ref_title);
	ref_comment = g_key_file_get_string (kf, "Flatpak Ref", "Comment", NULL);
	if (ref_comment != NULL)
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, ref_comment);
	ref_description = g_key_file_get_string (kf, "Flatpak Ref", "Description", NULL);
	if (ref_description != NULL)
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, ref_description);
	ref_homepage = g_key_file_get_string (kf, "Flatpak Ref", "Homepage", NULL);
	if (ref_homepage != NULL)
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, ref_homepage);
	ref_icon = g_key_file_get_string (kf, "Flatpak Ref", "Icon", NULL);
	if (ref_icon != NULL) {
		g_autoptr(AsIcon) ic = as_icon_new ();
		as_icon_set_kind (ic, AS_ICON_KIND_REMOTE);
		as_icon_set_url (ic, ref_icon);
		gs_app_add_icon (app, ic);
	}

	/* set the origin data */
	remote_name = flatpak_remote_ref_get_remote_name (xref);
	g_debug ("auto-created remote name: %s", remote_name);
	xremote = flatpak_installation_get_remote_by_name (self->installation,
							   remote_name,
							   cancellable,
							   error);
	if (xremote == NULL) {
		gs_flatpak_error_convert (error);
		return NULL;
	}
	origin_url = flatpak_remote_get_url (xremote);
	if (origin_url == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_INVALID_FORMAT,
			     "no URL for remote %s",
			     flatpak_remote_get_name (xremote));
		return NULL;
	}
	gs_app_set_origin (app, remote_name);
	gs_app_set_origin_hostname (app, origin_url);

	/* get the new appstream data (nonfatal for failure) */
	if (!gs_flatpak_refresh_appstream_remote (self, remote_name,
						  cancellable, &error_local)) {
		g_autoptr(GsPluginEvent) event = gs_plugin_event_new ();
		gs_flatpak_error_convert (&error_local);
		gs_plugin_event_set_app (event, app);
		gs_plugin_event_set_error (event, error_local);
		gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
		gs_plugin_report_event (self->plugin, event);
	}

	/* get this now, as it's not going to be available at install time */
	if (!gs_plugin_refine_item_metadata (self, app, cancellable, error))
		return NULL;

	/* the new runtime is available from the RuntimeRepo */
	runtime = gs_app_get_runtime (app);
	if (runtime != NULL && gs_app_get_state (runtime) == AS_APP_STATE_UNKNOWN) {
		g_autofree gchar *uri = NULL;
		uri = g_key_file_get_string (kf, "Flatpak Ref", "RuntimeRepo", NULL);
		gs_flatpak_app_set_runtime_url (runtime, uri);
	}

	/* parse it */
	if (!gs_flatpak_add_apps_from_xremote (self, xremote, cancellable, error))
		return NULL;

	/* get extra AppStream data if available */
	if (!gs_flatpak_refine_appstream (self, app, error))
		return NULL;

	/* success */
	return g_steal_pointer (&app);
}

gboolean
gs_flatpak_search (GsFlatpak *self,
		   gchar **values,
		   GsAppList *list,
		   GCancellable *cancellable,
		   GError **error)
{
	g_autoptr(GsAppList) list_tmp = gs_app_list_new ();
	if (!gs_appstream_store_search (self->plugin, self->store, values, list_tmp,
					cancellable, error))
		return FALSE;
	gs_flatpak_claim_app_list (self, list_tmp);
	gs_app_list_add_list (list, list_tmp);
	return TRUE;
}

gboolean
gs_flatpak_add_category_apps (GsFlatpak *self,
			      GsCategory *category,
			      GsAppList *list,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autoptr(GsAppList) list_tmp = gs_app_list_new ();
	if (!gs_appstream_store_add_category_apps (self->plugin, self->store,
						   category, list_tmp,
						   cancellable, error))
		return FALSE;
	gs_flatpak_claim_app_list (self, list_tmp);
	gs_app_list_add_list (list, list_tmp);
	return TRUE;
}

gboolean
gs_flatpak_add_categories (GsFlatpak *self,
			   GPtrArray *list,
			   GCancellable *cancellable,
			   GError **error)
{
	return gs_appstream_store_add_categories (self->plugin, self->store,
						  list, cancellable, error);
}

gboolean
gs_flatpak_add_popular (GsFlatpak *self,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(GsAppList) list_tmp = gs_app_list_new ();
	if (!gs_appstream_add_popular (self->plugin, self->store, list_tmp,
				       cancellable, error))
		return FALSE;
	gs_flatpak_claim_app_list (self, list_tmp);
	gs_app_list_add_list (list, list_tmp);
	return TRUE;
}

gboolean
gs_flatpak_add_featured (GsFlatpak *self,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autoptr(GsAppList) list_tmp = gs_app_list_new ();
	if (!gs_appstream_add_featured (self->plugin, self->store, list_tmp,
					cancellable, error))
		return FALSE;
	gs_flatpak_claim_app_list (self, list_tmp);
	gs_app_list_add_list (list, list_tmp);
	return TRUE;
}

gboolean
gs_flatpak_add_recent (GsFlatpak *self,
		       GsAppList *list,
		       guint64 age,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(GsAppList) list_tmp = gs_app_list_new ();
	if (!gs_appstream_add_recent (self->plugin, self->store, list_tmp, age,
				      cancellable, error))
		return FALSE;
	gs_flatpak_claim_app_list (self, list_tmp);
	gs_app_list_add_list (list, list_tmp);
	return TRUE;
}

static void
gs_flatpak_store_app_added_cb (AsStore *store, AsApp *app, GsFlatpak *self)
{
	gs_appstream_add_extra_info (self->plugin, app);
}

static void
gs_flatpak_store_app_removed_cb (AsStore *store, AsApp *app, GsFlatpak *self)
{
	g_debug ("AppStream app was removed, doing delete from global cache");
	gs_plugin_cache_remove (self->plugin, as_app_get_unique_id (app));
}

const gchar *
gs_flatpak_get_id (GsFlatpak *self)
{
	if (self->id == NULL) {
		GString *str = g_string_new ("GsFlatpak");
		g_string_append_printf (str, "-%s",
					as_app_scope_to_string (self->scope));
		if (flatpak_installation_get_id (self->installation) != NULL) {
			g_string_append_printf (str, "-%s",
						flatpak_installation_get_id (self->installation));
		}
		if (self->flags & GS_FLATPAK_FLAG_IS_TEMPORARY)
			g_string_append (str, "-temp");
		self->id = g_string_free (str, FALSE);
	}
	return self->id;
}

AsAppScope
gs_flatpak_get_scope (GsFlatpak *self)
{
	return self->scope;
}

FlatpakInstallation *
gs_flatpak_get_installation (GsFlatpak *self)
{
	return self->installation;
}

static void
gs_flatpak_finalize (GObject *object)
{
	GsFlatpak *self;
	g_return_if_fail (GS_IS_FLATPAK (object));
	self = GS_FLATPAK (object);

	if (self->changed_id > 0) {
		g_signal_handler_disconnect (self->monitor, self->changed_id);
		self->changed_id = 0;
	}

	g_free (self->id);
	g_object_unref (self->installation);
	g_object_unref (self->plugin);
	g_object_unref (self->store);
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
	self->store = as_store_new ();
	g_signal_connect (self->store, "app-added",
			  G_CALLBACK (gs_flatpak_store_app_added_cb),
			  self);
	g_signal_connect (self->store, "app-removed",
			  G_CALLBACK (gs_flatpak_store_app_removed_cb),
			  self);
	as_store_set_add_flags (self->store, AS_STORE_ADD_FLAG_USE_UNIQUE_ID);
	as_store_set_watch_flags (self->store, AS_STORE_WATCH_FLAG_REMOVED);
	as_store_set_search_match (self->store,
				   AS_APP_SEARCH_MATCH_MIMETYPE |
				   AS_APP_SEARCH_MATCH_PKGNAME |
				   AS_APP_SEARCH_MATCH_COMMENT |
				   AS_APP_SEARCH_MATCH_NAME |
				   AS_APP_SEARCH_MATCH_KEYWORD |
				   AS_APP_SEARCH_MATCH_ID);
}

GsFlatpak *
gs_flatpak_new (GsPlugin *plugin, FlatpakInstallation *installation, GsFlatpakFlags flags)
{
	GsFlatpak *self;
	self = g_object_new (GS_TYPE_FLATPAK, NULL);
	self->installation = g_object_ref (installation);
	self->scope = flatpak_installation_get_is_user (installation)
				? AS_APP_SCOPE_USER : AS_APP_SCOPE_SYSTEM;
	self->plugin = g_object_ref (plugin);
	self->flags = flags;
	return GS_FLATPAK (self);
}
