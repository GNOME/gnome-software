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

#ifndef GS_STAR_WIDGET_H
#define GS_STAR_WIDGET_H

#include <gtk/gtk.h>

#include "gs-app.h"

#define GS_TYPE_STAR_WIDGET		(gs_star_widget_get_type())
#define GS_STAR_WIDGET(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GS_TYPE_STAR_WIDGET, GsStarWidget))
#define GS_STAR_WIDGET_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), GS_TYPE_STAR_WIDGET, GsStarWidgetClass))
#define GS_IS_STAR_WIDGET(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GS_TYPE_STAR_WIDGET))
#define GS_IS_STAR_WIDGET_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GS_TYPE_STAR_WIDGET))
#define GS_STAR_WIDGET_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GS_TYPE_STAR_WIDGET, GsStarWidgetClass))

G_BEGIN_DECLS

typedef struct _GsStarWidget			GsStarWidget;
typedef struct _GsStarWidgetClass		GsStarWidgetClass;
typedef struct _GsStarWidgetPrivate		GsStarWidgetPrivate;

struct _GsStarWidget
{
	GtkBin		   parent;
	GsStarWidgetPrivate	*priv;
};

struct _GsStarWidgetClass
{
	GtkBinClass	 parent_class;

	void			(*rating_changed)		(GsStarWidget	*star);
};

GType		 gs_star_widget_get_type		(void);
GtkWidget	*gs_star_widget_new			(void);
gint		 gs_star_widget_get_rating		(GsStarWidget	*star);
void		 gs_star_widget_set_rating		(GsStarWidget	*star,
							 GsAppRatingKind rating_kind,
							 gint		 rating);

G_END_DECLS

#endif /* GS_STAR_WIDGET_H */

/* vim: set noexpandtab: */
