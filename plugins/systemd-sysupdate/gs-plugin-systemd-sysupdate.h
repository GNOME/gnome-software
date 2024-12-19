/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (c) 2024 Codethink Limited
 * Copyright (c) 2024 GNOME Foundation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_SYSTEMD_SYSUPDATE (gs_plugin_systemd_sysupdate_get_type())

G_DECLARE_FINAL_TYPE (GsPluginSystemdSysupdate, gs_plugin_systemd_sysupdate, GS, PLUGIN_SYSTEMD_SYSUPDATE, GsPlugin)

G_END_DECLS
