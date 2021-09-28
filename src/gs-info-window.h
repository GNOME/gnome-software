/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Purism SPC
 *
 * Author: Adrien Plazas <adrien.plazas@puri.sm>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <handy.h>

G_BEGIN_DECLS

#define GS_TYPE_INFO_WINDOW (gs_info_window_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsInfoWindow, gs_info_window, GS, INFO_WINDOW, HdyWindow)

struct _GsInfoWindowClass
{
	HdyWindowClass	 parent_class;
};

GsInfoWindow	*gs_info_window_new	(void);

G_END_DECLS
