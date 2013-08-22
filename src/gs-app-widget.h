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

#ifndef GS_APP_WIDGET_H
#define GS_APP_WIDGET_H

#include <gtk/gtk.h>

#include "gs-app.h"

#define GS_TYPE_APP_WIDGET		(gs_app_widget_get_type())
#define GS_APP_WIDGET(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_APP_WIDGET, GsAppWidget))
#define GS_APP_WIDGET_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_APP_WIDGET, GsAppWidgetClass))
#define GS_IS_APP_WIDGET(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_APP_WIDGET))
#define GS_IS_APP_WIDGET_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_APP_WIDGET))
#define GS_APP_WIDGET_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_APP_WIDGET, GsAppWidgetClass))

G_BEGIN_DECLS

typedef struct _GsAppWidget			GsAppWidget;
typedef struct _GsAppWidgetClass		GsAppWidgetClass;
typedef struct _GsAppWidgetPrivate		GsAppWidgetPrivate;

struct _GsAppWidget
{
	GtkBox			 parent;

	/*< private >*/
	GsAppWidgetPrivate	*priv;
};

struct _GsAppWidgetClass
{
	GtkBoxClass		 parent_class;
	void			(*button_clicked)	(GsAppWidget	*app_widget);
	void			(*read_more_clicked)   	(GsAppWidget	*app_widget);
};

typedef enum {
	GS_APP_WIDGET_KIND_INSTALL,
	GS_APP_WIDGET_KIND_UPDATE,
	GS_APP_WIDGET_KIND_REMOVE,
	GS_APP_WIDGET_KIND_BUSY,
	GS_APP_WIDGET_KIND_BLANK,
	GS_APP_WIDGET_KIND_LAST
} GsAppWidgetKind;

GType		 gs_app_widget_get_type			(void);
GtkWidget	*gs_app_widget_new			(void);
GsApp		*gs_app_widget_get_app			(GsAppWidget	*app_widget);
void		 gs_app_widget_set_app			(GsAppWidget	*app_widget,
							 GsApp		*app);
const gchar	*gs_app_widget_get_status		(GsAppWidget	*app_widget);
void		 gs_app_widget_set_status		(GsAppWidget	*app_widget,
							 const gchar	*status);
GsAppWidgetKind	 gs_app_widget_get_kind			(GsAppWidget	*app_widget);
void		 gs_app_widget_set_kind			(GsAppWidget	*app_widget,
							 GsAppWidgetKind kind);
void             gs_app_widget_set_size_groups          (GsAppWidget    *app_widget,
                                                         GtkSizeGroup   *image,
                                                         GtkSizeGroup   *name);

G_END_DECLS

#endif /* GS_APP_WIDGET_H */

