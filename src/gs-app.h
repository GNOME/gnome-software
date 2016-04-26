/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <appstream-glib.h>

#include "gs-review.h"

G_BEGIN_DECLS

#define GS_TYPE_APP (gs_app_get_type ())

G_DECLARE_FINAL_TYPE (GsApp, gs_app, GS, APP, GObject)

typedef enum {
	GS_APP_ERROR_FAILED,
	GS_APP_ERROR_LAST
} GsAppError;

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
	GS_APP_KUDO_IBUS_HAS_SYMBOL		= 1 << 11,
	GS_APP_KUDO_PERFECT_SCREENSHOTS		= 1 << 12,
	GS_APP_KUDO_HIGH_CONTRAST		= 1 << 13,
	GS_APP_KUDO_HI_DPI_ICON			= 1 << 14,
	GS_APP_KUDO_LAST
} GsAppKudo;

#define	GS_APP_INSTALL_DATE_UNSET		0
#define	GS_APP_INSTALL_DATE_UNKNOWN		1 /* 1s past the epoch */
#define	GS_APP_SIZE_UNKNOWN			0
#define	GS_APP_SIZE_MISSING			1

typedef enum {
	GS_APP_QUALITY_UNKNOWN,
	GS_APP_QUALITY_LOWEST,
	GS_APP_QUALITY_NORMAL,
	GS_APP_QUALITY_HIGHEST,
	GS_APP_QUALITY_LAST
} GsAppQuality;

#define	GS_APP_KUDOS_WEIGHT_TO_PERCENTAGE(w)	(w * 20)

#if !AS_CHECK_VERSION(0,5,12)
#define AS_APP_QUIRK_MATCH_ANY_PREFIX		(1 << 3)
#endif

#if !AS_CHECK_VERSION(0,5,14)
#define AS_APP_QUIRK_NEEDS_REBOOT		(1 << 4)
#define AS_APP_QUIRK_NOT_REVIEWABLE		(1 << 5)
#endif

#if !AS_CHECK_VERSION(0,5,15)
#define AS_APP_QUIRK_HAS_SHORTCUT		(1 << 6)
#define AS_APP_QUIRK_NOT_LAUNCHABLE		(1 << 7)
#endif

GQuark		 gs_app_error_quark		(void);

GsApp		*gs_app_new			(const gchar	*id);
gchar		*gs_app_to_string		(GsApp		*app);

const gchar	*gs_app_get_id			(GsApp		*app);
void		 gs_app_set_id			(GsApp		*app,
						 const gchar	*id);
const gchar	*gs_app_get_id_no_prefix	(GsApp		*app);
AsAppKind	 gs_app_get_kind		(GsApp		*app);
void		 gs_app_set_kind		(GsApp		*app,
						 AsAppKind	 kind);
AsAppState	 gs_app_get_state		(GsApp		*app);
void		 gs_app_set_state		(GsApp		*app,
						 AsAppState	 state);
void		 gs_app_set_state_recover	(GsApp		*app);
guint		 gs_app_get_progress		(GsApp		*app);
void		 gs_app_set_progress		(GsApp		*app,
						 guint		 percentage);
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
						 const gchar	*source);
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
						 const gchar	*missing);
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
AsIcon		*gs_app_get_icon		(GsApp		*app);
void		 gs_app_set_icon		(GsApp		*app,
						 AsIcon		*icon);
GsApp		*gs_app_get_runtime		(GsApp		*app);
void		 gs_app_set_runtime		(GsApp		*app,
						 GsApp		*runtime);
gboolean	 gs_app_load_icon		(GsApp		*app,
						 gint		 scale,
						 GError		**error);
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
						 GsReview	*review);
void		 gs_app_remove_review		(GsApp		*app,
						 GsReview	*review);
guint64		 gs_app_get_size		(GsApp		*app);
void		 gs_app_set_size		(GsApp		*app,
						 guint64	 size);
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
GError		*gs_app_get_last_error		(GsApp		*app);
void		 gs_app_set_last_error		(GsApp		*app,
						 GError		*error);

G_END_DECLS

#endif /* __GS_APP_H */

/* vim: set noexpandtab: */
