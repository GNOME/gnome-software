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

#include "config.h"

#include "appstream-common.h"
#include "appstream-screenshot.h"

struct AppstreamScreenshot
{
	AppstreamScreenshotKind	 kind;
	GPtrArray		*array;
	gchar			*caption;
	guint			 caption_value;
};

/**
 * appstream_screenshot_new:
 */
AppstreamScreenshot *
appstream_screenshot_new (void)
{
	AppstreamScreenshot *screenshot;
	screenshot = g_slice_new0 (AppstreamScreenshot);
	screenshot->caption_value = G_MAXUINT;
	screenshot->kind = APPSTREAM_SCREENSHOT_KIND_NORMAL;
	screenshot->array = g_ptr_array_new_with_free_func ((GDestroyNotify) appstream_image_free);
	return screenshot;
}

/**
 * appstream_screenshot_free:
 */
void
appstream_screenshot_free (AppstreamScreenshot *screenshot)
{
	g_free (screenshot->caption);
	g_ptr_array_unref (screenshot->array);
	g_slice_free (AppstreamScreenshot, screenshot);
}

/**
 * appstream_screenshot_get_kind:
 */
AppstreamScreenshotKind
appstream_screenshot_get_kind (AppstreamScreenshot *screenshot)
{
	return screenshot->kind;
}

/**
 * appstream_screenshot_get_images:
 */
GPtrArray *
appstream_screenshot_get_images (AppstreamScreenshot *screenshot)
{
	return screenshot->array;
}

/**
 * appstream_screenshot_set_kind:
 */
void
appstream_screenshot_set_kind (AppstreamScreenshot *screenshot,
			       AppstreamScreenshotKind kind)
{
	screenshot->kind = kind;
}

/**
 * appstream_screenshot_add_image:
 */
void
appstream_screenshot_add_image (AppstreamScreenshot *screenshot,
				AppstreamImage *image)
{
	g_ptr_array_add (screenshot->array, image);
}

/**
 * appstream_screenshot_get_caption:
 */
const gchar *
appstream_screenshot_get_caption (AppstreamScreenshot *app)
{
	return app->caption;
}

/**
 * appstream_screenshot_set_caption:
 */
void
appstream_screenshot_set_caption (AppstreamScreenshot *app,
				  const gchar *lang,
				  const gchar *caption,
				  gsize length)
{
	guint new_value;

	new_value = appstream_get_locale_value (lang);
	if (new_value < app->caption_value) {
		g_free (app->caption);
		app->caption = g_strndup (caption, length);
		app->caption_value = new_value;
	}
}

/**
 * appstream_screenshot_kind_from_string:
 */
AppstreamScreenshotKind
appstream_screenshot_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "normal") == 0)
		return APPSTREAM_SCREENSHOT_KIND_NORMAL;
	if (g_strcmp0 (kind, "default") == 0)
		return APPSTREAM_SCREENSHOT_KIND_DEFAULT;
	return APPSTREAM_SCREENSHOT_KIND_UNKNOWN;
}

/* vim: set noexpandtab: */
