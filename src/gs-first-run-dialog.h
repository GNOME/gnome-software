/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014-2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_FIRST_RUN_DIALOG (gs_first_run_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsFirstRunDialog, gs_first_run_dialog, GS, FIRST_RUN_DIALOG, GtkDialog)

GtkWidget	*gs_first_run_dialog_new	(void);

G_END_DECLS
