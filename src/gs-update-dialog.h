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

#ifndef GS_UPDATE_DIALOG_H
#define GS_UPDATE_DIALOG_H

#include <gtk/gtk.h>

#include "gs-app.h"

#define GS_TYPE_UPDATE_DIALOG		(gs_update_dialog_get_type())
#define GS_UPDATE_DIALOG(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_UPDATE_DIALOG, GsUpdateDialog))
#define GS_UPDATE_DIALOG_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_UPDATE_DIALOG, GsUpdateDialogClass))
#define GS_IS_UPDATE_DIALOG(obj)	(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_UPDATE_DIALOG))
#define GS_IS_UPDATE_DIALOG_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_UPDATE_DIALOG))
#define GS_UPDATE_DIALOG_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_UPDATE_DIALOG, GsUpdateDialogClass))

G_BEGIN_DECLS

typedef struct _GsUpdateDialog		GsUpdateDialog;
typedef struct _GsUpdateDialogClass	GsUpdateDialogClass;
typedef struct _GsUpdateDialogPrivate	GsUpdateDialogPrivate;

struct _GsUpdateDialog
{
	GtkDialog	 parent;
};

struct _GsUpdateDialogClass
{
	GtkDialogClass	 parent_class;
};

GType		 gs_update_dialog_get_type			(void);
GtkWidget	*gs_update_dialog_new				(void);
void		 gs_update_dialog_show_update_details		(GsUpdateDialog		*dialog,
								 GsApp			*app);

#endif /* GS_UPDATE_DIALOG_H */

/* vim: set noexpandtab: */
