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

#ifndef __GS_COMMON_H
#define __GS_COMMON_H

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-plugin-loader.h"

G_BEGIN_DECLS

void	 gs_start_spinner		(GtkSpinner	*spinner);
void	 gs_stop_spinner		(GtkSpinner	*spinner);
void	 gs_container_remove_all	(GtkContainer	*container);
void	 gs_grab_focus_when_mapped	(GtkWidget	*widget);

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

void	gs_image_set_from_pixbuf_with_scale	(GtkImage		*image,
						 const GdkPixbuf	*pixbuf,
						 gint			 scale);
void	gs_image_set_from_pixbuf		(GtkImage		*image,
						 const GdkPixbuf	*pixbuf);

#if AS_CHECK_VERSION(0,5,12)
const gchar 	*gs_utils_get_content_rating	(void);
#endif

const gchar	*gs_user_agent			(void);
gboolean	 gs_utils_is_current_desktop	(const gchar	*name);
void		 gs_utils_widget_set_custom_css	(GsApp		*app,
						 GtkWidget	*widget,
						 const gchar	*metadata_css);
gboolean	 gs_utils_strv_fnmatch		(gchar		**strv,
						 const gchar	*str);

G_END_DECLS

#endif /* __GS_COMMON_H */

/* vim: set noexpandtab: */
