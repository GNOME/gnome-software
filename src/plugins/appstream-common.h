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

#ifndef __APPSTREAM_COMMON_H
#define __APPSTREAM_COMMON_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	APPSTREAM_TAG_UNKNOWN,
	APPSTREAM_TAG_APPLICATIONS,
	APPSTREAM_TAG_APPLICATION,
	APPSTREAM_TAG_ID,
	APPSTREAM_TAG_PKGNAME,
	APPSTREAM_TAG_NAME,
	APPSTREAM_TAG_SUMMARY,
	APPSTREAM_TAG_DESCRIPTION,
	APPSTREAM_TAG_URL,
	APPSTREAM_TAG_ICON,
	APPSTREAM_TAG_APPCATEGORIES,
	APPSTREAM_TAG_APPCATEGORY,
	APPSTREAM_TAG_KEYWORDS,
	APPSTREAM_TAG_KEYWORD,
	APPSTREAM_TAG_MIMETYPES,
	APPSTREAM_TAG_MIMETYPE,
	APPSTREAM_TAG_PROJECT_GROUP,
	APPSTREAM_TAG_LICENCE,
	APPSTREAM_TAG_SCREENSHOT,
	APPSTREAM_TAG_SCREENSHOTS,
	APPSTREAM_TAG_UPDATECONTACT,
	APPSTREAM_TAG_IMAGE,
	APPSTREAM_TAG_COMPULSORY_FOR_DESKTOP,
	APPSTREAM_TAG_PRIORITY,
	APPSTREAM_TAG_CAPTION,
	APPSTREAM_TAG_LAST
} AppstreamTag;

AppstreamTag	 appstream_tag_from_string	(const gchar	*element_name);
const gchar	*appstream_tag_to_string	(AppstreamTag	 tag);
guint		 appstream_get_locale_value	(const gchar	*lang);
gchar		*appstream_xml_unmunge		(const gchar	*text,
						 gssize		 text_length);
gchar		*appstream_xml_unmunge_safe	(const gchar	*text,
						 gssize		 text_length);

G_END_DECLS

#endif /* __APPSTREAM_COMMON_H */
