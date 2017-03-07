/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>

#include <flatpak.h>
#include <string.h>

#include "gs-flatpak-symlinks.h"

static gboolean
gs_flatpak_symlinks_cleanup_kind (const gchar *cache_dir,
				  const gchar *prefix,
				  const gchar *kind,
				  GCancellable *cancellable,
				  GError **error)
{
	const gchar *tmp;
	g_autofree gchar *subdir = NULL;
	g_autoptr(GDir) dir = NULL;

	subdir = g_build_filename (cache_dir, kind, NULL);
	if (!g_file_test (subdir, G_FILE_TEST_EXISTS))
		return TRUE;
	dir = g_dir_open (subdir, 0, error);
	if (dir == NULL) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *fn = NULL;
		g_autofree gchar *origin = NULL;
		g_autofree gchar *prefix_colon = g_strdup_printf ("%s:", prefix);
		g_autoptr(FlatpakRemote) xremote = NULL;

		/* not interesting */
		if (!g_str_has_prefix (tmp, prefix_colon))
			continue;

		/* only a symlink */
		fn = g_build_filename (subdir, tmp, NULL);
		if (!g_file_test (fn, G_FILE_TEST_IS_SYMLINK))
			continue;
		g_debug ("deleting %s as symlinks no longer required", fn);
		if (!gs_utils_unlink (fn, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_flatpak_symlinks_cleanup (FlatpakInstallation *installation,
			     GCancellable *cancellable,
			     GError **error)
{
	const gchar *prefix = "flatpak";
	g_autofree gchar *cache_dir = NULL;

	/* use the correct symlink target */
	cache_dir = g_build_filename (g_get_user_data_dir (),
				      "app-info",
				      NULL);
	if (flatpak_installation_get_is_user (installation))
		prefix = "user-flatpak";

	/* go through each symlink and check the remote still valid */
	if (!gs_flatpak_symlinks_cleanup_kind (cache_dir,
					       prefix,
					       "icons",
					       cancellable,
					       error))
		return FALSE;
	if (!gs_flatpak_symlinks_cleanup_kind (cache_dir,
					       prefix,
					       "xmls",
					       cancellable,
					       error))
		return FALSE;

	/* success */
	return TRUE;
}
