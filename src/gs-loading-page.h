/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_LOADING_PAGE_H
#define __GS_LOADING_PAGE_H

#include "gs-page.h"

G_BEGIN_DECLS

#define GS_TYPE_LOADING_PAGE (gs_loading_page_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsLoadingPage, gs_loading_page, GS, LOADING_PAGE, GsPage)

struct _GsLoadingPageClass
{
	GsPageClass		 parent_class;

	void	(*refreshed)	(GsLoadingPage	*self);
};

GsLoadingPage	*gs_loading_page_new		(void);

G_END_DECLS

#endif /* __GS_LOADING_PAGE_H */

/* vim: set noexpandtab: */
