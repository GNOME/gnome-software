/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gs-plugin-job.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_JOB_FILE_TO_APP (gs_plugin_job_file_to_app_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJobFileToApp, gs_plugin_job_file_to_app, GS, PLUGIN_JOB_FILE_TO_APP, GsPluginJob)

GsPluginJob	*gs_plugin_job_file_to_app_new	(GFile			*file,
						 GsPluginFileToAppFlags	 flags,
						 GsPluginRefineRequireFlags require_flags);
GsAppList	*gs_plugin_job_file_to_app_get_result_list
						(GsPluginJobFileToApp	 *self);

G_END_DECLS
