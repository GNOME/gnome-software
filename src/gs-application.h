/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#ifndef __GS_APPLICATION_H
#define __GS_APPLICATION_H

#include <gtk/gtk.h>

#include "gs-plugin-loader.h"

#define GS_APPLICATION_TYPE (gs_application_get_type ())

G_DECLARE_FINAL_TYPE (GsApplication, gs_application, GS, APPLICATION, GtkApplication)

GsApplication	*gs_application_new		(void);
GsPluginLoader	*gs_application_get_plugin_loader	(GsApplication *application);
gboolean	 gs_application_has_active_window	(GsApplication *application);

#endif  /* __GS_APPLICATION_H */

/* vim: set noexpandtab: */
