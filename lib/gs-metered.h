/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Endless Mobile, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#ifndef GS_ENABLE_EXPERIMENTAL_MOGWAI
#error Define GS_ENABLE_EXPERIMENTAL_MOGWAI to use this file
#endif

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

gboolean gs_metered_block_on_download_scheduler (GVariant      *parameters,
                                                 GCancellable  *cancellable,
                                                 GError       **error);

G_END_DECLS
