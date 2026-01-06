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

#define GS_TYPE_PLUGIN_JOB_SET_OFFLINE_UPDATE_ACTION (gs_plugin_job_set_offline_update_action_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJobSetOfflineUpdateAction, gs_plugin_job_set_offline_update_action, GS, PLUGIN_JOB_SET_OFFLINE_UPDATE_ACTION, GsPluginJob)

GsPluginJob	*gs_plugin_job_set_offline_update_action_new		(GsPluginSetOfflineUpdateActionFlags flags);

G_END_DECLS
