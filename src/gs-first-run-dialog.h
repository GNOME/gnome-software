/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Kalev Lember <kalevlember@gmail.com>
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

#ifndef GS_FIRST_RUN_DIALOG_H
#define GS_FIRST_RUN_DIALOG_H

#include <gtk/gtk.h>

#define GS_TYPE_FIRST_RUN_DIALOG		(gs_first_run_dialog_get_type())
#define GS_FIRST_RUN_DIALOG(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_FIRST_RUN_DIALOG, GsFirstRunDialog))
#define GS_FIRST_RUN_DIALOG_CLASS(cls)		(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_FIRST_RUN_DIALOG, GsFirstRunDialogClass))
#define GS_IS_FIRST_RUN_DIALOG(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_FIRST_RUN_DIALOG))
#define GS_IS_FIRST_RUN_DIALOG_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_FIRST_RUN_DIALOG))
#define GS_FIRST_RUN_DIALOG_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_FIRST_RUN_DIALOG, GsFirstRunDialogClass))

G_BEGIN_DECLS

typedef struct _GsFirstRunDialog		GsFirstRunDialog;
typedef struct _GsFirstRunDialogClass		GsFirstRunDialogClass;
typedef struct _GsFirstRunDialogPrivate		GsFirstRunDialogPrivate;

struct _GsFirstRunDialog
{
	GtkDialog	 parent;
};

struct _GsFirstRunDialogClass
{
	GtkDialogClass	 parent_class;
};

GType		 gs_first_run_dialog_get_type	(void);
GtkWidget	*gs_first_run_dialog_new		(void);

#endif /* GS_FIRST_RUN_DIALOG_H */

/* vim: set noexpandtab: */
