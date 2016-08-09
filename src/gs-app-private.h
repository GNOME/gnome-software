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

#ifndef __GS_APP_PRIVATE_H
#define __GS_APP_PRIVATE_H

#include "gs-app.h"

G_BEGIN_DECLS

GError		*gs_app_get_last_error		(GsApp		*app);
void		 gs_app_set_last_error		(GsApp		*app,
						 GError		*error);
void		 gs_app_set_priority		(GsApp		*app,
						 guint		 priority);
guint		 gs_app_get_priority		(GsApp		*app);
void		 gs_app_set_unique_id		(GsApp		*app,
						 const gchar	*unique_id);

G_END_DECLS

#endif /* __GS_APP_PRIVATE_H */

/* vim: set noexpandtab: */
