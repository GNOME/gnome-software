/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
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
                                                 gpointer      *schedule_entry_handle_out,
                                                 GCancellable  *cancellable,
                                                 GError       **error);
gboolean gs_metered_remove_from_download_scheduler (gpointer       schedule_entry_handle,
                                                    GCancellable  *cancellable,
                                                    GError       **error);
gboolean gs_metered_block_app_on_download_scheduler (GsApp         *app,
                                                     gpointer      *schedule_entry_handle_out,
                                                     GCancellable  *cancellable,
                                                     GError       **error);
gboolean gs_metered_block_app_list_on_download_scheduler (GsAppList     *app_list,
                                                          gpointer      *schedule_entry_handle_out,
                                                          GCancellable  *cancellable,
                                                          GError       **error);

G_END_DECLS
