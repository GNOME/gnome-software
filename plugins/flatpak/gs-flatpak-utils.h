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

#ifndef __GS_FLATPAK_UTILS_H
#define __GS_FLATPAK_UTILS_H

G_BEGIN_DECLS

#include <gnome-software.h>

/* helpers */
#define	gs_app_get_flatpak_kind_as_str(app)	gs_app_get_metadata_item(app,"flatpak::kind")
#define	gs_app_get_flatpak_name(app)		gs_app_get_metadata_item(app,"flatpak::name")
#define	gs_app_get_flatpak_arch(app)		gs_app_get_metadata_item(app,"flatpak::arch")
#define	gs_app_get_flatpak_branch(app)		gs_app_get_metadata_item(app,"flatpak::branch")
#define	gs_app_get_flatpak_commit(app)		gs_app_get_metadata_item(app,"flatpak::commit")
#define	gs_app_get_flatpak_file_type(app)	gs_app_get_metadata_item(app,"flatpak::file-type")
#define	gs_app_get_flatpak_object_id(app)	gs_app_get_metadata_item(app,"flatpak::object-id")
#define	gs_app_set_flatpak_name(app,val)	gs_app_set_metadata(app,"flatpak::name",val)
#define	gs_app_set_flatpak_arch(app,val)	gs_app_set_metadata(app,"flatpak::arch",val)
#define	gs_app_set_flatpak_branch(app,val)	gs_app_set_metadata(app,"flatpak::branch",val)
#define	gs_app_set_flatpak_commit(app,val)	gs_app_set_metadata(app,"flatpak::commit",val)
#define	gs_app_set_flatpak_file_type(app,val)	gs_app_set_metadata(app,"flatpak::file-type",val)
#define	gs_app_set_flatpak_object_id(app,val)	gs_app_set_metadata(app,"flatpak::object-id",val)

void		 gs_flatpak_error_convert		(GError		**perror);

G_END_DECLS

#endif /* __GS_FLATPAK_UTILS_H */

