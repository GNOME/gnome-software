/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Endless Mobile, Inc.
 *
 * Authors: Joaquim Rocha <jrocha@endlessm.com>
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

#ifndef GS_EXTERNAL_APPSTREAM_UTILS_H
#define GS_EXTERNAL_APPSTREAM_UTILS_H

#include <config.h>
#include <glib.h>

const gchar	*gs_external_appstream_utils_get_system_dir (void);
gchar		*gs_external_appstream_utils_get_file_cache_path (const gchar	*file_name);

#endif /* GS_EXTERNAL_APPSTREAM_UTILS_H */
