/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2007-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>

#include "gs-plugin-loader.h"

G_BEGIN_DECLS

GsAppList	*gs_plugin_loader_job_process		(GsPluginLoader	*plugin_loader,
							 GsPluginJob	*plugin_job,
							 GCancellable	*cancellable,
							 GError		**error);
GsApp		*gs_plugin_loader_job_process_app	(GsPluginLoader	*plugin_loader,
							 GsPluginJob	*plugin_job,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_loader_job_action		(GsPluginLoader	*plugin_loader,
							 GsPluginJob	*plugin_job,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*gs_plugin_loader_job_get_categories	(GsPluginLoader	*plugin_loader,
							 GsPluginJob	*plugin_job,
							 GCancellable	*cancellable,
							 GError		**error);

G_END_DECLS
