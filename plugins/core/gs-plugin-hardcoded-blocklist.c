/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <fnmatch.h>
#include <gnome-software.h>

#include "gs-plugin-hardcoded-blocklist.h"

/*
 * SECTION:
 * Blocklists some applications based on a hardcoded list.
 */

struct _GsPluginHardcodedBlocklist
{
	GsPlugin		 parent;
};

G_DEFINE_TYPE (GsPluginHardcodedBlocklist, gs_plugin_hardcoded_blocklist, GS_TYPE_PLUGIN)

static void
gs_plugin_hardcoded_blocklist_init (GsPluginHardcodedBlocklist *self)
{
	/* need ID */
	gs_plugin_add_rule (GS_PLUGIN (self), GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GCancellable         *cancellable,
	    GError              **error)
{
	guint i;
	const gchar *app_globs[] = {
		"freeciv-server.desktop",
		"links.desktop",
		"nm-connection-editor.desktop",
		"plank.desktop",
		"*release-notes*.desktop",
		"*Release_Notes*.desktop",
		"Rodent-*.desktop",
		"rygel-preferences.desktop",
		"system-config-keyboard.desktop",
		"tracker-preferences.desktop",
		"Uninstall*.desktop",
		"wine-*.desktop",
		NULL };

	/* not set yet */
	if (gs_app_get_id (app) == NULL)
		return TRUE;

	/* search */
	for (i = 0; app_globs[i] != NULL; i++) {
		if (fnmatch (app_globs[i], gs_app_get_id (app), 0) == 0) {
			gs_app_add_quirk (app, GS_APP_QUIRK_HIDE_EVERYWHERE);
			break;
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

static void
gs_plugin_hardcoded_blocklist_class_init (GsPluginHardcodedBlocklistClass *klass)
{
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_HARDCODED_BLOCKLIST;
}
