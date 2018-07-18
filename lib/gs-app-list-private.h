/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_APP_LIST_PRIVATE_H
#define __GS_APP_LIST_PRIVATE_H

#include "gs-app-list.h"

G_BEGIN_DECLS

/**
 * GsAppListFlags:
 * @GS_APP_LIST_FLAG_NONE:			No flags set
 * @GS_APP_LIST_FLAG_IS_RANDOMIZED:		List has been randomized
 * @GS_APP_LIST_FLAG_IS_TRUNCATED:		List has been truncated
 * @GS_APP_LIST_FLAG_WATCH_APPS:		Applications will be monitored
 * @GS_APP_LIST_FLAG_WATCH_APPS_RELATED:	Applications related apps will be monitored
 * @GS_APP_LIST_FLAG_WATCH_APPS_ADDONS:		Applications addon apps will be monitored
 *
 * Flags used to describe the list.
 **/
typedef enum {
	GS_APP_LIST_FLAG_NONE			= 0,
	GS_APP_LIST_FLAG_IS_RANDOMIZED		= 1 << 0,
	GS_APP_LIST_FLAG_IS_TRUNCATED		= 1 << 1,
	GS_APP_LIST_FLAG_WATCH_APPS		= 1 << 2,
	GS_APP_LIST_FLAG_WATCH_APPS_RELATED	= 1 << 3,
	GS_APP_LIST_FLAG_WATCH_APPS_ADDONS	= 1 << 4,
	/*< private >*/
	GS_APP_LIST_FLAG_LAST
} GsAppListFlags;

/**
 * GsAppListFilterFlags:
 * @GS_APP_LIST_FILTER_FLAG_NONE:		No flags set
 * @GS_APP_LIST_FILTER_FLAG_KEY_ID:		Filter by ID
 * @GS_APP_LIST_FILTER_FLAG_KEY_SOURCE:		Filter by default source
 * @GS_APP_LIST_FILTER_FLAG_KEY_VERSION:	Filter by version
 *
 * Flags to use when filtering. The priority of eash #GsApp is used to choose
 * which application object to keep.
 **/
typedef enum {
	GS_APP_LIST_FILTER_FLAG_NONE		= 0,
	GS_APP_LIST_FILTER_FLAG_KEY_ID		= 1 << 0,
	GS_APP_LIST_FILTER_FLAG_KEY_SOURCE	= 1 << 1,
	GS_APP_LIST_FILTER_FLAG_KEY_VERSION	= 1 << 2,
	/*< private >*/
	GS_APP_LIST_FILTER_FLAG_LAST
} GsAppListFilterFlags;

GsAppList	*gs_app_list_copy		(GsAppList	*list);
guint		 gs_app_list_get_size_peak	(GsAppList	*list);
void		 gs_app_list_filter_duplicates	(GsAppList	*list,
						 GsAppListFilterFlags flags);
void		 gs_app_list_randomize		(GsAppList	*list);
void		 gs_app_list_remove_all		(GsAppList	*list);
void		 gs_app_list_truncate		(GsAppList	*list,
						 guint		 length);
gboolean	 gs_app_list_has_flag		(GsAppList	*list,
						 GsAppListFlags	 flag);
void		 gs_app_list_add_flag		(GsAppList	*list,
						 GsAppListFlags	 flag);
AsAppState	 gs_app_list_get_state		(GsAppList	*list);
guint		 gs_app_list_get_progress	(GsAppList	*list);

G_END_DECLS

#endif /* __GS_APP_LIST_PRIVATE_H */

/* vim: set noexpandtab: */
