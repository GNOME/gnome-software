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
#include "gs-auth.h"
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
	void			(*reload)		(GsPlugin	*plugin);
	gpointer		 padding[28];
};

typedef struct	GsPluginData	GsPluginData;

/**
 * GsPluginStatus:
 * @GS_PLUGIN_STATUS_UNKNOWN:		Unknown status
 * @GS_PLUGIN_STATUS_WAITING:		Waiting
 * @GS_PLUGIN_STATUS_FINISHED:		Finished
 * @GS_PLUGIN_STATUS_SETUP:		Setup in progress
 * @GS_PLUGIN_STATUS_DOWNLOADING:	Downloading in progress
 * @GS_PLUGIN_STATUS_QUERYING:		Querying in progress
 * @GS_PLUGIN_STATUS_INSTALLING:	Installing in progress
 * @GS_PLUGIN_STATUS_REMOVING:		Removing in progress
 *
 * The ststus of the plugin.
 **/
typedef enum {
	GS_PLUGIN_STATUS_UNKNOWN,
	GS_PLUGIN_STATUS_WAITING,
	GS_PLUGIN_STATUS_FINISHED,
	GS_PLUGIN_STATUS_SETUP,
	GS_PLUGIN_STATUS_DOWNLOADING,
	GS_PLUGIN_STATUS_QUERYING,
	GS_PLUGIN_STATUS_INSTALLING,
	GS_PLUGIN_STATUS_REMOVING,
	/*< private >*/
	GS_PLUGIN_STATUS_LAST
} GsPluginStatus;

/**
 * GsPluginFlags:
 * @GS_PLUGIN_FLAGS_NONE:		No flags set
 * @GS_PLUGIN_FLAGS_RUNNING_SELF:	The plugin is running
 * @GS_PLUGIN_FLAGS_RUNNING_OTHER:	Another plugin is running
 * @GS_PLUGIN_FLAGS_EXCLUSIVE:		An exclusive action is running
 * @GS_PLUGIN_FLAGS_RECENT:		This plugin recently ran
 * @GS_PLUGIN_FLAGS_GLOBAL_CACHE:	Use the global app cache
 *
 * The flags for the plugin at this point in time.
 **/
#define GS_PLUGIN_FLAGS_NONE		(0u)
#define GS_PLUGIN_FLAGS_RUNNING_SELF	(1u << 0)
#define GS_PLUGIN_FLAGS_RUNNING_OTHER	(1u << 1)
#define GS_PLUGIN_FLAGS_EXCLUSIVE	(1u << 2)
#define GS_PLUGIN_FLAGS_RECENT		(1u << 3)
#define GS_PLUGIN_FLAGS_GLOBAL_CACHE	(1u << 4)
typedef guint64 GsPluginFlags;

/**
 * GsPluginError:
 * @GS_PLUGIN_ERROR_FAILED:			Generic failure
 * @GS_PLUGIN_ERROR_NOT_SUPPORTED:		Action not supported
 * @GS_PLUGIN_ERROR_CANCELLED:			Action was cancelled
 * @GS_PLUGIN_ERROR_NO_NETWORK:			No network connection available
 * @GS_PLUGIN_ERROR_NO_SECURITY:		Security policy forbid action
 * @GS_PLUGIN_ERROR_NO_SPACE:			No disk space to allow action
 * @GS_PLUGIN_ERROR_AUTH_REQUIRED:		Authentication was required
 * @GS_PLUGIN_ERROR_AUTH_INVALID:		Provided authentication was invalid
 * @GS_PLUGIN_ERROR_PIN_REQUIRED:		PIN required for authentication
 * @GS_PLUGIN_ERROR_ACCOUNT_SUSPENDED:		User account has been suspended
 * @GS_PLUGIN_ERROR_ACCOUNT_DEACTIVATED:	User account has been deactivated
 *
 * The failure error types.
 **/
typedef enum {
	GS_PLUGIN_ERROR_FAILED,
	GS_PLUGIN_ERROR_NOT_SUPPORTED,
	GS_PLUGIN_ERROR_CANCELLED,
	GS_PLUGIN_ERROR_NO_NETWORK,
	GS_PLUGIN_ERROR_NO_SECURITY,
	GS_PLUGIN_ERROR_NO_SPACE,
	GS_PLUGIN_ERROR_AUTH_REQUIRED,
	GS_PLUGIN_ERROR_AUTH_INVALID,
	GS_PLUGIN_ERROR_PIN_REQUIRED,
	GS_PLUGIN_ERROR_ACCOUNT_SUSPENDED,
	GS_PLUGIN_ERROR_ACCOUNT_DEACTIVATED,
	/*< private >*/
	GS_PLUGIN_ERROR_LAST
} GsPluginError;

/**
 * GsPluginRefineFlags:
 * @GS_PLUGIN_REFINE_FLAGS_DEFAULT:			No explicit flags set
 * @GS_PLUGIN_REFINE_FLAGS_USE_HISTORY:			Get the historical view
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE:		Require the license
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL:			Require the URL
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION:		Require the long description
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE:		Require the installed and download sizes
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING:		Require the rating
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION:		Require the version
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY:		Require the history
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION:	Require enough to install or remove the package
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS:	Require update details
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN:		Require the origin
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED:		Require related packages
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH:		Require the menu path
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS:		Require available addons
 * @GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES:		Allow packages to be returned
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_SEVERITY:	Require update severity
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED:	Require distro upgrades
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE:		Require the provenance
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS:		Require user-reviews
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS:	Require user-ratings
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_KEY_COLORS:		Require the key colors
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON:		Require the icon to be loaded
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS:		Require the needed permissions
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME:	Require the origin hostname
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_UI:		Require the origin for UI
 *
 * The refine flags.
 **/
#define GS_PLUGIN_REFINE_FLAGS_DEFAULT			(0u)
#define GS_PLUGIN_REFINE_FLAGS_USE_HISTORY		(1u << 0)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE		(1u << 1)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL		(1u << 2)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION	(1u << 3)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE		(1u << 4)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING		(1u << 5)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION		(1u << 6)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY		(1u << 7)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION	(1u << 8)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS	(1u << 9)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN		(1u << 10)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED		(1u << 11)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH	(1u << 12)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS		(1u << 13)
#define GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES		(1u << 14)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_SEVERITY	(1u << 15)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED	(1u << 16)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE	(1u << 17)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS		(1u << 18)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS	(1u << 19)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_KEY_COLORS	(1u << 20)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON		(1u << 21)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS	(1u << 22)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME	(1u << 23)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_UI	(1u << 24)
typedef guint64 GsPluginRefineFlags;

/**
 * GsPluginRefreshFlags:
 * @GS_PLUGIN_REFRESH_FLAGS_NONE:	Generate new metadata if possible
 * @GS_PLUGIN_REFRESH_FLAGS_METADATA:	Download new metadata
 * @GS_PLUGIN_REFRESH_FLAGS_PAYLOAD:	Download any pending payload
 * @GS_PLUGIN_REFRESH_FLAGS_INTERACTIVE: Running by user request
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
	GS_PLUGIN_REFRESH_FLAGS_INTERACTIVE		= 1 << 2,
	/*< private >*/
	GS_PLUGIN_REFRESH_FLAGS_LAST
} GsPluginRefreshFlags;

/**
 * GsPluginRule:
 * @GS_PLUGIN_RULE_CONFLICTS:		The plugin conflicts with another
 * @GS_PLUGIN_RULE_RUN_AFTER:		Order the plugin after another
 * @GS_PLUGIN_RULE_RUN_BEFORE:		Order the plugin before another
 * @GS_PLUGIN_RULE_BETTER_THAN:		Results are better than another
 *
 * The rules used for ordering plugins.
 * Plugins are expected to add rules in gs_plugin_initialize().
 **/
typedef enum {
	GS_PLUGIN_RULE_CONFLICTS,
	GS_PLUGIN_RULE_RUN_AFTER,
	GS_PLUGIN_RULE_RUN_BEFORE,
	GS_PLUGIN_RULE_BETTER_THAN,
	/*< private >*/
	GS_PLUGIN_RULE_LAST
} GsPluginRule;

/* helpers */
#define	GS_PLUGIN_ERROR					gs_plugin_error_quark ()

GQuark		 gs_plugin_error_quark			(void);

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
void		 gs_plugin_add_flags			(GsPlugin	*plugin,
							 GsPluginFlags	 flags);
void		 gs_plugin_remove_flags			(GsPlugin	*plugin,
							 GsPluginFlags	 flags);
guint		 gs_plugin_get_scale			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_locale			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_language			(GsPlugin	*plugin);
AsProfile	*gs_plugin_get_profile			(GsPlugin	*plugin);
SoupSession	*gs_plugin_get_soup_session		(GsPlugin	*plugin);
void		 gs_plugin_add_auth			(GsPlugin	*plugin,
							 GsAuth		*auth);
GsAuth		*gs_plugin_get_auth_by_id		(GsPlugin	*plugin,
							 const gchar	*provider_id);
void		 gs_plugin_add_rule			(GsPlugin	*plugin,
							 GsPluginRule	 rule,
							 const gchar	*name);

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
void		 gs_plugin_cache_invalidate		(GsPlugin	*plugin);
void		 gs_plugin_status_update		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginStatus	 status);
gboolean	 gs_plugin_app_launch			(GsPlugin	*plugin,
							 GsApp		*app,
							 GError		**error);
void		 gs_plugin_updates_changed		(GsPlugin	*plugin);
void		 gs_plugin_reload			(GsPlugin	*plugin);
const gchar	*gs_plugin_status_to_string		(GsPluginStatus	 status);

G_END_DECLS

#endif /* __GS_PLUGIN_H */

/* vim: set noexpandtab: */
