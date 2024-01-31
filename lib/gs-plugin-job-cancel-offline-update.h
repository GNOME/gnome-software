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

#define GS_TYPE_PLUGIN_JOB_CANCEL_OFFLINE_UPDATE (gs_plugin_job_cancel_offline_update_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJobCancelOfflineUpdate, gs_plugin_job_cancel_offline_update, GS, PLUGIN_JOB_CANCEL_OFFLINE_UPDATE, GsPluginJob)

GsPluginJob	*gs_plugin_job_cancel_offline_update_new	(GsPluginCancelOfflineUpdateFlags flags);

G_END_DECLS
