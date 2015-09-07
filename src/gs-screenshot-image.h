/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#ifndef GS_SCREENSHOT_IMAGE_H
#define GS_SCREENSHOT_IMAGE_H

#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include <appstream-glib.h>

G_BEGIN_DECLS

#define GS_TYPE_SCREENSHOT_IMAGE (gs_screenshot_image_get_type ())

G_DECLARE_FINAL_TYPE (GsScreenshotImage, gs_screenshot_image, GS, SCREENSHOT_IMAGE, GtkBin)

GtkWidget	*gs_screenshot_image_new		(SoupSession		*session);

AsScreenshot	*gs_screenshot_image_get_screenshot	(GsScreenshotImage	*ssimg);
void		 gs_screenshot_image_set_screenshot	(GsScreenshotImage	*ssimg,
							 AsScreenshot		*screenshot);
void		 gs_screenshot_image_set_cachedir	(GsScreenshotImage	*ssimg,
							 const gchar		*cachedir);
void		 gs_screenshot_image_set_size		(GsScreenshotImage	*ssimg,
							 guint			 width,
							 guint			 height);
void		 gs_screenshot_image_set_use_desktop_background
							(GsScreenshotImage	*ssimg,
							 gboolean		 use_desktop_background);
void		 gs_screenshot_image_load_async		(GsScreenshotImage	*ssimg,
							 GCancellable		*cancellable);

G_END_DECLS

#endif /* GS_SCREENSHOT_IMAGE_H */

/* vim: set noexpandtab: */
