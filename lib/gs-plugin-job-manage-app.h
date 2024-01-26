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

#define GS_TYPE_PLUGIN_JOB_MANAGE_APP (gs_plugin_job_manage_app_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJobManageApp, gs_plugin_job_manage_app, GS, PLUGIN_JOB_MANAGE_APP, GsPluginJob)

GsPluginJob	*gs_plugin_job_manage_app_new	(GsApp			 *app,
						 GsPluginManageAppFlags	  flags);

G_END_DECLS
