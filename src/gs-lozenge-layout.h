/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2022 Red Hat (www.redhat.com)
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-layout-manager.h"

G_BEGIN_DECLS

#define GS_TYPE_LOZENGE_LAYOUT (gs_lozenge_layout_get_type ())
G_DECLARE_FINAL_TYPE (GsLozengeLayout, gs_lozenge_layout, GS, LOZENGE_LAYOUT, GsLayoutManager)

GsLozengeLayout *
		gs_lozenge_layout_new		(void);
gboolean	gs_lozenge_layout_get_circular	(GsLozengeLayout *self);
void		gs_lozenge_layout_set_circular	(GsLozengeLayout *self,
						 gboolean value);

G_END_DECLS
