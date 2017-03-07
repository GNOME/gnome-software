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

#ifndef __GS_DETAILS_PAGE_H
#define __GS_DETAILS_PAGE_H

#include "gs-page.h"

G_BEGIN_DECLS

#define GS_TYPE_DETAILS_PAGE (gs_details_page_get_type ())

G_DECLARE_FINAL_TYPE (GsDetailsPage, gs_details_page, GS, DETAILS_PAGE, GsPage)

GsDetailsPage	*gs_details_page_new		(void);
void		 gs_details_page_set_app	(GsDetailsPage		*self,
						 GsApp			*app);
void		 gs_details_page_set_local_file(GsDetailsPage		*self,
						 GFile			*file);
void		 gs_details_page_set_url	(GsDetailsPage		*self,
						 const gchar		*url);
GsApp		*gs_details_page_get_app	(GsDetailsPage		*self);

G_END_DECLS

#endif /* __GS_DETAILS_PAGE_H */

/* vim: set noexpandtab: */
