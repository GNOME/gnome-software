/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Kalev Lember <klember@redhat.com>
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

#ifndef __GS_SEARCH_PAGE_H
#define __GS_SEARCH_PAGE_H

#include "gs-page.h"

G_BEGIN_DECLS

#define GS_TYPE_SEARCH_PAGE (gs_search_page_get_type ())

G_DECLARE_FINAL_TYPE (GsSearchPage, gs_search_page, GS, SEARCH_PAGE, GsPage)

GsSearchPage	*gs_search_page_new			(void);
void		 gs_search_page_set_appid_to_show	(GsSearchPage		*self,
							 const gchar		*appid);
const gchar	*gs_search_page_get_text		(GsSearchPage		*self);
void		 gs_search_page_set_text		(GsSearchPage		*self,
							 const gchar		*value);

G_END_DECLS

#endif /* __GS_SEARCH_PAGE_H */

/* vim: set noexpandtab: */
