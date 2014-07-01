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

#include "config.h"

#include <glib/gi18n.h>

#include "gs-screenshot.h"

static void	gs_screenshot_finalize	(GObject	*object);

struct GsScreenshotPrivate
{
	GPtrArray		*array;
	gboolean		 is_default;
	gchar			*caption;
};

typedef struct {
	gchar			*url;
	guint			 width;
	guint			 height;
} GsScreenshotItem;

G_DEFINE_TYPE_WITH_PRIVATE (GsScreenshot, gs_screenshot, G_TYPE_OBJECT)

/**
 * gs_screenshot_item_free:
 **/
static void
gs_screenshot_item_free (GsScreenshotItem *item)
{
	g_free (item->url);
	g_slice_free (GsScreenshotItem, item);
}

/**
 * gs_screenshot_get_is_default:
 **/
gboolean
gs_screenshot_get_is_default (GsScreenshot *screenshot)
{
	g_return_val_if_fail (GS_IS_SCREENSHOT (screenshot), FALSE);
	return screenshot->priv->is_default;
}

/**
 * gs_screenshot_set_is_default:
 **/
void
gs_screenshot_set_is_default (GsScreenshot *screenshot, gboolean is_default)
{
	g_return_if_fail (GS_IS_SCREENSHOT (screenshot));
	screenshot->priv->is_default = is_default;
}

/**
 * gs_screenshot_get_item_exact:
 **/
static GsScreenshotItem *
gs_screenshot_get_item_exact (GsScreenshot *screenshot, guint width, guint height)
{
	GsScreenshotItem *item;
	guint i;

	g_return_val_if_fail (GS_IS_SCREENSHOT (screenshot), NULL);

	for (i = 0; i < screenshot->priv->array->len; i++) {
		item = g_ptr_array_index (screenshot->priv->array, i);
		if (item->width == width && item->height == height)
			return item;
	}

	return NULL;
}

/**
 * gs_screenshot_add_image:
 **/
void
gs_screenshot_add_image (GsScreenshot *screenshot,
			 const gchar *url,
			 guint width,
			 guint height)
{
	GsScreenshotItem *item;

	g_return_if_fail (GS_IS_SCREENSHOT (screenshot));
	g_return_if_fail (url != NULL);

	/* check if already exists */
	item = gs_screenshot_get_item_exact (screenshot, width, height);
	if (item != NULL) {
		g_debug ("replaced URL %s with %s for %ux%u",
			 item->url, url, width, height);
		g_free (item->url);
		item->url = g_strdup (url);
	} else {
		item = g_slice_new0 (GsScreenshotItem);
		item->url = g_strdup (url);
		item->width = width;
		item->height = height;
		g_ptr_array_add (screenshot->priv->array, item);
	}
}

/**
 * gs_screenshot_get_url:
 *
 * Gets the URL with the closest size to @width and @height.
 *
 **/
const gchar *
gs_screenshot_get_url (GsScreenshot *screenshot,
		       guint width,
		       guint height,
		       GtkRequisition *provided)
{
	GsScreenshotItem *item_tmp;
	GsScreenshotItem *item = NULL;
	guint best_size = G_MAXUINT;
	guint i;
	guint tmp;

	g_return_val_if_fail (GS_IS_SCREENSHOT (screenshot), NULL);
	g_return_val_if_fail (width > 0, NULL);
	g_return_val_if_fail (height > 0, NULL);

	/* find the image with the closest size */
	for (i = 0; i < screenshot->priv->array->len; i++) {
		item_tmp = g_ptr_array_index (screenshot->priv->array, i);
		tmp = ABS ((gint64) (width * height) -
			   (gint64) (item_tmp->width * item_tmp->height));
		if (tmp < best_size) {
			best_size = tmp;
			item = item_tmp;
		}
	}

	if (item == NULL)
		return NULL;

	if (provided != NULL) {
		provided->width = item->width;
		provided->height = item->height;
	}

	return item->url;
}

/**
 * gs_screenshot_get_caption:
 **/
const gchar *
gs_screenshot_get_caption (GsScreenshot *screenshot)
{
	g_return_val_if_fail (GS_IS_SCREENSHOT (screenshot), NULL);
	return screenshot->priv->caption;
}

/**
 * gs_screenshot_set_caption:
 **/
void
gs_screenshot_set_caption (GsScreenshot *screenshot, const gchar *caption)
{
	g_return_if_fail (GS_IS_SCREENSHOT (screenshot));
	g_free (screenshot->priv->caption);
	screenshot->priv->caption = g_strdup (caption);
}

/**
 * gs_screenshot_class_init:
 **/
static void
gs_screenshot_class_init (GsScreenshotClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_screenshot_finalize;
}

/**
 * gs_screenshot_init:
 **/
static void
gs_screenshot_init (GsScreenshot *screenshot)
{
	screenshot->priv = gs_screenshot_get_instance_private (screenshot);
	screenshot->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_screenshot_item_free);
}

/**
 * gs_screenshot_finalize:
 **/
static void
gs_screenshot_finalize (GObject *object)
{
	GsScreenshot *screenshot = GS_SCREENSHOT (object);
	GsScreenshotPrivate *priv = screenshot->priv;

	g_free (priv->caption);
	g_ptr_array_unref (priv->array);

	G_OBJECT_CLASS (gs_screenshot_parent_class)->finalize (object);
}

/**
 * gs_screenshot_new:
 **/
GsScreenshot *
gs_screenshot_new (void)
{
	GsScreenshot *screenshot;
	screenshot = g_object_new (GS_TYPE_SCREENSHOT, NULL);
	return GS_SCREENSHOT (screenshot);
}

/* vim: set noexpandtab: */
