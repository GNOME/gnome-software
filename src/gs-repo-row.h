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
	void			(*remove_clicked)	(GsRepoRow	*row);
	void			(*switch_clicked)	(GsRepoRow	*row);
};

GtkWidget	*gs_repo_row_new			(GsPluginLoader	*plugin_loader,
							 GsApp		*repo);
GsApp		*gs_repo_row_get_repo			(GsRepoRow	*row);
void		 gs_repo_row_mark_busy			(GsRepoRow	*row);
void		 gs_repo_row_unmark_busy		(GsRepoRow	*row);
gboolean	 gs_repo_row_get_is_busy		(GsRepoRow	*row);
void		 gs_repo_row_emit_switch_clicked	(GsRepoRow	*self);

G_END_DECLS
