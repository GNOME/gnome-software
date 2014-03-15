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

#ifndef GS_SOURCES_DIALOG_H
#define GS_SOURCES_DIALOG_H

#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-plugin-loader.h"

#define GS_TYPE_SOURCES_DIALOG			(gs_sources_dialog_get_type())
#define GS_SOURCES_DIALOG(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_SOURCES_DIALOG, GsSourcesDialog))
#define GS_SOURCES_DIALOG_CLASS(cls)		(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_SOURCES_DIALOG, GsSourcesDialogClass))
#define GS_IS_SOURCES_DIALOG(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_SOURCES_DIALOG))
#define GS_IS_SOURCES_DIALOG_CLASS(cls)		(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_SOURCES_DIALOG))
#define GS_SOURCES_DIALOG_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_SOURCES_DIALOG, GsSourcesDialogClass))

G_BEGIN_DECLS

typedef struct _GsSourcesDialog		GsSourcesDialog;
typedef struct _GsSourcesDialogClass	GsSourcesDialogClass;
typedef struct _GsSourcesDialogPrivate	GsSourcesDialogPrivate;

struct _GsSourcesDialog
{
	GtkDialog	 parent;
};

struct _GsSourcesDialogClass
{
	GtkDialogClass	 parent_class;
};

GType		 gs_sources_dialog_get_type	(void);
GtkWidget	*gs_sources_dialog_new		(GtkWindow	*parent,
						 GsPluginLoader	*plugin_loader);

#endif /* GS_SOURCES_DIALOG_H */

/* vim: set noexpandtab: */
