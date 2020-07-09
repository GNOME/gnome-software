/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gnome-software.h>

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* let appstream add metadata first */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GCancellable         *cancellable,
	    GError              **error)
{
	const gchar *keys[] = {
		"GnomeSoftware::AppTile-css",
		"GnomeSoftware::FeatureTile-css",
		"GnomeSoftware::UpgradeBanner-css",
		NULL };

	/* rewrite URIs */
	for (guint i = 0; keys[i] != NULL; i++) {
		const gchar *css = gs_app_get_metadata_item (app, keys[i]);
		if (css != NULL) {
			g_autofree gchar *css_new = NULL;
			g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));
			gs_app_set_summary_missing (app_dl,
						    /* TRANSLATORS: status text when downloading */
						    _("Downloading featured images…"));
			css_new = gs_plugin_download_rewrite_resource (plugin,
								       app,
								       css,
								       cancellable,
								       error);
			if (css_new == NULL)
				return FALSE;
			if (g_strcmp0 (css, css_new) != 0) {
				gs_app_set_metadata (app, keys[i], NULL);
				gs_app_set_metadata (app, keys[i], css_new);
			}
		}
	}
	return TRUE;
}

gboolean
gs_plugin_refine (GsPlugin             *plugin,
		  GsAppList            *list,
		  GsPluginRefineFlags   flags,
		  GCancellable         *cancellable,
		  GError              **error)
{
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (plugin, app, flags, cancellable, error))
			return FALSE;
	}

	return TRUE;
}
