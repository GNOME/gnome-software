/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

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
 * @GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED:	Prefer installed applications
 * @GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES:	Filter using the provides ID
 *
 * Flags to use when filtering. The priority of each #GsApp is used to choose
 * which application object to keep.
 **/
typedef enum {
	GS_APP_LIST_FILTER_FLAG_NONE		= 0,
	GS_APP_LIST_FILTER_FLAG_KEY_ID		= 1 << 0,
	GS_APP_LIST_FILTER_FLAG_KEY_SOURCE	= 1 << 1,
	GS_APP_LIST_FILTER_FLAG_KEY_VERSION	= 1 << 2,
	GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED= 1 << 3,
	GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES	= 1 << 4,
	/*< private >*/
	GS_APP_LIST_FILTER_FLAG_LAST,
	GS_APP_LIST_FILTER_FLAG_MASK		= G_MAXUINT64
} GsAppListFilterFlags;

/* All the properties which use #GsAppListFilterFlags are guint64s. */
G_STATIC_ASSERT (sizeof (GsAppListFilterFlags) == sizeof (guint64));

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
