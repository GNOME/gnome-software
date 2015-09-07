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

#ifndef __GS_LANGUAGE_H
#define __GS_LANGUAGE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_LANGUAGE (gs_language_get_type ())

G_DECLARE_FINAL_TYPE (GsLanguage, gs_language, GS, LANGUAGE, GObject)

GsLanguage	*gs_language_new			(void);
gboolean	 gs_language_populate			(GsLanguage	 *language,
							 GError		**error);
gchar		*gs_language_iso639_to_language		(GsLanguage	 *language,
							 const gchar	 *iso639);

G_END_DECLS

#endif /* __GS_LANGUAGE_H */
