/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib.h>
#include <gnome-software.h>

#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

GsPluginStatus 	packagekit_status_enum_to_plugin_status		(PkStatusEnum	 status);

gboolean	gs_plugin_packagekit_add_results		(GsPlugin	*plugin,
								 GsAppList	*list,
								 PkResults	*results,
								 GError		**error);
gboolean	gs_plugin_packagekit_error_convert		(GError		**error);
gboolean	gs_plugin_packagekit_results_valid		(PkResults	*results,
								 GError		**error);
void		gs_plugin_packagekit_resolve_packages_app	(GsPlugin *plugin,
								 GPtrArray *packages,
								 GsApp *app);
void		gs_plugin_packagekit_set_metadata_from_package	(GsPlugin *plugin,
								 GsApp *app,
								 PkPackage *package);
GHashTable *	gs_plugin_packagekit_details_array_to_hash	(GPtrArray *array);
void		gs_plugin_packagekit_refine_details_app		(GsPlugin *plugin,
								 GHashTable *details_collection,
								 GsApp *app);
void		gs_plugin_packagekit_set_packaging_format	(GsPlugin *plugin,
								 GsApp *app);

G_END_DECLS
