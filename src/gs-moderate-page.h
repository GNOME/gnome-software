/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include "gs-page.h"

G_BEGIN_DECLS

#define GS_TYPE_MODERATE_PAGE (gs_moderate_page_get_type ())

G_DECLARE_FINAL_TYPE (GsModeratePage, gs_moderate_page, GS, MODERATE_PAGE, GsPage)

GsModeratePage	*gs_moderate_page_new		(void);

G_END_DECLS
