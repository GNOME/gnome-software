/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * gs_plugin_job_list_installed_apps_new:
 * @refine_flags: flags to affect how the results are refined, or
 *   %GS_PLUGIN_REFINE_FLAGS_NONE to skip refining them
 * @max_results: maximum number of results to return, or `0` to not limit the
 *   results
 * @dedupe_flags: flags to control deduplicating the results
 *
 * Create a new #GsPluginJob for listing the installed apps.
 *
 * Returns: (transfer full): a new #GsPluginJob
 * Since: 42
 */
GsPluginJob *
gs_plugin_job_list_installed_apps_new (GsPluginRefineFlags  refine_flags,
                                       guint                max_results,
                                       GsAppListFilterFlags dedupe_flags)
{
	return gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_INSTALLED,
				   "refine-flags", refine_flags,
				   "max-results", max_results,
				   "dedupe-flags", dedupe_flags,
				   NULL);
}
