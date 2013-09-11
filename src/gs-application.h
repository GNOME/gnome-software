/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#ifndef __GS_APPLICATION_H
#define __GS_APPLICATION_H

#include <gtk/gtk.h>

#define GS_APPLICATION_TYPE (gs_application_get_type ())
#define GS_APPLICATION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GS_APPLICATION_TYPE, GsApplication))

typedef struct _GsApplication		GsApplication;
typedef struct _GsApplicationClass	GsApplicationClass;

GType		 gs_application_get_type	(void);
GsApplication	*gs_application_new		(void);

#endif  /* __GS_APPLICATION_H */

/* vim: set noexpandtab: */
