/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2012-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

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
 * @GS_PLUGIN_ERROR_PLUGIN_DEPSOLVE_FAILED:	The plugins installed are incompatible
 * @GS_PLUGIN_ERROR_DOWNLOAD_FAILED:		The download action failed
 * @GS_PLUGIN_ERROR_WRITE_FAILED:		The save-to-disk failed
 * @GS_PLUGIN_ERROR_INVALID_FORMAT:		The data format is invalid
 * @GS_PLUGIN_ERROR_DELETE_FAILED:		The delete action failed
 * @GS_PLUGIN_ERROR_RESTART_REQUIRED:		A restart is required
 * @GS_PLUGIN_ERROR_AC_POWER_REQUIRED:		AC power is required
 * @GS_PLUGIN_ERROR_TIMED_OUT:			The job timed out
 * @GS_PLUGIN_ERROR_BATTERY_LEVEL_TOO_LOW:	The system battery level is too low
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
	GS_PLUGIN_ERROR_PLUGIN_DEPSOLVE_FAILED,
	GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
	GS_PLUGIN_ERROR_WRITE_FAILED,
	GS_PLUGIN_ERROR_INVALID_FORMAT,
	GS_PLUGIN_ERROR_DELETE_FAILED,
	GS_PLUGIN_ERROR_RESTART_REQUIRED,
	GS_PLUGIN_ERROR_AC_POWER_REQUIRED,
	GS_PLUGIN_ERROR_TIMED_OUT,
	GS_PLUGIN_ERROR_BATTERY_LEVEL_TOO_LOW,
	GS_PLUGIN_ERROR_LAST  /*< skip >*/
} GsPluginError;

/**
 * GsPluginRefineFlags:
 * @GS_PLUGIN_REFINE_FLAGS_NONE: No explicit flags set
 * @GS_PLUGIN_REFINE_FLAGS_INTERACTIVE: User initiated the job
 * @GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES: Allow packages to be returned
 * @GS_PLUGIN_REFINE_FLAGS_DISABLE_FILTERING: Normally the results of a refine are
 *   filtered to remove non-valid apps; if this flag is set, that won’t happen.
 *   This is intended to be used by internal #GsPluginLoader code.
 *
 * Flags for an operation to refine apps.
 *
 * See #GsPluginRefineRequireFlags for the flags which specify which properties
 * to refine on each app.
 *
 * Since: 49
 */
typedef enum {
	GS_PLUGIN_REFINE_FLAGS_NONE			= 0,
	GS_PLUGIN_REFINE_FLAGS_INTERACTIVE		= 1 << 0,
	GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES		= 1 << 1,
	GS_PLUGIN_REFINE_FLAGS_DISABLE_FILTERING	= 1 << 2,
} GsPluginRefineFlags;

/**
 * GsPluginRefineRequireFlags:
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE:		No explicit flags set
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_ID:			Require the app’s ID; this is the minimum possible requirement
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_LICENSE:		Require the license
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_URL:			Require the URL
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_DESCRIPTION:		Require the long description
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE:		Require the installed and download sizes
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_RATING:		Require the rating
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_VERSION:		Require the version
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_HISTORY:		Require the history
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION:	Require enough to install or remove the package
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_DETAILS:	Require update details
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN:		Require the origin
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_RELATED:		Require related packages
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_ADDONS:		Require available addons
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_SEVERITY:	Require update severity
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPGRADE_REMOVED:	Require distro upgrades
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_PROVENANCE:		Require the provenance
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEWS:		Require user-reviews
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEW_RATINGS:	Require user-ratings
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_ICON:		Require the icon to be loaded
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_PERMISSIONS:		Require the needed permissions
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN_HOSTNAME:	Require the origin hostname
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN_UI:		Require the origin for UI
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_RUNTIME:		Require the runtime
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_SCREENSHOTS:		Require screenshot information
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_CATEGORIES:		Require categories
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_PROJECT_GROUP:	Require project group
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_DEVELOPER_NAME:	Require developer name
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_KUDOS:		Require kudos
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE_DATA:		Require user and cache data sizes
 * @GS_PLUGIN_REFINE_REQUIRE_FLAGS_MASK:		All flags
 *
 * Flags specifying which pieces of data to refine on a #GsApp.
 *
 * See #GsPluginRefineFlags for flags affecting the behaviour of the refine job
 * as a whole.
 *
 * Since: 49
 */
typedef enum {
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_NONE		= 0U,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_ID		= 1U << 0,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_LICENSE		= 1U << 1,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_URL		= 1U << 2,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_DESCRIPTION	= 1U << 3,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE		= 1U << 4,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_RATING		= 1U << 5,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_VERSION		= 1U << 6,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_HISTORY		= 1U << 7,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_SETUP_ACTION	= 1U << 8,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_DETAILS	= 1U << 9,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN		= 1U << 10,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_RELATED		= 1U << 11,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE_DATA	= 1U << 12,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_ADDONS		= 1U << 13,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPDATE_SEVERITY	= 1U << 14,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_UPGRADE_REMOVED	= 1U << 15,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_PROVENANCE	= 1U << 16,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEWS		= 1U << 17,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEW_RATINGS	= 1U << 18,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_ICON		= 1U << 19,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_PERMISSIONS	= 1U << 20,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN_HOSTNAME	= 1U << 21,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_ORIGIN_UI	= 1U << 22,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_RUNTIME		= 1U << 23,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_SCREENSHOTS	= 1U << 24,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_CATEGORIES	= 1U << 25,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_PROJECT_GROUP	= 1U << 26,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_DEVELOPER_NAME	= 1U << 27,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_KUDOS		= 1U << 28,
	GS_PLUGIN_REFINE_REQUIRE_FLAGS_MASK		= ~0U,
} GsPluginRefineRequireFlags;

/**
 * GsPluginListAppsFlags:
 * @GS_PLUGIN_LIST_APPS_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE: User initiated the job.
 *
 * Flags for an operation to list apps matching a given query.
 *
 * Since: 43
 */
typedef enum {
	GS_PLUGIN_LIST_APPS_FLAGS_NONE = 0,
	GS_PLUGIN_LIST_APPS_FLAGS_INTERACTIVE = 1 << 0,
} GsPluginListAppsFlags;

/**
 * GsPluginRefineCategoriesFlags:
 * @GS_PLUGIN_REFINE_CATEGORIES_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_REFINE_CATEGORIES_FLAGS_INTERACTIVE: User initiated the job.
 * @GS_PLUGIN_REFINE_CATEGORIES_FLAGS_SIZE: Work out the number of apps in each category.
 *
 * Flags for an operation to refine categories.
 *
 * Since: 43
 */
typedef enum {
	GS_PLUGIN_REFINE_CATEGORIES_FLAGS_NONE = 0,
	GS_PLUGIN_REFINE_CATEGORIES_FLAGS_INTERACTIVE = 1 << 0,
	GS_PLUGIN_REFINE_CATEGORIES_FLAGS_SIZE = 1 << 1,
} GsPluginRefineCategoriesFlags;

/**
 * GsPluginRefreshMetadataFlags:
 * @GS_PLUGIN_REFRESH_METADATA_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE: User initiated the job.
 *
 * Flags for an operation to refresh metadata.
 *
 * Since: 42
 */
typedef enum {
	GS_PLUGIN_REFRESH_METADATA_FLAGS_NONE = 0,
	GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE = 1 << 0,
} GsPluginRefreshMetadataFlags;

/**
 * GsPluginListDistroUpgradesFlags:
 * @GS_PLUGIN_LIST_DISTRO_UPGRADES_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_LIST_DISTRO_UPGRADES_FLAGS_INTERACTIVE: User initiated the job.
 *
 * Flags for an operation to list available distro upgrades.
 *
 * Since: 42
 */
typedef enum {
	GS_PLUGIN_LIST_DISTRO_UPGRADES_FLAGS_NONE = 0,
	GS_PLUGIN_LIST_DISTRO_UPGRADES_FLAGS_INTERACTIVE = 1 << 0,
} GsPluginListDistroUpgradesFlags;

/**
 * GsPluginManageRepositoryFlags:
 * @GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE: User initiated the job.
 * @GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INSTALL: Install the repository.
 * @GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_REMOVE: Remove the repository.
 * @GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_ENABLE: Enable the repository.
 * @GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_DISABLE: Disable the repository.
 *
 * Flags for an operation on a repository.
 *
 * Since: 42
 */
typedef enum {
	GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_NONE		= 0,
	GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE	= 1 << 0,
	GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INSTALL	= 1 << 1,
	GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_REMOVE	= 1 << 2,
	GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_ENABLE	= 1 << 3,
	GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_DISABLE	= 1 << 4,
} GsPluginManageRepositoryFlags;

/**
 * GsPluginInstallAppsFlags:
 * @GS_PLUGIN_INSTALL_APPS_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE: User initiated the job.
 * @GS_PLUGIN_INSTALL_APPS_FLAGS_NO_DOWNLOAD: Only use locally cached resources,
 *   and error if they don’t exist.
 * @GS_PLUGIN_INSTALL_APPS_FLAGS_NO_APPLY: Only download the resources, and don’t
 *   do the installation.
 *
 * Flags for an operation to download or install apps.
 *
 * Since: 47
 */
typedef enum {
	GS_PLUGIN_INSTALL_APPS_FLAGS_NONE = 0,
	GS_PLUGIN_INSTALL_APPS_FLAGS_INTERACTIVE = 1 << 0,
	GS_PLUGIN_INSTALL_APPS_FLAGS_NO_DOWNLOAD = 1 << 1,
	GS_PLUGIN_INSTALL_APPS_FLAGS_NO_APPLY = 1 << 2,
} GsPluginInstallAppsFlags;

/**
 * GsPluginUninstallAppsFlags:
 * @GS_PLUGIN_UNINSTALL_APPS_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_UNINSTALL_APPS_FLAGS_INTERACTIVE: User initiated the job.
 *
 * Flags for an operation to uninstall apps.
 *
 * Since: 47
 */
typedef enum {
	GS_PLUGIN_UNINSTALL_APPS_FLAGS_NONE = 0,
	GS_PLUGIN_UNINSTALL_APPS_FLAGS_INTERACTIVE = 1 << 0,
} GsPluginUninstallAppsFlags;

/**
 * GsPluginUpdateAppsFlags:
 * @GS_PLUGIN_UPDATE_APPS_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE: User initiated the job.
 * @GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD: Only use locally cached resources,
 *   and error if they don’t exist.
 * @GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY: Only download the resources, and don’t
 *   apply the updates.
 *
 * Flags for an operation to download or update apps.
 *
 * Since: 44
 */
typedef enum {
	GS_PLUGIN_UPDATE_APPS_FLAGS_NONE = 0,
	GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE = 1 << 0,
	GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD = 1 << 1,
	GS_PLUGIN_UPDATE_APPS_FLAGS_NO_APPLY = 1 << 2,
} GsPluginUpdateAppsFlags;

/**
 * GsPluginCancelOfflineUpdateFlags:
 * @GS_PLUGIN_CANCEL_OFFLINE_UPDATE_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_CANCEL_OFFLINE_UPDATE_FLAGS_INTERACTIVE: User initiated the job.
 *
 * Flags for an operation to cancel a pending offline update.
 *
 * Since: 47
 */
typedef enum {
	GS_PLUGIN_CANCEL_OFFLINE_UPDATE_FLAGS_NONE		= 0,
	GS_PLUGIN_CANCEL_OFFLINE_UPDATE_FLAGS_INTERACTIVE	= 1 << 0,
} GsPluginCancelOfflineUpdateFlags;

/**
 * GsPluginDownloadUpgradeFlags:
 * @GS_PLUGIN_DOWNLOAD_UPGRADE_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_DOWNLOAD_UPGRADE_FLAGS_INTERACTIVE: User initiated the job.
 *
 * Flags for an operation to download an upgrade.
 *
 * Since: 47
 */
typedef enum {
	GS_PLUGIN_DOWNLOAD_UPGRADE_FLAGS_NONE		= 0,
	GS_PLUGIN_DOWNLOAD_UPGRADE_FLAGS_INTERACTIVE	= 1 << 0,
} GsPluginDownloadUpgradeFlags;

/**
 * GsPluginTriggerUpgradeFlags:
 * @GS_PLUGIN_TRIGGER_UPGRADE_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_TRIGGER_UPGRADE_FLAGS_INTERACTIVE: User initiated the job.
 *
 * Flags for an operation to trigger an upgrade.
 *
 * Since: 47
 */
typedef enum {
	GS_PLUGIN_TRIGGER_UPGRADE_FLAGS_NONE		= 0,
	GS_PLUGIN_TRIGGER_UPGRADE_FLAGS_INTERACTIVE	= 1 << 0,
} GsPluginTriggerUpgradeFlags;

/**
 * GsPluginLaunchFlags:
 * @GS_PLUGIN_LAUNCH_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_LAUNCH_FLAGS_INTERACTIVE: User initiated the job.
 *
 * Flags for a launch operation.
 *
 * Since: 47
 */
typedef enum {
	GS_PLUGIN_LAUNCH_FLAGS_NONE		= 0,
	GS_PLUGIN_LAUNCH_FLAGS_INTERACTIVE	= 1 << 0,
} GsPluginLaunchFlags;

/**
 * GsPluginFileToAppFlags:
 * @GS_PLUGIN_FILE_TO_APP_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_FILE_TO_APP_FLAGS_INTERACTIVE: User initiated the job.
 *
 * Flags for a file-to-app operation.
 *
 * Since: 47
 */
typedef enum {
	GS_PLUGIN_FILE_TO_APP_FLAGS_NONE	= 0,
	GS_PLUGIN_FILE_TO_APP_FLAGS_INTERACTIVE	= 1 << 0,
} GsPluginFileToAppFlags;

/**
 * GsPluginUrlToAppFlags:
 * @GS_PLUGIN_URL_TO_APP_FLAGS_NONE: No flags set.
 * @GS_PLUGIN_URL_TO_APP_FLAGS_INTERACTIVE: User initiated the job.
 * @GS_PLUGIN_URL_TO_APP_FLAGS_ALLOW_PACKAGES: Allow unconverted generic
 *   packages to be returned. This is equivalent to passing
 *   %GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES to the internal refine job. (Since: 49)
 *
 * Flags for a url-to-app operation.
 *
 * Since: 47
 */
typedef enum {
	GS_PLUGIN_URL_TO_APP_FLAGS_NONE	= 0,
	GS_PLUGIN_URL_TO_APP_FLAGS_INTERACTIVE	= 1 << 0,
	GS_PLUGIN_URL_TO_APP_FLAGS_ALLOW_PACKAGES = 1 << 1,
} GsPluginUrlToAppFlags;

/**
 * GsPluginProgressCallback:
 * @plugin: the #GsPlugin reporting its progress
 * @progress: the percentage completion (0–100 inclusive), or
 *   %GS_APP_PROGRESS_UNKNOWN for unknown
 * @user_data: user data passed to the calling function
 *
 * Callback to report the progress of a particular @plugin through a particular
 * operation.
 *
 * Since: 44
 */
typedef void (* GsPluginProgressCallback)		(GsPlugin	*plugin,
							 guint		 progress,
							 gpointer	 user_data);

typedef struct _GsPluginEvent GsPluginEvent;

/**
 * GsPluginEventCallback:
 * @plugin: the #GsPlugin reporting an event
 * @event: the event being reported
 * @user_data: user data passed to the calling function
 *
 * Callback to report an event from a particular @plugin through a particular
 * operation.
 *
 * Typically these events will be errors to potentially show to the user.
 *
 * Since: 49
 */
typedef void (* GsPluginEventCallback)			(GsPlugin	*plugin,
							 GsPluginEvent	*event,
							 void		*user_data);

/**
 * GsPluginAppNeedsUserActionCallback:
 * @plugin: the #GsPlugin asking for user action
 * @app: (nullable) (transfer none): the related #GsApp, or %NULL if no app is
 *   explicitly related to the necessary user action
 * @action_screenshot: (nullable) (transfer none): a screenshot (with caption
 *   set) which gives the user instructions about what action to take, or %NULL
 *   if no instructions are available
 * @user_data: user data passed to the calling function
 *
 * Callback to ask the user to perform a physical action during a plugin
 * operation.
 *
 * This will typically be something like unplugging and reconnecting a hardware
 * device, and instructions will be given via @action_screenshot.
 *
 * Since: 44
 */
typedef void (* GsPluginAppNeedsUserActionCallback)	(GsPlugin	*plugin,
							 GsApp		*app,
							 AsScreenshot	*action_screenshot,
							 gpointer	 user_data);

/**
 * GsPluginRule:
 * @GS_PLUGIN_RULE_CONFLICTS:		The plugin conflicts with another
 * @GS_PLUGIN_RULE_RUN_AFTER:		Order the plugin after another
 * @GS_PLUGIN_RULE_RUN_BEFORE:		Order the plugin before another
 * @GS_PLUGIN_RULE_BETTER_THAN:		Results are better than another
 *
 * The rules used for ordering plugins.
 * Plugins are expected to add rules in the init function for their #GsPlugin
 * subclass.
 **/
typedef enum {
	GS_PLUGIN_RULE_CONFLICTS,
	GS_PLUGIN_RULE_RUN_AFTER,
	GS_PLUGIN_RULE_RUN_BEFORE,
	GS_PLUGIN_RULE_BETTER_THAN,
	GS_PLUGIN_RULE_LAST  /*< skip >*/
} GsPluginRule;

G_END_DECLS
