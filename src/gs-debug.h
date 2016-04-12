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

#ifndef __GS_DEBUG_H
#define __GS_DEBUG_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct {
	GMutex		 mutex;
	gboolean	 use_time;
	gboolean	 use_color;
} GsDebug;

GsDebug		*gs_debug_new	(void);
void		 gs_debug_free	(GsDebug	*debug);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsDebug, gs_debug_free);

G_END_DECLS

#endif /* __GS_DEBUG_H */

/* vim: set noexpandtab: */
