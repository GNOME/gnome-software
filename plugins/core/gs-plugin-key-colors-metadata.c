/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <gnome-software.h>

void
gs_plugin_initialize (GsPlugin *plugin)
{
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "key-colors");
}

static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GCancellable         *cancellable,
	    GError              **error)
{
	GPtrArray *key_colors;
	const gchar *keys[] = {
		"GnomeSoftware::AppTile-css",
		"GnomeSoftware::FeatureTile-css",
		"GnomeSoftware::UpgradeBanner-css",
		NULL };

	/* not set */
	key_colors = gs_app_get_key_colors (app);
	if (key_colors->len == 0)
		return TRUE;

	/* rewrite URIs */
	for (guint i = 0; keys[i] != NULL; i++) {
		const gchar *css;
		g_autoptr(GString) css_new = NULL;

		/* metadata is not set */
		css = gs_app_get_metadata_item (app, keys[i]);
		if (css == NULL)
			continue;
		if (g_strstr_len (css, -1, "@keycolor") == NULL)
			continue;

		/* replace key color values */
		css_new = g_string_new (css);
		for (guint j = 0; j < key_colors->len; j++) {
			GdkRGBA *color = g_ptr_array_index (key_colors, j);
			g_autofree gchar *key = NULL;
			g_autofree gchar *value = NULL;
			key = g_strdup_printf ("@keycolor-%02u@", j);
			value = g_strdup_printf ("rgb(%.0f,%.0f,%.0f)",
						 color->red * 255.f,
						 color->green * 255.f,
						 color->blue * 255.f);
			as_utils_string_replace (css_new, key, value);
		}

		/* only replace if it's different */
		if (g_strcmp0 (css, css_new->str) != 0) {
			gs_app_set_metadata (app, keys[i], NULL);
			gs_app_set_metadata (app, keys[i], css_new->str);
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
