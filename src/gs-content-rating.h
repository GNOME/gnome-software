/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

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
