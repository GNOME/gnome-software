/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_TILE (gs_app_tile_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsAppTile, gs_app_tile, GS, APP_TILE, GtkButton)

struct _GsAppTileClass
{
	GtkButtonClass		parent_class;
	void			 (*refresh)		(GsAppTile	*self);
};

GtkWidget	*gs_app_tile_new	(GsApp *app);
GsApp		*gs_app_tile_get_app	(GsAppTile	*self);
void		 gs_app_tile_set_app	(GsAppTile	*self,
					 GsApp		*app);

G_END_DECLS
