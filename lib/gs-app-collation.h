/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __GS_APP_COLLATION_H
#define __GS_APP_COLLATION_H

#include <glib-object.h>

#include "gs-app-list.h"

G_BEGIN_DECLS

GsAppList	*gs_app_get_related		(GsApp		*app);
GsAppList	*gs_app_get_addons		(GsApp		*app);
GsAppList	*gs_app_get_history		(GsApp		*app);

G_END_DECLS

#endif /* __GS_APP_COLLATION_H */
