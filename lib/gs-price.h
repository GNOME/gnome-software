 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

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
