/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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
 * All GsApp's created have management-plugin set to XgdApp
 * Some GsApp's created have have XgdApp::kind of app or runtime
 * The GsApp:origin is the remote name, e.g. test-repo
 *
 * Some outstanding notes:
 *
 * - Where is the privaledge elevation helper?
 */

#include <config.h>

#include <xdg-app.h>

#include <gs-plugin.h>

#include "gs-appstream.h"
#include "gs-utils.h"

static gboolean		gs_plugin_refine_item_metadata (GsPlugin *plugin,
							GsApp *app,
							GCancellable *cancellable,
							GError **error);

struct GsPluginPrivate {
	XdgAppInstallation	*installation;
	GFileMonitor		*monitor;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "xdg-app";
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",
		NULL };
	return deps;
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	if (plugin->priv->installation != NULL)
		g_object_unref (plugin->priv->installation);
	if (plugin->priv->monitor != NULL)
		g_object_unref (plugin->priv->monitor);
}

/* helpers */
#define gs_app_get_xdgapp_kind_as_str(app)	gs_app_get_metadata_item(app,"XgdApp::kind")
#define gs_app_get_xdgapp_name(app)		gs_app_get_metadata_item(app,"XgdApp::name")
#define gs_app_get_xdgapp_arch(app)		gs_app_get_metadata_item(app,"XgdApp::arch")
#define gs_app_get_xdgapp_branch(app)		gs_app_get_metadata_item(app,"XgdApp::branch")
#define gs_app_get_xdgapp_commit(app)		gs_app_get_metadata_item(app,"XgdApp::commit")
#define gs_app_set_xdgapp_name(app,val)		gs_app_set_metadata(app,"XgdApp::name",val)
#define gs_app_set_xdgapp_arch(app,val)		gs_app_set_metadata(app,"XgdApp::arch",val)
#define gs_app_set_xdgapp_branch(app,val)	gs_app_set_metadata(app,"XgdApp::branch",val)
#define gs_app_set_xdgapp_commit(app,val)	gs_app_set_metadata(app,"XgdApp::commit",val)

/**
 * gs_app_get_xdgapp_kind:
 */
static XdgAppRefKind
gs_app_get_xdgapp_kind (GsApp *app)
{
	const gchar *kind = gs_app_get_metadata_item (app, "XgdApp::kind");
	if (g_strcmp0 (kind, "app") == 0)
		return XDG_APP_REF_KIND_APP;
	if (g_strcmp0 (kind, "runtime") == 0)
		return XDG_APP_REF_KIND_RUNTIME;
	g_warning ("unknown xdg-app kind: %s", kind);
	return XDG_APP_REF_KIND_APP;
}

/**
 * gs_app_set_xdgapp_kind:
 */
static void
gs_app_set_xdgapp_kind (GsApp *app, XdgAppRefKind kind)
{
	if (kind == XDG_APP_REF_KIND_APP)
		gs_app_set_metadata (app, "XgdApp::kind", "app");
	else if (kind == XDG_APP_REF_KIND_RUNTIME)
		gs_app_set_metadata (app, "XgdApp::kind", "runtime");
	else
		g_assert_not_reached ();
}

#ifndef HAVE_PACKAGEKIT
/**
 * gs_plugin_add_popular:
 */
gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	guint i;
	const gchar *apps[] = {
		"org.gnome.Builder.desktop",
		"org.gnome.Calculator.desktop",
		"org.gnome.clocks.desktop",
		"org.gnome.Dictionary.desktop",
		"org.gnome.Documents.desktop",
		"org.gnome.Evince.desktop",
		"org.gnome.gedit.desktop",
		"org.gnome.Maps.desktop",
		"org.gnome.Weather.desktop",
		NULL };

	/* just add all */
	for (i = 0; apps[i] != NULL; i++) {
		g_autoptr(GsApp) app = NULL;
		app = gs_app_new (apps[i]);
		gs_plugin_add_app (list, app);
	}
	return TRUE;
}
#endif

/**
 * gs_plugin_xdg_app_changed_cb:
 */
static void
gs_plugin_xdg_app_changed_cb (GFileMonitor *monitor,
			      GFile *child,
			      GFile *other_file,
			      GFileMonitorEvent event_type,
			      GsPlugin *plugin)
{
	gs_plugin_updates_changed (plugin);
}

/**
 * gs_plugin_refresh_appstream:
 */
static gboolean
gs_plugin_refresh_appstream (GsPlugin *plugin,
			     guint cache_age,
			     GCancellable *cancellable,
			     GError **error)
{
	gboolean ret;
	guint i;
	g_autoptr(GPtrArray) xremotes = NULL;

	xremotes = xdg_app_installation_list_remotes (plugin->priv->installation,
						      cancellable,
						      error);
	if (xremotes == NULL)
		return FALSE;
	for (i = 0; i < xremotes->len; i++) {
		guint tmp;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GFile) file = NULL;
		g_autoptr(GFile) file_timestamp = NULL;
		g_autofree gchar *appstream_fn = NULL;
		XdgAppRemote *xremote = g_ptr_array_index (xremotes, i);

		/* skip known-broken repos */
		if (g_strcmp0 (xdg_app_remote_get_name (xremote), "gnome-sdk") == 0)
			continue;
		if (g_strcmp0 (xdg_app_remote_get_name (xremote), "test-apps") == 0)
			continue;

		/* is the timestamp new enough */
		file_timestamp = xdg_app_remote_get_appstream_timestamp (xremote, NULL);
		tmp = gs_utils_get_file_age (file_timestamp);
		if (tmp < cache_age) {
			g_autofree gchar *fn = g_file_get_path (file_timestamp);
			g_debug ("%s is only %i seconds old, so ignoring refresh",
				 fn, tmp);
			continue;
		}

		/* download new data */
		ret = xdg_app_installation_update_appstream_sync (plugin->priv->installation,
								  xdg_app_remote_get_name (xremote),
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
		file = xdg_app_remote_get_appstream_dir (xremote, NULL);
		appstream_fn = g_file_get_path (file);
		g_debug ("using AppStream metadata found at: %s", appstream_fn);
	}
	return TRUE;
}

/**
 * gs_plugin_ensure_installation:
 */
static gboolean
gs_plugin_ensure_installation (GsPlugin *plugin,
			       GCancellable *cancellable,
			       GError **error)
{
	g_autofree gchar *install_path = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GFile) install_file = NULL;

	if (plugin->priv->installation != NULL)
		return TRUE;

	/* If we're running INSIDE the xdg-app environment we'll have the
	 * env var XDG_DATA_HOME set to "~/.var/app/org.gnome.Software/data"
	 * so specify the path manually to get the real data */
	install_path = g_build_filename (g_get_home_dir (),
					 ".local",
					 "share",
					 "xdg-app",
					 NULL);
	install_file = g_file_new_for_path (install_path);

	/* FIXME: this should default to system-wide, but we need a permissions
	 * helper to elevate privs */
	ptask = as_profile_start_literal (plugin->profile, "xdg-app::ensure-origin");
	plugin->priv->installation = xdg_app_installation_new_for_path (install_file,
									TRUE,
									cancellable,
									error);
	if (plugin->priv->installation == NULL)
		return FALSE;

	/* watch for changes */
	plugin->priv->monitor =
		xdg_app_installation_create_monitor (plugin->priv->installation,
						     cancellable,
						     error);
	if (plugin->priv->monitor == NULL)
		return FALSE;
	g_signal_connect (plugin->priv->monitor, "changed",
			  G_CALLBACK (gs_plugin_xdg_app_changed_cb), plugin);

	/* success */
	return TRUE;
}

/**
 * gs_plugin_xdg_app_set_metadata:
 */
static void
gs_plugin_xdg_app_set_metadata (GsApp *app, XdgAppRef *xref)
{
	gs_app_set_management_plugin (app, "XgdApp");
	gs_app_set_xdgapp_kind (app, xdg_app_ref_get_kind (xref));
	gs_app_set_xdgapp_name (app, xdg_app_ref_get_name (xref));
	gs_app_set_xdgapp_arch (app, xdg_app_ref_get_arch (xref));
	gs_app_set_xdgapp_branch (app, xdg_app_ref_get_branch (xref));
	gs_app_set_xdgapp_commit (app, xdg_app_ref_get_commit (xref));
}

/**
 * gs_plugin_xdg_app_set_metadata_installed:
 */
static void
gs_plugin_xdg_app_set_metadata_installed (GsApp *app, XdgAppInstalledRef *xref)
{
	guint64 mtime;
	guint64 size_installed;
	g_autofree gchar *metadata_fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	/* for all types */
	gs_plugin_xdg_app_set_metadata (app, XDG_APP_REF (xref));

	/* get the last time the app was updated */
	metadata_fn = g_build_filename (xdg_app_installed_ref_get_deploy_dir (xref),
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
	gs_app_set_origin (app, xdg_app_installed_ref_get_origin (xref));

	/* this is faster than xdg_app_installation_fetch_remote_size_sync() */
	size_installed = xdg_app_installed_ref_get_installed_size (xref);
	if (size_installed != 0)
		gs_app_set_size (app, size_installed);
}

/**
 * gs_plugin_xdg_app_build_id:
 */
static gchar *
gs_plugin_xdg_app_build_id (XdgAppRef *xref)
{
	if (xdg_app_ref_get_kind (xref) == XDG_APP_REF_KIND_APP)
		return g_strdup_printf ("user-xdgapp:%s.desktop", xdg_app_ref_get_name (xref));
	return g_strdup_printf ("user-xdgapp:%s.runtime", xdg_app_ref_get_name (xref));
}

/**
 * gs_plugin_xdg_app_create_installed:
 */
static GsApp *
gs_plugin_xdg_app_create_installed (GsPlugin *plugin,
				    XdgAppInstalledRef *xref,
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
	if (!xdg_app_installed_ref_get_is_current (xref) &&
	    xdg_app_ref_get_kind (XDG_APP_REF(xref)) == XDG_APP_REF_KIND_APP) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "%s not current, ignoring",
			     xdg_app_ref_get_name (XDG_APP_REF (xref)));
		return NULL;
	}

	/* create new object */
	id = gs_plugin_xdg_app_build_id (XDG_APP_REF (xref));
	app = gs_app_new (id);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_plugin_xdg_app_set_metadata_installed (app, xref);

	switch (xdg_app_ref_get_kind (XDG_APP_REF(xref))) {
	case XDG_APP_REF_KIND_APP:
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
		break;
	case XDG_APP_REF_KIND_RUNTIME:
		gs_app_set_xdgapp_kind (app, XDG_APP_REF_KIND_RUNTIME);
		gs_app_set_kind (app, AS_APP_KIND_RUNTIME);
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
				 xdg_app_ref_get_name (XDG_APP_REF (xref)));
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
				    "Framework for applications");
		gs_app_set_version (app, xdg_app_ref_get_branch (XDG_APP_REF (xref)));
		icon = as_icon_new ();
		as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
		as_icon_set_name (icon, "system-run-symbolic");
		gs_app_set_icon (app, icon);
		break;
	default:
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "XdgAppRefKind not known");
		return NULL;
	}
	return g_object_ref (app);
}

typedef struct {
	GsApp		*app;
	GsPlugin	*plugin;
} GsPluginHelper;

/**
 * gs_plugin_xdg_app_progress_cb:
 */
static void
gs_plugin_xdg_app_progress_cb (const gchar *status,
			       guint progress,
			       gboolean estimating,
			       gpointer user_data)
{
	GsPluginHelper *helper = (GsPluginHelper *) user_data;
	if (helper->app == NULL)
		return;
	gs_plugin_progress_update (helper->plugin, helper->app, progress);
}

/**
 * gs_plugin_add_installed:
 */
gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GList **list,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autoptr(GError) error_md = NULL;
	g_autoptr(GPtrArray) xrefs = NULL;
	guint i;

	/* ensure we can set up the repo */
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	/* if we've never ever run before, get the AppStream data */
	if (!gs_plugin_refresh_appstream (plugin,
					  G_MAXUINT,
					  cancellable,
					  &error_md)) {
		g_warning ("failed to get initial available data: %s",
			   error_md->message);
	}

	/* get apps and runtimes */
	xrefs = xdg_app_installation_list_installed_refs (plugin->priv->installation,
							  cancellable, error);
	if (xrefs == NULL)
		return FALSE;
	for (i = 0; i < xrefs->len; i++) {
		XdgAppInstalledRef *xref = g_ptr_array_index (xrefs, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GsApp) app = NULL;

		/* only apps */
		if (xdg_app_ref_get_kind (XDG_APP_REF (xref)) != XDG_APP_REF_KIND_APP)
			continue;

		app = gs_plugin_xdg_app_create_installed (plugin, xref, &error_local);
		if (app == NULL) {
			g_warning ("failed to add xdg-app: %s", error_local->message);
			continue;
		}
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_plugin_add_app (list, app);
	}

	return TRUE;
}

/**
 * gs_plugin_add_sources:
 */
gboolean
gs_plugin_add_sources (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(GPtrArray) xremotes = NULL;
	guint i;

	/* ensure we can set up the repo */
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	xremotes = xdg_app_installation_list_remotes (plugin->priv->installation,
						      cancellable,
						      error);
	if (xremotes == NULL)
		return FALSE;
	for (i = 0; i < xremotes->len; i++) {
		XdgAppRemote *xremote = g_ptr_array_index (xremotes, i);
		g_autoptr(GsApp) app = NULL;

		/* apps installed from bundles add their own remote that only
		 * can be used for updating that app only -- so hide them */
		if (xdg_app_remote_get_noenumerate (xremote))
			continue;

		app = gs_app_new (xdg_app_remote_get_name (xremote));
		gs_app_set_management_plugin (app, "XgdApp");
		gs_app_set_kind (app, AS_APP_KIND_SOURCE);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_set_name (app,
				 GS_APP_QUALITY_LOWEST,
				 xdg_app_remote_get_name (xremote));
		gs_app_set_summary (app,
				    GS_APP_QUALITY_LOWEST,
				    xdg_app_remote_get_title (xremote));
		gs_app_set_url (app,
				AS_URL_KIND_HOMEPAGE,
				xdg_app_remote_get_url (xremote));
		gs_plugin_add_app (list, app);
	}
	return TRUE;
}

/**
 * gs_plugin_add_updates:
 */
gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	guint i;
	g_autoptr(GPtrArray) xrefs = NULL;

	/* ensure we can set up the repo */
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	/* get all the installed apps (no network I/O) */
	xrefs = xdg_app_installation_list_installed_refs (plugin->priv->installation,
							  cancellable,
							  error);
	if (xrefs == NULL)
		return FALSE;
	for (i = 0; i < xrefs->len; i++) {
		XdgAppInstalledRef *xref = g_ptr_array_index (xrefs, i);
		const gchar *commit;
		const gchar *latest_commit;
		g_autoptr(GsApp) app = NULL;
		g_autoptr(GError) error_local = NULL;

		/* check the application has already been downloaded */
		commit = xdg_app_ref_get_commit (XDG_APP_REF (xref));
		latest_commit = xdg_app_installed_ref_get_latest_commit (xref);
		if (g_strcmp0 (commit, latest_commit) == 0) {
			g_debug ("no downloaded update for %s",
				 xdg_app_ref_get_name (XDG_APP_REF (xref)));
			continue;
		}

		/* we have an update to show */
		g_debug ("%s has a downloaded update %s->%s",
			 xdg_app_ref_get_name (XDG_APP_REF (xref)),
			 commit, latest_commit);
		app = gs_plugin_xdg_app_create_installed (plugin, xref, &error_local);
		if (app == NULL) {
			g_warning ("failed to add xdg-app: %s", error_local->message);
			continue;
		}
		if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
			gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
		gs_plugin_add_app (list, app);
	}

	return TRUE;
}

/**
 * gs_plugin_refresh:
 */
gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginHelper helper;
	guint i;
	g_autoptr(GPtrArray) xrefs = NULL;

	/* not us */
	if ((flags & GS_PLUGIN_REFRESH_FLAGS_UPDATES) == 0)
		return TRUE;

	/* ensure we can set up the repo */
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	/* update AppStream metadata */
	if (!gs_plugin_refresh_appstream (plugin, cache_age, cancellable, error))
		return FALSE;

	/* use helper: FIXME: new()&ref? */
	helper.plugin = plugin;

	/* get all the updates available from all remotes */
	xrefs = xdg_app_installation_list_installed_refs_for_update (plugin->priv->installation,
								     cancellable,
								     error);
	if (xrefs == NULL)
		return FALSE;
	for (i = 0; i < xrefs->len; i++) {
		XdgAppInstalledRef *xref = g_ptr_array_index (xrefs, i);
		g_autoptr(GsApp) app = NULL;
		g_autoptr(XdgAppInstalledRef) xref2 = NULL;

		/* try to create a GsApp so we can do progress reporting */
		app = gs_plugin_xdg_app_create_installed (plugin, xref, NULL);
		helper.app = app;

		/* fetch but do not deploy */
		g_debug ("pulling update for %s",
			 xdg_app_ref_get_name (XDG_APP_REF (xref)));
		xref2 = xdg_app_installation_update (plugin->priv->installation,
						     XDG_APP_UPDATE_FLAGS_NO_DEPLOY,
						     xdg_app_ref_get_kind (XDG_APP_REF (xref)),
						     xdg_app_ref_get_name (XDG_APP_REF (xref)),
						     xdg_app_ref_get_arch (XDG_APP_REF (xref)),
						     xdg_app_ref_get_branch (XDG_APP_REF (xref)),
						     gs_plugin_xdg_app_progress_cb, &helper,
						     cancellable, error);
		if (xref2 == NULL)
			return FALSE;
	}

	return TRUE;
}

/**
 * gs_plugin_refine_item_origin_ui:
 */
static gboolean
gs_plugin_refine_item_origin_ui (GsPlugin *plugin,
				 GsApp *app,
				 GCancellable *cancellable,
				 GError **error)
{
	const gchar *origin;
	guint i;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) xremotes = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* already set */
	origin = gs_app_get_origin_ui (app);
	if (origin != NULL)
		return TRUE;

	/* ensure we can set up the repo */
	ptask = as_profile_start_literal (plugin->profile, "xdg-app::refine-origin-ui");
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	/* find list of remotes */
	xremotes = xdg_app_installation_list_remotes (plugin->priv->installation,
						      cancellable,
						      error);
	if (xremotes == NULL)
		return FALSE;
	for (i = 0; i < xremotes->len; i++) {
		XdgAppRemote *xremote = g_ptr_array_index (xremotes, i);
		if (g_strcmp0 (gs_app_get_origin (app),
			       xdg_app_remote_get_name (xremote)) == 0) {
			gs_app_set_origin_ui (app, xdg_app_remote_get_title (xremote));
			break;
		}
	}

	return TRUE;
}

/**
 * gs_plugin_refine_item_origin:
 */
static gboolean
gs_plugin_refine_item_origin (GsPlugin *plugin,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	guint i;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) xremotes = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* already set */
	if (gs_app_get_origin (app) != NULL)
		return TRUE;

	/* ensure we can set up the repo */
	ptask = as_profile_start_literal (plugin->profile, "xdg-app::refine-origin");
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	/* ensure metadata exists */
	if (!gs_plugin_refine_item_metadata (plugin, app, cancellable, error))
		return FALSE;

	/* find list of remotes */
	g_debug ("looking for a remote for %s/%s/%s",
		 gs_app_get_xdgapp_name (app),
		 gs_app_get_xdgapp_arch (app),
		 gs_app_get_xdgapp_branch (app));
	xremotes = xdg_app_installation_list_remotes (plugin->priv->installation,
						      cancellable,
						      error);
	if (xremotes == NULL)
		return FALSE;
	for (i = 0; i < xremotes->len; i++) {
		const gchar *remote_name;
		XdgAppRemote *xremote = g_ptr_array_index (xremotes, i);
		g_autoptr(XdgAppRemoteRef) xref = NULL;
		remote_name = xdg_app_remote_get_name (xremote);
		g_debug ("looking at remote %s", remote_name);
		xref = xdg_app_installation_fetch_remote_ref_sync (plugin->priv->installation,
								   remote_name,
								   gs_app_get_xdgapp_kind (app),
								   gs_app_get_xdgapp_name (app),
								   gs_app_get_xdgapp_arch (app),
								   gs_app_get_xdgapp_branch (app),
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
		     gs_app_get_xdgapp_name (app),
		     gs_app_get_xdgapp_arch (app),
		     gs_app_get_xdgapp_branch (app));
	return FALSE;
}

/**
 * gs_plugin_refine_item_commit:
 */
static gboolean
gs_plugin_refine_item_commit (GsPlugin *plugin,
			      GsApp *app,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(XdgAppRemoteRef) xref_remote = NULL;

	if (gs_app_get_xdgapp_commit (app) != NULL)
		return TRUE;
	if (gs_app_get_origin (app) == NULL) {
		g_debug ("no origin got commit, so refining origin first");
		if (!gs_plugin_refine_item_origin (plugin, app, cancellable, error))
			return FALSE;
	}

	ptask = as_profile_start_literal (plugin->profile, "xdg-app::fetch-remote-ref");
	xref_remote = xdg_app_installation_fetch_remote_ref_sync (plugin->priv->installation,
								  gs_app_get_origin (app),
								  gs_app_get_xdgapp_kind (app),
								  gs_app_get_xdgapp_name (app),
								  gs_app_get_xdgapp_arch (app),
								  gs_app_get_xdgapp_branch (app),
								  cancellable,
								  error);
	if (xref_remote == NULL)
		return FALSE;
	gs_app_set_xdgapp_commit (app, xdg_app_ref_get_commit (XDG_APP_REF (xref_remote)));
	return TRUE;
}

/**
 * gs_plugin_xdg_app_is_xref:
 */
static gboolean
gs_plugin_xdg_app_is_xref (GsApp *app, XdgAppRef *xref)
{
	g_autofree gchar *id = NULL;

	/* check ID */
	id = gs_plugin_xdg_app_build_id (xref);
	if (g_strcmp0 (id, gs_app_get_id (app)) == 0)
		return TRUE;

	/* check source ID */
//	if (g_strcmp0 (id, gs_app_get_id (app)) == 0)
//		return TRUE;

	/* do all the metadata items match? */
	if (g_strcmp0 (gs_app_get_xdgapp_name (app),
		       xdg_app_ref_get_name (xref)) == 0 &&
	    g_strcmp0 (gs_app_get_xdgapp_arch (app),
		       xdg_app_ref_get_arch (xref)) == 0 &&
	    g_strcmp0 (gs_app_get_xdgapp_branch (app),
		       xdg_app_ref_get_branch (xref)) == 0)
		return TRUE;

	/* sad panda */
	return FALSE;
}

/**
 * gs_plugin_refine_item_metadata:
 */
static gboolean
gs_plugin_refine_item_metadata (GsPlugin *plugin,
				GsApp *app,
				GCancellable *cancellable,
				GError **error)
{
	g_autoptr(XdgAppRef) xref = NULL;

	/* already set */
	if (gs_app_get_metadata_item (app, "XgdApp::kind") != NULL)
		return TRUE;

	/* AppStream sets the source to appname/arch/branch, if this isn't set
	 * we can't break out the fields */
	if (gs_app_get_source_default (app) == NULL)
		return TRUE;

	/* parse the ref */
	xref = xdg_app_ref_parse (gs_app_get_source_default (app), error);
	if (xref == NULL) {
		g_prefix_error (error, "failed to parse '%s': ",
				gs_app_get_source_default (app));
		return FALSE;
	}
	gs_plugin_xdg_app_set_metadata (app, xref);

	/* success */
	return TRUE;
}

/**
 * gs_plugin_refine_item_state:
 */
static gboolean
gs_plugin_refine_item_state (GsPlugin *plugin,
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
	if (!gs_plugin_refine_item_metadata (plugin, app, cancellable, error))
		return FALSE;

	/* get apps and runtimes */
	ptask = as_profile_start_literal (plugin->profile, "xdg-app::refine-action");
	xrefs = xdg_app_installation_list_installed_refs (plugin->priv->installation,
							  cancellable, error);
	if (xrefs == NULL)
		return FALSE;
	for (i = 0; i < xrefs->len; i++) {
		XdgAppInstalledRef *xref = g_ptr_array_index (xrefs, i);

		/* check xref is app */
		if (!gs_plugin_xdg_app_is_xref (app, XDG_APP_REF(xref)))
			continue;

		/* mark as installed */
		g_debug ("marking %s as installed with xdg-app",
			 gs_app_get_id (app));
		gs_plugin_xdg_app_set_metadata_installed (app, xref);
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	}

	/* anything not installed just check the remote is still present */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN &&
	    gs_app_get_origin (app) != NULL) {
		g_autoptr(XdgAppRemote) xremote = NULL;
		xremote = xdg_app_installation_get_remote_by_name (plugin->priv->installation,
								   gs_app_get_origin (app),
								   cancellable, NULL);
		if (xremote != NULL) {
			g_debug ("marking %s as available with xdg-app",
				 gs_app_get_id (app));
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		}
	}

	/* success */
	return TRUE;
}

/**
 * gs_plugin_xdg_app_set_app_metadata:
 */
static gboolean
gs_plugin_xdg_app_set_app_metadata (GsApp *app,
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
	gs_app_set_xdgapp_name (app, name);
	runtime = g_key_file_get_string (kf, "Application", "runtime", error);
	if (runtime == NULL)
		return FALSE;
	g_debug ("runtime for %s is %s", name, runtime);

	/* create runtime */
	app_runtime = gs_appstream_create_runtime (app, runtime);
	if (app_runtime != NULL)
		gs_app_set_runtime (app, app_runtime);

	return TRUE;
}

/**
 * gs_plugin_refine_item_runtime:
 */
static gboolean
gs_plugin_refine_item_runtime (GsPlugin *plugin,
			       GsApp *app,
			       GCancellable *cancellable,
			       GError **error)
{
	const gchar *commit;
	const gchar *str;
	gsize len = -1;
	g_autofree gchar *contents = NULL;
	g_autofree gchar *installation_path_str = NULL;
	g_autofree gchar *install_path = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GFile) installation_path = NULL;
	g_autoptr(XdgAppInstalledRef) xref = NULL;

	/* not applicable */
	if (gs_app_get_xdgapp_kind (app) != XDG_APP_REF_KIND_APP)
		return TRUE;

	/* already exists */
	if (gs_app_get_runtime (app) != NULL)
		return TRUE;

	/* this is quicker than doing network IO */
	installation_path = xdg_app_installation_get_path (plugin->priv->installation);
	installation_path_str = g_file_get_path (installation_path);
	install_path = g_build_filename (installation_path_str,
					 gs_app_get_xdgapp_kind_as_str (app),
					 gs_app_get_xdgapp_name (app),
					 gs_app_get_xdgapp_arch (app),
					 gs_app_get_xdgapp_branch (app),
					 "active",
					 "metadata",
					 NULL);
	if (g_file_test (install_path, G_FILE_TEST_EXISTS)) {
		if (!g_file_get_contents (install_path, &contents, &len, error))
			return FALSE;
		str = contents;
	} else {

		/* need commit */
		if (!gs_plugin_refine_item_commit (plugin, app, cancellable, error))
			return FALSE;

		/* fetch from the server */
		commit = gs_app_get_xdgapp_commit (app);
		data = xdg_app_installation_fetch_remote_metadata_sync (plugin->priv->installation,
									gs_app_get_origin (app),
									commit,
									cancellable,
									error);
		if (data == NULL)
			return FALSE;
		str = g_bytes_get_data (data, &len);
	}

	/* parse key file */
	if (!gs_plugin_xdg_app_set_app_metadata (app, str, len, error))
		return FALSE;
	return TRUE;
}

/**
 * gs_plugin_refine_item_size:
 */
static gboolean
gs_plugin_refine_item_size (GsPlugin *plugin,
			    GsApp *app,
			    GCancellable *cancellable,
			    GError **error)
{
	gboolean ret;
	guint64 download_size;
	guint64 installed_size;
	guint64 size = 0;
	g_auto(GStrv) split = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GError) error_local = NULL;

	if (gs_app_get_size (app) > 0)
		return TRUE;

	/* need commit */
	if (!gs_plugin_refine_item_commit (plugin, app, cancellable, error))
		return FALSE;

	/* need runtime */
	if (!gs_plugin_refine_item_runtime (plugin, app, cancellable, error))
		return FALSE;

	/* calculate the platform size too if the app is not installed */
	if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE &&
	    gs_app_get_xdgapp_kind (app) == XDG_APP_REF_KIND_APP) {
		GsApp *app_runtime;

		/* find out what runtime the application depends on */
		if (!gs_plugin_refine_item_runtime (plugin,
						    app,
						    cancellable,
						    error))
			return FALSE;

		/* is the app_runtime already installed? */
		app_runtime = gs_app_get_runtime (app);
		if (!gs_plugin_refine_item_state (plugin,
						  app_runtime,
						  cancellable,
						  error))
			return FALSE;
		if (gs_app_get_state (app_runtime) == AS_APP_STATE_INSTALLED) {
			g_debug ("runtime %s is already installed, so not adding size",
				 gs_app_get_id (app_runtime));
		} else {
			if (!gs_plugin_refine_item_size (plugin,
							 app_runtime,
							 cancellable,
							 error))
				return FALSE;
			g_debug ("runtime %s is not installed, so adding download",
				 gs_app_get_id (app_runtime));
			size += gs_app_get_size (app_runtime);
		}
	}

	/* just get the size of the runtime */
	ptask = as_profile_start_literal (plugin->profile, "xdg-app::refine-size");
	ret = xdg_app_installation_fetch_remote_size_sync (plugin->priv->installation,
							   gs_app_get_origin (app),
							   gs_app_get_xdgapp_commit (app),
							   &download_size,
							   &installed_size,
							   cancellable, &error_local);
	if (!ret) {
		g_warning ("libxdgapp failed to return application size: %s",
			   error_local->message);
	} else {
		if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED) {
			size += installed_size;
		} else {
			size += download_size;
		}
	}
	if (size == 0)
		size = GS_APP_SIZE_MISSING;
	gs_app_set_size (app, size);
	return TRUE;
}

/**
 * gs_plugin_refine_item:
 */
static gboolean
gs_plugin_refine_item (GsPlugin *plugin,
		       GsApp *app,
		       GsPluginRefineFlags flags,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(AsProfileTask) ptask = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "XgdApp") != 0)
		return TRUE;

	/* profile */
	ptask = as_profile_start (plugin->profile,
				  "xdg-app::refine{%s}",
				  gs_app_get_id (app));

	/* AppStream sets the source to appname/arch/branch */
	if (!gs_plugin_refine_item_metadata (plugin, app, cancellable, error))
		return FALSE;

	/* check the installed state */
	if (!gs_plugin_refine_item_state (plugin, app, cancellable, error))
		return FALSE;

	/* version fallback */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) {
		if (gs_app_get_version (app) == NULL) {
			const gchar *branch;
			branch = gs_app_get_xdgapp_branch (app);
			gs_app_set_version (app, branch);
		}
	}

	/* size */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE) {
		if (!gs_plugin_refine_item_size (plugin, app, cancellable, error))
			return FALSE;
	}

	/* origin */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) {
		if (!gs_plugin_refine_item_origin_ui (plugin, app, cancellable, error))
			return FALSE;
	}

	return TRUE;
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
	GsApp *app;

	/* ensure we can set up the repo */
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (!gs_plugin_refine_item (plugin, app, flags, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * gs_plugin_launch:
 */
gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *branch = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "XgdApp") != 0)
		return TRUE;

	/* ensure we can set up the repo */
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	branch = gs_app_get_xdgapp_branch (app);
	if (branch == NULL)
		branch = "master";
	return xdg_app_installation_launch (plugin->priv->installation,
					    gs_app_get_xdgapp_name (app),
					    NULL,
					    branch,
					    NULL,
					    cancellable,
					    error);
}

/**
 * gs_plugin_app_remove:
 */
gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginHelper helper;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "XgdApp") != 0)
		return TRUE;

	/* ensure we can set up the repo */
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	/* use helper: FIXME: new()&ref? */
	helper.app = app;
	helper.plugin = plugin;

	/* remove */
	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	return xdg_app_installation_uninstall (plugin->priv->installation,
					       XDG_APP_REF_KIND_APP,
					       gs_app_get_xdgapp_name (app),
					       gs_app_get_xdgapp_arch (app),
					       gs_app_get_xdgapp_branch (app),
					       gs_plugin_xdg_app_progress_cb, &helper,
					       cancellable, error);
}

/**
 * gs_plugin_app_install:
 */
gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginHelper helper;
	g_autoptr(XdgAppInstalledRef) xref = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "XgdApp") != 0)
		return TRUE;

	/* ensure we can set up the repo */
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	/* ensure we have metadata and state */
	if (!gs_plugin_refine_item (plugin, app, 0, cancellable, error))
		return FALSE;

	/* use helper: FIXME: new()&ref? */
	helper.app = app;
	helper.plugin = plugin;

	/* install */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);

	/* install required runtime if not already installed */
	if (gs_app_get_kind (app) == AS_APP_KIND_DESKTOP) {
		GsApp *runtime;
		runtime = gs_app_get_runtime (app);

		/* the runtime could come from a different remote to the app */
		if (!gs_plugin_refine_item_metadata (plugin, runtime, cancellable, error))
			return FALSE;
		if (!gs_plugin_refine_item_origin (plugin, runtime, cancellable, error))
			return FALSE;
		if (!gs_plugin_refine_item_state (plugin, runtime, cancellable, error))
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
			xref = xdg_app_installation_install (plugin->priv->installation,
							     gs_app_get_origin (runtime),
							     gs_app_get_xdgapp_kind (runtime),
							     gs_app_get_xdgapp_name (runtime),
							     gs_app_get_xdgapp_arch (runtime),
							     gs_app_get_xdgapp_branch (runtime),
							     gs_plugin_xdg_app_progress_cb, &helper,
							     cancellable, error);
			if (xref == NULL) {
				gs_app_set_state (runtime, AS_APP_STATE_AVAILABLE);
				return FALSE;
			}
			gs_app_set_state (runtime, AS_APP_STATE_INSTALLED);
		} else {
			g_debug ("%s is already installed, so skipping",
				 gs_app_get_id (runtime));
		}
	}

	/* now the main application */
	g_debug ("installing %s", gs_app_get_id (app));
	xref = xdg_app_installation_install (plugin->priv->installation,
					     gs_app_get_origin (app),
					     gs_app_get_xdgapp_kind (app),
					     gs_app_get_xdgapp_name (app),
					     gs_app_get_xdgapp_arch (app),
					     gs_app_get_xdgapp_branch (app),
					     gs_plugin_xdg_app_progress_cb, &helper,
					     cancellable, error);
	return xref != NULL;
}

/**
 * gs_plugin_app_update:
 *
 * This is only called when updating live.
 */
gboolean
gs_plugin_app_update (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginHelper helper;
	g_autoptr(XdgAppInstalledRef) xref = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "XgdApp") != 0)
		return TRUE;

	/* ensure we can set up the repo */
	if (!gs_plugin_ensure_installation (plugin, cancellable, error))
		return FALSE;

	/* use helper: FIXME: new()&ref? */
	helper.app = app;
	helper.plugin = plugin;

	/* install */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	xref = xdg_app_installation_update (plugin->priv->installation,
					    XDG_APP_UPDATE_FLAGS_NONE,
					    gs_app_get_xdgapp_kind (app),
					    gs_app_get_xdgapp_name (app),
					    gs_app_get_xdgapp_arch (app),
					    gs_app_get_xdgapp_branch (app),
					    gs_plugin_xdg_app_progress_cb, &helper,
					    cancellable, error);
	return xref != NULL;
}
