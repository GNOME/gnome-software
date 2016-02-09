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

#ifndef __GS_UTILS_H
#define __GS_UTILS_H

#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-plugin-loader.h"

G_BEGIN_DECLS

void	 gs_start_spinner		(GtkSpinner	*spinner);
void	 gs_stop_spinner		(GtkSpinner	*spinner);
void	 gs_container_remove_all	(GtkContainer	*container);
void	 gs_grab_focus_when_mapped	(GtkWidget	*widget);

guint	 gs_utils_get_file_age		(GFile		*file);

void	 gs_app_notify_installed	(GsApp		*app);
void	 gs_app_notify_failed_modal	(GsApp		*app,
					 GtkWindow	*parent_window,
					 GsPluginLoaderAction action,
					 const GError	*error);
GtkResponseType
	gs_app_notify_unavailable	(GsApp		*app,
					 GtkWindow	*parent);
void	 gs_app_show_url		(GsApp		*app,
					 AsUrlKind	 kind);

guint	 gs_string_replace		(GString	*string,
					 const gchar	*search,
					 const gchar	*replace);
gboolean gs_mkdir_parent		(const gchar	*path,
					 GError		**error);

void	gs_image_set_from_pixbuf_with_scale	(GtkImage		*image,
						 const GdkPixbuf	*pixbuf,
						 gint			 scale);
void	gs_image_set_from_pixbuf		(GtkImage		*image,
						 const GdkPixbuf	*pixbuf);
const gchar	 *gs_user_agent			(void);

gchar		*gs_utils_get_cachedir		(const gchar	*kind,
						 GError		**error);
gchar		*gs_utils_get_user_hash		(GError		**error);

G_END_DECLS

#endif /* __GS_UTILS_H */

/* vim: set noexpandtab: */
