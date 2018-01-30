/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
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
#include <string.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <sys/sysinfo.h>

#ifdef HAVE_POLKIT
#include <polkit/polkit.h>
#endif

#include "gs-app.h"
#include "gs-utils.h"
#include "gs-plugin.h"

#define LOW_RESOLUTION_WIDTH  800
#define LOW_RESOLUTION_HEIGHT 600

#define MB_IN_BYTES (1024 * 1024)

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

static gchar *
gs_utils_filename_array_return_newest (GPtrArray *array)
{
	const gchar *filename_best = NULL;
	guint age_lowest = G_MAXUINT;
	guint i;
	for (i = 0; i < array->len; i++) {
		const gchar *fn = g_ptr_array_index (array, i);
		g_autoptr(GFile) file = g_file_new_for_path (fn);
		guint age_tmp = gs_utils_get_file_age (file);
		if (age_tmp < age_lowest) {
			age_lowest = age_tmp;
			filename_best = fn;
		}
	}
	return g_strdup (filename_best);
}

/**
 * gs_utils_get_cache_filename:
 * @kind: A cache kind, e.g. "fwupd" or "screenshots/123x456"
 * @resource: A resource, e.g. "system.bin" or "http://foo.bar/baz.bin"
 * @flags: Some #GsUtilsCacheFlags, e.g. %GS_UTILS_CACHE_FLAG_WRITEABLE
 * @error: A #GError, or %NULL
 *
 * Returns a filename that points into the cache.
 * This may be per-system or per-user, the latter being more likely
 * when %GS_UTILS_CACHE_FLAG_WRITEABLE is specified in @flags.
 *
 * If %GS_UTILS_CACHE_FLAG_USE_HASH is set in @flags then the returned filename
 * will contain the hashed version of @resource.
 *
 * If there is more than one match, the file that has been modified last is
 * returned.
 *
 * If a plugin requests a file to be saved in the cache it is the plugins
 * responsibility to remove the file when it is no longer valid or is too old
 * -- gnome-software will not ever clean the cache for the plugin.
 * For this reason it is a good idea to use the plugin name as @kind.
 *
 * Returns: The full path and filename, which may or may not exist, or %NULL
 **/
gchar *
gs_utils_get_cache_filename (const gchar *kind,
			     const gchar *resource,
			     GsUtilsCacheFlags flags,
			     GError **error)
{
	g_autofree gchar *basename = NULL;
	g_autofree gchar *cachedir = NULL;
	g_autoptr(GFile) cachedir_file = NULL;
	g_autoptr(GPtrArray) candidates = g_ptr_array_new_with_free_func (g_free);

	/* get basename */
	if (flags & GS_UTILS_CACHE_FLAG_USE_HASH) {
		g_autofree gchar *basename_tmp = g_path_get_basename (resource);
		g_autofree gchar *hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1,
									resource, -1);
		basename = g_strdup_printf ("%s-%s", hash, basename_tmp);
	} else {
		basename = g_path_get_basename (resource);
	}

	/* not writable, so try the system cache first */
	if ((flags & GS_UTILS_CACHE_FLAG_WRITEABLE) == 0) {
		g_autofree gchar *cachefn = NULL;
		cachefn = g_build_filename (LOCALSTATEDIR,
					    "cache",
					    "gnome-software",
		                            kind,
					    basename,
					    NULL);
		if (g_file_test (cachefn, G_FILE_TEST_EXISTS)) {
			g_ptr_array_add (candidates,
					 g_steal_pointer (&cachefn));
		}
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
		if (g_file_test (cachefn, G_FILE_TEST_EXISTS)) {
			g_ptr_array_add (candidates,
					 g_steal_pointer (&cachefn));
		}
	}

	/* create the cachedir in a per-release location, creating
	 * if it does not already exist */
	cachedir = g_build_filename (g_get_user_cache_dir (),
				     "gnome-software",
				     kind,
				     NULL);
	cachedir_file = g_file_new_for_path (cachedir);
	if (g_file_query_exists (cachedir_file, NULL) &&
	    flags & GS_UTILS_CACHE_FLAG_ENSURE_EMPTY) {
		if (!gs_utils_rmtree (cachedir, error))
			return FALSE;
	}
	if (!g_file_query_exists (cachedir_file, NULL) &&
	    !g_file_make_directory_with_parents (cachedir_file, NULL, error))
		return NULL;
	g_ptr_array_add (candidates, g_build_filename (cachedir, basename, NULL));

	/* common case: we only have one option */
	if (candidates->len == 1)
		return g_strdup (g_ptr_array_index (candidates, 0));

	/* return the newest (i.e. one with least age) */
	return gs_utils_filename_array_return_newest (candidates);
}

/**
 * gs_utils_get_user_hash:
 * @error: A #GError, or %NULL
 *
 * This SHA1 hash is composed of the contents of machine-id and your
 * username and is also salted with a hardcoded value.
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
 * @cancellable: A #GCancellable, or %NULL
 * @error: A #GError, or %NULL
 *
 * Gets a permission object for an ID.
 *
 * Returns: a #GPermission, or %NULL if this if not possible.
 **/
GPermission *
gs_utils_get_permission (const gchar *id, GCancellable *cancellable, GError **error)
{
#ifdef HAVE_POLKIT
	g_autoptr(GPermission) permission = NULL;
	permission = polkit_permission_new_sync (id, NULL, cancellable, error);
	if (permission == NULL) {
		g_prefix_error (error, "failed to create permission %s: ", id);
		gs_utils_error_convert_gio (error);
		return NULL;
	}
	return g_steal_pointer (&permission);
#else
	g_set_error (error,
		     GS_PLUGIN_ERROR,
		     GS_PLUGIN_ERROR_NOT_SUPPORTED,
		     "no PolicyKit, so can't return GPermission for %s", id);
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
			     GS_PLUGIN_ERROR_DELETE_FAILED,
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
					     GS_PLUGIN_ERROR_DELETE_FAILED,
					     "Failed to delete: %s", src);
				return FALSE;
			}
		}
	}

	if (g_rmdir (directory) != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_DELETE_FAILED,
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

/**
 * gs_utils_error_add_unique_id:
 * @error: a #GError
 * @app: a #GsApp
 *
 * Adds a unique ID prefix to the error.
 *
 * Since: 3.22
 **/
void
gs_utils_error_add_unique_id (GError **error, GsApp *app)
{
	g_return_if_fail (GS_APP (app));
	if (error == NULL || *error == NULL)
		return;
	g_prefix_error (error, "[%s] ", gs_app_get_unique_id (app));
}

/**
 * gs_utils_error_strip_unique_id:
 * @error: a #GError
 * @app: a #GsApp
 *
 * Removes a possible unique ID prefix from the error.
 *
 * Since: 3.22
 **/
void
gs_utils_error_strip_unique_id (GError *error)
{
	gchar *str;
	if (error == NULL)
		return;
	if (!g_str_has_prefix (error->message, "["))
		return;
	str = g_strstr_len (error->message, -1, " ");
	if (str == NULL)
		return;

	/* gahh, my eyes are bleeding */
	str = g_strdup (str + 1);
	g_free (error->message);
	error->message = str;
}

/**
 * gs_utils_error_convert_gdbus:
 * @perror: a pointer to a #GError, or %NULL
 *
 * Converts the #GDBusError to an error with a GsPluginError domain.
 *
 * Returns: %TRUE if the error was converted, or already correct
 **/
gboolean
gs_utils_error_convert_gdbus (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return FALSE;
	if (error->domain == GS_PLUGIN_ERROR)
		return TRUE;
	if (error->domain != G_DBUS_ERROR)
		return FALSE;
	switch (error->code) {
	case G_DBUS_ERROR_FAILED:
	case G_DBUS_ERROR_NO_REPLY:
	case G_DBUS_ERROR_TIMEOUT:
		error->code = GS_PLUGIN_ERROR_FAILED;
		break;
	case G_DBUS_ERROR_IO_ERROR:
	case G_DBUS_ERROR_NAME_HAS_NO_OWNER:
	case G_DBUS_ERROR_NOT_SUPPORTED:
	case G_DBUS_ERROR_SERVICE_UNKNOWN:
	case G_DBUS_ERROR_UNKNOWN_INTERFACE:
	case G_DBUS_ERROR_UNKNOWN_METHOD:
	case G_DBUS_ERROR_UNKNOWN_OBJECT:
	case G_DBUS_ERROR_UNKNOWN_PROPERTY:
		error->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
		break;
	case G_DBUS_ERROR_NO_MEMORY:
		error->code = GS_PLUGIN_ERROR_NO_SPACE;
		break;
	case G_DBUS_ERROR_ACCESS_DENIED:
	case G_DBUS_ERROR_AUTH_FAILED:
		error->code = GS_PLUGIN_ERROR_NO_SECURITY;
		break;
	case G_DBUS_ERROR_NO_NETWORK:
		error->code = GS_PLUGIN_ERROR_NO_NETWORK;
		break;
	case G_DBUS_ERROR_INVALID_FILE_CONTENT:
		error->code = GS_PLUGIN_ERROR_INVALID_FORMAT;
		break;
	default:
		g_warning ("can't reliably fixup error code %i in domain %s",
			   error->code, g_quark_to_string (error->domain));
		error->code = GS_PLUGIN_ERROR_FAILED;
		break;
	}
	error->domain = GS_PLUGIN_ERROR;
	return TRUE;
}

/**
 * gs_utils_error_convert_gio:
 * @perror: a pointer to a #GError, or %NULL
 *
 * Converts the #GIOError to an error with a GsPluginError domain.
 *
 * Returns: %TRUE if the error was converted, or already correct
 **/
gboolean
gs_utils_error_convert_gio (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return FALSE;
	if (error->domain == GS_PLUGIN_ERROR)
		return TRUE;
	if (error->domain != G_IO_ERROR)
		return FALSE;
	switch (error->code) {
	case G_IO_ERROR_FAILED:
	case G_IO_ERROR_NOT_FOUND:
	case G_IO_ERROR_EXISTS:
		error->code = GS_PLUGIN_ERROR_FAILED;
		break;
	case G_IO_ERROR_TIMED_OUT:
		error->code = GS_PLUGIN_ERROR_TIMED_OUT;
		break;
	case G_IO_ERROR_NOT_SUPPORTED:
		error->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
		break;
	case G_IO_ERROR_CANCELLED:
		error->code = GS_PLUGIN_ERROR_CANCELLED;
		break;
	case G_IO_ERROR_NO_SPACE:
		error->code = GS_PLUGIN_ERROR_NO_SPACE;
		break;
	case G_IO_ERROR_PERMISSION_DENIED:
		error->code = GS_PLUGIN_ERROR_NO_SECURITY;
		break;
	case G_IO_ERROR_HOST_NOT_FOUND:
	case G_IO_ERROR_HOST_UNREACHABLE:
	case G_IO_ERROR_CONNECTION_REFUSED:
	case G_IO_ERROR_PROXY_FAILED:
	case G_IO_ERROR_PROXY_AUTH_FAILED:
	case G_IO_ERROR_PROXY_NOT_ALLOWED:
		error->code = GS_PLUGIN_ERROR_DOWNLOAD_FAILED;
		break;
	case G_IO_ERROR_NETWORK_UNREACHABLE:
		error->code = GS_PLUGIN_ERROR_NO_NETWORK;
		break;
	default:
		g_warning ("can't reliably fixup error code %i in domain %s",
			   error->code, g_quark_to_string (error->domain));
		error->code = GS_PLUGIN_ERROR_FAILED;
		break;
	}
	error->domain = GS_PLUGIN_ERROR;
	return TRUE;
}

/**
 * gs_utils_error_convert_gresolver:
 * @perror: a pointer to a #GError, or %NULL
 *
 * Converts the #GResolverError to an error with a GsPluginError domain.
 *
 * Returns: %TRUE if the error was converted, or already correct
 **/
gboolean
gs_utils_error_convert_gresolver (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return FALSE;
	if (error->domain == GS_PLUGIN_ERROR)
		return TRUE;
	if (error->domain != G_RESOLVER_ERROR)
		return FALSE;
	switch (error->code) {
	case G_RESOLVER_ERROR_INTERNAL:
		error->code = GS_PLUGIN_ERROR_FAILED;
		break;
	case G_RESOLVER_ERROR_NOT_FOUND:
	case G_RESOLVER_ERROR_TEMPORARY_FAILURE:
		error->code = GS_PLUGIN_ERROR_DOWNLOAD_FAILED;
		break;
	default:
		g_warning ("can't reliably fixup error code %i in domain %s",
			   error->code, g_quark_to_string (error->domain));
		error->code = GS_PLUGIN_ERROR_FAILED;
		break;
	}
	error->domain = GS_PLUGIN_ERROR;
	return TRUE;
}

/**
 * gs_utils_error_convert_gdk_pixbuf:
 * @perror: a pointer to a #GError, or %NULL
 *
 * Converts the #GdkPixbufError to an error with a GsPluginError domain.
 *
 * Returns: %TRUE if the error was converted, or already correct
 **/
gboolean
gs_utils_error_convert_gdk_pixbuf (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return FALSE;
	if (error->domain == GS_PLUGIN_ERROR)
		return TRUE;
	if (error->domain != GDK_PIXBUF_ERROR)
		return FALSE;
	switch (error->code) {
	case GDK_PIXBUF_ERROR_UNSUPPORTED_OPERATION:
	case GDK_PIXBUF_ERROR_UNKNOWN_TYPE:
		error->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
		break;
	case GDK_PIXBUF_ERROR_FAILED:
		error->code = GS_PLUGIN_ERROR_FAILED;
		break;
	case GDK_PIXBUF_ERROR_CORRUPT_IMAGE:
		error->code = GS_PLUGIN_ERROR_INVALID_FORMAT;
		break;
	default:
		g_warning ("can't reliably fixup error code %i in domain %s",
			   error->code, g_quark_to_string (error->domain));
		error->code = GS_PLUGIN_ERROR_FAILED;
		break;
	}
	error->domain = GS_PLUGIN_ERROR;
	return TRUE;
}

/**
 * gs_utils_error_convert_json_glib:
 * @perror: a pointer to a #GError, or %NULL
 *
 * Converts the #JsonParserError to an error with a GsPluginError domain.
 *
 * Returns: %TRUE if the error was converted, or already correct
 **/
gboolean
gs_utils_error_convert_json_glib (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return FALSE;
	if (error->domain == GS_PLUGIN_ERROR)
		return TRUE;
	if (error->domain != JSON_PARSER_ERROR)
		return FALSE;
	switch (error->code) {
	case JSON_PARSER_ERROR_UNKNOWN:
		error->code = GS_PLUGIN_ERROR_FAILED;
		break;
	default:
		error->code = GS_PLUGIN_ERROR_INVALID_FORMAT;
		break;
	}
	error->domain = GS_PLUGIN_ERROR;
	return TRUE;
}

/**
 * gs_utils_error_convert_appstream:
 * @perror: a pointer to a #GError, or %NULL
 *
 * Converts the various AppStream error types to an error with a GsPluginError
 * domain.
 *
 * Returns: %TRUE if the error was converted, or already correct
 **/
gboolean
gs_utils_error_convert_appstream (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return FALSE;
	if (error->domain == GS_PLUGIN_ERROR)
		return TRUE;

	/* custom to this plugin */
	if (error->domain == AS_UTILS_ERROR) {
		switch (error->code) {
		case AS_UTILS_ERROR_INVALID_TYPE:
			error->code = GS_PLUGIN_ERROR_INVALID_FORMAT;
			break;
		case AS_UTILS_ERROR_FAILED:
		default:
			error->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}
	} else if (error->domain == AS_STORE_ERROR) {
		switch (error->code) {
		case AS_UTILS_ERROR_FAILED:
		default:
			error->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}
	} else if (error->domain == G_FILE_ERROR) {
		switch (error->code) {
		case G_FILE_ERROR_EXIST:
		case G_FILE_ERROR_ACCES:
		case G_FILE_ERROR_PERM:
			error->code = GS_PLUGIN_ERROR_NO_SECURITY;
			break;
		case G_FILE_ERROR_NOSPC:
			error->code = GS_PLUGIN_ERROR_NO_SPACE;
			break;
		default:
			error->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}
	} else {
		g_warning ("can't reliably fixup error from domain %s",
			   g_quark_to_string (error->domain));
		error->code = GS_PLUGIN_ERROR_FAILED;
	}
	error->domain = GS_PLUGIN_ERROR;
	return TRUE;
}

/**
 * gs_utils_get_url_scheme:
 * @url: A URL, e.g. "appstream://gimp.desktop"
 *
 * Gets the scheme from the URL string.
 *
 * Returns: the URL scheme, e.g. "appstream"
 */
gchar *
gs_utils_get_url_scheme	(const gchar *url)
{
	g_autoptr(SoupURI) uri = NULL;

	/* no data */
	if (url == NULL)
		return NULL;

	/* create URI from URL */
	uri = soup_uri_new (url);
	if (!SOUP_URI_IS_VALID (uri))
		return NULL;

	/* success */
	return g_strdup (soup_uri_get_scheme (uri));
}

/**
 * gs_utils_get_url_path:
 * @url: A URL, e.g. "appstream://gimp.desktop"
 *
 * Gets the path from the URL string, removing any leading slashes.
 *
 * Returns: the URL path, e.g. "gimp.desktop"
 */
gchar *
gs_utils_get_url_path (const gchar *url)
{
	g_autoptr(SoupURI) uri = NULL;
	const gchar *host;
	const gchar *path;

	uri = soup_uri_new (url);
	if (!SOUP_URI_IS_VALID (uri))
		return NULL;

	/* foo://bar -> scheme: foo, host: bar, path: / */
	/* foo:bar -> scheme: foo, host: (empty string), path: /bar */
	host = soup_uri_get_host (uri);
	path = soup_uri_get_path (uri);
	if (host != NULL && (strlen (host) > 0))
		path = host;

	/* trim any leading slashes */
	while (*path == '/')
		path++;

	/* success */
	return g_strdup (path);
}

/**
 * gs_utils_get_url_query:
 * @url: A URL, e.g. "snap://moon-buggy?channel=beta"
 * @url: A parameter name, e.g. "channel"
 *
 * Gets a query parameter from the URL string.
 *
 * Returns: the URL query parameter, e.g. "beta"
 */
gchar *
gs_utils_get_url_query_param (const gchar *url, const gchar *name)
{
	g_autoptr(SoupURI) uri = NULL;
	const gchar *query;
	g_autofree gchar *prefix = NULL;
	g_auto(GStrv) params = NULL;
	int i;

	uri = soup_uri_new (url);
	if (!SOUP_URI_IS_VALID (uri))
		return NULL;

	query = soup_uri_get_query (uri);
	if (query == NULL)
		return NULL;
	params = g_strsplit (query, "&", -1);
	prefix = g_strdup_printf ("%s=", name);
	for (i = 0; params[i] != NULL; i++) {
		if (g_str_has_prefix (params[i], prefix))
			return g_strdup (params[i] + strlen (prefix));
	}

	return NULL;
}

/**
 * gs_user_agent:
 *
 * Gets the user agent to use for remote requests.
 *
 * Returns: the user-agent, e.g. "gnome-software/3.22.1"
 */
const gchar *
gs_user_agent (void)
{
	return PACKAGE_NAME "/" PACKAGE_VERSION;
}

/**
 * gs_utils_append_key_value:
 * @str: A #GString
 * @align_len: The alignment of the @value compared to the @key
 * @key: The text to use as a title
 * @value: The text to use as a value
 *
 * Adds a line to an existing string, padding the key to a set number of spaces.
 *
 * Since: 3.26
 */
void
gs_utils_append_key_value (GString *str, gsize align_len,
			   const gchar *key, const gchar *value)
{
	gsize len = 0;

	g_return_if_fail (str != NULL);
	g_return_if_fail (value != NULL);

	if (key != NULL) {
		len = strlen (key) + 2;
		g_string_append (str, key);
		g_string_append (str, ": ");
	}
	for (gsize i = len; i < align_len + 1; i++)
		g_string_append (str, " ");
	g_string_append (str, value);
	g_string_append (str, "\n");
}

/**
 * gs_utils_is_low_resolution:
 *
 * Retrieves whether the primary monitor has a low resolution.
 *
 * Returns: %TRUE if the monitor has low resolution
 **/
gboolean
gs_utils_is_low_resolution (GtkWidget *toplevel)
{
	GdkRectangle geometry;
	GdkDisplay *display;
	GdkMonitor *monitor;

	display = gtk_widget_get_display (toplevel);
	monitor = gdk_display_get_monitor_at_window (display, gtk_widget_get_window (toplevel));

	gdk_monitor_get_geometry (monitor, &geometry);

	return geometry.width < LOW_RESOLUTION_WIDTH || geometry.height < LOW_RESOLUTION_HEIGHT;
}

guint
gs_utils_get_memory_total (void)
{
	struct sysinfo si = { 0 };
	sysinfo (&si);
	return si.totalram / MB_IN_BYTES / si.mem_unit;
}

/* vim: set noexpandtab: */
