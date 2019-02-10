/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_REVIEW_ROW (gs_review_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsReviewRow, gs_review_row, GS, REVIEW_ROW, GtkListBoxRow)

struct _GsReviewRowClass
{
	GtkListBoxRowClass	 parent_class;
	void			(*button_clicked)	(GsReviewRow	*review_row,
							 GsPluginAction	 action);
};

GtkWidget	*gs_review_row_new		(AsReview	*review);
AsReview	*gs_review_row_get_review	(GsReviewRow	*review_row);
void		 gs_review_row_set_actions	(GsReviewRow	*review_row,
						 guint64	 actions);
void		 gs_review_row_set_network_available	(GsReviewRow	*review_row,
							 gboolean	 network_available);

G_END_DECLS
