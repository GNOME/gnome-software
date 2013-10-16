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

#define _GNU_SOURCE
#include <string.h>

#include "appstream-common.h"

/**
 * appstream_tag_from_string:
 */
AppstreamTag
appstream_tag_from_string (const gchar *element_name)
{
	if (g_strcmp0 (element_name, "applications") == 0)
		return APPSTREAM_TAG_APPLICATIONS;
	if (g_strcmp0 (element_name, "application") == 0)
		return APPSTREAM_TAG_APPLICATION;
	if (g_strcmp0 (element_name, "id") == 0)
		return APPSTREAM_TAG_ID;
	if (g_strcmp0 (element_name, "pkgname") == 0)
		return APPSTREAM_TAG_PKGNAME;
	if (g_strcmp0 (element_name, "name") == 0)
		return APPSTREAM_TAG_NAME;
	if (g_strcmp0 (element_name, "summary") == 0)
		return APPSTREAM_TAG_SUMMARY;
	if (g_strcmp0 (element_name, "project_group") == 0)
		return APPSTREAM_TAG_PROJECT_GROUP;
	if (g_strcmp0 (element_name, "url") == 0)
		return APPSTREAM_TAG_URL;
	if (g_strcmp0 (element_name, "description") == 0)
		return APPSTREAM_TAG_DESCRIPTION;
	if (g_strcmp0 (element_name, "icon") == 0)
		return APPSTREAM_TAG_ICON;
	if (g_strcmp0 (element_name, "appcategories") == 0)
		return APPSTREAM_TAG_APPCATEGORIES;
	if (g_strcmp0 (element_name, "appcategory") == 0)
		return APPSTREAM_TAG_APPCATEGORY;
	if (g_strcmp0 (element_name, "keywords") == 0)
		return APPSTREAM_TAG_KEYWORDS;
	if (g_strcmp0 (element_name, "keyword") == 0)
		return APPSTREAM_TAG_KEYWORD;
	if (g_strcmp0 (element_name, "licence") == 0)
		return APPSTREAM_TAG_LICENCE;
	if (g_strcmp0 (element_name, "screenshots") == 0)
		return APPSTREAM_TAG_SCREENSHOTS;
	if (g_strcmp0 (element_name, "screenshot") == 0)
		return APPSTREAM_TAG_SCREENSHOT;
	if (g_strcmp0 (element_name, "updatecontact") == 0)
		return APPSTREAM_TAG_UPDATECONTACT;
	if (g_strcmp0 (element_name, "image") == 0)
		return APPSTREAM_TAG_IMAGE;
	if (g_strcmp0 (element_name, "compulsory_for_desktop") == 0)
		return APPSTREAM_TAG_COMPULSORY_FOR_DESKTOP;
	if (g_strcmp0 (element_name, "priority") == 0)
		return APPSTREAM_TAG_PRIORITY;
	return APPSTREAM_TAG_UNKNOWN;
}

/**
 * appstream_tag_to_string:
 */
const gchar *
appstream_tag_to_string (AppstreamTag tag)
{
	if (tag == APPSTREAM_TAG_APPLICATIONS)
		return "applications";
	if (tag == APPSTREAM_TAG_APPLICATION)
		return "application";
	if (tag == APPSTREAM_TAG_ID)
		return "id";
	if (tag == APPSTREAM_TAG_PKGNAME)
		return "pkgname";
	if (tag == APPSTREAM_TAG_NAME)
		return "name";
	if (tag == APPSTREAM_TAG_SUMMARY)
		return "summary";
	if (tag == APPSTREAM_TAG_PROJECT_GROUP)
		return "project_group";
	if (tag == APPSTREAM_TAG_URL)
		return "url";
	if (tag == APPSTREAM_TAG_DESCRIPTION)
		return "description";
	if (tag == APPSTREAM_TAG_ICON)
		return "icon";
	if (tag == APPSTREAM_TAG_APPCATEGORIES)
		return "appcategories";
	if (tag == APPSTREAM_TAG_APPCATEGORY)
		return "appcategory";
	if (tag == APPSTREAM_TAG_KEYWORDS)
		return "keywords";
	if (tag == APPSTREAM_TAG_KEYWORD)
		return "keyword";
	if (tag == APPSTREAM_TAG_LICENCE)
		return "licence";
	if (tag == APPSTREAM_TAG_SCREENSHOTS)
		return "screenshots";
	if (tag == APPSTREAM_TAG_SCREENSHOT)
		return "screenshot";
	if (tag == APPSTREAM_TAG_UPDATECONTACT)
		return "updatecontact";
	if (tag == APPSTREAM_TAG_IMAGE)
		return "image";
	if (tag == APPSTREAM_TAG_COMPULSORY_FOR_DESKTOP)
		return "compulsory_for_desktop";
	if (tag == APPSTREAM_TAG_PRIORITY)
		return "priority";
	return NULL;
}

/**
 * appstream_get_locale_value:
 *
 * Returns a metric on how good a match the locale is, with 0 being an
 * exact match and higher numbers meaning further away from perfect.
 */
guint
appstream_get_locale_value (const gchar *lang)
{
	const gchar * const *locales;
	guint i;

	/* shortcut as C will always match */
	if (lang == NULL || strcmp (lang, "C") == 0)
		return G_MAXUINT - 1;

	locales = g_get_language_names ();
	for (i = 0; locales[i] != NULL; i++) {
		if (g_ascii_strcasecmp (locales[i], lang) == 0)
			return i;
	}

	return G_MAXUINT;
}
