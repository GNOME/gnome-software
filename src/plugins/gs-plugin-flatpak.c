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
#include <gnome-software.h>

#include "gs-appstream.h"
#include "gs-flatpak.h"

struct GsPluginData {
	GPtrArray		*flatpaks; /* of GsFlatpak */
	gboolean		 has_system_helper;
	const gchar		*destdir_for_tests;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	const gchar *action_id = "org.freedesktop.Flatpak.appstream-update";
	g_autoptr(GPermission) permission = NULL;

	priv->flatpaks = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* old names */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "flatpak-system");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "flatpak-user");

	/* set plugin flags */
	gs_plugin_add_flags (plugin, GS_PLUGIN_FLAGS_GLOBAL_CACHE);

	/* getting app properties from appstream is quicker */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");

	/* prioritize over packages */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_BETTER_THAN, "packagekit");

	/* if we can't update the AppStream database system-wide don't even
	 * pull the data as we can't do anything with it */
	permission = gs_utils_get_permission (action_id);
	if (permission != NULL) {
		priv->has_system_helper = g_permission_get_allowed (permission) ||
					  g_permission_get_can_acquire (permission);
	}

	/* used for self tests */
	priv->destdir_for_tests = g_getenv ("GS_SELF_TEST_FLATPACK_DATADIR");
}

static gboolean
_as_app_scope_is_compatible (AsAppScope scope1, AsAppScope scope2)
{
	if (scope1 == AS_APP_SCOPE_UNKNOWN)
		return TRUE;
	if (scope2 == AS_APP_SCOPE_UNKNOWN)
		return TRUE;
	return scope1 == scope2;
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_ptr_array_unref (priv->flatpaks);
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_FLATPAK)
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
}

static gboolean
gs_plugin_flatpak_add_installation (GsPlugin *plugin,
				    FlatpakInstallation *installation,
				    GCancellable *cancellable,
				    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(GsFlatpak) flatpak = NULL;

	ptask = as_profile_start_literal (gs_plugin_get_profile (plugin),
					  "flatpak::add-installation");
	g_assert (ptask != NULL);

	/* create and set up */
	flatpak = gs_flatpak_new (plugin, installation);
	if (!gs_flatpak_setup (flatpak, cancellable, error))
		return FALSE;

	/* add objects that set up correctly */
	g_ptr_array_add (priv->flatpaks, g_steal_pointer (&flatpak));
	return TRUE;
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* we use a permissions helper to elevate privs */
	if (priv->has_system_helper && priv->destdir_for_tests == NULL) {
		g_autoptr(FlatpakInstallation) installation = NULL;
		installation = flatpak_installation_new_system (cancellable, error);
		if (installation == NULL) {
			return FALSE;
		}
		if (!gs_plugin_flatpak_add_installation (plugin, installation,
							 cancellable, error)) {
			return FALSE;
		}
	}

	/* in gs-self-test */
	if (priv->destdir_for_tests != NULL) {
		g_autofree gchar *full_path = g_build_filename (priv->destdir_for_tests,
								"flatpak",
								NULL);
		g_autoptr(GFile) file = g_file_new_for_path (full_path);
		g_autoptr(FlatpakInstallation) installation = NULL;
		g_debug ("using custom flatpak path %s", full_path);
		installation = flatpak_installation_new_for_path (file, TRUE,
								  cancellable,
								  error);
		if (installation == NULL) {
			return FALSE;
		}
		if (!gs_plugin_flatpak_add_installation (plugin, installation,
							 cancellable, error)) {
			return FALSE;
		}
	}

	/* per-user instalations always available when not in self tests */
	if (priv->destdir_for_tests == NULL) {
		g_autoptr(FlatpakInstallation) installation = NULL;
		installation = flatpak_installation_new_user (cancellable, error);
		if (installation == NULL) {
			return FALSE;
		}
		if (!gs_plugin_flatpak_add_installation (plugin, installation,
							 cancellable, error)) {
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_add_installed (flatpak, list, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_add_sources (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_add_sources (flatpak, list, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_add_updates (flatpak, list, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_refresh (flatpak, cache_age, flags,
					 cancellable, error)) {
			return FALSE;
		}
	}
	return TRUE;
}

static GsFlatpak *
gs_plugin_flatpak_get_handler (GsPlugin *plugin, GsApp *app)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0) {
		return NULL;
	}

	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);

		/* check scope */
		if (!_as_app_scope_is_compatible (gs_flatpak_get_scope (flatpak),
						  gs_app_get_scope (app))) {
			continue;
		}
		return flatpak;
	}
	return NULL;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	GsFlatpak *flatpak = gs_plugin_flatpak_get_handler (plugin, app);
	if (flatpak == NULL)
		return TRUE;
	return gs_flatpak_refine_app (flatpak, app, flags, cancellable, error);
}

gboolean
gs_plugin_refine_wildcard (GsPlugin *plugin,
			   GsApp *app,
			   GsAppList *list,
			   GsPluginRefineFlags flags,
			   GCancellable *cancellable,
			   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_refine_wildcard (flatpak, app, list, flags,
						 cancellable, error)) {
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	GsFlatpak *flatpak = gs_plugin_flatpak_get_handler (plugin, app);
	if (flatpak == NULL)
		return TRUE;
	return gs_flatpak_launch (flatpak, app, cancellable, error);
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsFlatpak *flatpak = gs_plugin_flatpak_get_handler (plugin, app);
	if (flatpak == NULL)
		return TRUE;
	return gs_flatpak_app_remove (flatpak, app, cancellable, error);
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsFlatpak *flatpak = gs_plugin_flatpak_get_handler (plugin, app);
	if (flatpak == NULL)
		return TRUE;
	return gs_flatpak_app_install (flatpak, app, cancellable, error);
}

gboolean
gs_plugin_update_app (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsFlatpak *flatpak = gs_plugin_flatpak_get_handler (plugin, app);
	if (flatpak == NULL)
		return TRUE;
	return gs_flatpak_update_app (flatpak, app, cancellable, error);
}

gboolean
gs_plugin_file_to_app (GsPlugin *plugin,
		       GsAppList *list,
		       GFile *file,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	AsAppScope scope = AS_APP_SCOPE_UNKNOWN;

	/* get the policy for handling of local files when the helper is available */
	if (priv->has_system_helper && priv->destdir_for_tests == NULL) {
		g_autoptr(GSettings) settings = g_settings_new ("org.gnome.software");
		scope = g_settings_get_boolean (settings, "install-bundles-system-wide") ?
				AS_APP_SCOPE_SYSTEM : AS_APP_SCOPE_USER;
	}

	/* run any objects with the corrext scope */
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!_as_app_scope_is_compatible (scope, gs_flatpak_get_scope (flatpak))) {
			g_debug ("not handling bundle as scope incorrect");
			continue;
		}
		if (!gs_flatpak_file_to_app (flatpak, list, file,
					     cancellable, error)) {
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_search (flatpak, values, list,
					cancellable, error)) {
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_add_categories (flatpak, list, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_add_category_apps (flatpak,
						   category,
						   list,
						   cancellable,
						   error)) {
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_add_popular (flatpak, list, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_plugin_add_featured (GsPlugin *plugin,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	for (guint i = 0; i < priv->flatpaks->len; i++) {
		GsFlatpak *flatpak = g_ptr_array_index (priv->flatpaks, i);
		if (!gs_flatpak_add_featured (flatpak, list, cancellable, error))
			return FALSE;
	}
	return TRUE;
}
