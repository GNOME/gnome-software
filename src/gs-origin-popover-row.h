/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef GS_ORIGIN_POPOVER_ROW_H
#define GS_ORIGIN_POPOVER_ROW_H

#include "gnome-software-private.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_ORIGIN_POPOVER_ROW (gs_origin_popover_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsOriginPopoverRow, gs_origin_popover_row, GS, ORIGIN_POPOVER_ROW, GtkListBoxRow)

struct _GsOriginPopoverRowClass
{
	GtkListBoxRowClass	  parent_class;
};

GtkWidget	*gs_origin_popover_row_new		(GsApp			*app);
GsApp		*gs_origin_popover_row_get_app		(GsOriginPopoverRow	*row);
void		 gs_origin_popover_row_set_selected	(GsOriginPopoverRow	*row,
							 gboolean		 selected);
void		 gs_origin_popover_row_set_size_group	(GsOriginPopoverRow	*row,
							 GtkSizeGroup		*size_group);

G_END_DECLS

#endif /* GS_ORIGIN_POPOVER_ROW_H */

/* vim: set noexpandtab: */
