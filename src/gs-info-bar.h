/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Rafał Lużyński <digitalfreak@lingonborough.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef GS_INFO_BAR_H
#define GS_INFO_BAR_H

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

#endif /* GS_INFO_BAR_H */

/* vim: set noexpandtab: */
