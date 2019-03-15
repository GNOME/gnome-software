/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Endless Mobile, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "gs-app.h"
#include "gs-app-list.h"

G_BEGIN_DECLS

gboolean gs_metered_block_on_download_scheduler (GVariant      *parameters,
                                                 GCancellable  *cancellable,
                                                 GError       **error);
gboolean gs_metered_block_app_on_download_scheduler (GsApp         *app,
                                                     GCancellable  *cancellable,
                                                     GError       **error);
gboolean gs_metered_block_app_list_on_download_scheduler (GsAppList     *app_list,
                                                          GCancellable  *cancellable,
                                                          GError       **error);

G_END_DECLS
