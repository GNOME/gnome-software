/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_REWRITE_RESOURCE (gs_plugin_rewrite_resource_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginRewriteResource, gs_plugin_rewrite_resource, GS, PLUGIN_REWRITE_RESOURCE, GsPlugin)

G_END_DECLS
