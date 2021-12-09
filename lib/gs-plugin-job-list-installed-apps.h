/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gs-plugin-job.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_JOB_LIST_INSTALLED_APPS (gs_plugin_job_list_installed_apps_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJobListInstalledApps, gs_plugin_job_list_installed_apps, GS, PLUGIN_JOB_LIST_INSTALLED_APPS, GsPluginJob)

GsPluginJob	*gs_plugin_job_list_installed_apps_new	(GsPluginRefineFlags  refine_flags,
							 guint                max_results,
							 GsAppListFilterFlags dedupe_flags);

GsAppList	*gs_plugin_job_list_installed_apps_get_result_list	(GsPluginJobListInstalledApps *self);

G_END_DECLS
