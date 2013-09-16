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

#ifndef __GS_UPDATE_MONITOR_H
#define __GS_UPDATE_MONITOR_H

#include <glib-object.h>
#include "gs-application.h"

G_BEGIN_DECLS

#define GS_TYPE_UPDATE_MONITOR		(gs_update_monitor_get_type ())
#define GS_UPDATE_MONITOR(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_UPDATE_MONITOR, GsUpdateMonitor))
#define GS_UPDATE_MONITOR_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_UPDATE_MONITOR, GsUpdateMonitorClass))
#define GS_IS_UPDATE_MONITOR(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_UPDATE_MONITOR))
#define GS_IS_UPDATE_MONITOR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_UPDATE_MONITOR))
#define GS_UPDATE_MONITOR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_UPDATE_MONITOR, GsUpdateMonitorClass))

typedef struct _GsUpdateMonitor		GsUpdateMonitor;
typedef struct _GsUpdateMonitorClass	GsUpdateMonitorClass;

GType		 gs_update_monitor_get_type	(void);
GsUpdateMonitor	*gs_update_monitor_new		(GsApplication *app);

G_END_DECLS

#endif /* __GS_UPDATE_MONITOR_H */

/* vim: set noexpandtab: */
