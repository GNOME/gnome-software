/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_PICTURE (gs_picture_get_type ())

G_DECLARE_FINAL_TYPE (GsPicture, gs_picture, GS, PICTURE, GtkDrawingArea)

GtkWidget	*gs_picture_new		(void);

GdkPixbuf	*gs_picture_get_pixbuf	(GsPicture	*picture);
void		 gs_picture_set_pixbuf	(GsPicture	*picture,
					 GdkPixbuf	*pixbuf);

G_END_DECLS
