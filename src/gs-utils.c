/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:gs-utils
 * @title: GsUtils
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: Utilities that plugins can use
 *
 * These functions provide useful functionality that makes it easy to
 * add new plugin functions.
 */

#include "config.h"

#include <errno.h>
#include <fnmatch.h>
#include <math.h>
#include <glib/gstdio.h>

#ifdef HAVE_POLKIT
#include <polkit/polkit.h>
#endif

#include "gs-app.h"
#include "gs-utils.h"
#include "gs-plugin.h"

/**
 * gs_mkdir_parent:
 * @path: A full pathname
 * @error: A #GError, or %NULL
 *
 * Creates any required directories, including any parent directories.
 *
 * Returns: %TRUE for success
 **/
gboolean
gs_mkdir_parent (const gchar *path, GError **error)
{
	g_autofree gchar *parent = NULL;

	parent = g_path_get_dirname (path);
	if (g_mkdir_with_parents (parent, 0755) == -1) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to create '%s': %s",
			     parent, g_strerror (errno));
		return FALSE;
	}
	return TRUE;
}

/**
 * gs_utils_get_file_age:
 * @file: A #GFile
 *
 * Gets a file age.
 *
 * Returns: The time in seconds since the file was modified, or %G_MAXUINT for error
 */
guint
gs_utils_get_file_age (GFile *file)
{
	guint64 now;
	guint64 mtime;
	g_autoptr(GFileInfo) info = NULL;

	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_TIME_MODIFIED,
				  G_FILE_QUERY_INFO_NONE,
				  NULL,
				  NULL);
	if (info == NULL)
		return G_MAXUINT;
	mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	now = (guint64) g_get_real_time () / G_USEC_PER_SEC;
	if (mtime > now)
		return G_MAXUINT;
	if (now - mtime > G_MAXUINT)
		return G_MAXUINT;
	return (guint) (now - mtime);
}

/**
 * gs_utils_get_cache_filename:
 * @kind: A cache kind, e.g. "firmware" or "screenshots/123x456"
 * @basename: A filename basename, e.g. "system.bin"
 * @flags: Some #GsUtilsCacheFlags, e.g. %GS_UTILS_CACHE_FLAG_WRITEABLE
 * @error: A #GError, or %NULL
 *
 * Returns a filename that points into the cache.
 * This may be per-system or per-user, the latter being more likely
 * when %GS_UTILS_CACHE_FLAG_WRITEABLE is specified in @flags.
 *
 * Returns: The full path and filename, which may or may not exist, or %NULL
 **/
gchar *
gs_utils_get_cache_filename (const gchar *kind,
			     const gchar *basename,
			     GsUtilsCacheFlags flags, /* ignored */
			     GError **error)
{
	g_autofree gchar *cachedir = NULL;
	g_autofree gchar *vername = NULL;
	g_auto(GStrv) version = g_strsplit (VERSION, ".", 3);
	g_autoptr(GFile) cachedir_file = NULL;

	/* not writable, so try the system cache first */
	if ((flags & GS_UTILS_CACHE_FLAG_WRITEABLE) == 0) {
		g_autofree gchar *cachefn = NULL;
		cachefn = g_build_filename (LOCALSTATEDIR,
					    "cache",
					    "gnome-software",
		                            kind,
					    basename,
					    NULL);
		if (g_file_test (cachefn, G_FILE_TEST_EXISTS))
			return g_steal_pointer (&cachefn);
	}

	/* not writable, so try the system cache first */
	if ((flags & GS_UTILS_CACHE_FLAG_WRITEABLE) == 0) {
		g_autofree gchar *cachefn = NULL;
		cachefn = g_build_filename (DATADIR,
					    "gnome-software",
					    "cache",
					    kind,
					    basename,
					    NULL);
		if (g_file_test (cachefn, G_FILE_TEST_EXISTS))
			return g_steal_pointer (&cachefn);
	}

	/* create the cachedir in a per-release location, creating
	 * if it does not already exist */
	vername = g_strdup_printf ("%s.%s", version[0], version[1]);
	cachedir = g_build_filename (g_get_user_cache_dir (),
				     "gnome-software",
				     vername,
				     kind,
				     NULL);
	cachedir_file = g_file_new_for_path (cachedir);
	if (!g_file_query_exists (cachedir_file, NULL) &&
	    !g_file_make_directory_with_parents (cachedir_file, NULL, error))
		return NULL;
	return g_build_filename (cachedir, basename, NULL);
}

/**
 * gs_utils_get_user_hash:
 * @error: A #GError, or %NULL
 *
 * This SHA1 hash is composed of the contents of machine-id and your
 * usename and is also salted with a hardcoded value.
 *
 * This provides an identifier that can be used to identify a specific
 * user on a machine, allowing them to cast only one vote or perform
 * one review on each application.
 *
 * There is no known way to calculate the machine ID or username from
 * the machine hash and there should be no privacy issue.
 *
 * Returns: The user hash, or %NULL on error
 */
gchar *
gs_utils_get_user_hash (GError **error)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *salted = NULL;

	if (!g_file_get_contents ("/etc/machine-id", &data, NULL, error))
		return NULL;

	salted = g_strdup_printf ("gnome-software[%s:%s]",
				  g_get_user_name (), data);
	return g_compute_checksum_for_string (G_CHECKSUM_SHA1, salted, -1);
}

/**
 * gs_utils_get_permission:
 * @id: A PolicyKit ID, e.g. "org.gnome.Desktop"
 *
 * Gets a permission object for an ID.
 *
 * Returns: a #GPermission, or %NULL if this if not possible.
 **/
GPermission *
gs_utils_get_permission (const gchar *id)
{
#ifdef HAVE_POLKIT
	g_autoptr(GPermission) permission = NULL;
	g_autoptr(GError) error = NULL;

	permission = polkit_permission_new_sync (id, NULL, NULL, &error);
	if (permission == NULL) {
		g_warning ("Failed to create permission %s: %s", id, error->message);
		return NULL;
	}
	return g_steal_pointer (&permission);
#else
	g_debug ("no PolicyKit, so can't return GPermission for %s", id);
	return NULL;
#endif
}

/**
 * gs_utils_get_content_type:
 * @file: A GFile
 * @cancellable: A #GCancellable, or %NULL
 * @error: A #GError, or %NULL
 *
 * Gets the standard content type for a file.
 *
 * Returns: the content type, or %NULL, e.g. "text/plain"
 */
gchar *
gs_utils_get_content_type (GFile *file,
			   GCancellable *cancellable,
			   GError **error)
{
	const gchar *tmp;
	g_autoptr(GFileInfo) info = NULL;

	/* get content type */
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  G_FILE_QUERY_INFO_NONE,
				  cancellable,
				  error);
	if (info == NULL)
		return NULL;
	tmp = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
	if (tmp == NULL)
		return NULL;
	return g_strdup (tmp);
}

/**
 * gs_utils_strv_fnmatch:
 * @strv: A NUL-terminated list of strings
 * @str: A string
 *
 * Matches a string against a list of globs.
 *
 * Returns: %TRUE if the list matches
 */
gboolean
gs_utils_strv_fnmatch (gchar **strv, const gchar *str)
{
	guint i;

	/* empty */
	if (strv == NULL)
		return FALSE;

	/* look at each one */
	for (i = 0; strv[i] != NULL; i++) {
		if (fnmatch (strv[i], str, 0) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * gs_utils_get_desktop_app_info:
 * @id: A desktop ID, e.g. "gimp.desktop"
 *
 * Gets a a #GDesktopAppInfo taking into account the kde4- prefix.
 *
 * Returns: a #GDesktopAppInfo for a specific ID, or %NULL
 */
GDesktopAppInfo *
gs_utils_get_desktop_app_info (const gchar *id)
{
	GDesktopAppInfo *app_info;

	/* try to get the standard app-id */
	app_info = g_desktop_app_info_new (id);

	/* KDE is a special project because it believes /usr/share/applications
	 * isn't KDE enough. For this reason we support falling back to the
	 * "kde4-" prefixed ID to avoid educating various self-righteous
	 * upstreams about the correct ID to use in the AppData file. */
	if (app_info == NULL) {
		g_autofree gchar *kde_id = NULL;
		kde_id = g_strdup_printf ("%s-%s", "kde4", id);
		app_info = g_desktop_app_info_new (kde_id);
	}

	return app_info;
}

/**
 * gs_utils_symlink:
 * @target: the full path of the symlink to create
 * @linkpath: where the symlink should point to
 * @error: A #GError, or %NULL
 *
 * Creates a symlink that can cross filesystem boundaries.
 * Any parent directories needed for target to exist are also created.
 *
 * Returns: %TRUE for success
 **/
gboolean
gs_utils_symlink (const gchar *target, const gchar *linkpath, GError **error)
{
	if (!gs_mkdir_parent (target, error))
		return FALSE;
	if (symlink (target, linkpath) != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_WRITE_FAILED,
			     "failed to create symlink from %s to %s",
			     linkpath, target);
		return FALSE;
	}
	return TRUE;
}

/**
 * gs_utils_unlink:
 * @filename: A full pathname to delete
 * @error: A #GError, or %NULL
 *
 * Deletes a file from disk.
 *
 * Returns: %TRUE for success
 **/
gboolean
gs_utils_unlink (const gchar *filename, GError **error)
{
	if (g_unlink (filename) != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "failed to delete %s",
			     filename);
		return FALSE;
	}
	return TRUE;
}

static gboolean
gs_utils_rmtree_real (const gchar *directory, GError **error)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;

	/* try to open */
	dir = g_dir_open (directory, 0, error);
	if (dir == NULL)
		return FALSE;

	/* find each */
	while ((filename = g_dir_read_name (dir))) {
		g_autofree gchar *src = NULL;
		src = g_build_filename (directory, filename, NULL);
		if (g_file_test (src, G_FILE_TEST_IS_DIR) &&
		    !g_file_test (src, G_FILE_TEST_IS_SYMLINK)) {
			if (!gs_utils_rmtree_real (src, error))
				return FALSE;
		} else {
			if (g_unlink (src) != 0) {
				g_set_error (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "Failed to delete: %s", src);
				return FALSE;
			}
		}
	}

	if (g_rmdir (directory) != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to remove: %s", directory);
		return FALSE;
	}
	return TRUE;
}

/**
 * gs_utils_rmtree:
 * @directory: A full directory pathname to delete
 * @error: A #GError, or %NULL
 *
 * Deletes a directory from disk and all its contents.
 *
 * Returns: %TRUE for success
 **/
gboolean
gs_utils_rmtree (const gchar *directory, GError **error)
{
	g_debug ("recursively removing directory '%s'", directory);
	return gs_utils_rmtree_real (directory, error);
}

static gdouble
pnormaldist (gdouble qn)
{
	static gdouble b[11] = { 1.570796288,      0.03706987906,   -0.8364353589e-3,
				-0.2250947176e-3,  0.6841218299e-5,  0.5824238515e-5,
				-0.104527497e-5,   0.8360937017e-7, -0.3231081277e-8,
				 0.3657763036e-10, 0.6936233982e-12 };
	gdouble w1, w3;
	guint i;

	if (qn < 0 || qn > 1)
		return 0; // This is an error case
	if (qn == 0.5)
		return 0;

	w1 = qn;
	if (qn > 0.5)
		w1 = 1.0 - w1;
	w3 = -log (4.0 * w1 * (1.0 - w1));
	w1 = b[0];
	for (i = 1; i < 11; i++)
		w1 = w1 + (b[i] * pow (w3, i));

	if (qn > 0.5)
		return sqrt (w1 * w3);
	else
		return -sqrt (w1 * w3);
}

static gdouble
wilson_score (gdouble value, gdouble n, gdouble power)
{
	gdouble z, phat;
	if (value == 0)
		return 0;
	z = pnormaldist (1 - power / 2);
	phat = value / n;
	return (phat + z * z / (2 * n) -
		z * sqrt ((phat * (1 - phat) + z * z / (4 * n)) / n)) /
		(1 + z * z / n);
}

/**
 * gs_utils_get_wilson_rating:
 * @star1: The number of 1 star reviews
 * @star2: The number of 2 star reviews
 * @star3: The number of 3 star reviews
 * @star4: The number of 4 star reviews
 * @star5: The number of 5 star reviews
 *
 * Returns the lower bound of Wilson score confidence interval for a
 * Bernoulli parameter. This ensures small numbers of ratings don't give overly
 * high scores.
 * See https://en.wikipedia.org/wiki/Binomial_proportion_confidence_interval
 * for details.
 *
 * Returns: Wilson rating percentage, or -1 for error
 **/
gint
gs_utils_get_wilson_rating (guint64 star1,
			    guint64 star2,
			    guint64 star3,
			    guint64 star4,
			    guint64 star5)
{
	gdouble val;
	guint64 star_sum = star1 + star2 + star3 + star4 + star5;
	if (star_sum == 0)
		return -1;

	/* get score */
	val =  (wilson_score ((gdouble) star1, (gdouble) star_sum, 0.2) * -2);
	val += (wilson_score ((gdouble) star2, (gdouble) star_sum, 0.2) * -1);
	val += (wilson_score ((gdouble) star4, (gdouble) star_sum, 0.2) * 1);
	val += (wilson_score ((gdouble) star5, (gdouble) star_sum, 0.2) * 2);

	/* normalize from -2..+2 to 0..5 */
	val += 3;

	/* multiply to a percentage */
	val *= 20;

	/* return rounded up integer */
	return (gint) ceil (val);
}

/* vim: set noexpandtab: */
