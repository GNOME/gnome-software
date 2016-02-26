/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <gs-plugin.h>

/*
 * SECTION:
 * Sets the package provanance to TRUE if installed by an official
 * Fedora repo.
 *
 * It will self-disable if not run on a Fedora system.
 */

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "fedora-provenance";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* check that we are running on Fedora */
	if (!gs_plugin_check_distro_id (plugin, "fedora")) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as we're not Fedora", plugin->name);
		return;
	}
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"packagekit-refine",	/* after the package source is set */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
}

/**
 * gs_plugin_fedora_provenance_refine_app:
 */
static void
gs_plugin_fedora_provenance_refine_app (GsApp *app)
{
	const gchar *origin;
	guint i;
	const gchar *valid[] = { "fedora",
				 "fedora-debuginfo",
				 "fedora-source",
				 "koji-override-0",
				 "koji-override-1",
				 "rawhide",
				 "rawhide-debuginfo",
				 "rawhide-source",
				 "updates",
				 "updates-debuginfo",
				 "updates-source",
				 "updates-testing",
				 "updates-testing-debuginfo",
				 "updates-testing-source",
				 NULL };

	/* simple case */
	origin = gs_app_get_origin (app);
	if (origin != NULL && g_strv_contains (valid, origin)) {
		gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
		return;
	}

	/* this only works for packages */
	origin = gs_app_get_source_id_default (app);
	if (origin == NULL)
		return;
	origin = g_strrstr (origin, ";");
	if (origin == NULL)
		return;
	if (g_str_has_prefix (origin + 1, "installed:"))
		origin += 10;
	for (i = 0; valid[i] != NULL; i++) {
		if (g_strcmp0 (origin + 1, valid[i]) == 0) {
			gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
			break;
		}
	}
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

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE) == 0)
		return TRUE;

	/* refine apps */
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_has_quirk (app, AS_APP_QUIRK_PROVENANCE))
			continue;
		gs_plugin_fedora_provenance_refine_app (app);
	}
	return TRUE;
}
