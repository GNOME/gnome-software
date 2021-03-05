/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <string.h>

#include <gnome-software.h>

/*
 * SECTION:
 * Loads remote icons and converts them into local cached ones.
 *
 * It is provided so that each plugin handling icons does not
 * have to handle the download and caching functionality.
 *
 * FIXME: This plugin will eventually go away. Currently it only exists as the
 * plugin threading code is a convenient way of ensuring that loading the remote
 * icons happens in a worker thread.
 */

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* needs remote icons downloaded */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	/* Nothing to do here */
}

static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GCancellable         *cancellable,
	    GError              **error)
{
	GPtrArray *icons;
	guint i;
	SoupSession *soup_session;
	guint maximum_icon_size;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON) == 0)
		return TRUE;

	soup_session = gs_plugin_get_soup_session (plugin);

	/* Currently a 160px icon is needed for #GsFeatureTile, at most. */
	maximum_icon_size = 160 * gs_plugin_get_scale (plugin);

	/* process all icons */
	icons = gs_app_get_icons (app);
	for (i = 0; icons != NULL && i < icons->len; i++) {
		GIcon *icon = g_ptr_array_index (icons, i);
		g_autoptr(GError) error_local = NULL;

		/* Only remote icons need to be cached. */
		if (!GS_IS_REMOTE_ICON (icon))
			continue;

		if (!gs_remote_icon_ensure_cached (GS_REMOTE_ICON (icon),
						   soup_session,
						   maximum_icon_size,
						   cancellable,
						   &error_local)) {
			/* we failed, but keep going */
			g_debug ("failed to cache icon for %s: %s",
				 gs_app_get_id (app),
				 error_local->message);
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
	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON) == 0)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (plugin, app, flags, cancellable, error))
			return FALSE;
	}

	return TRUE;
}
