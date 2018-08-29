/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
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

#ifndef __GS_UTILS_H
#define __GS_UTILS_H

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>

#include "gs-app.h"

G_BEGIN_DECLS

/**
 * GsUtilsCacheFlags:
 * @GS_UTILS_CACHE_FLAG_NONE:		No flags set
 * @GS_UTILS_CACHE_FLAG_WRITEABLE:	A writable directory is required
 * @GS_UTILS_CACHE_FLAG_USE_HASH:	Prefix a hash to the filename
 * @GS_UTILS_CACHE_FLAG_ENSURE_EMPTY:	Clear existing cached items
 *
 * The cache flags.
 **/
typedef enum {
	GS_UTILS_CACHE_FLAG_NONE		= 0,
	GS_UTILS_CACHE_FLAG_WRITEABLE		= 1 << 0,
	GS_UTILS_CACHE_FLAG_USE_HASH		= 1 << 1,
	GS_UTILS_CACHE_FLAG_ENSURE_EMPTY	= 1 << 2,
	/*< private >*/
	GS_UTILS_CACHE_FLAG_LAST
} GsUtilsCacheFlags;

guint		 gs_utils_get_file_age		(GFile		*file);
gchar		*gs_utils_get_content_type	(GFile		*file,
						 GCancellable	*cancellable,
						 GError		**error);
gboolean	 gs_utils_symlink		(const gchar	*target,
						 const gchar	*linkpath,
						 GError		**error);
gboolean	 gs_utils_unlink		(const gchar	*filename,
						 GError		**error);
gboolean	 gs_mkdir_parent		(const gchar	*path,
						 GError		**error);
gchar		*gs_utils_get_cache_filename	(const gchar	*kind,
						 const gchar	*resource,
						 GsUtilsCacheFlags flags,
						 GError		**error);
gchar		*gs_utils_get_user_hash		(GError		**error);
GPermission	*gs_utils_get_permission	(const gchar	*id,
						 GCancellable	*cancellable,
						 GError		**error);
gboolean	 gs_utils_strv_fnmatch		(gchar		**strv,
						 const gchar	*str);
GDesktopAppInfo *gs_utils_get_desktop_app_info	(const gchar	*id);
gboolean	 gs_utils_rmtree		(const gchar	*directory,
						 GError		**error);
gint		 gs_utils_get_wilson_rating	(guint64	 star1,
						 guint64	 star2,
						 guint64	 star3,
						 guint64	 star4,
						 guint64	 star5);
void		 gs_utils_error_add_unique_id	(GError		**error,
						 GsApp		*app);
void		 gs_utils_error_strip_unique_id	(GError		*error);
gboolean	 gs_utils_error_convert_gio	(GError		**perror);
gboolean	 gs_utils_error_convert_gresolver (GError	**perror);
gboolean	 gs_utils_error_convert_gdbus	(GError		**perror);
gboolean	 gs_utils_error_convert_gdk_pixbuf(GError	**perror);
gboolean	 gs_utils_error_convert_json_glib (GError	**perror);
gboolean	 gs_utils_error_convert_appstream (GError	**perror);
gboolean	 gs_utils_is_low_resolution	  (GtkWidget     *toplevel);

gchar		*gs_utils_get_url_scheme	(const gchar	*url);
gchar		*gs_utils_get_url_path		(const gchar	*url);
const gchar	*gs_user_agent			(void);
void		 gs_utils_append_key_value	(GString	*str,
						 gsize		 align_len,
						 const gchar	*key,
						 const gchar	*value);
guint		 gs_utils_get_memory_total	(void);
gboolean	 gs_utils_parse_evr		(const gchar	 *evr,
						 gchar		**out_epoch,
						 gchar		**out_version,
						 gchar		**out_release);

G_END_DECLS

#endif /* __GS_UTILS_H */

/* vim: set noexpandtab: */
