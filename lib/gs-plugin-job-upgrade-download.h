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

#define GS_TYPE_PLUGIN_JOB_UPGRADE_DOWNLOAD (gs_plugin_job_upgrade_download_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJobUpgradeDownload, gs_plugin_job_upgrade_download, GS, PLUGIN_JOB_UPGRADE_DOWNLOAD, GsPluginJob)

GsPluginJob	*gs_plugin_job_upgrade_download_new	(GsApp				 *app,
							 GsPluginUpgradeDownloadFlags	  flags);

G_END_DECLS
