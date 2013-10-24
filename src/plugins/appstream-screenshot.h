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

#ifndef __APPSTREAM_SCREENSHOT_H
#define __APPSTREAM_SCREENSHOT_H

#include <glib.h>

#include "appstream-image.h"

G_BEGIN_DECLS

typedef struct	AppstreamScreenshot	AppstreamScreenshot;

typedef enum {
	APPSTREAM_SCREENSHOT_KIND_NORMAL,
	APPSTREAM_SCREENSHOT_KIND_DEFAULT,
	APPSTREAM_SCREENSHOT_KIND_UNKNOWN,
	APPSTREAM_SCREENSHOT_KIND_LAST
} AppstreamScreenshotKind;

AppstreamScreenshot*	 appstream_screenshot_new	(void);
void			 appstream_screenshot_free	(AppstreamScreenshot	*screenshot);

AppstreamScreenshotKind	 appstream_screenshot_get_kind	(AppstreamScreenshot	*screenshot);
GPtrArray		*appstream_screenshot_get_images (AppstreamScreenshot	*screenshot);

void			 appstream_screenshot_set_kind	(AppstreamScreenshot	*screenshot,
							 AppstreamScreenshotKind kind);
void			 appstream_screenshot_add_image	(AppstreamScreenshot	*screenshot,
							 AppstreamImage		*image);
const gchar		*appstream_screenshot_get_caption (AppstreamScreenshot	*app);
void			 appstream_screenshot_set_caption (AppstreamScreenshot	*app,
							 const gchar    *lang,
							 const gchar	*caption,
							 gsize		 length);

AppstreamScreenshotKind	 appstream_screenshot_kind_from_string (const gchar	*kind);

G_END_DECLS

#endif /* __APPSTREAM_SCREENSHOT_H */
