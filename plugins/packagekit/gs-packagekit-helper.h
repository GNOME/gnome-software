/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_PACKAGEKIT_HELPER_H
#define __GS_PACKAGEKIT_HELPER_H

#include <glib-object.h>
#include <gnome-software.h>
#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

#define GS_TYPE_PACKAGEKIT_HELPER (gs_packagekit_helper_get_type ())

G_DECLARE_FINAL_TYPE (GsPackagekitHelper, gs_packagekit_helper, GS, PACKAGEKIT_HELPER, GObject)

GsPackagekitHelper *gs_packagekit_helper_new		(GsPlugin		*plugin);
GsPlugin	*gs_packagekit_helper_get_plugin	(GsPackagekitHelper	*self);
void		 gs_packagekit_helper_add_app		(GsPackagekitHelper	*self,
							 GsApp			*app);
GsApp		*gs_packagekit_helper_get_app_by_id	(GsPackagekitHelper	*progress,
							 const gchar		*package_id);
void		 gs_packagekit_helper_cb		(PkProgress		*progress,
							 PkProgressType		 type,
							 gpointer		 user_data);


G_END_DECLS

#endif /* __GS_PACKAGEKIT_HELPER_H */

