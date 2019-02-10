/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Rafał Lużyński <digitalfreak@lingonborough.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_HIDING_BOX (gs_hiding_box_get_type ())

G_DECLARE_FINAL_TYPE (GsHidingBox, gs_hiding_box, GS, HIDING_BOX, GtkContainer)

GtkWidget	*gs_hiding_box_new		(void);
void		 gs_hiding_box_set_spacing	(GsHidingBox	*box,
						 gint		 spacing);
gint		 gs_hiding_box_get_spacing	(GsHidingBox	*box);

G_END_DECLS
