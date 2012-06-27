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
};

typedef enum {
	GS_APP_WIDGET_KIND_INSTALL,
	GS_APP_WIDGET_KIND_UPDATE,
	GS_APP_WIDGET_KIND_REMOVE,
	GS_APP_WIDGET_KIND_LAST
} GsAppWidgetKind;

GType		 gs_app_widget_get_type			(void);
GtkWidget	*gs_app_widget_new			(void);
const gchar	*gs_app_widget_get_id			(GsAppWidget	*app_widget);
void		 gs_app_widget_set_id			(GsAppWidget	*app_widget,
							 const gchar	*id);
const gchar	*gs_app_widget_get_name			(GsAppWidget	*app_widget);
void		 gs_app_widget_set_name			(GsAppWidget	*app_widget,
							 const gchar	*name);
const gchar	*gs_app_widget_get_version		(GsAppWidget	*app_widget);
void		 gs_app_widget_set_version		(GsAppWidget	*app_widget,
							 const gchar	*version);
const gchar	*gs_app_widget_get_description		(GsAppWidget	*app_widget);
void		 gs_app_widget_set_description		(GsAppWidget	*app_widget,
							 const gchar	*description);
void		 gs_app_widget_set_pixbuf		(GsAppWidget	*app_widget,
							 GdkPixbuf	*pixbuf);
GsAppWidgetKind	 gs_app_widget_get_kind			(GsAppWidget	*app_widget);
void		 gs_app_widget_set_kind			(GsAppWidget	*app_widget,
							 GsAppWidgetKind kind);

G_END_DECLS

#endif /* GS_APP_WIDGET_H */

