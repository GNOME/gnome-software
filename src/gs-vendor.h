/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_VENDOR_H
#define __GS_VENDOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_VENDOR (gs_vendor_get_type ())

G_DECLARE_FINAL_TYPE (GsVendor, gs_vendor, GS, VENDOR, GObject)

typedef enum
{
	GS_VENDOR_URL_TYPE_CODEC,
	GS_VENDOR_URL_TYPE_FONT,
	GS_VENDOR_URL_TYPE_MIME,
	GS_VENDOR_URL_TYPE_HARDWARE,
	GS_VENDOR_URL_TYPE_DEFAULT
} GsVendorUrlType;

GsVendor	*gs_vendor_new				(void);
gchar		*gs_vendor_get_not_found_url		(GsVendor		*vendor,
							 GsVendorUrlType	 type);

G_END_DECLS

#endif /* __GS_VENDOR_H */
