/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright © 2020 Endless Mobile, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_METERED_DATA_DIALOG (gs_metered_data_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsMeteredDataDialog, gs_metered_data_dialog, GS, METERED_DATA_DIALOG, GtkDialog)

GtkWidget	*gs_metered_data_dialog_new	(GtkWindow	*parent);

G_END_DECLS
