 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
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

#ifndef __GS_PRICE_H
#define __GS_PRICE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_PRICE (gs_price_get_type ())

G_DECLARE_FINAL_TYPE (GsPrice, gs_price, GS, PRICE, GObject)

GsPrice		*gs_price_new				(gdouble	 amount,
							 const gchar	*currency);

gdouble		 gs_price_get_amount			(GsPrice	*price);
void		 gs_price_set_amount			(GsPrice	*price,
							 gdouble	 amount);

const gchar	*gs_price_get_currency			(GsPrice	*price);
void		 gs_price_set_currency			(GsPrice	*price,
							 const gchar	*currency);

gchar		*gs_price_to_string			(GsPrice	*price);

G_END_DECLS

#endif /* __GS_PRICE_H */

/* vim: set noexpandtab: */
