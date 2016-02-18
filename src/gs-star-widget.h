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

G_BEGIN_DECLS

#define GS_TYPE_STAR_WIDGET (gs_star_widget_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsStarWidget, gs_star_widget, GS, STAR_WIDGET, GtkBin)

struct _GsStarWidgetClass
{
	GtkBinClass	 parent_class;

	void			(*rating_changed)		(GsStarWidget	*star);
};

GtkWidget	*gs_star_widget_new			(void);
gint		 gs_star_widget_get_rating		(GsStarWidget	*star);
void		 gs_star_widget_set_rating		(GsStarWidget	*star,
							 gint		 rating);
void		 gs_star_widget_set_icon_size		(GsStarWidget	*star,
							 guint		 pixel_size);
void		 gs_star_widget_set_interactive		(GsStarWidget	*star,
							 gboolean	 interactive);

G_END_DECLS

#endif /* GS_STAR_WIDGET_H */

/* vim: set noexpandtab: */
