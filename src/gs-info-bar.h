/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Rafał Lużyński <digitalfreak@lingonborough.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_INFO_BAR (gs_info_bar_get_type ())

G_DECLARE_FINAL_TYPE (GsInfoBar, gs_info_bar, GS, INFO_BAR, GtkInfoBar)

GtkWidget	*gs_info_bar_new		(void);
const gchar	*gs_info_bar_get_title		(GsInfoBar	*bar);
void		 gs_info_bar_set_title		(GsInfoBar	*bar,
						 const gchar	*text);
const gchar	*gs_info_bar_get_body		(GsInfoBar	*bar);
void		 gs_info_bar_set_body		(GsInfoBar	*bar,
						 const gchar	*text);
const gchar	*gs_info_bar_get_warning	(GsInfoBar	*bar);
void		 gs_info_bar_set_warning	(GsInfoBar	*bar,
						 const gchar	*text);
G_END_DECLS
