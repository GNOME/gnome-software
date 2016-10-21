/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_APP_LIST_H
#define __GS_APP_LIST_H

#include <glib-object.h>

#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_LIST (gs_app_list_get_type ())

G_DECLARE_FINAL_TYPE (GsAppList, gs_app_list, GS, APP_LIST, GObject)

GsAppList	*gs_app_list_new		(void);
void		 gs_app_list_add		(GsAppList	*list,
						 GsApp		*app);
void		 gs_app_list_add_list		(GsAppList	*list,
						 GsAppList	*donor);
GsApp		*gs_app_list_index		(GsAppList	*list,
						 guint		 idx);
GsApp		*gs_app_list_lookup		(GsAppList	*list,
						 const gchar	*unique_id);
guint		 gs_app_list_length		(GsAppList	*list);

G_END_DECLS

#endif /* __GS_APP_LIST_H */

/* vim: set noexpandtab: */
