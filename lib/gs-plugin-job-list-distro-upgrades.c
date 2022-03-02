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
#include "gs-plugin-job-list-distro-upgrades.h"
#include "gs-plugin-types.h"

/**
 * gs_plugin_job_list_distro_upgrades_new:
 * @flags: flags affecting how the operation runs
 * @refine_flags: flags to affect how the results are refined, or
 *   %GS_PLUGIN_REFINE_FLAGS_NONE to skip refining them
 *
 * Create a new #GsPluginJob for listing the available distro
 * upgrades.
 *
 * Returns: (transfer full): a new #GsPluginJob
 * Since: 42
 */
GsPluginJob *
gs_plugin_job_list_distro_upgrades_new (GsPluginListDistroUpgradesFlags flags,
                                        GsPluginRefineFlags             refine_flags)
{
       return gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_DISTRO_UPDATES,
                                  "refine-flags", refine_flags,
                                  "interactive", (flags & GS_PLUGIN_LIST_DISTRO_UPGRADES_FLAGS_INTERACTIVE) ? TRUE : FALSE,
                                  NULL);
}
