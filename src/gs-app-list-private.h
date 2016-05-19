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

typedef gboolean (*GsAppListFilterFunc)		(GsApp		*app,
						 gpointer	 user_data);
typedef gboolean (*GsAppListSortFunc)		(GsApp		*app1,
						 GsApp		*app2,
						 gpointer	 user_data);

GsAppList	*gs_app_list_copy		(GsAppList	*list);
void		 gs_app_list_filter		(GsAppList	*list,
						 GsAppListFilterFunc func,
						 gpointer	 user_data);
void		 gs_app_list_sort		(GsAppList	*list,
						 GsAppListSortFunc func,
						 gpointer	 user_data);
void		 gs_app_list_filter_duplicates	(GsAppList	*list);
void		 gs_app_list_randomize		(GsAppList	*list);

G_END_DECLS

#endif /* __GS_APP_LIST_PRIVATE_H */

/* vim: set noexpandtab: */
