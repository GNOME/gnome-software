/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <gnome-software.h>

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* let appstream add applications first */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
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
		"org.gnome.Evince",
		"org.gnome.gedit.desktop",
		"org.gnome.Maps.desktop",
		"org.gnome.Weather",
		NULL };

	/* we've already got enough popular apps */
	if (gs_app_list_length (list) >= 9)
		return TRUE;

	/* just add all */
	g_debug ("using hardcoded as only %u apps", gs_app_list_length (list));
	for (i = 0; apps[i] != NULL; i++) {
		g_autoptr(GsApp) app = NULL;

		/* look in the cache */
		app = gs_plugin_cache_lookup (plugin, apps[i]);
		if (app != NULL) {
			gs_app_list_add (list, app);
			continue;
		}

		/* create new */
		app = gs_app_new (apps[i]);
		gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_list_add (list, app);

		/* save in the cache */
		gs_plugin_cache_add (plugin, apps[i], app);
	}
	return TRUE;
}
