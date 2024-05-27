/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_DKMS (gs_plugin_dkms_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginDkms, gs_plugin_dkms, GS, PLUGIN_DKMS, GsPlugin)

G_END_DECLS
