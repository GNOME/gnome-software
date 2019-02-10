/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __GS_DESKTOP_GROUP_H
#define __GS_DESKTOP_GROUP_H

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
	const gchar	*key_colors;
	gint		 score;
} GsDesktopData;

const GsDesktopData	*gs_desktop_get_data		(void);

G_END_DECLS

#endif /* __GS_DESKTOP_GROUP_H */
