/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#include <config.h>

#include <glib/gi18n.h>
#include <gnome-software.h>

void
gs_plugin_initialize (GsPlugin *plugin)
{
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit-refine");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "icons");
}

static gboolean
gs_plugin_generic_updates_merge_os_update (GsApp *app)
{
	/* this is only for grouping system-installed packages */
	if (gs_app_get_bundle_kind (app) != AS_BUNDLE_KIND_PACKAGE ||
	    gs_app_get_scope (app) != AS_APP_SCOPE_SYSTEM)
		return FALSE;

	if (gs_app_get_kind (app) == AS_APP_KIND_GENERIC)
		return TRUE;
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
		return TRUE;

	return FALSE;
}

static GsApp *
gs_plugin_generic_updates_get_os_update (GsPlugin *plugin)
{
	GsApp *app;
	const gchar *id = "org.gnome.Software.OsUpdate";
	g_autoptr(AsIcon) ic = NULL;

	/* create new */
	app = gs_app_new (id);
	gs_app_add_quirk (app, AS_APP_QUIRK_IS_PROXY);
	gs_app_set_management_plugin (app, "");
	gs_app_set_kind (app, AS_APP_KIND_OS_UPDATE);
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
	gs_app_set_name (app,
			 GS_APP_QUALITY_NORMAL,
			 /* TRANSLATORS: this is a group of updates that are not
			  * packages and are not shown in the main list */
			 _("OS Updates"));
	gs_app_set_summary (app,
			    GS_APP_QUALITY_NORMAL,
			    /* TRANSLATORS: this is a longer description of the
			     * "OS Updates" string */
			    _("Includes performance, stability and security improvements."));
	gs_app_set_description (app,
				GS_APP_QUALITY_NORMAL,
				gs_app_get_summary (app));
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "software-update-available-symbolic");
	gs_app_add_icon (app, ic);
	return app;
}

gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GsAppList *list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsAppList) os_updates = gs_app_list_new ();

	/* not from get_updates() */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS) == 0)
		return TRUE;

	/* do we have any packages left that are not apps? */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app_tmp = gs_app_list_index (list, i);
		if (gs_app_has_quirk (app_tmp, AS_APP_QUIRK_MATCH_ANY_PREFIX))
			continue;
		if (gs_plugin_generic_updates_merge_os_update (app_tmp))
			gs_app_list_add (os_updates, app_tmp);
	}
	if (gs_app_list_length (os_updates) == 0)
		return TRUE;

	/* create new meta object */
	app = gs_plugin_generic_updates_get_os_update (plugin);
	for (guint i = 0; i < gs_app_list_length (os_updates); i++) {
		GsApp *app_tmp = gs_app_list_index (os_updates, i);
		const gchar *id = gs_app_get_unique_id (app_tmp);
		if (id == NULL)
			id = gs_app_get_source_default (app_tmp);
		g_debug ("moving %s to parent %s",
			 id, gs_app_get_unique_id (app));
		gs_app_add_related (app, app_tmp);
		gs_app_list_remove (list, app_tmp);
	}
	gs_app_list_add (list, app);
	return TRUE;
}
