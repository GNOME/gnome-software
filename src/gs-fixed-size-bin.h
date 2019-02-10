/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Rafał Lużyński <digitalfreak@lingonborough.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef GS_FIXED_SIZE_BIN_H
#define GS_FIXED_SIZE_BIN_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_FIXED_SIZE_BIN (gs_fixed_size_bin_get_type ())

G_DECLARE_FINAL_TYPE (GsFixedSizeBin, gs_fixed_size_bin, GS, FIXED_SIZE_BIN, GtkBin)

GtkWidget	*gs_fixed_size_bin_new	(void);

G_END_DECLS

#endif /* GS_FIXED_SIZE_BIN_H */

/* vim: set noexpandtab: */
