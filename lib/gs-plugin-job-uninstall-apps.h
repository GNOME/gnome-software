/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 GNOME Foundation, Inc.
 *
 * Author: Philip Withnall <pwithnall@gnome.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gs-plugin-job.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_JOB_UNINSTALL_APPS (gs_plugin_job_uninstall_apps_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJobUninstallApps, gs_plugin_job_uninstall_apps, GS, PLUGIN_JOB_UNINSTALL_APPS, GsPluginJob)

GsPluginJob			*gs_plugin_job_uninstall_apps_new	(GsAppList                *apps,
									 GsPluginUninstallAppsFlags  flags);

GsAppList			*gs_plugin_job_uninstall_apps_get_apps	(GsPluginJobUninstallApps	*self);
GsPluginUninstallAppsFlags	 gs_plugin_job_uninstall_apps_get_flags	(GsPluginJobUninstallApps	*self);

G_END_DECLS
