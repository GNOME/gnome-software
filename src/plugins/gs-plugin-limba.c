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
static gboolean
gs_plugin_refine_app (GsPlugin *plugin, GsApp *app, GError **error)
{
	LiPkgInfo *pki;
	g_autoptr(GError) error_local = NULL;

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

	if (pki == NULL) {
		return TRUE;
	}

	if (li_pkg_info_has_flag (pki, LI_PACKAGE_FLAG_INSTALLED))
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	else
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

	gs_app_set_version (app, li_pkg_info_get_version (pki));

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
	g_autoptr(AsProfileTask) ptask = NULL;

	ptask = as_profile_start_literal (plugin->profile, "limba::refine");
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);

		/* not us */
		if (g_strcmp0 (gs_app_get_management_plugin (app), "Limba") != 0)
			continue;

		if (!gs_plugin_refine_app (plugin, app, error))
			return FALSE;
	}

	/* sucess */
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

	/* not us */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "Limba") != 0)
		return TRUE;

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

	/* not us */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "Limba") != 0)
		return TRUE;

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
	if ((flags & GS_PLUGIN_REFRESH_FLAGS_UPDATES) == 0)
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

