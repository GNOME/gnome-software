/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
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

#ifndef GS_THIRD_PARTY_REPO_ROW_H
#define GS_THIRD_PARTY_REPO_ROW_H

#include "gnome-software-private.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_THIRD_PARTY_REPO_ROW (gs_third_party_repo_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsThirdPartyRepoRow, gs_third_party_repo_row, GS, THIRD_PARTY_REPO_ROW, GtkListBoxRow)

struct _GsThirdPartyRepoRowClass
{
	GtkListBoxRowClass	  parent_class;
	void			(*button_clicked)	(GsThirdPartyRepoRow	*row);
};

GtkWidget	*gs_third_party_repo_row_new		(void);
void		 gs_third_party_repo_row_set_name	(GsThirdPartyRepoRow	*row,
							 const gchar		*name);
void		 gs_third_party_repo_row_set_comment	(GsThirdPartyRepoRow	*row,
							 const gchar		*comment);
void		 gs_third_party_repo_row_set_app	(GsThirdPartyRepoRow	*row,
							 GsApp			*app);
GsApp		*gs_third_party_repo_row_get_app	(GsThirdPartyRepoRow	*row);

G_END_DECLS

#endif /* GS_THIRD_PARTY_REPO_ROW_H */

/* vim: set noexpandtab: */
