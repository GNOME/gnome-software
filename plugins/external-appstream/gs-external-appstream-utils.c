 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Endless Mobile, Inc.
 *
 * Authors: Joaquim Rocha <jrocha@endlessm.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "gs-external-appstream-utils.h"

#define APPSTREAM_SYSTEM_DIR LOCALSTATEDIR "/cache/app-info/xmls"

gchar *
gs_external_appstream_utils_get_file_cache_path (const gchar *file_name)
{
	g_autofree gchar *prefixed_file_name = g_strdup_printf ("org.gnome.Software-%s",
								file_name);
	return g_build_filename (APPSTREAM_SYSTEM_DIR, prefixed_file_name, NULL);
}

const gchar *
gs_external_appstream_utils_get_system_dir (void)
{
	return APPSTREAM_SYSTEM_DIR;
}
