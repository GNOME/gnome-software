/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2011-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct {
	const gchar	*id;
	const gchar	*name;
	const gchar	*fdo_cats[16];
} GsDesktopMap;

typedef struct {
	const gchar	*id;
	const GsDesktopMap *mapping;
	const gchar	*name;
	const gchar	*icon;
	gint		 score;
} GsDesktopData;

const GsDesktopData	*gs_desktop_get_data		(void);

G_END_DECLS
