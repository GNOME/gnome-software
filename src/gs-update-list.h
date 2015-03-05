/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#ifndef GS_UPDATE_LIST_H
#define GS_UPDATE_LIST_H

#include <gtk/gtk.h>

#include "gs-app.h"

#define GS_TYPE_UPDATE_LIST			(gs_update_list_get_type())
#define GS_UPDATE_LIST(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_UPDATE_LIST, GsUpdateList))
#define GS_UPDATE_LIST_CLASS(cls)		(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_UPDATE_LIST, GsUpdateListClass))
#define GS_IS_UPDATE_LIST(obj)			(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_UPDATE_LIST))
#define GS_IS_UPDATE_LIST_CLASS(cls)		(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_UPDATE_LIST))
#define GS_UPDATE_LIST_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_UPDATE_LIST, GsUpdateListClass))

G_BEGIN_DECLS

typedef struct _GsUpdateList		GsUpdateList;
typedef struct _GsUpdateListClass	GsUpdateListClass;
typedef struct _GsUpdateListPrivate	GsUpdateListPrivate;

struct _GsUpdateList
{
	GtkListBox	 parent;
};

struct _GsUpdateListClass
{
	GtkListBoxClass	 parent_class;
};

GType		 gs_update_list_get_type	(void);
GtkWidget	*gs_update_list_new		(void);
void		 gs_update_list_add_app		(GsUpdateList	*update_list,
						 GsApp		*app);
GPtrArray	*gs_update_list_get_apps	(GsUpdateList	*update_list);

G_END_DECLS

#endif /* GS_UPDATE_LIST_H */

/* vim: set noexpandtab: */
