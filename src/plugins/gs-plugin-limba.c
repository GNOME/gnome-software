/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Matthias Klumpp <matthias@tenstral.net>
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

#include <limba.h>
#include <appstream-glib.h>
#include <gs-plugin.h>

/*
 * SECTION:
 * Adds and removes limba packages.
 */

struct GsPluginPrivate {
	LiManager	*mgr;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "limba";
}

/**
 * gs_plugin_order_after:
 */
const gchar **
gs_plugin_order_after (GsPlugin *plugin)
{
	static const gchar *deps[] = { "appstream",
				       "packagekit",
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
	plugin->priv->mgr = li_manager_new ();
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->mgr);
}

/**
 * gs_plugin_refine_app:
 */
gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	LiPkgInfo *pki;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* not us */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "limba") != 0)
		return TRUE;

	/* profile */
	ptask = as_profile_start (plugin->profile,
				  "limba::refine{%s}",
				  gs_app_get_id (app));

	/* sanity check */
	if (gs_app_get_source_default (app) == NULL)
		return TRUE;

	pki = li_manager_get_software_by_pkid (plugin->priv->mgr,
						gs_app_get_source_default (app),
						&error_local);
	if (error_local != NULL) {
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Unable to refine metadata: %s",
				     error_local->message);
		return FALSE;
	}

	if (pki == NULL)
		return TRUE;

	if (li_pkg_info_has_flag (pki, LI_PACKAGE_FLAG_INSTALLED))
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	else
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

	gs_app_set_version (app, li_pkg_info_get_version (pki));

	return TRUE;
}

/**
 * GsPluginHelper:
 * Helper structure for Limba callbacks.
 */
typedef struct {
	GsApp		*app;
	GsPlugin	*plugin;
} GsPluginHelper;

/**
 * gs_plugin_installer_progress_cb:
 */
static void
gs_plugin_installer_progress_cb (LiInstaller *inst, guint percentage, const gchar *id, gpointer user_data)
{
	GsPluginHelper *helper = (GsPluginHelper *) user_data;
	if (helper->app == NULL)
		return;

	/* we only catch the main progress */
	if (id != NULL)
		return;

	gs_plugin_progress_update (helper->plugin, helper->app, percentage);
}

/**
 * gs_plugin_manager_progress_cb:
 */
static void
gs_plugin_manager_progress_cb (LiManager *mgr, guint percentage, const gchar *id, gpointer user_data)
{
	GsPluginHelper *helper = (GsPluginHelper *) user_data;
	if (helper->app == NULL)
		return;

	/* we only catch the main progress */
	if (id != NULL)
		return;

	gs_plugin_progress_update (helper->plugin, helper->app, percentage);
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
	g_autoptr(LiManager) mgr = NULL;
	GsPluginHelper helper;
	g_autoptr(GError) error_local = NULL;

	mgr = li_manager_new ();

	/* set up progress forwarding */
	helper.app = app;
	helper.plugin = plugin;
	g_signal_connect (mgr,
			  "progress",
			  G_CALLBACK (gs_plugin_manager_progress_cb),
			  &helper);

	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	li_manager_remove_software (mgr,
				    gs_app_get_source_default (app),
				    &error_local);
	if (error_local != NULL) {
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Failed to remove software: %s",
				     error_local->message);
		return FALSE;
	}

	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

	return TRUE;
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
	g_autoptr(LiInstaller) inst = NULL;
	GsPluginHelper helper;
	g_autoptr(GError) error_local = NULL;

	/* create new installer and select remote package */
	inst = li_installer_new ();
	li_installer_open_remote (inst,
				  gs_app_get_source_default (app),
				  &error_local);
	if (error_local != NULL) {
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Failed to install software: %s",
				     error_local->message);
		return FALSE;
	}

	/* set up progress forwarding */
	helper.app = app;
	helper.plugin = plugin;
	g_signal_connect (inst,
			  "progress",
			  G_CALLBACK (gs_plugin_installer_progress_cb),
			  &helper);

	/* install software */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	li_installer_install (inst, &error_local);
	if (error_local != NULL) {
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Failed to install software: %s",
				     error_local->message);
		return FALSE;
	}

	gs_app_set_state (app, AS_APP_STATE_INSTALLED);

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
	/* TODO: Limba does not expose "simple" API for this yet - add this feature later. */

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
	g_autoptr(LiManager) mgr = NULL;
	GError *error_local = NULL;

	/* not us */
	if ((flags & GS_PLUGIN_REFRESH_FLAGS_METADATA) == 0)
		return TRUE;

	mgr = li_manager_new ();
	li_manager_refresh_cache (mgr, &error_local);
	if (error_local != NULL) {
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Failed to refresh Limba metadata: %s",
				     error_local->message);
		return FALSE;
	}

	return TRUE;
}

/**
 * gs_plugin_app_from_pki:
 */
static GsApp*
gs_plugin_app_from_pki (LiPkgInfo *pki)
{
	const gchar *cptkind_str;
	GsApp *app;

	cptkind_str = li_pkg_info_get_component_kind (pki);
	if ((cptkind_str != NULL) && (g_strcmp0 (cptkind_str, "desktop") == 0)) {
		g_autofree gchar *tmp = NULL;
		/* type=desktop AppStream components result in a Limba bundle name which has the .desktop stripped away.
		 * We need to re-add it for GNOME Software.
		 * In any other case, the Limba bundle name equals the AppStream ID of the component it contains */
		tmp = g_strdup_printf ("%s.desktop", li_pkg_info_get_name (pki));
		app = gs_app_new (tmp);
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	} else {
		app = gs_app_new (li_pkg_info_get_name (pki));
		gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	}

	gs_app_set_management_plugin (app, "limba");
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
	gs_app_set_name (app,
			 GS_APP_QUALITY_LOWEST,
			 li_pkg_info_get_name (pki));
	gs_app_set_summary (app,
			    GS_APP_QUALITY_LOWEST,
			    li_pkg_info_get_name (pki));
	gs_app_set_version (app, li_pkg_info_get_version (pki));
	gs_app_add_source (app, li_pkg_info_get_id (pki));

	return app;
}

/**
 * gs_plugin_add_sources:
 */
gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GList **list,
			 GCancellable *cancellable,
			 GError **error)
{
	guint i;
	g_autoptr(GPtrArray) swlist = NULL;
	g_autoptr(GError) error_local = NULL;

	/* HINT: We also emit not-installed but available software here. */

	swlist = li_manager_get_software_list (plugin->priv->mgr, &error_local);
	if (error_local != NULL) {
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Failed to list software: %s",
				error_local->message);
		return FALSE;
	}

	for (i = 0; i < swlist->len; i++) {
		g_autoptr(GsApp) app = NULL;
		LiPkgInfo *pki = LI_PKG_INFO (g_ptr_array_index (swlist, i));

		app = gs_plugin_app_from_pki (pki);
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
	g_autoptr(GList) updates = NULL;
	GList *l;
	g_autoptr(GError) error_local = NULL;

	updates = li_manager_get_update_list (plugin->priv->mgr, &error_local);
	if (error_local != NULL) {
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Failed to list updates: %s",
				error_local->message);
		return FALSE;
	}

	for (l = updates; l != NULL; l = l->next) {
		LiPkgInfo *old_pki;
		LiPkgInfo *new_pki;
		g_autoptr(GsApp) app = NULL;
		LiUpdateItem *uitem = LI_UPDATE_ITEM (l->data);

		old_pki = li_update_item_get_installed_pkg (uitem);
		new_pki = li_update_item_get_available_pkg (uitem);

		app = gs_plugin_app_from_pki (old_pki);
		gs_app_set_update_version (app,
				   li_pkg_info_get_version (new_pki));
		gs_plugin_add_app (list, app);
	}

	return TRUE;
}

/**
 * gs_plugin_update_app:
 *
 * Used only for online-updates.
 */
gboolean
gs_plugin_update_app (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginHelper helper;
	g_autoptr(LiManager) mgr = NULL;
	LiUpdateItem *uitem;
	g_autoptr(GError) error_local = NULL;

	/* sanity check */
	if (gs_app_get_source_default (app) == NULL) {
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Failed to run update: Default source was NULL.");
		return FALSE;
	}

	mgr = li_manager_new ();

	/* set up progress forwarding */
	helper.app = app;
	helper.plugin = plugin;
	g_signal_connect (mgr,
			  "progress",
			  G_CALLBACK (gs_plugin_manager_progress_cb),
			  &helper);

	/* find update which matches the ID we have */
	uitem = li_manager_get_update_for_id (mgr,
					      gs_app_get_source_default (app),
					      &error_local);
	if (error_local != NULL) {
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Failed to find update: %s",
				error_local->message);
		return FALSE;
	}

	if (uitem == NULL) {
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Could not find update for '%s'.",
				gs_app_get_source_default (app));
		return FALSE;
	}

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	li_manager_update (mgr, uitem, &error_local);
	if (error_local != NULL) {
		g_set_error (error,
				GS_PLUGIN_ERROR,
				GS_PLUGIN_ERROR_FAILED,
				"Software update failed: %s",
				error_local->message);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);

	return TRUE;
}
