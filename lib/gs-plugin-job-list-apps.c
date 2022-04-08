/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2022 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <glib.h>

#include "gs-app-query.h"
#include "gs-plugin-job.h"
#include "gs-plugin-job-list-apps.h"
#include "gs-plugin-types.h"

/**
 * gs_plugin_job_list_apps_new:
 * @query: (nullable) (transfer none): query to affect which apps to return
 * @flags: flags affecting how the operation runs
 *
 * Create a new #GsPluginJobListApps for listing apps according to the given
 * @query.
 *
 * Returns: (transfer full): a new #GsPluginJobListApps
 * Since: 43
 */
GsPluginJob *
gs_plugin_job_list_apps_new (GsAppQuery            *query,
                             GsPluginListAppsFlags  flags)
{
	g_autofree gchar *search = NULL;
	GsPluginRefineFlags refine_flags = GS_PLUGIN_REFINE_FLAGS_NONE;

	g_return_val_if_fail (query == NULL || GS_IS_APP_QUERY (query), NULL);

	if (query != NULL && gs_app_query_get_provides_files (query) != NULL) {
		search = g_strjoinv (" ", (gchar **) gs_app_query_get_provides_files (query));
		refine_flags = gs_app_query_get_refine_flags (query);
	}

	return gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH_FILES,
				   "search", search,
				   "refine-flags", refine_flags,
				   NULL);
}
