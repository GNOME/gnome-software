/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_CONTENT_RATING_H
#define __GS_CONTENT_RATING_H

G_BEGIN_DECLS

#include <appstream-glib.h>
#include <glib-object.h>

typedef enum {
	GS_CONTENT_RATING_SYSTEM_UNKNOWN,
	GS_CONTENT_RATING_SYSTEM_INCAA,
	GS_CONTENT_RATING_SYSTEM_ACB,
	GS_CONTENT_RATING_SYSTEM_DJCTQ,
	GS_CONTENT_RATING_SYSTEM_GSRR,
	GS_CONTENT_RATING_SYSTEM_PEGI,
	GS_CONTENT_RATING_SYSTEM_KAVI,
	GS_CONTENT_RATING_SYSTEM_USK,
	GS_CONTENT_RATING_SYSTEM_ESRA,
	GS_CONTENT_RATING_SYSTEM_CERO,
	GS_CONTENT_RATING_SYSTEM_OFLCNZ,
	GS_CONTENT_RATING_SYSTEM_RUSSIA,
	GS_CONTENT_RATING_SYSTEM_MDA,
	GS_CONTENT_RATING_SYSTEM_GRAC,
	GS_CONTENT_RATING_SYSTEM_ESRB,
	GS_CONTENT_RATING_SYSTEM_IARC,
	/*< private >*/
	GS_CONTENT_RATING_SYSTEM_LAST
} GsContentRatingSystem;

const gchar *gs_utils_content_rating_age_to_str (GsContentRatingSystem system,
						 guint age);
GsContentRatingSystem gs_utils_content_rating_system_from_locale (const gchar *locale);
const gchar *gs_content_rating_key_value_to_str (const gchar *id,
						 AsContentRatingValue value);
const gchar *gs_content_rating_system_to_str (GsContentRatingSystem system);

G_END_DECLS

#endif /* __GS_CONTENT_RATING_H */

/* vim: set noexpandtab: */
