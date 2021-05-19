/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016-2018 Endless Mobile, Inc.
 *
 * Authors: Joaquim Rocha <jrocha@endlessm.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>
#include <glib/gi18n.h>

#include <gnome-software.h>
#include "gs-external-appstream-utils.h"

void
gs_plugin_initialize (GsPlugin *plugin)
{
	const gchar *system_dir = gs_external_appstream_utils_get_system_dir ();

	/* run it before the appstream plugin */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "appstream");

	g_debug ("appstream system dir: %s", system_dir);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	return gs_external_appstream_refresh (plugin, cache_age, cancellable, error);
}
