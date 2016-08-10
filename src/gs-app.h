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

#ifndef __GS_APP_H
#define __GS_APP_H

#include <glib-object.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <appstream-glib.h>

G_BEGIN_DECLS

#define GS_TYPE_APP (gs_app_get_type ())

G_DECLARE_FINAL_TYPE (GsApp, gs_app, GS, APP, GObject)

/**
 * GsAppKudo:
 * @GS_APP_KUDO_MY_LANGUAGE:		Localised in my language
 * @GS_APP_KUDO_RECENT_RELEASE:		Released recently
 * @GS_APP_KUDO_FEATURED_RECOMMENDED:	Chosen for the front page
 * @GS_APP_KUDO_MODERN_TOOLKIT:		Uses a modern toolkit
 * @GS_APP_KUDO_SEARCH_PROVIDER:	Provides a search provider
 * @GS_APP_KUDO_INSTALLS_USER_DOCS:	Installs user docs
 * @GS_APP_KUDO_USES_NOTIFICATIONS:	Registers notifications
 * @GS_APP_KUDO_HAS_KEYWORDS:		Has at least 1 keyword
 * @GS_APP_KUDO_USES_APP_MENU:		Uses an AppMenu for navigation
 * @GS_APP_KUDO_HAS_SCREENSHOTS:	Supplies screenshots
 * @GS_APP_KUDO_POPULAR:		Is popular
 * @GS_APP_KUDO_PERFECT_SCREENSHOTS:	Supplies perfect screenshots
 * @GS_APP_KUDO_HIGH_CONTRAST:		Installs a high contrast icon
 * @GS_APP_KUDO_HI_DPI_ICON:		Installs a HiDPI icon
 * @GS_APP_KUDO_SANDBOXED:		Application is sandboxed
 * @GS_APP_KUDO_SANDBOXED_SECURE:	Application is sandboxed securely
 *
 * Any awards given to the application.
 **/
typedef enum {
	GS_APP_KUDO_MY_LANGUAGE			= 1 << 0,
	GS_APP_KUDO_RECENT_RELEASE		= 1 << 1,
	GS_APP_KUDO_FEATURED_RECOMMENDED	= 1 << 2,
	GS_APP_KUDO_MODERN_TOOLKIT		= 1 << 3,
	GS_APP_KUDO_SEARCH_PROVIDER		= 1 << 4,
	GS_APP_KUDO_INSTALLS_USER_DOCS		= 1 << 5,
	GS_APP_KUDO_USES_NOTIFICATIONS		= 1 << 6,
	GS_APP_KUDO_HAS_KEYWORDS		= 1 << 7,
	GS_APP_KUDO_USES_APP_MENU		= 1 << 8,
	GS_APP_KUDO_HAS_SCREENSHOTS		= 1 << 9,
	GS_APP_KUDO_POPULAR			= 1 << 10,
	GS_APP_KUDO_PERFECT_SCREENSHOTS		= 1 << 12,
	GS_APP_KUDO_HIGH_CONTRAST		= 1 << 13,
	GS_APP_KUDO_HI_DPI_ICON			= 1 << 14,
	GS_APP_KUDO_SANDBOXED			= 1 << 15,
	GS_APP_KUDO_SANDBOXED_SECURE		= 1 << 16,
	/*< private >*/
	GS_APP_KUDO_LAST
} GsAppKudo;

#define	GS_APP_INSTALL_DATE_UNSET		0
#define	GS_APP_INSTALL_DATE_UNKNOWN		1 /* 1s past the epoch */
#define	GS_APP_SIZE_UNKNOWABLE			G_MAXUINT64

/**
 * GsAppQuality:
 * @GS_APP_QUALITY_UNKNOWN:	The quality value is unknown
 * @GS_APP_QUALITY_LOWEST:	Lowest quality
 * @GS_APP_QUALITY_NORMAL:	Normal quality
 * @GS_APP_QUALITY_HIGHEST:	Highest quality
 *
 * Any awards given to the application.
 **/
typedef enum {
	GS_APP_QUALITY_UNKNOWN,
	GS_APP_QUALITY_LOWEST,
	GS_APP_QUALITY_NORMAL,
	GS_APP_QUALITY_HIGHEST,
	/*< private >*/
	GS_APP_QUALITY_LAST
} GsAppQuality;

GsApp		*gs_app_new			(const gchar	*id);
gchar		*gs_app_to_string		(GsApp		*app);

const gchar	*gs_app_get_id			(GsApp		*app);
void		 gs_app_set_id			(GsApp		*app,
						 const gchar	*id);
AsAppKind	 gs_app_get_kind		(GsApp		*app);
void		 gs_app_set_kind		(GsApp		*app,
						 AsAppKind	 kind);
AsAppState	 gs_app_get_state		(GsApp		*app);
void		 gs_app_set_state		(GsApp		*app,
						 AsAppState	 state);
AsAppScope	 gs_app_get_scope		(GsApp		*app);
void		 gs_app_set_scope		(GsApp		*app,
						 AsAppScope	 scope);
AsBundleKind	 gs_app_get_bundle_kind		(GsApp		*app);
void		 gs_app_set_bundle_kind		(GsApp		*app,
						 AsBundleKind	 bundle_kind);
void		 gs_app_set_state_recover	(GsApp		*app);
guint		 gs_app_get_progress		(GsApp		*app);
void		 gs_app_set_progress		(GsApp		*app,
						 guint		 percentage);
const gchar	*gs_app_get_unique_id		(GsApp		*app);
const gchar	*gs_app_get_branch		(GsApp		*app);
void		 gs_app_set_branch		(GsApp		*app,
						 const gchar	*branch);
const gchar	*gs_app_get_name		(GsApp		*app);
void		 gs_app_set_name		(GsApp		*app,
						 GsAppQuality	 quality,
						 const gchar	*name);
const gchar	*gs_app_get_source_default	(GsApp		*app);
void		 gs_app_add_source		(GsApp		*app,
						 const gchar	*source);
GPtrArray	*gs_app_get_sources		(GsApp		*app);
void		 gs_app_set_sources		(GsApp		*app,
						 GPtrArray	*sources);
const gchar	*gs_app_get_source_id_default	(GsApp		*app);
void		 gs_app_add_source_id		(GsApp		*app,
						 const gchar	*source_id);
GPtrArray	*gs_app_get_source_ids		(GsApp		*app);
void		 gs_app_set_source_ids		(GsApp		*app,
						 GPtrArray	*source_ids);
void		 gs_app_clear_source_ids	(GsApp		*app);
const gchar	*gs_app_get_project_group	(GsApp		*app);
void		 gs_app_set_project_group	(GsApp		*app,
						 const gchar	*project_group);
const gchar	*gs_app_get_version		(GsApp		*app);
const gchar	*gs_app_get_version_ui		(GsApp		*app);
void		 gs_app_set_version		(GsApp		*app,
						 const gchar	*version);
const gchar	*gs_app_get_summary		(GsApp		*app);
void		 gs_app_set_summary		(GsApp		*app,
						 GsAppQuality	 quality,
						 const gchar	*summary);
const gchar	*gs_app_get_summary_missing	(GsApp		*app);
void		 gs_app_set_summary_missing	(GsApp		*app,
						 const gchar	*summary_missing);
const gchar	*gs_app_get_description		(GsApp		*app);
void		 gs_app_set_description		(GsApp		*app,
						 GsAppQuality	 quality,
						 const gchar	*description);
const gchar	*gs_app_get_url			(GsApp		*app,
						 AsUrlKind	 kind);
void		 gs_app_set_url			(GsApp		*app,
						 AsUrlKind	 kind,
						 const gchar	*url);
const gchar	*gs_app_get_license		(GsApp		*app);
gboolean	 gs_app_get_license_is_free	(GsApp		*app);
void		 gs_app_set_license		(GsApp		*app,
						 GsAppQuality	 quality,
						 const gchar	*license);
gchar		**gs_app_get_menu_path		(GsApp		*app);
void		 gs_app_set_menu_path		(GsApp		*app,
						 gchar		**menu_path);
const gchar	*gs_app_get_origin		(GsApp		*app);
void		 gs_app_set_origin		(GsApp		*app,
						 const gchar	*origin);
const gchar	*gs_app_get_origin_ui		(GsApp		*app);
void		 gs_app_set_origin_ui		(GsApp		*app,
						 const gchar	*origin_ui);
const gchar	*gs_app_get_origin_hostname	(GsApp		*app);
void		 gs_app_set_origin_hostname	(GsApp		*app,
						 const gchar	*origin_hostname);
GPtrArray	*gs_app_get_screenshots		(GsApp		*app);
void		 gs_app_add_screenshot		(GsApp		*app,
						 AsScreenshot	*screenshot);
const gchar	*gs_app_get_update_version	(GsApp		*app);
const gchar	*gs_app_get_update_version_ui	(GsApp		*app);
void		 gs_app_set_update_version	(GsApp		*app,
						 const gchar	*update_version);
const gchar	*gs_app_get_update_details	(GsApp		*app);
void		 gs_app_set_update_details	(GsApp		*app,
						 const gchar	*update_details);
AsUrgencyKind	 gs_app_get_update_urgency	(GsApp		*app);
void		 gs_app_set_update_urgency	(GsApp		*app,
						 AsUrgencyKind	 update_urgency);
const gchar	*gs_app_get_management_plugin	(GsApp		*app);
void		 gs_app_set_management_plugin	(GsApp		*app,
						 const gchar	*management_plugin);
GdkPixbuf	*gs_app_get_pixbuf		(GsApp		*app);
void		 gs_app_set_pixbuf		(GsApp		*app,
						 GdkPixbuf	*pixbuf);
GPtrArray	*gs_app_get_icons		(GsApp		*app);
void		 gs_app_add_icon		(GsApp		*app,
						 AsIcon		*icon);
GFile		*gs_app_get_local_file		(GsApp		*app);
void		 gs_app_set_local_file		(GsApp		*app,
						 GFile		*local_file);
GsApp		*gs_app_get_runtime		(GsApp		*app);
void		 gs_app_set_runtime		(GsApp		*app,
						 GsApp		*runtime);
const gchar	*gs_app_get_metadata_item	(GsApp		*app,
						 const gchar	*key);
void		 gs_app_set_metadata		(GsApp		*app,
						 const gchar	*key,
						 const gchar	*value);
gint		 gs_app_get_rating		(GsApp		*app);
void		 gs_app_set_rating		(GsApp		*app,
						 gint		 rating);
GArray		*gs_app_get_review_ratings	(GsApp		*app);
void		 gs_app_set_review_ratings	(GsApp		*app,
						 GArray		*review_ratings);
GPtrArray	*gs_app_get_reviews		(GsApp		*app);
void		 gs_app_add_review		(GsApp		*app,
						 AsReview	*review);
void		 gs_app_remove_review		(GsApp		*app,
						 AsReview	*review);
guint64		 gs_app_get_size_installed	(GsApp		*app);
void		 gs_app_set_size_installed	(GsApp		*app,
						 guint64	 size_installed);
guint64		 gs_app_get_size_download	(GsApp		*app);
void		 gs_app_set_size_download	(GsApp		*app,
						 guint64	 size_download);
GPtrArray	*gs_app_get_addons		(GsApp		*app);
void		 gs_app_add_addon		(GsApp		*app,
						 GsApp		*addon);
GPtrArray	*gs_app_get_related		(GsApp		*app);
void		 gs_app_add_related		(GsApp		*app,
						 GsApp		*app2);
GPtrArray	*gs_app_get_history		(GsApp		*app);
void		 gs_app_add_history		(GsApp		*app,
						 GsApp		*app2);
guint64		 gs_app_get_install_date	(GsApp		*app);
void		 gs_app_set_install_date	(GsApp		*app,
						 guint64	 install_date);
GPtrArray	*gs_app_get_categories		(GsApp		*app);
void		 gs_app_set_categories		(GsApp		*app,
						 GPtrArray	*categories);
GPtrArray	*gs_app_get_key_colors		(GsApp		*app);
void		 gs_app_set_key_colors		(GsApp		*app,
						 GPtrArray	*key_colors);
void		 gs_app_add_key_color		(GsApp		*app,
						 GdkRGBA	*key_color);
gboolean	 gs_app_has_category		(GsApp		*app,
						 const gchar	*category);
void		 gs_app_add_category		(GsApp		*app,
						 const gchar	*category);
GPtrArray	*gs_app_get_keywords		(GsApp		*app);
void		 gs_app_set_keywords		(GsApp		*app,
						 GPtrArray	*keywords);
void		 gs_app_add_kudo		(GsApp		*app,
						 GsAppKudo	 kudo);
guint64		 gs_app_get_kudos		(GsApp		*app);
guint		 gs_app_get_kudos_percentage	(GsApp		*app);
gboolean	 gs_app_get_to_be_installed	(GsApp		*app);
void		 gs_app_set_to_be_installed	(GsApp		*app,
						 gboolean	 to_be_installed);
void		 gs_app_set_match_value		(GsApp		*app,
						 guint		 match_value);
guint		 gs_app_get_match_value		(GsApp		*app);

gboolean	 gs_app_has_quirk		(GsApp		*app,
						 AsAppQuirk	 quirk);
void		 gs_app_add_quirk		(GsApp		*app,
						 AsAppQuirk	 quirk);
void		 gs_app_remove_quirk		(GsApp		*app,
						 AsAppQuirk	 quirk);
gboolean	 gs_app_is_installed		(GsApp		*app);
G_END_DECLS

#endif /* __GS_APP_H */

/* vim: set noexpandtab: */
