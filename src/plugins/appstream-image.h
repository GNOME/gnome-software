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

#ifndef __APPSTREAM_IMAGE_H
#define __APPSTREAM_IMAGE_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct	AppstreamImage	AppstreamImage;

typedef enum {
	APPSTREAM_IMAGE_KIND_UNKNOWN,
	APPSTREAM_IMAGE_KIND_SOURCE,
	APPSTREAM_IMAGE_KIND_THUMBNAIL,
	APPSTREAM_IMAGE_KIND_LAST
} AppstreamImageKind;

AppstreamImage	*appstream_image_new			(void);
void		 appstream_image_free			(AppstreamImage	*image);

void		 appstream_image_set_url		(AppstreamImage	*image,
							 const gchar	*url,
							 gsize		 url_len);
void		 appstream_image_set_width		(AppstreamImage	*image,
							 guint		 width);
void		 appstream_image_set_height		(AppstreamImage	*image,
							 guint		 height);
void		 appstream_image_set_kind		(AppstreamImage	*image,
							 AppstreamImageKind kind);

const gchar	*appstream_image_get_url		(AppstreamImage	*image);
guint		 appstream_image_get_width		(AppstreamImage	*image);
guint		 appstream_image_get_height		(AppstreamImage	*image);
AppstreamImageKind appstream_image_get_kind		(AppstreamImage	*image);
AppstreamImageKind appstream_image_kind_from_string	(const gchar	*kind);

G_END_DECLS

#endif /* __APPSTREAM_IMAGE_H */
