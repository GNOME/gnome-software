/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#ifndef GS_APP_ADDON_ROW_H
#define GS_APP_ADDON_ROW_H

#include <gtk/gtk.h>

#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_ADDON_ROW (gs_app_addon_row_get_type ())

G_DECLARE_FINAL_TYPE (GsAppAddonRow, gs_app_addon_row, GS, APP_ADDON_ROW, GtkListBoxRow)

GtkWidget	*gs_app_addon_row_new			(GsApp		*app);
void		 gs_app_addon_row_refresh		(GsAppAddonRow	*row);
void		 gs_app_addon_row_set_selected		(GsAppAddonRow	*row,
							 gboolean        selected);
gboolean	 gs_app_addon_row_get_selected		(GsAppAddonRow	*row);
GsApp		*gs_app_addon_row_get_addon		(GsAppAddonRow	*row);

G_END_DECLS

#endif /* GS_APP_ADDON_ROW_H */

/* vim: set noexpandtab: */
