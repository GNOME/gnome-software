/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Endless Mobile, Inc.
 *
 * Authors: Joaquim Rocha <jrocha@endlessm.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <config.h>
#include <glib.h>

const gchar	*gs_external_appstream_utils_get_system_dir (void);
gchar		*gs_external_appstream_utils_get_file_cache_path (const gchar	*file_name);
