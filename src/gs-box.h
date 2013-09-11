/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#ifndef GS_BOX_H
#define GS_BOX_H

#include <gtk/gtk.h>

#define GS_TYPE_BOX		(gs_box_get_type())
#define GS_BOX(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_BOX, GsBox))
#define GS_BOX_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_BOX, GsBoxClass))
#define GS_IS_BOX(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_BOX))
#define GS_IS_BOX_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_BOX))
#define GS_BOX_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_BOX, GsBoxClass))

G_BEGIN_DECLS

typedef struct _GsBox		GsBox;
typedef struct _GsBoxClass	GsBoxClass;

GType		 gs_box_get_type        (void);
GtkWidget	*gs_box_new		(void);
void             gs_box_add             (GsBox     *box,
                                         GtkWidget *child,
                                         gdouble    relative_size);

G_END_DECLS

#endif /* GS_BOX_H */

/* vim: set noexpandtab: */
