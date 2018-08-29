/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2018 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_PLUGIN_TYPES_H
#define __GS_PLUGIN_TYPES_H

#include <glib-object.h>

G_BEGIN_DECLS

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
 * @GS_PLUGIN_FLAGS_INTERACTIVE:	User initiated the job
 *
 * The flags for the plugin at this point in time.
 **/
#define GS_PLUGIN_FLAGS_NONE		(0u)
#define GS_PLUGIN_FLAGS_RUNNING_SELF	(1u << 0)
#define GS_PLUGIN_FLAGS_RUNNING_OTHER	(1u << 1)
#define GS_PLUGIN_FLAGS_EXCLUSIVE	(1u << 2)
#define GS_PLUGIN_FLAGS_RECENT		(1u << 3)
#define GS_PLUGIN_FLAGS_INTERACTIVE	(1u << 4)
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
 * @GS_PLUGIN_ERROR_PLUGIN_DEPSOLVE_FAILED:	The plugins installed are incompatible
 * @GS_PLUGIN_ERROR_DOWNLOAD_FAILED:		The download action failed
 * @GS_PLUGIN_ERROR_WRITE_FAILED:		The save-to-disk failed
 * @GS_PLUGIN_ERROR_INVALID_FORMAT:		The data format is invalid
 * @GS_PLUGIN_ERROR_DELETE_FAILED:		The delete action failed
 * @GS_PLUGIN_ERROR_RESTART_REQUIRED:		A restart is required
 * @GS_PLUGIN_ERROR_AC_POWER_REQUIRED:		AC power is required
 * @GS_PLUGIN_ERROR_TIMED_OUT:			The job timed out
 * @GS_PLUGIN_ERROR_PURCHASE_NOT_SETUP:		Purchase support not setup
 * @GS_PLUGIN_ERROR_PURCHASE_DECLINED:		Purchase was declined
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
	GS_PLUGIN_ERROR_PLUGIN_DEPSOLVE_FAILED,
	GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
	GS_PLUGIN_ERROR_WRITE_FAILED,
	GS_PLUGIN_ERROR_INVALID_FORMAT,
	GS_PLUGIN_ERROR_DELETE_FAILED,
	GS_PLUGIN_ERROR_RESTART_REQUIRED,
	GS_PLUGIN_ERROR_AC_POWER_REQUIRED,
	GS_PLUGIN_ERROR_TIMED_OUT,
	GS_PLUGIN_ERROR_PURCHASE_NOT_SETUP,
	GS_PLUGIN_ERROR_PURCHASE_DECLINED,
	/*< private >*/
	GS_PLUGIN_ERROR_LAST
} GsPluginError;

/**
 * GsPluginRefineFlags:
 * @GS_PLUGIN_REFINE_FLAGS_DEFAULT:			No explicit flags set
 * @GS_PLUGIN_REFINE_FLAGS_USE_HISTORY:			Get the historical view (unused)
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
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME:		Require the runtime
 * @GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS:		Require screenshot information
 *
 * The refine flags.
 **/
#define GS_PLUGIN_REFINE_FLAGS_DEFAULT			((guint64) 0)
#define GS_PLUGIN_REFINE_FLAGS_USE_HISTORY		((guint64) 1 << 0) /* unused, TODO: perhaps ->STATE */
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE		((guint64) 1 << 1)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL		((guint64) 1 << 2)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION	((guint64) 1 << 3)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE		((guint64) 1 << 4)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING		((guint64) 1 << 5)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION		((guint64) 1 << 6)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY		((guint64) 1 << 7)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION	((guint64) 1 << 8)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS	((guint64) 1 << 9)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN		((guint64) 1 << 10)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED		((guint64) 1 << 11)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH	((guint64) 1 << 12)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS		((guint64) 1 << 13)
#define GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES		((guint64) 1 << 14) /* TODO: move to request */
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_SEVERITY	((guint64) 1 << 15)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPGRADE_REMOVED	((guint64) 1 << 16)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROVENANCE	((guint64) 1 << 17)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS		((guint64) 1 << 18)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS	((guint64) 1 << 19)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_KEY_COLORS	((guint64) 1 << 20)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON		((guint64) 1 << 21)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS	((guint64) 1 << 22)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME	((guint64) 1 << 23)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_UI	((guint64) 1 << 24)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_RUNTIME		((guint64) 1 << 25)
#define GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS	((guint64) 1 << 26)
typedef guint64 GsPluginRefineFlags;

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

/**
 * GsPluginAction:
 * @GS_PLUGIN_ACTION_UNKNOWN:			Action is unknown
 * @GS_PLUGIN_ACTION_SETUP:			Plugin setup (internal)
 * @GS_PLUGIN_ACTION_INSTALL:			Install an application
 * @GS_PLUGIN_ACTION_REMOVE:			Remove an application
 * @GS_PLUGIN_ACTION_UPDATE:			Update an application
 * @GS_PLUGIN_ACTION_SET_RATING:		Set rating on an application
 * @GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD:		Download a distro upgrade
 * @GS_PLUGIN_ACTION_UPGRADE_TRIGGER:		Trigger a distro upgrade
 * @GS_PLUGIN_ACTION_LAUNCH:			Launch an application
 * @GS_PLUGIN_ACTION_UPDATE_CANCEL:		Cancel the update
 * @GS_PLUGIN_ACTION_ADD_SHORTCUT:		Add a shortcut to an application
 * @GS_PLUGIN_ACTION_REMOVE_SHORTCUT:		Remove a shortcut to an application
 * @GS_PLUGIN_ACTION_REVIEW_SUBMIT:		Submit a new review
 * @GS_PLUGIN_ACTION_REVIEW_UPVOTE:		Upvote an existing review
 * @GS_PLUGIN_ACTION_REVIEW_DOWNVOTE:		Downvote an existing review
 * @GS_PLUGIN_ACTION_REVIEW_REPORT:		Report an existing review
 * @GS_PLUGIN_ACTION_REVIEW_REMOVE:		Remove a review written by the user
 * @GS_PLUGIN_ACTION_REVIEW_DISMISS:		Dismiss (ignore) a review when moderating
 * @GS_PLUGIN_ACTION_GET_UPDATES:		Get the list of updates
 * @GS_PLUGIN_ACTION_GET_DISTRO_UPDATES:	Get the list of distro updates
 * @GS_PLUGIN_ACTION_GET_UNVOTED_REVIEWS:	Get the list of moderatable reviews
 * @GS_PLUGIN_ACTION_GET_SOURCES:		Get the list of sources
 * @GS_PLUGIN_ACTION_GET_INSTALLED:		Get the list of installed applications
 * @GS_PLUGIN_ACTION_GET_POPULAR:		Get the list of popular applications
 * @GS_PLUGIN_ACTION_GET_FEATURED:		Get the list of featured applications
 * @GS_PLUGIN_ACTION_SEARCH:			Get the search results for a query
 * @GS_PLUGIN_ACTION_SEARCH_FILES:		Get the search results for a file query
 * @GS_PLUGIN_ACTION_SEARCH_PROVIDES:		Get the search results for a provide query
 * @GS_PLUGIN_ACTION_GET_CATEGORIES:		Get the list of categories
 * @GS_PLUGIN_ACTION_GET_CATEGORY_APPS:		Get the apps for a specific category
 * @GS_PLUGIN_ACTION_REFINE:			Refine the application
 * @GS_PLUGIN_ACTION_REFRESH:			Refresh all the sources
 * @GS_PLUGIN_ACTION_FILE_TO_APP:		Convert the file to an application
 * @GS_PLUGIN_ACTION_AUTH_LOGIN:		Authentication login action
 * @GS_PLUGIN_ACTION_AUTH_LOGOUT:		Authentication logout action
 * @GS_PLUGIN_ACTION_AUTH_REGISTER:		Authentication register action
 * @GS_PLUGIN_ACTION_AUTH_LOST_PASSWORD:	Authentication lost password action
 * @GS_PLUGIN_ACTION_URL_TO_APP:		Convert the file to an application
 * @GS_PLUGIN_ACTION_GET_RECENT:		Get the apps recently released
 * @GS_PLUGIN_ACTION_GET_UPDATES_HISTORICAL:    Get the list of historical updates
 * @GS_PLUGIN_ACTION_INITIALIZE:		Initialize the plugin
 * @GS_PLUGIN_ACTION_DESTROY:			Destroy the plugin
 * @GS_PLUGIN_ACTION_PURCHASE:			Purchase an app
 * @GS_PLUGIN_ACTION_DOWNLOAD:			Download an application
 *
 * The plugin action.
 **/
typedef enum {
	GS_PLUGIN_ACTION_UNKNOWN,
	GS_PLUGIN_ACTION_SETUP,
	GS_PLUGIN_ACTION_INSTALL,
	GS_PLUGIN_ACTION_REMOVE,
	GS_PLUGIN_ACTION_UPDATE,
	GS_PLUGIN_ACTION_SET_RATING,
	GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD,
	GS_PLUGIN_ACTION_UPGRADE_TRIGGER,
	GS_PLUGIN_ACTION_LAUNCH,
	GS_PLUGIN_ACTION_UPDATE_CANCEL,
	GS_PLUGIN_ACTION_ADD_SHORTCUT,
	GS_PLUGIN_ACTION_REMOVE_SHORTCUT,
	GS_PLUGIN_ACTION_REVIEW_SUBMIT,
	GS_PLUGIN_ACTION_REVIEW_UPVOTE,
	GS_PLUGIN_ACTION_REVIEW_DOWNVOTE,
	GS_PLUGIN_ACTION_REVIEW_REPORT,
	GS_PLUGIN_ACTION_REVIEW_REMOVE,
	GS_PLUGIN_ACTION_REVIEW_DISMISS,
	GS_PLUGIN_ACTION_GET_UPDATES,
	GS_PLUGIN_ACTION_GET_DISTRO_UPDATES,
	GS_PLUGIN_ACTION_GET_UNVOTED_REVIEWS,
	GS_PLUGIN_ACTION_GET_SOURCES,
	GS_PLUGIN_ACTION_GET_INSTALLED,
	GS_PLUGIN_ACTION_GET_POPULAR,
	GS_PLUGIN_ACTION_GET_FEATURED,
	GS_PLUGIN_ACTION_SEARCH,
	GS_PLUGIN_ACTION_SEARCH_FILES,
	GS_PLUGIN_ACTION_SEARCH_PROVIDES,
	GS_PLUGIN_ACTION_GET_CATEGORIES,
	GS_PLUGIN_ACTION_GET_CATEGORY_APPS,
	GS_PLUGIN_ACTION_REFINE,
	GS_PLUGIN_ACTION_REFRESH,
	GS_PLUGIN_ACTION_FILE_TO_APP,
	GS_PLUGIN_ACTION_AUTH_LOGIN,
	GS_PLUGIN_ACTION_AUTH_LOGOUT,
	GS_PLUGIN_ACTION_AUTH_REGISTER,
	GS_PLUGIN_ACTION_AUTH_LOST_PASSWORD,
	GS_PLUGIN_ACTION_URL_TO_APP,
	GS_PLUGIN_ACTION_GET_RECENT,
	GS_PLUGIN_ACTION_GET_UPDATES_HISTORICAL,
	GS_PLUGIN_ACTION_INITIALIZE,
	GS_PLUGIN_ACTION_DESTROY,
	GS_PLUGIN_ACTION_PURCHASE,
	GS_PLUGIN_ACTION_DOWNLOAD,
	/*< private >*/
	GS_PLUGIN_ACTION_LAST
} GsPluginAction;

G_END_DECLS

#endif /* __GS_PLUGIN_TYPES_H */

/* vim: set noexpandtab: */
