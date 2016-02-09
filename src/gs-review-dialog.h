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

#ifndef GS_REVIEW_DIALOG_H
#define GS_REVIEW_DIALOG_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GS_TYPE_REVIEW_DIALOG (gs_review_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsReviewDialog, gs_review_dialog, GS, REVIEW_DIALOG, GtkDialog)

GtkWidget	*gs_review_dialog_new		(void);
gint		 gs_review_dialog_get_rating	(GsReviewDialog	*dialog);
void		 gs_review_dialog_set_rating	(GsReviewDialog	*dialog,
						 gint		 rating);
const gchar	*gs_review_dialog_get_summary	(GsReviewDialog	*dialog);
gchar		*gs_review_dialog_get_text	(GsReviewDialog	*dialog);

G_END_DECLS

#endif /* GS_REVIEW_DIALOG_H */

/* vim: set noexpandtab: */
