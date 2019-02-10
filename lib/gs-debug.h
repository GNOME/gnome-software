/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __GS_DEBUG_H
#define __GS_DEBUG_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_DEBUG (gs_debug_get_type ())

G_DECLARE_FINAL_TYPE (GsDebug, gs_debug, GS, DEBUG, GObject)

GsDebug	 	*gs_debug_new		(void);

G_END_DECLS

#endif /* __GS_DEBUG_H */
