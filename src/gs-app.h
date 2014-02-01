/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "gs-screenshot.h"

G_BEGIN_DECLS

#define GS_TYPE_APP		(gs_app_get_type ())
#define GS_APP(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_APP, GsApp))
#define GS_APP_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_APP, GsAppClass))
#define GS_IS_APP(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_APP))
#define GS_IS_APP_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_APP))
#define GS_APP_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_APP, GsAppClass))
#define GS_APP_ERROR		(gs_app_error_quark ())

typedef struct GsAppPrivate GsAppPrivate;

typedef struct
{
	 GObject		 parent;
	 GsAppPrivate		*priv;
} GsApp;

typedef struct
{
	GObjectClass		 parent_class;
} GsAppClass;

typedef enum {
	GS_APP_ERROR_FAILED,
	GS_APP_ERROR_LAST
} GsAppError;

typedef enum {
	GS_APP_KIND_UNKNOWN,
	GS_APP_KIND_NORMAL,	/* can be updated, removed and installed */
	GS_APP_KIND_SYSTEM,	/* can be updated, but not installed or removed */
	GS_APP_KIND_PACKAGE,	/* can be updated, but not installed or removed */
	GS_APP_KIND_OS_UPDATE,	/* can be updated, but not installed or removed */
	GS_APP_KIND_MISSING,	/* you can't do anything to this */
	GS_APP_KIND_LAST
} GsAppKind;

typedef enum {
	GS_APP_STATE_UNKNOWN,
	GS_APP_STATE_INSTALLED,
	GS_APP_STATE_AVAILABLE,
	GS_APP_STATE_QUEUED,
	GS_APP_STATE_INSTALLING,
	GS_APP_STATE_REMOVING,
	GS_APP_STATE_UPDATABLE,
	GS_APP_STATE_UNAVAILABLE,	/* we found a reference to this */
	GS_APP_STATE_LOCAL,
	GS_APP_STATE_LAST
} GsAppState;

typedef enum {
	GS_APP_ID_KIND_UNKNOWN,
	GS_APP_ID_KIND_DESKTOP,
	GS_APP_ID_KIND_INPUT_METHOD,
	GS_APP_ID_KIND_FONT,
	GS_APP_ID_KIND_CODEC,
	GS_APP_ID_KIND_WEBAPP,
	GS_APP_ID_KIND_LAST
} GsAppIdKind;

typedef enum {
	GS_APP_RATING_KIND_UNKNOWN,
	GS_APP_RATING_KIND_USER,
	GS_APP_RATING_KIND_SYSTEM,
	GS_APP_RATING_KIND_LAST
} GsAppRatingKind;

#define	GS_APP_INSTALL_DATE_UNSET		0
#define	GS_APP_INSTALL_DATE_UNKNOWN		1 /* 1s past the epoch */
#define	GS_APP_SIZE_UNKNOWN			0
#define	GS_APP_SIZE_MISSING			1

#define	GS_APP_URL_KIND_HOMEPAGE		"homepage"
#define	GS_APP_URL_KIND_MISSING			"missing"

typedef enum {
	GS_APP_QUALITY_UNKNOWN,
	GS_APP_QUALITY_LOWEST,
	GS_APP_QUALITY_NORMAL,
	GS_APP_QUALITY_HIGHEST,
	GS_APP_QUALITY_LAST
} GsAppQuality;

GQuark		 gs_app_error_quark		(void);
GType		 gs_app_get_type		(void);

GsApp		*gs_app_new			(const gchar	*id);
gchar		*gs_app_to_string		(GsApp		*app);
const gchar	*gs_app_kind_to_string		(GsAppKind	 kind);
const gchar	*gs_app_id_kind_to_string	(GsAppIdKind	 id_kind);
const gchar	*gs_app_state_to_string		(GsAppState	 state);

void		 gs_app_subsume			(GsApp		*app,
						 GsApp		*other);

const gchar	*gs_app_get_id			(GsApp		*app);
const gchar	*gs_app_get_id_full		(GsApp		*app);
void		 gs_app_set_id			(GsApp		*app,
						 const gchar	*id);
GsAppKind	 gs_app_get_kind		(GsApp		*app);
void		 gs_app_set_kind		(GsApp		*app,
						 GsAppKind	 kind);
GsAppIdKind	 gs_app_get_id_kind		(GsApp		*app);
void		 gs_app_set_id_kind		(GsApp		*app,
						 GsAppIdKind	 id_kind);
GsAppState	 gs_app_get_state		(GsApp		*app);
void		 gs_app_set_state		(GsApp		*app,
						 GsAppState	 state);
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
						 const gchar	*kind);
void		 gs_app_set_url			(GsApp		*app,
						 const gchar	*kind,
						 const gchar	*url);
const gchar	*gs_app_get_licence		(GsApp		*app);
void		 gs_app_set_licence		(GsApp		*app,
						 const gchar	*licence);
const gchar	*gs_app_get_menu_path		(GsApp		*app);
void		 gs_app_set_menu_path		(GsApp		*app,
						 const gchar	*menu_path);
GPtrArray	*gs_app_get_screenshots		(GsApp		*app);
void		 gs_app_add_screenshot		(GsApp		*app,
						 GsScreenshot	*screenshot);
const gchar	*gs_app_get_update_version	(GsApp		*app);
const gchar	*gs_app_get_update_version_ui	(GsApp		*app);
void		 gs_app_set_update_version	(GsApp		*app,
						 const gchar	*update_version);
const gchar	*gs_app_get_update_details	(GsApp		*app);
void		 gs_app_set_update_details	(GsApp		*app,
						 const gchar	*update_details);
const gchar	*gs_app_get_management_plugin	(GsApp		*app);
void		 gs_app_set_management_plugin	(GsApp		*app,
						 const gchar	*management_plugin);
GdkPixbuf	*gs_app_get_pixbuf		(GsApp		*app);
void		 gs_app_set_pixbuf		(GsApp		*app,
						 GdkPixbuf	*pixbuf);
const gchar	*gs_app_get_icon		(GsApp		*app);
void		 gs_app_set_icon		(GsApp		*app,
						 const gchar	*icon);
gboolean	 gs_app_load_icon		(GsApp		*app,
						 GError		**error);
GdkPixbuf	*gs_app_get_featured_pixbuf	(GsApp		*app);
void		 gs_app_set_featured_pixbuf	(GsApp		*app,
						 GdkPixbuf	*pixbuf);
const gchar	*gs_app_get_metadata_item	(GsApp		*app,
						 const gchar	*key);
void		 gs_app_set_metadata		(GsApp		*app,
						 const gchar	*key,
						 const gchar	*value);
gint		 gs_app_get_rating		(GsApp		*app);
void		 gs_app_set_rating		(GsApp		*app,
						 gint		 rating);
gint		 gs_app_get_rating_confidence	(GsApp		*app);
void		 gs_app_set_rating_confidence	(GsApp		*app,
						 gint		 rating_confidence);
GsAppRatingKind	 gs_app_get_rating_kind		(GsApp		*app);
void		 gs_app_set_rating_kind		(GsApp		*app,
						 GsAppRatingKind rating_kind);
guint64		 gs_app_get_size		(GsApp		*app);
void		 gs_app_set_size		(GsApp		*app,
						 guint64	 size);
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

G_END_DECLS

#endif /* __GS_APP_H */

/* vim: set noexpandtab: */
