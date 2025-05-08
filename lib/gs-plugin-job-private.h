/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>

#include "gs-plugin-job.h"

G_BEGIN_DECLS

GsPluginAction		 gs_plugin_job_get_action		(GsPluginJob	*self);
GsAppListFilterFlags	 gs_plugin_job_get_dedupe_flags		(GsPluginJob	*self);
GsPluginRefineFlags	 gs_plugin_job_get_refine_flags		(GsPluginJob	*self);
GsPluginRefineRequireFlags	 gs_plugin_job_get_refine_require_flags	(GsPluginJob	*self);
gboolean		 gs_plugin_job_get_propagate_error	(GsPluginJob	*self);
guint			 gs_plugin_job_get_max_results		(GsPluginJob	*self);
const gchar		*gs_plugin_job_get_search		(GsPluginJob	*self);
GsApp			*gs_plugin_job_get_app			(GsPluginJob	*self);
GFile			*gs_plugin_job_get_file			(GsPluginJob	*self);
gchar			*gs_plugin_job_to_string		(GsPluginJob	*self);
void			 gs_plugin_job_set_action		(GsPluginJob	*self,
								 GsPluginAction	 action);
void			 gs_plugin_job_set_cancellable		(GsPluginJob	*self,
								 GCancellable	*cancellable);
void			 gs_plugin_job_cancel			(GsPluginJob	*self);

G_END_DECLS
