/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation, Inc
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gs-category.h"
#include "gs-category-manager.h"

G_BEGIN_DECLS

#define GS_TYPE_SIDEBAR (gs_sidebar_get_type ())
G_DECLARE_FINAL_TYPE (GsSidebar, gs_sidebar, GS, SIDEBAR, GtkBox)

GtkWidget		*gs_sidebar_new				(void);

GtkStack		*gs_sidebar_get_stack			(GsSidebar		*self);
void			 gs_sidebar_set_stack			(GsSidebar		*self,
								 GtkStack		*stack);

GsCategoryManager	*gs_sidebar_get_category_manager	(GsSidebar		*self);
void			 gs_sidebar_set_category_manager	(GsSidebar		*self,
								 GsCategoryManager	*manager);

G_END_DECLS
