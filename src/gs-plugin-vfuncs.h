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


/**
 * SECTION:gs-plugin-vfuncs
 * @title: GsPlugin Exports
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: Vfuncs that plugins can implement
 */

#ifndef __GS_PLUGIN_VFUNCS_H
#define __GS_PLUGIN_VFUNCS_H

#include <appstream-glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <gio/gio.h>
#include <libsoup/soup.h>

#include "gs-app.h"
#include "gs-app-list.h"
#include "gs-category.h"

G_BEGIN_DECLS

/**
 * gs_plugin_initialize:
 * @plugin: a #GsPlugin
 *
 * Checks if the plugin should run, and if initializes it. If the plugin should
 * not be run then gs_plugin_set_enabled() should be called.
 * This is also the place to call gs_plugin_alloc_data() if private data is
 * required for the plugin.
 *
 * NOTE: Do not do any failable actions in this function; use gs_plugin_setup()
 * instead.
 **/
void		 gs_plugin_initialize			(GsPlugin	*plugin);

/**
 * gs_plugin_destroy:
 * @plugin: a #GsPlugin
 *
 * Called when the plugin should destroy any private data.
 **/
void		 gs_plugin_destroy			(GsPlugin	*plugin);

/**
 * gs_plugin_adopt_app:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 *
 * Called when an #GsApp has not been claimed (i.e. a management plugin has not
 * been set).
 *
 * A claimed application means other plugins will not try to perform actions
 * such as install, remove or update. Most applications are claimed when they
 * are created.
 *
 * If a plugin can adopt this application then it should call
 * gs_app_set_management_plugin() on @app.
 **/
void		 gs_plugin_adopt_app			(GsPlugin	*plugin,
							 GsApp		*app);

/**
 * gs_plugin_add_search:
 * @plugin: a #GsPlugin
 * @values: a NULL terminated list of search terms, e.g. [ "gnome", "software" ]
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get search results for a specific query.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_search			(GsPlugin	*plugin,
							 gchar		**values,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_search_files:
 * @plugin: a #GsPlugin
 * @values: a list of filenames, e.g. [ "/usr/share/help/gimp/index.html" ]
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Called when searching for an application that provides a specific filename
 * on the filesystem.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_search_files		(GsPlugin	*plugin,
							 gchar		**values,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_search_what_provides
 * @plugin: a list of tags, e.g. [ "text/rtf" ]
 * @values: a #GStrv
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Called when searching for an application that provides specific defined tags,
 * for instance a codec string or mime-type.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_search_what_provides	(GsPlugin	*plugin,
							 gchar		**values,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_setup:
 * @plugin: a #GsPlugin
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Called when the plugin should set up the initial state, and with the write
 * lock held.
 *
 * All functions can block, but should sent progress notifications, e.g. using
 * gs_app_set_progress() if they will take more than tens of milliseconds
 * to complete.
 *
 * This function will also not be called if gs_plugin_initialize() self-disabled.
 *
 * Returns: %TRUE for success
 **/
gboolean	 gs_plugin_setup			(GsPlugin	*plugin,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_installed:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get the list of installed applications.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_installed		(GsPlugin	*plugin,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_updates:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get the list of pre-downloaded, pre-checked updates, with the write lock
 * held.
 *
 * NOTE: Actually downloading the updates is normally done in
 * gs_plugin_refresh() when called with %GS_PLUGIN_REFRESH_FLAGS_PAYLOAD.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_updates			(GsPlugin	*plugin,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_distro_upgrades:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get the list of distribution upgrades. Due to the download size, these
 * should not be downloaded until the user has explicitly opted-in.
 *
 * Plugins are expected to add new apps using gs_app_list_add() of type
 * %AS_APP_KIND_OS_UPGRADE.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_distro_upgrades		(GsPlugin	*plugin,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_sources:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get the list of sources, for example the repos listed in /etc/yum.repos.d
 * or the remotes configured in flatpak.
 *
 * Plugins are expected to add new apps using gs_app_list_add() of type
 * %AS_APP_KIND_SOURCE.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_sources			(GsPlugin	*plugin,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_updates_historical
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get the list of historical updates, i.e. the updates that have just been
 * installed.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_updates_historical	(GsPlugin	*plugin,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_categories:
 * @plugin: a #GsPlugin
 * @list (element-type GsCategory): a #GPtrArray
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get the category tree, for instance Games->Action or Internet->Email.
 *
 * Plugins are expected to add new categories using g_ptr_array_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_categories		(GsPlugin	*plugin,
							 GPtrArray	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_category_apps:
 * @plugin: a #GsPlugin
 * @category: a #GsCategory * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get all the applications that match a specific category.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_category_apps		(GsPlugin	*plugin,
							 GsCategory	*category,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_popular:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get popular applications that should be featured on the main page as
 * "Editors Picks".
 * This is expected to be a curated list of applications that are high quality
 * and feature-complete.
 *
 * The returned list of popular applications are not sorted, but each #GsApp has
 * to be valid, for instance having a known state and a valid icon.
 * If an insufficient number of applications are added by plugins then the
 * section on the overview shell may be hidden.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_popular			(GsPlugin	*plugin,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_featured:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Get applications that should be featured as a large full-width banner on the
 * overview page.
 * This is expected to be a curated list of applications that are high quality
 * and feature-complete.
 *
 * The returned list of popular applications are randomized in a way so that
 * the same application is featured for the entire calendar day.
 *
 * NOTE: The UI code may expect that applications have additional metadata set on
 * results, for instance <code>GnomeSoftware::FeatureTile-css</code>.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_featured			(GsPlugin	*plugin,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_unvoted_reviews:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Gets the list of unvoted reviews. Only applications should be returned where
 * there are reviews, and where the user has not previously moderated them.
 * This function is supposed to be used to display a moderation panel for
 * reviewers.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_unvoted_reviews		(GsPlugin	*plugin,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_refine:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @flags: a #GsPluginRefineFlags, e.g. %GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Adds required information to a list of #GsApp's.
 * This function is only really required when "batching up" requests, and most
 * plugins are better using the per-app gs_plugin_refine_app() function.
 *
 * An example for when this is useful would be in the PackageKit plugin where
 * we want to do one transaction of GetDetails with multiple source-ids rather
 * than scheduling a large number of pending requests.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_refine			(GsPlugin	*plugin,
							 GsAppList	*list,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_refine_app:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @flags: a #GsPluginRefineFlags, e.g. %GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Adds required information to @app.
 *
 * The general idea for @flags is that this indicates what the UI needs at the
 * moment. This doesn't mean you can't add more information if you have it,
 * for example, if we requested %GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE and had
 * to do some IO to get a blob of data, we can use gs_app_set_license() *and*
 * gs_app_set_origin() even though only the first thing was specified.
 *
 * If the plugin can't handle applications of the specific kind, or if the
 * plugin knows not of the #GsApp ID then it should just ignore the request and
 * return FALSE.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_refine_app			(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginRefineFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_launch:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Launch the specified application using a plugin-specific method.
 * This is normally setting some environment or launching a specific binary.
 *
 * Plugins can simply use gs_plugin_app_launch() if no plugin-specific
 * functionality is required.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_launch			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_add_shortcut:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Adds a shortcut for the application in a desktop-defined location.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_shortcut			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_remove_shortcut:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Removes a shortcut for the application in a desktop-defined location.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_remove_shortcut		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_update_cancel:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Cancels the offline update of @app.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_update_cancel		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_app_install:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Install the application.
 *
 * Plugins are expected to send progress notifications to the UI using
 * gs_app_set_progress() using the passed in @app.
 *
 * All functions can block, but should sent progress notifications, e.g. using
 * gs_app_set_progress() if they will take more than tens of milliseconds
 * to complete.
 *
 * On failure the error message returned will usually only be shown on the
 * console, but it may also be retained on the #GsApp object.
 * The UI code can retrieve the error using gs_app_get_last_error().
 *
 * NOTE: Once the action is complete, the plugin must set the new state of @app
 * to %AS_APP_STATE_INSTALLED.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_app_install			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_app_remove:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Remove the application.
 *
 * Plugins are expected to send progress notifications to the UI using
 * gs_app_set_progress() using the passed in @app.
 *
 * All functions can block, but should sent progress notifications, e.g. using
 * gs_app_set_progress() if they will take more than tens of milliseconds
 * to complete.
 *
 * On failure the error message returned will usually only be shown on the
 * console, but it may also be retained on the #GsApp object.
 * The UI code can retrieve the error using gs_app_get_last_error().
 *
 * NOTE: Once the action is complete, the plugin must set the new state of @app
 * to %AS_APP_STATE_AVAILABLE or %AS_APP_STATE_UNKNOWN if not known.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_app_remove			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_app_set_rating:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Gets any ratings for the applications.
 *
 * Plugins are expected to call gs_app_set_rating() on @app.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_app_set_rating		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_update_app:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Update the application live.
 *
 * Plugins are expected to send progress notifications to the UI using
 * gs_app_set_progress() using the passed in @app.
 *
 * All functions can block, but should sent progress notifications, e.g. using
 * gs_app_set_progress() if they will take more than tens of milliseconds
 * to complete.
 *
 * On failure the error message returned will usually only be shown on the
 * console, but it may also be retained on the #GsApp object.
 * The UI code can retrieve the error using gs_app_get_last_error().
 *
 * NOTE: Once the action is complete, the plugin must set the new state of @app
 * to %AS_APP_STATE_INSTALLED or %AS_APP_STATE_UNKNOWN if not known.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_update_app			(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_app_upgrade_download:
 * @plugin: a #GsPlugin
 * @app: a #GsApp, with kind %AS_APP_KIND_OS_UPGRADE
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Starts downloading a distribution upgrade in the background.
 *
 * All functions can block, but should sent progress notifications, e.g. using
 * gs_app_set_progress() if they will take more than tens of milliseconds
 * to complete.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_app_upgrade_download		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_app_upgrade_trigger:
 * @plugin: a #GsPlugin
 * @app: a #GsApp, with kind %AS_APP_KIND_OS_UPGRADE
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Triggers the distribution upgrade to be installed on next boot.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_app_upgrade_trigger		(GsPlugin	*plugin,
							 GsApp		*app,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_review_submit:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @review: a #AsReview
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Submits a new end-user application review.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_review_submit		(GsPlugin	*plugin,
							 GsApp		*app,
							 AsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_review_upvote:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @review: a #AsReview
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Upvote a specific review to indicate the review is helpful.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_review_upvote		(GsPlugin	*plugin,
							 GsApp		*app,
							 AsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_review_downvote:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @review: a #AsReview
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Downvote a specific review to indicate the review is unhelpful.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_review_downvote		(GsPlugin	*plugin,
							 GsApp		*app,
							 AsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_review_report:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @review: a #AsReview
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Report a review that is not suitable in some way.
 * It is expected that this action flags a review to be checked by a moderator
 * and that the review won't be shown to any users until this happens.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_review_report		(GsPlugin	*plugin,
							 GsApp		*app,
							 AsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_review_remove:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @review: a #AsReview
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Remove a review that the user wrote.
 * NOTE: Users should only be able to remove reviews with %AS_REVIEW_FLAG_SELF.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_review_remove		(GsPlugin	*plugin,
							 GsApp		*app,
							 AsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_review_dismiss:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 * @review: a #AsReview
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Dismisses a review, i.e. hide it from future moderated views.
 * This action is useful when the moderator is unable to speak the language of
 * the review for example.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_review_dismiss		(GsPlugin	*plugin,
							 GsApp		*app,
							 AsReview	*review,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_refresh:
 * @plugin: a #GsPlugin
 * @cache_age: the acceptable cache age in seconds, or MAXUINT for "any"
 * @flags: a bitfield of #GsPluginRefreshFlags, e.g. %GS_PLUGIN_REFRESH_FLAGS_METADATA
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Refreshes the state of all the plugins.
 *
 * The %GS_PLUGIN_REFRESH_FLAGS_METADATA flag can be used to make sure
 * there's enough metadata to start the application, for example lists of
 * available applications.
 *
 * The %GS_PLUGIN_REFRESH_FLAGS_PAYLOAD flag should only be used when
 * the session is idle and bandwidth is unmetered as the amount of data
 * and IO may be large.
 * This is used to pre-download package updates and firmware.
 *
 * All functions can block, but should sent progress notifications, e.g. using
 * gs_app_set_progress() if they will take more than tens of milliseconds
 * to complete.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_refresh			(GsPlugin	*plugin,
							 guint		 cache_age,
							 GsPluginRefreshFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_file_to_app:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @file: a #GFile
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Converts a local file to a #GsApp. It's expected that only one plugin will
 * match the mimetype of @file and that a single #GsApp will be in the returned
 * list. If no plugins can handle the file, the list will be empty.
 *
 * For example, the PackageKit plugin can turn a .rpm file into a application
 * of kind %AS_APP_KIND_UNKNOWN but that in some cases it will be futher refined
 * into a %AS_APP_KIND_DESKTOP (with all the extra metadata) by the appstream
 * plugin.
 *
 * Plugins are expected to add new apps using gs_app_list_add().
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_file_to_app			(GsPlugin	*plugin,
							 GsAppList	*list,
							 GFile		*file,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_update:
 * @plugin: a #GsPlugin
 * @apps: a #GsAppList
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Updates a list of applications, typically scheduling them for offline update.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_update			(GsPlugin	*plugin,
							 GsAppList	*apps,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_auth_login:
 * @plugin: a #GsPlugin
 * @auth: a #GsAuth
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Performs a login using the given authentication details.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_auth_login			(GsPlugin	*plugin,
							 GsAuth		*auth,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_auth_logout:
 * @plugin: a #GsPlugin
 * @auth: a #GsAuth
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Performs a logout using the given authentication details.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_auth_logout			(GsPlugin	*plugin,
							 GsAuth		*auth,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_auth_lost_password:
 * @plugin: a #GsPlugin
 * @auth: a #GsAuth
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Performs the lost password action using the given authentication details.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_auth_lost_password		(GsPlugin	*plugin,
							 GsAuth		*auth,
							 GCancellable	*cancellable,
							 GError		**error);

/**
 * gs_plugin_auth_register:
 * @plugin: a #GsPlugin
 * @auth: a #GsAuth
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Performs the registration action using the given authentication details.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_auth_register		(GsPlugin	*plugin,
							 GsAuth		*auth,
							 GCancellable	*cancellable,
							 GError		**error);

G_END_DECLS

#endif /* __GS_PLUGIN_VFUNCS_H */

/* vim: set noexpandtab: */
