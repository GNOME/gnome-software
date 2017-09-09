/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_FWUPD_APP_H
#define __GS_FWUPD_APP_H

#include <gnome-software.h>
#include <fwupd.h>

G_BEGIN_DECLS

const gchar		*gs_fwupd_app_get_device_id		(GsApp		*app);
const gchar		*gs_fwupd_app_get_update_uri		(GsApp		*app);
gboolean		 gs_fwupd_app_get_is_locked		(GsApp		*app);

void			 gs_fwupd_app_set_device_id		(GsApp		*app,
								 const gchar	*device_id);
void			 gs_fwupd_app_set_update_uri		(GsApp		*app,
								 const gchar	*update_uri);
void			 gs_fwupd_app_set_is_locked		(GsApp		*app,
								 gboolean	 is_locked);
void			 gs_fwupd_app_set_from_device		(GsApp		*app,
								 FwupdDevice	*dev);
void			 gs_fwupd_app_set_from_release		(GsApp		*app,
								 FwupdRelease	*rel);

G_END_DECLS

#endif /* __GS_FWUPD_APP_H */

/* vim: set noexpandtab: */
