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

#define GS_TYPE_UPDATES_PAGE (gs_updates_page_get_type ())

G_DECLARE_FINAL_TYPE (GsUpdatesPage, gs_updates_page, GS, UPDATES_PAGE, GsPage)

GsUpdatesPage	*gs_updates_page_new		(void);

G_END_DECLS
