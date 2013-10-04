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

#include "appstream-image.h"

struct AppstreamImage
{
	gchar			*url;
	guint			 width;
	guint			 height;
	AppstreamImageKind	 kind;
};

/**
 * appstream_image_new:
 */
AppstreamImage *
appstream_image_new (void)
{
	return g_slice_new0 (AppstreamImage);
}

/**
 * appstream_image_free:
 */
void
appstream_image_free (AppstreamImage *image)
{
	g_free (image->url);
	g_slice_free (AppstreamImage, image);
}

/**
 * appstream_image_set_url:
 */
void
appstream_image_set_url (AppstreamImage *image, const gchar *url, gsize url_len)
{
	g_free (image->url);
	image->url = g_strndup (url, url_len);
}

/**
 * appstream_image_set_width:
 */
void
appstream_image_set_width (AppstreamImage *image, guint width)
{
	image->width = width;
}

/**
 * appstream_image_set_height:
 */
void
appstream_image_set_height (AppstreamImage *image, guint height)
{
	image->height = height;
}

/**
 * appstream_image_set_kind:
 */
void
appstream_image_set_kind (AppstreamImage *image, AppstreamImageKind kind)
{
	image->kind = kind;
}

/**
 * appstream_image_get_url:
 */
const gchar *
appstream_image_get_url (AppstreamImage *image)
{
	return image->url;
}

/**
 * appstream_image_get_width:
 */
guint
appstream_image_get_width (AppstreamImage *image)
{
	return image->width;
}

/**
 * appstream_image_get_height:
 */
guint
appstream_image_get_height (AppstreamImage *image)
{
	return image->height;
}

/**
 * appstream_image_get_kind:
 */
AppstreamImageKind
appstream_image_get_kind (AppstreamImage *image)
{
	return image->kind;
}

/**
 * appstream_image_kind_from_string:
 */
AppstreamImageKind
appstream_image_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "source") == 0)
		return APPSTREAM_IMAGE_KIND_SOURCE;
	if (g_strcmp0 (kind, "thumbnail") == 0)
		return APPSTREAM_IMAGE_KIND_THUMBNAIL;
	return APPSTREAM_IMAGE_KIND_UNKNOWN;
}

/* vim: set noexpandtab: */
