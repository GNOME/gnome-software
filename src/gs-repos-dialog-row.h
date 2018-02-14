/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
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

#ifndef GS_REPOS_DIALOG_ROW_H
#define GS_REPOS_DIALOG_ROW_H

#include "gnome-software-private.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_REPOS_DIALOG_ROW (gs_repos_dialog_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsReposDialogRow, gs_repos_dialog_row, GS, REPOS_DIALOG_ROW, GtkListBoxRow)

struct _GsReposDialogRowClass
{
	GtkListBoxRowClass	  parent_class;
	void			(*button_clicked)	(GsReposDialogRow	*row);
};

GtkWidget	*gs_repos_dialog_row_new		(void);
void		 gs_repos_dialog_row_set_switch_enabled	(GsReposDialogRow	*row,
							 gboolean		 switch_enabled);
void		 gs_repos_dialog_row_set_switch_active	(GsReposDialogRow	*row,
							 gboolean		 switch_active);
gboolean	 gs_repos_dialog_row_get_switch_active	(GsReposDialogRow	*row);
void		 gs_repos_dialog_row_set_name		(GsReposDialogRow	*row,
							 const gchar		*name);
void		 gs_repos_dialog_row_set_comment	(GsReposDialogRow	*row,
							 const gchar		*comment);
void		 gs_repos_dialog_row_set_url		(GsReposDialogRow	*row,
							 const gchar		*url);
void		 gs_repos_dialog_row_set_repo		(GsReposDialogRow	*row,
							 GsApp			*repo);
GsApp		*gs_repos_dialog_row_get_repo		(GsReposDialogRow	*row);
void		 gs_repos_dialog_row_show_details	(GsReposDialogRow	*row);
void		 gs_repos_dialog_row_hide_details	(GsReposDialogRow	*row);
void		 gs_repos_dialog_row_show_status	(GsReposDialogRow	*row);

G_END_DECLS

#endif /* GS_REPOS_DIALOG_ROW_H */

/* vim: set noexpandtab: */
