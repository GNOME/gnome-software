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

#ifndef __GS_APPSTREAM_ITEM_H
#define __GS_APPSTREAM_ITEM_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	GS_APPSTREAM_ITEM_ICON_KIND_STOCK,
	GS_APPSTREAM_ITEM_ICON_KIND_CACHED,
	GS_APPSTREAM_ITEM_ICON_KIND_UNKNOWN,
	GS_APPSTREAM_ITEM_ICON_KIND_LAST
} GsAppstreamItemIconKind;

typedef struct	GsAppstreamItem	GsAppstreamItem;

void		 gs_appstream_item_free			(GsAppstreamItem	*item);
GsAppstreamItem	*gs_appstream_item_new			(void);

const gchar	*gs_appstream_item_get_id		(GsAppstreamItem	*item);
const gchar	*gs_appstream_item_get_pkgname		(GsAppstreamItem	*item);
const gchar	*gs_appstream_item_get_name		(GsAppstreamItem	*item);
const gchar	*gs_appstream_item_get_summary		(GsAppstreamItem	*item);
const gchar	*gs_appstream_item_get_url		(GsAppstreamItem	*item);
const gchar	*gs_appstream_item_get_description	(GsAppstreamItem	*item);
const gchar	*gs_appstream_item_get_icon		(GsAppstreamItem	*item);
gboolean	 gs_appstream_item_has_category		(GsAppstreamItem	*item,
							 const gchar		*category);
GsAppstreamItemIconKind	gs_appstream_item_get_icon_kind	(GsAppstreamItem	*item);

void		 gs_appstream_item_set_id		(GsAppstreamItem	*item,
							 const gchar		*id,
							 gsize			 length);
void		 gs_appstream_item_set_pkgname		(GsAppstreamItem	*item,
							 const gchar		*pkgname,
							 gsize			 length);
void		 gs_appstream_item_set_name		(GsAppstreamItem	*item,
							 const gchar		*name,
							 gsize			 length);
void		 gs_appstream_item_set_summary		(GsAppstreamItem	*item,
							 const gchar		*summary,
							 gsize			 length);
void		 gs_appstream_item_set_url		(GsAppstreamItem	*item,
							 const gchar		*summary,
							 gsize			 length);
void		 gs_appstream_item_set_description	(GsAppstreamItem	*item,
							 const gchar		*description,
							 gsize			 length);
void		 gs_appstream_item_set_icon		(GsAppstreamItem	*item,
							 const gchar		*icon,
							 gsize			 length);
void		 gs_appstream_item_add_category		(GsAppstreamItem	*item,
							 const gchar		*category,
							 gsize			 length);
void		 gs_appstream_item_set_icon_kind	(GsAppstreamItem	*item,
							 GsAppstreamItemIconKind icon_kind);

G_END_DECLS

#endif /* __GS_APPSTREAM_ITEM_H */
