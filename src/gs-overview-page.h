/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include "gs-page.h"

G_BEGIN_DECLS

#define GS_TYPE_OVERVIEW_PAGE (gs_overview_page_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsOverviewPage, gs_overview_page, GS, OVERVIEW_PAGE, GsPage)

struct _GsOverviewPageClass
{
	GsPageClass		 parent_class;

	void	(*refreshed)	(GsOverviewPage *self);
};

GsOverviewPage	*gs_overview_page_new		(void);
void		 gs_overview_page_set_category	(GsOverviewPage		*self,
						 const gchar		*category);

G_END_DECLS
