/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

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
