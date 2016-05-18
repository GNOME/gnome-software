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

#ifndef __APPSTREAM_CACHE_H
#define __APPSTREAM_CACHE_H

#include <glib.h>
#include <gnome-software.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

GsPluginStatus 	packagekit_status_enum_to_plugin_status	(PkStatusEnum	 status);

gboolean	gs_plugin_packagekit_add_results	(GsPlugin	*plugin,
							 GsAppList	*list,
							 PkResults	*results,
							 GError		**error);
gboolean	gs_plugin_packagekit_convert_gerror	(GError		**error);
gboolean	gs_plugin_packagekit_results_valid	(PkResults	*results,
							 GError		**error);

G_END_DECLS

#endif /* __APPSTREAM_CACHE_H */
