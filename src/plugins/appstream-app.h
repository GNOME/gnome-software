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

#ifndef __APPSTREAM_APP_H
#define __APPSTREAM_APP_H

#include <glib.h>

#include "appstream-screenshot.h"

G_BEGIN_DECLS

typedef enum {
	APPSTREAM_APP_ICON_KIND_STOCK,
	APPSTREAM_APP_ICON_KIND_CACHED,
	APPSTREAM_APP_ICON_KIND_UNKNOWN,
	APPSTREAM_APP_ICON_KIND_LAST
} AppstreamAppIconKind;

typedef struct	AppstreamApp	AppstreamApp;

void		 appstream_app_free			(AppstreamApp	*app);
AppstreamApp	*appstream_app_new			(void);

const gchar	*appstream_app_get_id			(AppstreamApp	*app);
const gchar	*appstream_app_get_pkgname		(AppstreamApp	*app);
const gchar	*appstream_app_get_name			(AppstreamApp	*app);
const gchar	*appstream_app_get_summary		(AppstreamApp	*app);
const gchar	*appstream_app_get_project_group	(AppstreamApp	*app);
const gchar	*appstream_app_get_url			(AppstreamApp	*app);
const gchar	*appstream_app_get_description		(AppstreamApp	*app);
const gchar	*appstream_app_get_icon			(AppstreamApp	*app);
gboolean	 appstream_app_has_category		(AppstreamApp	*app,
							 const gchar	*category);
AppstreamAppIconKind	appstream_app_get_icon_kind	(AppstreamApp	*app);

void		 appstream_app_set_id			(AppstreamApp	*app,
							 const gchar	*id,
							 gsize		 length);
void		 appstream_app_set_pkgname		(AppstreamApp	*app,
							 const gchar	*pkgname,
							 gsize		 length);
void		 appstream_app_set_name			(AppstreamApp	*app,
							 const gchar    *lang,
							 const gchar	*name,
							 gsize		 length);
void		 appstream_app_set_summary		(AppstreamApp	*app,
							 const gchar    *lang,
							 const gchar	*summary,
							 gsize		 length);
void		 appstream_app_set_url			(AppstreamApp	*app,
							 const gchar	*summary,
							 gsize		 length);
void		 appstream_app_set_project_group	(AppstreamApp	*app,
							 const gchar	*project_group,
							 gsize		 length);
void		 appstream_app_set_description		(AppstreamApp	*app,
							 const gchar    *lang,
							 const gchar	*description,
							 gsize		 length);
void		 appstream_app_set_icon			(AppstreamApp	*app,
							 const gchar	*icon,
							 gsize		 length);
void		 appstream_app_add_category		(AppstreamApp	*app,
							 const gchar	*category,
							 gsize		 length);
void		 appstream_app_add_keyword		(AppstreamApp	*app,
							 const gchar	*keyword,
							 gsize		 length);
void		 appstream_app_set_icon_kind		(AppstreamApp	*app,
							 AppstreamAppIconKind icon_kind);

gpointer	 appstream_app_get_userdata		(AppstreamApp	*app);
void		 appstream_app_set_userdata		(AppstreamApp	*app,
							 gpointer	 userdata,
							 GDestroyNotify	 userdata_destroy_func);
void		 appstream_app_add_screenshot		(AppstreamApp	*app,
							 AppstreamScreenshot *screenshot);
GPtrArray	*appstream_app_get_screenshots		(AppstreamApp	*app);
gboolean	 appstream_app_search_matches		(AppstreamApp	*app,
							 const gchar	*search);

G_END_DECLS

#endif /* __APPSTREAM_APP_H */
