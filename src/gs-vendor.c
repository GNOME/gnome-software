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

#include "config.h"

#include <glib/gi18n.h>

#include "gs-vendor.h"

struct _GsVendor
{
	GObject				  parent_instance;

	GKeyFile			 *file;
};

G_DEFINE_TYPE (GsVendor, gs_vendor, G_TYPE_OBJECT)

/**
 * gs_vendor_type_to_string:
 **/
static const gchar *
gs_vendor_type_to_string (GsVendorUrlType type)
{
	if (type == GS_VENDOR_URL_TYPE_CODEC)
		return "CodecUrl";
	if (type == GS_VENDOR_URL_TYPE_FONT)
		return "FontUrl";
	if (type == GS_VENDOR_URL_TYPE_MIME)
		return "MimeUrl";
	if (type == GS_VENDOR_URL_TYPE_HARDWARE)
		return "HardwareUrl";
	return "DefaultUrl";
}

/**
 * gs_vendor_get_not_found_url:
 **/
gchar *
gs_vendor_get_not_found_url (GsVendor *vendor, GsVendorUrlType type)
{
	const gchar *key;
	gchar *url = NULL;

	/* get data */
	key = gs_vendor_type_to_string (type);
	url = g_key_file_get_string (vendor->file, "PackagesNotFound", key, NULL);

	/* none is a special value */
	if (g_strcmp0 (url, "none") == 0) {
		g_free (url);
		url = NULL;
	}

	/* got a valid URL */
	if (url != NULL)
		goto out;

	/* default has no fallback */
	if (type == GS_VENDOR_URL_TYPE_DEFAULT)
		goto out;

	/* get fallback data */
	g_debug ("using fallback");
	key = gs_vendor_type_to_string (GS_VENDOR_URL_TYPE_DEFAULT);
	url = g_key_file_get_string (vendor->file, "PackagesNotFound", key, NULL);

	/* none is a special value */
	if (g_strcmp0 (url, "none") == 0) {
		g_free (url);
		url = NULL;
	}
out:
	g_debug ("url=%s", url);
	return url;
}

/**
 * gs_vendor_init:
 * @vendor: This class instance
 **/
static void
gs_vendor_init (GsVendor *vendor)
{
	gboolean ret;

	vendor->file = g_key_file_new ();
	ret = g_key_file_load_from_file (vendor->file, "/etc/PackageKit/Vendor.conf", G_KEY_FILE_NONE, NULL);
	if (!ret)
		g_warning ("file not found");
}

/**
 * gs_vendor_finalize:
 * @object: The object to finalize
 **/
static void
gs_vendor_finalize (GObject *object)
{
	GsVendor *vendor = GS_VENDOR (object);

	g_key_file_free (vendor->file);

	G_OBJECT_CLASS (gs_vendor_parent_class)->finalize (object);
}

/**
 * gs_vendor_class_init:
 * @klass: The GsVendorClass
 **/
static void
gs_vendor_class_init (GsVendorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_vendor_finalize;
}

/**
 * gs_vendor_new:
 *
 * Return value: a new GsVendor object.
 **/
GsVendor *
gs_vendor_new (void)
{
	GsVendor *vendor;
	vendor = g_object_new (GS_TYPE_VENDOR, NULL);
	return GS_VENDOR (vendor);
}

