/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __GS_INSTALLED_PAGE_H
#define __GS_INSTALLED_PAGE_H

#include "gs-page.h"

G_BEGIN_DECLS

#define GS_TYPE_INSTALLED_PAGE (gs_installed_page_get_type ())

G_DECLARE_FINAL_TYPE (GsInstalledPage, gs_installed_page, GS, INSTALLED_PAGE, GsPage)

GsInstalledPage	*gs_installed_page_new	(void);

G_END_DECLS

#endif /* __GS_INSTALLED_PAGE_H */
