/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_OFFLINE_UPDATES_H
#define __GS_OFFLINE_UPDATES_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

void     gs_offline_updates_trigger		(GCallback  child_exited);
void     gs_offline_updates_cancel		(void);
void     gs_offline_updates_clear_status	(void);
gboolean gs_offline_updates_results_available	(void);
gboolean gs_offline_updates_get_time_completed	(guint64   *time_completed);
gboolean gs_offline_updates_get_status		(gboolean  *success,
						 guint     *num_packages,
						 gchar    **error_code,
						 gchar    **error_details);
void     gs_offline_updates_show_error		(void);


G_END_DECLS

#endif /* __GS_OFFLINE_UPDATES_H */

/* vim: set noexpandtab: */
