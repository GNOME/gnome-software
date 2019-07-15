/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>
#include <libsoup/soup.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_SCREENSHOT_IMAGE (gs_screenshot_image_get_type ())

G_DECLARE_FINAL_TYPE (GsScreenshotImage, gs_screenshot_image, GS, SCREENSHOT_IMAGE, GtkBin)

GtkWidget	*gs_screenshot_image_new		(SoupSession		*session);

AsScreenshot	*gs_screenshot_image_get_screenshot	(GsScreenshotImage	*ssimg);
void		 gs_screenshot_image_set_screenshot	(GsScreenshotImage	*ssimg,
							 AsScreenshot		*screenshot);
void		 gs_screenshot_image_set_size		(GsScreenshotImage	*ssimg,
							 guint			 width,
							 guint			 height);
void		 gs_screenshot_image_load_async		(GsScreenshotImage	*ssimg,
							 GCancellable		*cancellable);
gboolean	 gs_screenshot_image_is_showing		(GsScreenshotImage	*ssimg);

G_END_DECLS
