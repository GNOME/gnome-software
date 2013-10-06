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

#ifndef __GS_SCREENSHOT_H
#define __GS_SCREENSHOT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_SCREENSHOT		(gs_screenshot_get_type ())
#define GS_SCREENSHOT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_SCREENSHOT, GsScreenshot))
#define GS_SCREENSHOT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_SCREENSHOT, GsScreenshotClass))
#define GS_IS_SCREENSHOT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_SCREENSHOT))
#define GS_IS_SCREENSHOT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_SCREENSHOT))
#define GS_SCREENSHOT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_SCREENSHOT, GsScreenshotClass))

typedef struct GsScreenshotPrivate GsScreenshotPrivate;

typedef struct
{
	 GObject		 parent;
	 GsScreenshotPrivate	*priv;
} GsScreenshot;

typedef struct
{
	GObjectClass		 parent_class;
} GsScreenshotClass;

GType		 gs_screenshot_get_type		(void);

GsScreenshot	*gs_screenshot_new		(void);

#define GS_SCREENSHOT_SIZE_SMALL_WIDTH		112
#define GS_SCREENSHOT_SIZE_SMALL_HEIGHT		63
#define GS_SCREENSHOT_SIZE_LARGE_WIDTH		624
#define GS_SCREENSHOT_SIZE_LARGE_HEIGHT		351

gboolean	 gs_screenshot_get_is_default	(GsScreenshot		*screenshot);
void		 gs_screenshot_set_is_default	(GsScreenshot		*screenshot,
						 gboolean		 is_default);
void		 gs_screenshot_add_image	(GsScreenshot		*screenshot,
						 const gchar		*url,
						 guint			 width,
						 guint			 height);
const gchar	*gs_screenshot_get_url		(GsScreenshot		*screenshot,
						 guint			 width,
						 guint			 height);

G_END_DECLS

#endif /* __GS_SCREENSHOT_H */

/* vim: set noexpandtab: */
