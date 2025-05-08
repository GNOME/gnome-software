/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>

#include "gs-app-list.h"
#include "gs-plugin-types.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_JOB (gs_plugin_job_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsPluginJob, gs_plugin_job, GS, PLUGIN_JOB, GObject)

#include "gs-plugin-loader.h"

struct _GsPluginJobClass
{
	GObjectClass parent_class;

	void (*run_async) (GsPluginJob         *self,
	                   GsPluginLoader      *plugin_loader,
	                   GCancellable        *cancellable,
	                   GAsyncReadyCallback  callback,
	                   gpointer             user_data);
	gboolean (*run_finish) (GsPluginJob   *self,
	                        GAsyncResult  *result,
	                        GError       **error);
	gboolean (*get_interactive) (GsPluginJob *self);
};

gboolean	 gs_plugin_job_get_interactive		(GsPluginJob	*self);
void		 gs_plugin_job_set_refine_flags		(GsPluginJob	*self,
							 GsPluginRefineFlags refine_flags);
void		 gs_plugin_job_set_refine_require_flags		(GsPluginJob	*self,
								 GsPluginRefineRequireFlags require_flags);
void		 gs_plugin_job_set_propagate_error	(GsPluginJob	*self,
							 gboolean	 propagate_error);
void		 gs_plugin_job_set_app			(GsPluginJob	*self,
							 GsApp		*app);

G_END_DECLS
