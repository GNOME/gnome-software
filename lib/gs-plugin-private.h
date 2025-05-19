/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gio/gio.h>
#include <glib-object.h>
#include <gmodule.h>

#include "gs-plugin.h"

G_BEGIN_DECLS

GsPlugin	*gs_plugin_create			(const gchar	*filename,
							 GDBusConnection *session_bus_connection,
							 GDBusConnection *system_bus_connection,
							 GError		**error);
const gchar	*gs_plugin_error_to_string		(GsPluginError	 error);

void		 gs_plugin_set_scale			(GsPlugin	*plugin,
							 guint		 scale);
guint		 gs_plugin_get_order			(GsPlugin	*plugin);
void		 gs_plugin_set_order			(GsPlugin	*plugin,
							 guint		 order);
guint		 gs_plugin_get_priority			(GsPlugin	*plugin);
void		 gs_plugin_set_priority			(GsPlugin	*plugin,
							 guint		 priority);
void		 gs_plugin_set_language			(GsPlugin	*plugin,
							 const gchar	*language);
GPtrArray	*gs_plugin_get_rules			(GsPlugin	*plugin,
							 GsPluginRule	 rule);
gchar		*gs_plugin_refine_flags_to_string	(GsPluginRefineFlags refine_flags);
gchar		*gs_plugin_refine_require_flags_to_string	(GsPluginRefineRequireFlags require_flags);
void		 gs_plugin_set_network_monitor		(GsPlugin		*plugin,
							 GNetworkMonitor	*monitor);

G_END_DECLS
