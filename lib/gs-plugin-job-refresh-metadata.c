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

#include "gs-plugin-job.h"
#include "gs-plugin-job-refresh-metadata.h"
#include "gs-plugin-types.h"

/**
 * gs_plugin_job_refresh_metadata_new:
 * @cache_age_secs: maximum allowed cache age, in seconds
 * @flags: flags to affect the refresh
 *
 * Create a new #GsPluginJob for refreshing metadata about available
 * applications.
 *
 * Caches will be refreshed if they are older than @cache_age_secs.
 *
 * Returns: (transfer full): a new #GsPluginJob
 * Since: 42
 */
GsPluginJob *
gs_plugin_job_refresh_metadata_new (guint64                      cache_age_secs,
                                    GsPluginRefreshMetadataFlags flags)
{
	return gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
				   "age", cache_age_secs,
				   "interactive", (flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE) ? TRUE : FALSE,
				   NULL);
}
