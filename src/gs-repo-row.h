/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

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
