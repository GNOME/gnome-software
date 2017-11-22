/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __GS_PLUGIN_JOB_PRIVATE
#define __GS_PLUGIN_JOB_PRIVATE

#include <glib-object.h>

#include "gs-plugin-job.h"

G_BEGIN_DECLS

GsPluginAction		 gs_plugin_job_get_action		(GsPluginJob	*self);
GsPluginRefineFlags	 gs_plugin_job_get_filter_flags		(GsPluginJob	*self);
GsPluginRefineFlags	 gs_plugin_job_get_refine_flags		(GsPluginJob	*self);
gboolean		 gs_plugin_job_has_refine_flags		(GsPluginJob	*self,
								 GsPluginRefineFlags refine_flags);
void			 gs_plugin_job_add_refine_flags		(GsPluginJob	*self,
								 GsPluginRefineFlags refine_flags);
void			 gs_plugin_job_remove_refine_flags	(GsPluginJob	*self,
								 GsPluginRefineFlags refine_flags);
gboolean		 gs_plugin_job_get_interactive		(GsPluginJob	*self);
guint			 gs_plugin_job_get_max_results		(GsPluginJob	*self);
guint			 gs_plugin_job_get_timeout		(GsPluginJob	*self);
guint64			 gs_plugin_job_get_age			(GsPluginJob	*self);
GsAppListSortFunc	 gs_plugin_job_get_sort_func		(GsPluginJob	*self);
gpointer		 gs_plugin_job_get_sort_func_data	(GsPluginJob	*self);
const gchar		*gs_plugin_job_get_search		(GsPluginJob	*self);
GsAuth			*gs_plugin_job_get_auth			(GsPluginJob	*self);
GsApp			*gs_plugin_job_get_app			(GsPluginJob	*self);
GsAppList		*gs_plugin_job_get_list			(GsPluginJob	*self);
GFile			*gs_plugin_job_get_file			(GsPluginJob	*self);
GsPlugin		*gs_plugin_job_get_plugin		(GsPluginJob	*self);
GsCategory		*gs_plugin_job_get_category		(GsPluginJob	*self);
AsReview		*gs_plugin_job_get_review		(GsPluginJob	*self);
GsPrice			*gs_plugin_job_get_price		(GsPluginJob	*self);
GsChannel		*gs_plugin_job_get_channel		(GsPluginJob	*self);
gchar			*gs_plugin_job_to_string		(GsPluginJob	*self);
void			 gs_plugin_job_set_action		(GsPluginJob	*self,
								 GsPluginAction	 action);

G_END_DECLS

#endif /* __GS_PLUGIN_JOB_PRIVATE */

/* vim: set noexpandtab: */
