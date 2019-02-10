/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

struct {
	const gchar *id;
	const gchar *css;
} myapps[] = {
	{ "org.gnome.Builder.desktop",
		"border-color: #000000;\n"
		"text-shadow: 0 1px 1px rgba(0,0,0,0.5);\n"
		"color: #ffffff;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#ffffff, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-builder.png')"
		" left center / 100% auto no-repeat,"
		" url('@datadir@/gnome-software/featured-builder-bg.jpg')"
		" center / cover no-repeat;" },
	{ NULL, NULL }
};

gboolean
gs_plugin_add_featured (GsPlugin *plugin,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	guint i;

	/* we've already got enough featured apps */
	if (gs_app_list_length (list) >= 9)
		return TRUE;

	/* just add all */
	g_debug ("using hardcoded as only %u apps", gs_app_list_length (list));
	for (i = 0; myapps[i].id != NULL; i++) {
		g_autoptr(GsApp) app = NULL;

		/* look in the cache */
		app = gs_plugin_cache_lookup (plugin, myapps[i].id);
		if (app != NULL) {
			gs_app_list_add (list, app);
			continue;
		}

		/* create new */
		app = gs_app_new (myapps[i].id);
		gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_set_metadata (app, "GnomeSoftware::FeatureTile-css",
				     myapps[i].css);
		gs_app_list_add (list, app);

		/* save in the cache */
		gs_plugin_cache_add (plugin, myapps[i].id, app);
	}
	return TRUE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *key = "GnomeSoftware::FeatureTile-css";
	guint i;
	for (i = 0; myapps[i].id != NULL; i++) {
		if (g_strcmp0 (gs_app_get_id (app),
			       myapps[i].id) != 0)
			continue;
		if (gs_app_get_metadata_item (app, key) != NULL)
			continue;
		gs_app_set_metadata (app, key, myapps[i].css);
	}
	return TRUE;
}
