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
gs_flatpak_symlinks_remote_valid (FlatpakRemote *xremote)
{
	if (xremote == NULL)
		return FALSE;
	if (flatpak_remote_get_disabled (xremote))
		return FALSE;
	if (flatpak_remote_get_noenumerate (xremote))
		return FALSE;
	return TRUE;
}

/* encode the symlink name with ${scope}:${name}[.xml.gz] */
static gboolean
gs_flatpak_symlinks_check_exist (FlatpakRemote *xremote,
				 const gchar *cache_dir,
				 const gchar *prefix,
				 const gchar *kind,
				 GError **error)
{
	g_autofree gchar *appstream_dir_fn = NULL;
	g_autofree gchar *flatpak_remote_fn = NULL;
	g_autofree gchar *subdir = NULL;
	g_autofree gchar *symlink_source = NULL;
	g_autofree gchar *symlink_target = NULL;
	g_autofree gchar *xml_dir = NULL;
	g_autoptr(GFile) appstream_dir = NULL;

	/* get the AppStream data location */
	appstream_dir = flatpak_remote_get_appstream_dir (xremote, NULL);
	if (appstream_dir == NULL) {
		g_debug ("no appstream dir for %s, skipping",
			 flatpak_remote_get_name (xremote));
		return TRUE;
	}

	/* ensure all the remotes have an XML symlink */
	appstream_dir_fn = g_file_get_path (appstream_dir);
	subdir = g_build_filename (cache_dir, kind, NULL);
	if (g_strcmp0 (kind, "xmls") == 0) {
		flatpak_remote_fn = g_strdup_printf ("%s:%s.xml.gz",
						     prefix,
						     flatpak_remote_get_name (xremote));
		symlink_target = g_build_filename (appstream_dir_fn,
						   "appstream.xml.gz",
						   NULL);
	} else {
		flatpak_remote_fn = g_strdup_printf ("%s:%s",
						     prefix,
						     flatpak_remote_get_name (xremote));
		symlink_target = g_build_filename (appstream_dir_fn,
						   "icons",
						   NULL);
	}
	symlink_source = g_build_filename (subdir,
					   flatpak_remote_fn,
					   NULL);
	if (!gs_mkdir_parent (symlink_source, error))
		return FALSE;

	/* check XML symbolic link is correct */
	if (g_file_test (symlink_source, G_FILE_TEST_IS_SYMLINK)) {
		g_autofree gchar *symlink_target_actual = NULL;

		/* target does not exist */
		symlink_target_actual = g_file_read_link (symlink_source, NULL);
		if (!g_file_test (symlink_target_actual, G_FILE_TEST_EXISTS)) {
			g_debug ("symlink %s is dangling (no %s), deleting",
				  symlink_source, symlink_target_actual);
			return gs_utils_unlink (symlink_source, error);
		}

		/* same */
		if (g_strcmp0 (symlink_target_actual, symlink_target) == 0) {
			g_debug ("symlink %s already points to %s",
				 symlink_source, symlink_target);
			return TRUE;
		}
		g_warning ("symlink incorrect expected %s target to "
			   "be %s, got %s, deleting",
			   symlink_source,
			   symlink_target,
			   symlink_target_actual);
		if (!gs_utils_unlink (symlink_source, error))
			return FALSE;
	}

	/* create it if required, but only if the destination exists */
	if (!g_file_test (symlink_source, G_FILE_TEST_EXISTS)) {
		if (g_file_test (symlink_target, G_FILE_TEST_EXISTS)) {
			g_debug ("creating missing symbolic link from %s to %s",
				 symlink_source, symlink_target);
			if (!gs_utils_symlink (symlink_target, symlink_source, error))
				return FALSE;
		} else {
			g_debug ("not creating missing symbolic link from "
				 "%s to %s as target does not yet exist",
				 symlink_source, symlink_target);
		}
	}

	return TRUE;
}

/* encode the symlink name with ${scope}:${name}, i.e. the origin */
static gboolean
gs_flatpak_symlinks_check_valid (FlatpakInstallation *installation,
				 const gchar *cache_dir,
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
	if (dir == NULL)
		return FALSE;
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		gchar *str;
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

		/* can we find a valid remote for this file */
		origin = g_strdup (tmp + strlen (prefix_colon));
		str = g_strrstr (origin, ".xml.gz");
		if (str != NULL)
			*str = '\0';
		xremote = flatpak_installation_get_remote_by_name (installation,
								   origin,
								   cancellable,
								   NULL);
		if (gs_flatpak_symlinks_remote_valid (xremote)) {
			g_debug ("%s remote symlink is valid", origin);
			continue;
		}
		g_debug ("deleting %s symlink as no longer valid", fn);
		if (!gs_utils_unlink (fn, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_flatpak_symlinks_rebuild (FlatpakInstallation *installation,
			     GCancellable *cancellable,
			     GError **error)
{
	const gchar *prefix = "flatpak";
	guint i;
	g_autofree gchar *cache_dir = NULL;
	g_autoptr(GPtrArray) xremotes = NULL;

	/* use the correct symlink target */
	cache_dir = g_build_filename (g_get_user_data_dir (),
				      "app-info",
				      NULL);
	if (flatpak_installation_get_is_user (installation))
		prefix = "user-flatpak";

	/* go through each remote checking the symlink is in place */
	xremotes = flatpak_installation_list_remotes (installation,
						      cancellable,
						      error);
	if (xremotes == NULL)
		return FALSE;
	for (i = 0; i < xremotes->len; i++) {
		FlatpakRemote *xremote = g_ptr_array_index (xremotes, i);
		if (!gs_flatpak_symlinks_remote_valid (xremote))
			continue;
		g_debug ("found remote %s:%s",
			 prefix,
			 flatpak_remote_get_name (xremote));
		if (!gs_flatpak_symlinks_check_exist (xremote,
						      cache_dir,
						      prefix,
						      "icons",
						      error))
			return FALSE;
		if (!gs_flatpak_symlinks_check_exist (xremote,
						      cache_dir,
						      prefix,
						      "xmls",
						      error))
			return FALSE;
	}

	/* go through each symlink and check the remote still valid */
	if (!gs_flatpak_symlinks_check_valid (installation,
					      cache_dir,
					      prefix,
					      "icons",
					      cancellable,
					      error))
		return FALSE;
	if (!gs_flatpak_symlinks_check_valid (installation,
					      cache_dir,
					      prefix,
					      "xmls",
					      cancellable,
					      error))
		return FALSE;

	/* success */
	return TRUE;
}
