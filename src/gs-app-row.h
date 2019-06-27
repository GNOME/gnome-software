/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_ROW (gs_app_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsAppRow, gs_app_row, GS, APP_ROW, GtkListBoxRow)

struct _GsAppRowClass
{
	GtkListBoxRowClass	 parent_class;
	void			(*button_clicked)	(GsAppRow	*app_row);
	void			(*unrevealed)		(GsAppRow	*app_row);
};

GtkWidget	*gs_app_row_new				(GsApp		*app);
void		 gs_app_row_refresh			(GsAppRow	*app_row);
void		 gs_app_row_unreveal			(GsAppRow	*app_row);
void		 gs_app_row_set_colorful		(GsAppRow	*app_row,
							 gboolean	 colorful);
void		 gs_app_row_set_show_folders		(GsAppRow	*app_row,
							 gboolean	 show_folders);
void		 gs_app_row_set_show_buttons		(GsAppRow	*app_row,
							 gboolean	 show_buttons);
void		 gs_app_row_set_show_rating		(GsAppRow	*app_row,
							 gboolean	 show_rating);
void		 gs_app_row_set_show_source		(GsAppRow	*app_row,
							 gboolean	 show_source);
void		 gs_app_row_set_show_update		(GsAppRow	*app_row,
							 gboolean	 show_update);
void		 gs_app_row_set_selectable 		(GsAppRow	*app_row,
							 gboolean        selectable);
void		 gs_app_row_set_selected		(GsAppRow	*app_row,
							 gboolean        selected);
gboolean	 gs_app_row_get_selected		(GsAppRow	*app_row);
GsApp		*gs_app_row_get_app			(GsAppRow	*app_row);
void		 gs_app_row_set_size_groups		(GsAppRow	*app_row,
							 GtkSizeGroup	*image,
							 GtkSizeGroup	*name,
							 GtkSizeGroup	*desc,
							 GtkSizeGroup	*button);
void		 gs_app_row_set_show_installed_size	(GsAppRow	*app_row,
							 gboolean	 show_size);

G_END_DECLS
