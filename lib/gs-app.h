/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <appstream-glib.h>

#include "gs-price.h"

G_BEGIN_DECLS

#define GS_TYPE_APP (gs_app_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsApp, gs_app, GS, APP, GObject)

struct _GsAppClass
{
	GObjectClass		 parent_class;
	void			 (*to_string)	(GsApp		*app,
						 GString	*str);
	gpointer		 padding[30];
};

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
 * @GS_APP_KUDO_HAS_SCREENSHOTS:	Supplies screenshots
 * @GS_APP_KUDO_POPULAR:		Is popular
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
	GS_APP_KUDO_HAS_SCREENSHOTS		= 1 << 9,
	GS_APP_KUDO_POPULAR			= 1 << 10,
	GS_APP_KUDO_HIGH_CONTRAST		= 1 << 13,
	GS_APP_KUDO_HI_DPI_ICON			= 1 << 14,
	GS_APP_KUDO_SANDBOXED			= 1 << 15,
	GS_APP_KUDO_SANDBOXED_SECURE		= 1 << 16,
	/*< private >*/
	GS_APP_KUDO_LAST
} GsAppKudo;

/**
 * GsAppQuirk:
 * @GS_APP_QUIRK_NONE:			No special attributes
 * @GS_APP_QUIRK_PROVENANCE:		Installed by OS vendor
 * @GS_APP_QUIRK_COMPULSORY:		Cannot be removed
 * @GS_APP_QUIRK_HAS_SOURCE:		Has a source to allow staying up-to-date
 * @GS_APP_QUIRK_IS_WILDCARD:		Matches applications from any plugin
 * @GS_APP_QUIRK_NEEDS_REBOOT:		A reboot is required after the action
 * @GS_APP_QUIRK_NOT_REVIEWABLE:	The app is not reviewable
 * @GS_APP_QUIRK_HAS_SHORTCUT:		The app has a shortcut in the system
 * @GS_APP_QUIRK_NOT_LAUNCHABLE:	The app is not launchable (run-able)
 * @GS_APP_QUIRK_NEEDS_USER_ACTION:	The component requires some kind of user action
 * @GS_APP_QUIRK_IS_PROXY:		Is a proxy app that operates on other applications
 * @GS_APP_QUIRK_REMOVABLE_HARDWARE:	The device is unusable whilst the action is performed
 * @GS_APP_QUIRK_DEVELOPER_VERIFIED:	The app developer has been verified
 * @GS_APP_QUIRK_PARENTAL_FILTER:	The app has been filtered by parental controls, and should be hidden
 * @GS_APP_QUIRK_NEW_PERMISSIONS:	The update requires new permissions
 * @GS_APP_QUIRK_PARENTAL_NOT_LAUNCHABLE:	The app cannot be run by the current user due to parental controls, and should not be launchable
 *
 * The application attributes.
 **/
typedef enum {
	GS_APP_QUIRK_NONE		= 0,		/* Since: 3.32 */
	GS_APP_QUIRK_PROVENANCE		= 1 << 0,	/* Since: 3.32 */
	GS_APP_QUIRK_COMPULSORY		= 1 << 1,	/* Since: 3.32 */
	GS_APP_QUIRK_HAS_SOURCE		= 1 << 2,	/* Since: 3.32 */
	GS_APP_QUIRK_IS_WILDCARD	= 1 << 3,	/* Since: 3.32 */
	GS_APP_QUIRK_NEEDS_REBOOT	= 1 << 4,	/* Since: 3.32 */
	GS_APP_QUIRK_NOT_REVIEWABLE	= 1 << 5,	/* Since: 3.32 */
	GS_APP_QUIRK_HAS_SHORTCUT	= 1 << 6,	/* Since: 3.32 */
	GS_APP_QUIRK_NOT_LAUNCHABLE	= 1 << 7,	/* Since: 3.32 */
	GS_APP_QUIRK_NEEDS_USER_ACTION	= 1 << 8,	/* Since: 3.32 */
	GS_APP_QUIRK_IS_PROXY 		= 1 << 9,	/* Since: 3.32 */
	GS_APP_QUIRK_REMOVABLE_HARDWARE	= 1 << 10,	/* Since: 3.32 */
	GS_APP_QUIRK_DEVELOPER_VERIFIED	= 1 << 11,	/* Since: 3.32 */
	GS_APP_QUIRK_PARENTAL_FILTER	= 1 << 12,	/* Since: 3.32 */
	GS_APP_QUIRK_NEW_PERMISSIONS	= 1 << 13,	/* Since: 3.32 */
	GS_APP_QUIRK_PARENTAL_NOT_LAUNCHABLE	= 1 << 14,	/* Since: 3.32 */
	/*< private >*/
	GS_APP_QUIRK_LAST
} GsAppQuirk;

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

typedef enum {
	GS_APP_PERMISSIONS_UNKNOWN 		= 0,
	GS_APP_PERMISSIONS_NONE			= 1 << 0,
	GS_APP_PERMISSIONS_NETWORK 		= 1 << 1,
	GS_APP_PERMISSIONS_SYSTEM_BUS   	= 1 << 2,
	GS_APP_PERMISSIONS_SESSION_BUS		= 1 << 3,
	GS_APP_PERMISSIONS_DEVICES 		= 1 << 4,
	GS_APP_PERMISSIONS_HOME_FULL 		= 1 << 5,
	GS_APP_PERMISSIONS_HOME_READ		= 1 << 6,
	GS_APP_PERMISSIONS_FILESYSTEM_FULL	= 1 << 7,
	GS_APP_PERMISSIONS_FILESYSTEM_READ	= 1 << 8,
	GS_APP_PERMISSIONS_DOWNLOADS_FULL 	= 1 << 9,
	GS_APP_PERMISSIONS_DOWNLOADS_READ	= 1 << 10,
	GS_APP_PERMISSIONS_SETTINGS		= 1 << 11,
	GS_APP_PERMISSIONS_X11			= 1 << 12,
	/*< private >*/
	GS_APP_PERMISSIONS_LAST
} GsAppPermissions;

#define LIMITED_PERMISSIONS (GS_APP_PERMISSIONS_SETTINGS | \
			GS_APP_PERMISSIONS_NETWORK | \
			GS_APP_PERMISSIONS_DOWNLOADS_READ | \
			GS_APP_PERMISSIONS_DOWNLOADS_FULL)
#define MEDIUM_PERMISSIONS (LIMITED_PERMISSIONS | \
			GS_APP_PERMISSIONS_X11)

GsApp		*gs_app_new			(const gchar	*id);
G_DEPRECATED_FOR(gs_app_set_from_unique_id)
GsApp		*gs_app_new_from_unique_id	(const gchar	*unique_id);
void		 gs_app_set_from_unique_id	(GsApp		*app,
						 const gchar	*unique_id);
gchar		*gs_app_to_string		(GsApp		*app);
void		 gs_app_to_string_append	(GsApp		*app,
						 GString	*str);

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
gboolean	gs_app_get_allow_cancel		(GsApp	*app);
void		gs_app_set_allow_cancel		 (GsApp	*app,
						  gboolean	allow_cancel);
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
const gchar	*gs_app_get_developer_name	(GsApp		*app);
void		 gs_app_set_developer_name	(GsApp		*app,
						 const gchar	*developer_name);
const gchar	*gs_app_get_agreement		(GsApp		*app);
void		 gs_app_set_agreement		(GsApp		*app,
						 const gchar	*agreement);
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
const gchar	*gs_app_get_launchable		(GsApp		*app,
						 AsLaunchableKind kind);
void		 gs_app_set_launchable		(GsApp		*app,
						 AsLaunchableKind kind,
						 const gchar	*launchable);
gboolean	 gs_app_is_launchable		(GsApp		*app);
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
const gchar	*gs_app_get_origin_appstream	(GsApp		*app);
void		 gs_app_set_origin_appstream	(GsApp		*app,
						 const gchar	*origin_appstream);
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
GsPrice		*gs_app_get_price		(GsApp		*app);
void		 gs_app_set_price		(GsApp		*app,
						 gdouble	 amount,
						 const gchar	*currency);
GPtrArray	*gs_app_get_icons		(GsApp		*app);
void		 gs_app_add_icon		(GsApp		*app,
						 AsIcon		*icon);
GFile		*gs_app_get_local_file		(GsApp		*app);
void		 gs_app_set_local_file		(GsApp		*app,
						 GFile		*local_file);
AsContentRating	*gs_app_get_content_rating	(GsApp		*app);
void		 gs_app_set_content_rating	(GsApp		*app,
						 AsContentRating *content_rating);
GsApp		*gs_app_get_runtime		(GsApp		*app);
void		 gs_app_set_runtime		(GsApp		*app,
						 GsApp		*runtime);
const gchar	*gs_app_get_metadata_item	(GsApp		*app,
						 const gchar	*key);
GVariant	*gs_app_get_metadata_variant	(GsApp		*app,
						 const gchar	*key);
void		 gs_app_set_metadata		(GsApp		*app,
						 const gchar	*key,
						 const gchar	*value);
void		 gs_app_set_metadata_variant	(GsApp		*app,
						 const gchar	*key,
						 GVariant	*value);
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
GPtrArray	*gs_app_get_provides		(GsApp		*app);
void		 gs_app_add_provide		(GsApp		*app,
						 AsProvide	*provide);
guint64		 gs_app_get_size_installed	(GsApp		*app);
void		 gs_app_set_size_installed	(GsApp		*app,
						 guint64	 size_installed);
guint64		 gs_app_get_size_download	(GsApp		*app);
void		 gs_app_set_size_download	(GsApp		*app,
						 guint64	 size_download);
void		 gs_app_add_related		(GsApp		*app,
						 GsApp		*app2);
void		 gs_app_add_addon		(GsApp		*app,
						 GsApp		*addon);
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
gboolean	 gs_app_remove_category		(GsApp		*app,
						 const gchar	*category);
void		 gs_app_add_kudo		(GsApp		*app,
						 GsAppKudo	 kudo);
void		 gs_app_remove_kudo		(GsApp		*app,
						 GsAppKudo	 kudo);
gboolean	 gs_app_has_kudo		(GsApp		*app,
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
						 GsAppQuirk	 quirk);
void		 gs_app_add_quirk		(GsApp		*app,
						 GsAppQuirk	 quirk);
void		 gs_app_remove_quirk		(GsApp		*app,
						 GsAppQuirk	 quirk);
gboolean	 gs_app_is_installed		(GsApp		*app);
gboolean	 gs_app_is_updatable		(GsApp		*app);
gchar		*gs_app_get_origin_ui		(GsApp		*app);
gchar		*gs_app_get_packaging_format	(GsApp		*app);
void		 gs_app_subsume_metadata	(GsApp		*app,
						 GsApp		*donor);
GsAppPermissions gs_app_get_permissions		(GsApp		*app);
void		 gs_app_set_permissions		(GsApp		*app,
						 GsAppPermissions permissions);
GsAppPermissions gs_app_get_update_permissions	(GsApp		*app);
void		 gs_app_set_update_permissions	(GsApp		*app,
						 GsAppPermissions update_permissions);

G_END_DECLS
