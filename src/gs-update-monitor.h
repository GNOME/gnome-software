/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
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

#ifndef __GS_UPDATE_MONITOR_H
#define __GS_UPDATE_MONITOR_H

#include <glib-object.h>

#include "gs-application.h"
#include "gs-shell.h"

G_BEGIN_DECLS

#define GS_TYPE_UPDATE_MONITOR (gs_update_monitor_get_type ())

G_DECLARE_FINAL_TYPE (GsUpdateMonitor, gs_update_monitor, GS, UPDATE_MONITOR, GObject)

GsUpdateMonitor	*gs_update_monitor_new			(GsApplication	*app);
void		 gs_update_monitor_get_updates		(GsUpdateMonitor *monitor);
void		 gs_update_monitor_show_error		(GsUpdateMonitor *monitor,
							 GsShell	*shell);

G_END_DECLS

#endif /* __GS_UPDATE_MONITOR_H */

/* vim: set noexpandtab: */
