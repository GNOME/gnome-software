/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_DBUS_HELPER (gs_dbus_helper_get_type ())

G_DECLARE_FINAL_TYPE (GsDbusHelper, gs_dbus_helper, GS, DBUS_HELPER, GObject)

GsDbusHelper	*gs_dbus_helper_new		(void);

G_END_DECLS
