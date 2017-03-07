/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __GS_OS_RELEASE_H
#define __GS_OS_RELEASE_H

#include <glib-object.h>

#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_OS_RELEASE (gs_os_release_get_type ())

G_DECLARE_FINAL_TYPE (GsOsRelease, gs_os_release, GS, OS_RELEASE, GObject)

GsOsRelease	*gs_os_release_new			(GError		**error);
const gchar	*gs_os_release_get_name			(GsOsRelease	*os_release);
const gchar	*gs_os_release_get_version		(GsOsRelease	*os_release);
const gchar	*gs_os_release_get_id			(GsOsRelease	*os_release);
const gchar	*gs_os_release_get_version_id		(GsOsRelease	*os_release);
const gchar	*gs_os_release_get_pretty_name		(GsOsRelease	*os_release);
const gchar	*gs_os_release_get_distro_codename	(GsOsRelease	*os_release);

G_END_DECLS

#endif /* __GS_OS_RELEASE_H */

/* vim: set noexpandtab: */
