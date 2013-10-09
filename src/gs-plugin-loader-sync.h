/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2013 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_PLUGIN_LOADER_SYNC_H
#define __GS_PLUGIN_LOADER_SYNC_H

#include <glib-object.h>

#include "gs-plugin-loader.h"

G_BEGIN_DECLS

GList		*gs_plugin_loader_get_installed		(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
GList		*gs_plugin_loader_get_updates		(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
GList		*gs_plugin_loader_get_popular		(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
GList		*gs_plugin_loader_get_categories	(GsPluginLoader	*plugin_loader,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
GList		*gs_plugin_loader_get_category_apps	(GsPluginLoader	*plugin_loader,
							 GsCategory	*category,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);

G_END_DECLS

#endif /* __GS_PLUGIN_LOADER_SYNC_H */

/* vim: set noexpandtab: */
