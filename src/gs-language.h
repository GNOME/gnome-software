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

#define GS_TYPE_LANGUAGE		(gs_language_get_type ())
#define GS_LANGUAGE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_LANGUAGE, GsLanguage))
#define GS_LANGUAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_LANGUAGE, GsLanguageClass))
#define GS_IS_LANGUAGE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_LANGUAGE))
#define GS_IS_LANGUAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_LANGUAGE))
#define GS_LANGUAGE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_LANGUAGE, GsLanguageClass))
#define GS_LANGUAGE_ERROR		(gs_language_error_quark ())
#define GS_LANGUAGE_TYPE_ERROR		(gs_language_error_get_type ())

typedef struct GsLanguagePrivate GsLanguagePrivate;

typedef struct
{
	 GObject		 parent;
	 GsLanguagePrivate	*priv;
} GsLanguage;

typedef struct
{
	GObjectClass	parent_class;
} GsLanguageClass;

GType		 gs_language_get_type			(void);
GsLanguage	*gs_language_new			(void);
gboolean	 gs_language_populate			(GsLanguage	 *language,
							 GError		**error);
gchar		*gs_language_iso639_to_language		(GsLanguage	 *language,
							 const gchar	 *iso639);

G_END_DECLS

#endif /* __GS_LANGUAGE_H */
