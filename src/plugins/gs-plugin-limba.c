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
	AsBundle *bundle;
	LiPkgInfo *pki;
	GError *local_error = NULL;

	bundle = gs_app_get_bundle (app);

	/* check if we should process this application */
	if (bundle == NULL)
		return TRUE;
	if (as_bundle_get_kind (bundle) != AS_BUNDLE_KIND_LIMBA)
		return TRUE;

	pki = li_manager_get_software_by_pkid (plugin->priv->mgr,
						as_bundle_get_id (bundle),
						&local_error);
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	/* we will handle installations and removals of this application */
	gs_app_set_management_plugin (app, "Limba");

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
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		GList **list,
		GsPluginRefineFlags flags,
		GCancellable *cancellable,
		GError **error)
{
	gboolean ret;
	GList *l;
	GsApp *app;

	gs_profile_start (plugin->profile, "limba::refine");
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);

		if (gs_app_get_bundle (app) == NULL)
			continue;

		ret = gs_plugin_refine_app (plugin, app, error);
		if (!ret)
			goto out;
	}

	/* sucess */
	ret = TRUE;
out:
	gs_profile_stop (plugin->profile, "limba::refine");
	return ret;
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
	AsBundle *bundle;
	GError *local_error = NULL;

	bundle = gs_app_get_bundle (app);

	/* check if we can remove this application */
	if (bundle == NULL)
		return FALSE;
	if (as_bundle_get_kind (bundle) != AS_BUNDLE_KIND_LIMBA)
		return FALSE;

	mgr = li_manager_new ();

	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	li_manager_remove_software (mgr,
				as_bundle_get_id (bundle),
				&local_error);
	if (local_error != NULL) {
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		g_propagate_error (error, local_error);
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
	AsBundle *bundle;
	GError *local_error = NULL;

	bundle = gs_app_get_bundle (app);

	/* check if we can install this application */
	if (bundle == NULL)
		return FALSE;
	if (as_bundle_get_kind (bundle) != AS_BUNDLE_KIND_LIMBA)
		return FALSE;

	/* create new installer and select remote package */
	inst = li_installer_new ();
	li_installer_open_remote (inst,
				as_bundle_get_id (bundle),
				&local_error);
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	/* install software */
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	li_installer_install (inst, &local_error);
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		return FALSE;
	}

	gs_app_set_state (app, AS_APP_STATE_INSTALLED);

	return TRUE;
}
