/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * SPDX-FileCopyrightText: (C) 2026 Red Hat <www.redhat.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gs-plugin-job.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_JOB_GET_OFFLINE_UPDATE_STATE (gs_plugin_job_get_offline_update_state_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJobGetOfflineUpdateState, gs_plugin_job_get_offline_update_state, GS, PLUGIN_JOB_GET_OFFLINE_UPDATE_STATE, GsPluginJob)

GsPluginJob	*gs_plugin_job_get_offline_update_state_new		(GsPluginGetOfflineUpdateStateFlags flags);

GsPluginOfflineUpdateState
		 gs_plugin_job_get_offline_update_state_get_result	(GsPluginJobGetOfflineUpdateState *self);

G_END_DECLS
