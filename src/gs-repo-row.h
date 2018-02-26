/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
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

#ifndef GS_REPO_ROW_H
#define GS_REPO_ROW_H

#include "gnome-software-private.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_REPO_ROW (gs_repo_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsRepoRow, gs_repo_row, GS, REPO_ROW, GtkListBoxRow)

struct _GsRepoRowClass
{
	GtkListBoxRowClass	  parent_class;
	void			(*button_clicked)	(GsRepoRow	*row);
};

GtkWidget	*gs_repo_row_new			(void);
void		 gs_repo_row_set_switch_enabled		(GsRepoRow	*row,
							 gboolean	 switch_enabled);
GtkWidget	*gs_repo_row_get_switch			(GsRepoRow	*row);
void		 gs_repo_row_set_name			(GsRepoRow	*row,
							 const gchar	*name);
void		 gs_repo_row_set_comment		(GsRepoRow	*row,
							 const gchar	*comment);
void		 gs_repo_row_set_url			(GsRepoRow	*row,
							 const gchar	*url);
void		 gs_repo_row_set_repo			(GsRepoRow	*row,
							 GsApp		*repo);
GsApp		*gs_repo_row_get_repo			(GsRepoRow	*row);
void		 gs_repo_row_show_details		(GsRepoRow	*row);
void		 gs_repo_row_hide_details		(GsRepoRow	*row);
void		 gs_repo_row_show_status		(GsRepoRow	*row);

G_END_DECLS

#endif /* GS_REPO_ROW_H */

/* vim: set noexpandtab: */
