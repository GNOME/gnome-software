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

#ifndef GS_APP_ROW_H
#define GS_APP_ROW_H

#include <gtk/gtk.h>

#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_ROW (gs_app_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsAppRow, gs_app_row, GS, APP_ROW, GtkListBoxRow)

struct _GsAppRowClass
{
	GtkListBoxRowClass	 parent_class;
	void			(*button_clicked)	(GsAppRow	*app_row);
	void			(*unrevealed)		(GsAppRow	*app_row);
};

GtkWidget	*gs_app_row_new				(void);
void		 gs_app_row_refresh			(GsAppRow	*app_row);
void		 gs_app_row_unreveal			(GsAppRow	*app_row);
void		 gs_app_row_set_colorful		(GsAppRow	*app_row,
							 gboolean	 colorful);
void		 gs_app_row_set_show_codec		(GsAppRow	*app_row,
							 gboolean	 show_codec);
void		 gs_app_row_set_show_update		(GsAppRow	*app_row,
							 gboolean	 show_update);
void		 gs_app_row_set_selectable 		(GsAppRow	*app_row,
							 gboolean        selectable);
void		 gs_app_row_set_selected		(GsAppRow	*app_row,
							 gboolean        selected);
gboolean	 gs_app_row_get_selected		(GsAppRow	*app_row);
GsApp		*gs_app_row_get_app			(GsAppRow	*app_row);
void		 gs_app_row_set_app			(GsAppRow	*app_row,
							 GsApp		*app);
void		 gs_app_row_set_size_groups		(GsAppRow	*app_row,
							 GtkSizeGroup	*image,
							 GtkSizeGroup	*name);

G_END_DECLS

#endif /* GS_APP_ROW_H */

/* vim: set noexpandtab: */
