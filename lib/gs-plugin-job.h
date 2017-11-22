/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_PLUGIN_JOB
#define __GS_PLUGIN_JOB

#include <glib-object.h>

#include "gs-app-list.h"
#include "gs-auth.h"
#include "gs-category.h"
#include "gs-plugin-types.h"
#include "gs-price.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_JOB (gs_plugin_job_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginJob, gs_plugin_job, GS, PLUGIN_JOB, GObject)

void		 gs_plugin_job_set_refine_flags		(GsPluginJob	*self,
							 GsPluginRefineFlags refine_flags);
void		 gs_plugin_job_set_filter_flags		(GsPluginJob	*self,
							 GsPluginRefineFlags filter_flags);
void		 gs_plugin_job_set_interactive		(GsPluginJob	*self,
							 gboolean	 interactive);
void		 gs_plugin_job_set_max_results		(GsPluginJob	*self,
							 guint		 max_results);
void		 gs_plugin_job_set_timeout		(GsPluginJob	*self,
							 guint		 timeout);
void		 gs_plugin_job_set_age			(GsPluginJob	*self,
							 guint64	 age);
void		 gs_plugin_job_set_sort_func		(GsPluginJob	*self,
							 GsAppListSortFunc sort_func);
void		 gs_plugin_job_set_sort_func_data	(GsPluginJob	*self,
							 gpointer	 sort_func_data);
void		 gs_plugin_job_set_search		(GsPluginJob	*self,
							 const gchar	*search);
void		 gs_plugin_job_set_auth			(GsPluginJob	*self,
							 GsAuth		*auth);
void		 gs_plugin_job_set_app			(GsPluginJob	*self,
							 GsApp		*app);
void		 gs_plugin_job_set_list			(GsPluginJob	*self,
							 GsAppList	*list);
void		 gs_plugin_job_set_file			(GsPluginJob	*self,
							 GFile		*file);
void		 gs_plugin_job_set_plugin		(GsPluginJob	*self,
							 GsPlugin	*plugin);
void		 gs_plugin_job_set_category		(GsPluginJob	*self,
							 GsCategory	*category);
void		 gs_plugin_job_set_review		(GsPluginJob	*self,
							 AsReview	*review);
void		 gs_plugin_job_set_price		(GsPluginJob	*self,
							 GsPrice	*price);
void		 gs_plugin_job_set_channel		(GsPluginJob	*self,
							 GsChannel	*channel);

#define		 gs_plugin_job_newv(a,...)		GS_PLUGIN_JOB(g_object_new(GS_TYPE_PLUGIN_JOB, "action", a, __VA_ARGS__))

G_END_DECLS

#endif /* __GS_PLUGIN_JOB */

/* vim: set noexpandtab: */
