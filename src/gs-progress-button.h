/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_PROGRESS_BUTTON (gs_progress_button_get_type ())

G_DECLARE_FINAL_TYPE (GsProgressButton, gs_progress_button, GS, PROGRESS_BUTTON, GtkButton)

GtkWidget	*gs_progress_button_new			(void);
void		 gs_progress_button_set_progress	(GsProgressButton	*button,
							 guint			 percentage);
void		 gs_progress_button_set_show_progress	(GsProgressButton	*button,
							 gboolean		 show_progress);

G_END_DECLS
