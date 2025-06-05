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

#define GS_TYPE_PLUGIN_JOB_DOWNLOAD_UPGRADE (gs_plugin_job_download_upgrade_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJobDownloadUpgrade, gs_plugin_job_download_upgrade, GS, PLUGIN_JOB_DOWNLOAD_UPGRADE, GsPluginJob)

GsPluginJob	*gs_plugin_job_download_upgrade_new	(GsApp				 *app,
							 GsPluginDownloadUpgradeFlags	  flags);

GsApp		*gs_plugin_job_download_upgrade_get_app	(GsPluginJobDownloadUpgrade	 *self);

G_END_DECLS
