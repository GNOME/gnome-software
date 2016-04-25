/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_PLUGIN_H
#define __GS_PLUGIN_H

#include <appstream-glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <gio/gio.h>
#include <libsoup/soup.h>

#include "gs-app.h"
#include "gs-app-list.h"
#include "gs-category.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN (gs_plugin_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsPlugin, gs_plugin, GS, PLUGIN, GObject)

struct _GsPluginClass
{
	GObjectClass		 parent_class;
	void			(*updates_changed)	(GsPlugin	*plugin);
	void			(*status_changed)	(GsPlugin	*plugin,
							 GsApp		*app,
							 guint		 status);
	gpointer		 padding[29];
};

typedef struct	GsPluginData	GsPluginData;

typedef enum {
	GS_PLUGIN_STATUS_UNKNOWN,
	GS_PLUGIN_STATUS_WAITING,
	GS_PLUGIN_STATUS_FINISHED,
	GS_PLUGIN_STATUS_SETUP,
	GS_PLUGIN_STATUS_DOWNLOADING,
	GS_PLUGIN_STATUS_QUERYING,
	GS_PLUGIN_STATUS_INSTALLING,
	GS_PLUGIN_STATUS_REMOVING,
	GS_PLUGIN_STATUS_LAST
} GsPluginStatus;

typedef enum {
	GS_PLUGIN_FLAGS_NONE		= 0,
	GS_PLUGIN_FLAGS_RUNNING_SELF	= 1 << 0,
	GS_PLUGIN_FLAGS_RUNNING_OTHER	= 1 << 1,
	GS_PLUGIN_FLAGS_EXCLUSIVE	= 1 << 2,
	GS_PLUGIN_FLAGS_RECENT		= 1 << 3,
	GS_PLUGIN_FLAGS_LAST
} GsPluginFlags;

typedef enum {
	GS_PLUGIN_ERROR_FAILED,
	GS_PLUGIN_ERROR_NOT_SUPPORTED,
	GS_PLUGIN_ERROR_CANCELLED,
	GS_PLUGIN_ERROR_NO_NETWORK,
	GS_PLUGIN_ERROR_NO_SECURITY,
	GS_PLUGIN_ERROR_NO_SPACE,
	GS_PLUGIN_ERROR_LAST
} GsPluginError;

typedef enum {
	GS_PLUGIN_REFINE_FLAGS_DEFAULT			= 0,
	GS_PLUGIN_REFINE_FLAGS_USE_HISTORY		= 1 << 0,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE		= 1 << 1,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL		= 1 << 2,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION	= 1 << 3,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE		= 1 << 4,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING		= 1 << 5,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION		= 1 << 6,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY		= 1 << 7,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION	= 1 << 8,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS	= 1 << 9,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN		= 1 << 10,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED		= 1 << 11,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH	= 1 << 12,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS		= 1 << 13,
	GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES		= 1 << 14,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_SEVERITY	= 1 << 15,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED	= 1 << 16,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE	= 1 << 17,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS		= 1 << 18,
	GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS	= 1 << 19,
	/*< private >*/
	GS_PLUGIN_REFINE_FLAGS_LAST
} GsPluginRefineFlags;

/**
 * GsPluginRefreshFlags:
 * @GS_PLUGIN_REFRESH_FLAGS_NONE:	Generate new metadata if possible
 * @GS_PLUGIN_REFRESH_FLAGS_METADATA:	Download new metadata
 * @GS_PLUGIN_REFRESH_FLAGS_PAYLOAD:	Download any pending payload
 *
 * The flags used for refresh. Regeneration and downloading is only
 * done if the cache is older than the %cache_age.
 *
 * The %GS_PLUGIN_REFRESH_FLAGS_METADATA can be used to make sure
 * there's enough metadata to start the application.
 * The %GS_PLUGIN_REFRESH_FLAGS_PAYLOAD flag should only be used when
 * the session is idle and bandwidth is unmetered as the amount of data
 * and IO may be large.
 **/
typedef enum {
	GS_PLUGIN_REFRESH_FLAGS_NONE			= 0,
	GS_PLUGIN_REFRESH_FLAGS_METADATA		= 1 << 0,
	GS_PLUGIN_REFRESH_FLAGS_PAYLOAD			= 1 << 1,
	/*< private >*/
	GS_PLUGIN_REFRESH_FLAGS_LAST
} GsPluginRefreshFlags;

/* helpers */
#define	GS_PLUGIN_ERROR					1

/* public getters and setters */
GsPluginData	*gs_plugin_alloc_data			(GsPlugin	*plugin,
							 gsize		 sz);
GsPluginData	*gs_plugin_get_data			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_name			(GsPlugin	*plugin);
gboolean	 gs_plugin_get_enabled			(GsPlugin	*plugin);
void		 gs_plugin_set_enabled			(GsPlugin	*plugin,
							 gboolean	 enabled);
gboolean	 gs_plugin_has_flags			(GsPlugin	*plugin,
							 GsPluginFlags	 flags);
guint		 gs_plugin_get_scale			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_locale			(GsPlugin	*plugin);
AsProfile	*gs_plugin_get_profile			(GsPlugin	*plugin);
SoupSession	*gs_plugin_get_soup_session		(GsPlugin	*plugin);

/* helpers */
GBytes		*gs_plugin_download_data		(GsPlugin	*plugin,
							 GsApp		*app,
							 const gchar	*uri,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_download_file		(GsPlugin	*plugin,
							 GsApp		*app,
							 const gchar	*uri,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_check_distro_id		(GsPlugin	*plugin,
							 const gchar	*distro_id);
GsApp		*gs_plugin_cache_lookup			(GsPlugin	*plugin,
							 const gchar	*key);
void		 gs_plugin_cache_add			(GsPlugin	*plugin,
							 const gchar	*key,
							 GsApp		*app);
void		 gs_plugin_status_update		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginStatus	 status);
gboolean	 gs_plugin_app_launch			(GsPlugin	*plugin,
							 GsApp		*app,
							 GError		**error);
void		 gs_plugin_updates_changed		(GsPlugin	*plugin);
const gchar	*gs_plugin_status_to_string		(GsPluginStatus	 status);

/* vfuncs */
void		 gs_plugin_initialize			(GsPlugin	*plugin);
void		 gs_plugin_destroy			(GsPlugin	*plugin);
void		 gs_plugin_adopt_app			(GsPlugin	*plugin,
							 GsApp		*app);
gboolean	 gs_plugin_add_search			(GsPlugin	*plugin,
							 gchar		**values,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_search_files		(GsPlugin	*plugin,
							 gchar		**values,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_search_what_provides	(GsPlugin	*plugin,
							 gchar		**values,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
const gchar	**gs_plugin_order_after			(GsPlugin	*plugin);
const gchar	**gs_plugin_order_before		(GsPlugin	*plugin);
const gchar	**gs_plugin_get_conflicts		(GsPlugin	*plugin);
gboolean	 gs_plugin_setup			(GsPlugin	*plugin,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_installed		(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_updates			(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_distro_upgrades		(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_sources			(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_updates_historical	(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_categories		(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_category_apps		(GsPlugin	*plugin,
							 GsCategory	*category,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_popular			(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_featured			(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_unvoted_reviews		(GsPlugin	*plugin,
							 GList		**list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_refine			(GsPlugin	*plugin,
							 GList		**list,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_refine_app			(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_launch			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_add_shortcut			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_remove_shortcut		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_update_cancel		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_install			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_remove			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_set_rating		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_update_app			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_upgrade_download		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_app_upgrade_trigger		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_review_submit		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_review_upvote		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_review_downvote		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_review_report		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_review_remove		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_review_dismiss		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_refresh			(GsPlugin	*plugin,
							 guint		 cache_age,
							 GsPluginRefreshFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_file_to_app			(GsPlugin	*plugin,
							 GList		**list,
							 GFile		*file,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_update			(GsPlugin	*plugin,
							 GList		*apps,
							 GCancellable	*cancellable,
							 GError		**error);

G_END_DECLS

#endif /* __GS_PLUGIN_H */

/* vim: set noexpandtab: */
