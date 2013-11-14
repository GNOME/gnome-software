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

#ifndef __APPSTREAM_MARKUP_H
#define __APPSTREAM_MARKUP_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	APPSTREAM_MARKUP_MODE_START,
	APPSTREAM_MARKUP_MODE_END,
	APPSTREAM_MARKUP_MODE_P_START,
	APPSTREAM_MARKUP_MODE_P_CONTENT,
	APPSTREAM_MARKUP_MODE_P_END,
	APPSTREAM_MARKUP_MODE_UL_START,
	APPSTREAM_MARKUP_MODE_UL_CONTENT,
	APPSTREAM_MARKUP_MODE_UL_END,
	APPSTREAM_MARKUP_MODE_LI_START,
	APPSTREAM_MARKUP_MODE_LI_CONTENT,
	APPSTREAM_MARKUP_MODE_LI_END,
	APPSTREAM_MARKUP_MODE_LAST
} AppstreamMarkupMode;

typedef struct	AppstreamMarkup	AppstreamMarkup;

AppstreamMarkup	*appstream_markup_new		(void);
void		 appstream_markup_free		(AppstreamMarkup	*markup);
void		 appstream_markup_reset		(AppstreamMarkup	*markup);
void		 appstream_markup_set_enabled	(AppstreamMarkup	*markup,
						 gboolean		 enabled);
void		 appstream_markup_set_mode	(AppstreamMarkup	*markup,
						 AppstreamMarkupMode	 mode);
void		 appstream_markup_set_lang	(AppstreamMarkup	*markup,
						 const gchar		*lang);
void		 appstream_markup_add_content	(AppstreamMarkup	*markup,
						 const gchar		*text,
						 gssize			 length);
const gchar	*appstream_markup_get_lang	(AppstreamMarkup	*markup);
const gchar	*appstream_markup_get_text	(AppstreamMarkup	*markup);

G_END_DECLS

#endif /* __APPSTREAM_MARKUP_H */

