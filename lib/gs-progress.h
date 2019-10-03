/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_PROGRESS (gs_progress_get_type ())

G_DECLARE_FINAL_TYPE (GsProgress, gs_progress, GS, PROGRESS, GObject)

GsProgress	*gs_progress_new			(void);
guint64		 gs_progress_get_size_downloaded	(GsProgress	*self);
void		 gs_progress_set_size_downloaded	(GsProgress	*self,
							 guint64	 size_downloaded);
guint64		 gs_progress_get_size_total		(GsProgress	*self);
void		 gs_progress_set_size_total		(GsProgress	*self,
							 guint64	 size_total);
gchar		*gs_progress_get_message		(GsProgress	*self);
void		 gs_progress_set_message		(GsProgress	*self,
							 const gchar	*message);
guint		 gs_progress_get_percentage		(GsProgress	*self);
void		 gs_progress_set_percentage		(GsProgress	*self,
							 guint		 percentage);

G_END_DECLS
