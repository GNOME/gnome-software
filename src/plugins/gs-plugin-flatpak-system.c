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

struct GsPluginData {
	GsFlatpak		*flatpak;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	g_autoptr(GSettings) settings = g_settings_new ("org.gnome.software");

	priv->flatpak = gs_flatpak_new (plugin, AS_APP_SCOPE_SYSTEM);

	/* set plugin flags */
	gs_plugin_add_flags (plugin, GS_PLUGIN_FLAGS_GLOBAL_CACHE);

	/* getting app properties from appstream is quicker */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");

	/* prioritize over packages */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_BETTER_THAN, "packagekit");

	/* prefer system-wide handling of local files */
	if (g_settings_get_boolean (settings, "install-bundles-system-wide"))
		gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "flatpak-user");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_clear_object (&priv->flatpak);
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_FLATPAK &&
	    gs_app_get_scope (app) == AS_APP_SCOPE_SYSTEM) {
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	}
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *action_id = "org.freedesktop.Flatpak.appstream-update";
	g_autoptr(GPermission) permission = NULL;

	/* if we can't update the AppStream database system-wide don't even
	 * pull the data as we can't do anything with it */
	permission = gs_utils_get_permission (action_id);
	if (permission != NULL) {
		gboolean ret = g_permission_get_allowed (permission) ||
				g_permission_get_can_acquire (permission);
		if (!ret) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "no way to update using %s", action_id);
			return FALSE;
		}
	}

	return gs_flatpak_setup (priv->flatpak, cancellable, error);
}

gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_add_installed (priv->flatpak, list, cancellable, error);
}

gboolean
gs_plugin_add_sources (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_add_sources (priv->flatpak, list, cancellable, error);
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_add_updates (priv->flatpak, list, cancellable, error);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_refresh (priv->flatpak, cache_age, flags,
				   cancellable, error);
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_refine_app (priv->flatpak, app, flags,
				      cancellable, error);
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
	return gs_flatpak_refine_wildcard (priv->flatpak, app, list, flags,
					   cancellable, error);
}

gboolean
gs_plugin_launch (GsPlugin *plugin,
		  GsApp *app,
		  GCancellable *cancellable,
		  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_launch (priv->flatpak, app, cancellable, error);
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_app_remove (priv->flatpak, app, cancellable, error);
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_app_install (priv->flatpak, app, cancellable, error);
}

gboolean
gs_plugin_update_app (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_update_app (priv->flatpak, app, cancellable, error);
}

gboolean
gs_plugin_file_to_app (GsPlugin *plugin,
		       GsAppList *list,
		       GFile *file,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* only handle when nothing yet has added this */
	if (gs_app_list_length (list) > 0) {
		g_debug ("not handling bundle as already added");
		return TRUE;
	}

	return gs_flatpak_file_to_app (priv->flatpak, list, file,
				       cancellable, error);
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_search (priv->flatpak,
				  values,
				  list,
				  cancellable,
				  error);
}

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_add_categories (priv->flatpak, list,
					  cancellable, error);
}

gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_add_category_apps (priv->flatpak,
					     category,
					     list,
					     cancellable,
					     error);
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_add_popular (priv->flatpak,
				       list,
				       cancellable,
				       error);
}

gboolean
gs_plugin_add_featured (GsPlugin *plugin,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	return gs_flatpak_add_featured (priv->flatpak,
					list,
					cancellable,
					error);
}
