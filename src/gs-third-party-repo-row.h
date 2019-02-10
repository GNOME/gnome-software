/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

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
