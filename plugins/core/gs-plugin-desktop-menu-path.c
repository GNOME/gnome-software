/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2011-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <glib/gi18n.h>

#include <gnome-software.h>

#include "gs-desktop-common.h"

/*
 * SECTION:
 * Adds categories from a hardcoded list based on the the desktop menu
 * specification.
 */

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* need categories */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

static gboolean
_gs_app_has_desktop_group (GsApp *app, const gchar *desktop_group)
{
	guint i;
	g_auto(GStrv) split = g_strsplit (desktop_group, "::", -1);
	for (i = 0; split[i] != NULL; i++) {
		if (!gs_app_has_category (app, split[i]))
			return FALSE;
	}
	return TRUE;
}

/* adds the menu-path for applications */
static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GCancellable         *cancellable,
	    GError              **error)
{
	const gchar *strv[] = { "", NULL, NULL };
	const GsDesktopData *msdata;
	gboolean found = FALSE;
	guint i, j, k;

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH) == 0)
		return TRUE;
	if (gs_app_get_menu_path (app) != NULL)
		return TRUE;

	/* find a top level category the app has */
	msdata = gs_desktop_get_data ();
	for (i = 0; !found && msdata[i].id != NULL; i++) {
		const GsDesktopData *data = &msdata[i];
		for (j = 0; !found && data->mapping[j].id != NULL; j++) {
			const GsDesktopMap *map = &data->mapping[j];
			g_autofree gchar *msgctxt = NULL;

			if (g_strcmp0 (map->id, "all") == 0)
				continue;
			if (g_strcmp0 (map->id, "featured") == 0)
				continue;
			msgctxt = g_strdup_printf ("Menu of %s", data->name);
			for (k = 0; !found && map->fdo_cats[k] != NULL; k++) {
				const gchar *tmp = msdata[i].mapping[j].fdo_cats[k];
				if (_gs_app_has_desktop_group (app, tmp)) {
					strv[0] = g_dgettext (GETTEXT_PACKAGE, msdata[i].name);
					strv[1] = g_dpgettext2 (GETTEXT_PACKAGE, msgctxt,
					                        msdata[i].mapping[j].name);
					found = TRUE;
					break;
				}
			}
		}
	}

	/* always set something to avoid keep searching for this */
	gs_app_set_menu_path (app, (gchar **) strv);
	return TRUE;
}

gboolean
gs_plugin_refine (GsPlugin             *plugin,
		  GsAppList            *list,
		  GsPluginRefineFlags   flags,
		  GCancellable         *cancellable,
		  GError              **error)
{
	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH) == 0)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (plugin, app, flags, cancellable, error))
			return FALSE;
	}

	return TRUE;
}
