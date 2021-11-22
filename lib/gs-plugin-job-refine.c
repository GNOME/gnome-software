/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib.h>

/**
 * gs_plugin_job_refine_new:
 * @app_list: the list of #GsApps to refine
 * @flags: flags to affect what is refined
 *
 * Create a new #GsPluginJob for refining the given @app_list.
 *
 * Returns: (transfer full): a new #GsPluginJob
 * Since: 42
 */
GsPluginJob *
gs_plugin_job_refine_new (GsAppList           *app_list,
                          GsPluginRefineFlags  flags)
{
	return gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
				   "list", app_list,
				   "refine-flags", flags,
				   NULL);
}

/**
 * gs_plugin_job_refine_new_for_app:
 * @app: the #GsApp to refine
 * @flags: flags to affect what is refined
 *
 * Create a new #GsPluginJob for refining the given @app.
 *
 * Returns: (transfer full): a new #GsPluginJob
 * Since: 42
 */
GsPluginJob *
gs_plugin_job_refine_new_for_app (GsApp               *app,
                                  GsPluginRefineFlags  flags)
{
	return gs_plugin_job_newv (GS_PLUGIN_ACTION_REFINE,
				   "app", app,
				   "refine-flags", flags,
				   NULL);
}
