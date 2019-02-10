/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_UPDATE_LIST (gs_update_list_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsUpdateList, gs_update_list, GS, UPDATE_LIST, GtkListBox)

struct _GsUpdateListClass
{
	GtkListBoxClass		 parent_class;
};

GtkWidget	*gs_update_list_new			(void);
void		 gs_update_list_add_app			(GsUpdateList	*update_list,
							 GsApp		*app);

G_END_DECLS
