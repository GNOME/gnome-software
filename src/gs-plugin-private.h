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

#ifndef __GS_PLUGIN_PRIVATE_H
#define __GS_PLUGIN_PRIVATE_H

#include <appstream-glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <libsoup/soup.h>

#include "gs-plugin.h"

G_BEGIN_DECLS

/**
 * GsPluginAction:
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
 *
 * The plugin action.
 **/
typedef enum {
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
	/*< private >*/
	GS_PLUGIN_ACTION_LAST
} GsPluginAction;

GsPlugin	*gs_plugin_new				(void);
GsPlugin	*gs_plugin_create			(const gchar	*filename,
							 GError		**error);
const gchar	*gs_plugin_error_to_string		(GsPluginError	 error);
const gchar	*gs_plugin_action_to_string		(GsPluginAction	 action);

void		 gs_plugin_action_start			(GsPlugin	*plugin,
							 gboolean	 exclusive);
void		 gs_plugin_action_stop			(GsPlugin	*plugin);
void		 gs_plugin_set_scale			(GsPlugin	*plugin,
							 guint		 scale);
guint		 gs_plugin_get_order			(GsPlugin	*plugin);
void		 gs_plugin_set_order			(GsPlugin	*plugin,
							 guint		 order);
guint		 gs_plugin_get_priority			(GsPlugin	*plugin);
void		 gs_plugin_set_priority			(GsPlugin	*plugin,
							 guint		 priority);
void		 gs_plugin_set_locale			(GsPlugin	*plugin,
							 const gchar	*locale);
void		 gs_plugin_set_language			(GsPlugin	*plugin,
							 const gchar	*language);
void		 gs_plugin_set_profile			(GsPlugin	*plugin,
							 AsProfile	*profile);
void		 gs_plugin_set_auth_array		(GsPlugin	*plugin,
							 GPtrArray	*auth_array);
void		 gs_plugin_set_soup_session		(GsPlugin	*plugin,
							 SoupSession	*soup_session);
void		 gs_plugin_set_global_cache		(GsPlugin	*plugin,
							 GsAppList	*global_cache);
void		 gs_plugin_set_running_other		(GsPlugin	*plugin,
							 gboolean	 running_other);
GPtrArray	*gs_plugin_get_rules			(GsPlugin	*plugin,
							 GsPluginRule	 rule);
GModule		*gs_plugin_get_module			(GsPlugin	*plugin);

G_END_DECLS

#endif /* __GS_PLUGIN_PRIVATE_H */

/* vim: set noexpandtab: */
