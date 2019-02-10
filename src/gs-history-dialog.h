/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_HISTORY_DIALOG (gs_history_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsHistoryDialog, gs_history_dialog, GS, HISTORY_DIALOG, GtkDialog)

GtkWidget	*gs_history_dialog_new		(void);
void		 gs_history_dialog_set_app	(GsHistoryDialog	*dialog,
						 GsApp			*app);

G_END_DECLS
