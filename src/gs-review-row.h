/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
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

#ifndef GS_REVIEW_ROW_H
#define GS_REVIEW_ROW_H

#include <gtk/gtk.h>

#include "gs-review.h"

G_BEGIN_DECLS

#define GS_TYPE_REVIEW_ROW (gs_review_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsReviewRow, gs_review_row, GS, REVIEW_ROW, GtkListBoxRow)

struct _GsReviewRowClass
{
	GtkListBoxRowClass	 parent_class;
	void			(*button_clicked)	(GsReviewRow	*review_row,
							 GsReviewAction	 action);
};

GtkWidget	*gs_review_row_new		(GsReview	*review);
GsReview	*gs_review_row_get_review	(GsReviewRow	*review_row);
void		 gs_review_row_set_actions	(GsReviewRow	*review_row,
						 guint64	 actions);

G_END_DECLS

#endif /* GS_REVIEW_ROW_H */

/* vim: set noexpandtab: */
