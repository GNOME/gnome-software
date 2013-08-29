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

G_BEGIN_DECLS

#define GS_TYPE_APP		(gs_app_get_type ())
#define GS_APP(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_APP, GsApp))
#define GS_APP_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_APP, GsAppClass))
#define GS_IS_APP(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_APP))
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
	void			(*state_changed)	(GsApp		*app);
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
	GS_APP_KIND_LAST
} GsAppKind;

typedef enum {
	GS_APP_STATE_UNKNOWN,
	GS_APP_STATE_INSTALLED,
	GS_APP_STATE_AVAILABLE,
	GS_APP_STATE_INSTALLING,
	GS_APP_STATE_REMOVING,
	GS_APP_STATE_UPDATABLE,
	GS_APP_STATE_LAST
} GsAppState;

GQuark		 gs_app_error_quark		(void);
GType		 gs_app_get_type		(void);

GsApp		*gs_app_new			(const gchar	*id);
const gchar	*gs_app_get_id			(GsApp		*app);
void 		 gs_app_set_id			(GsApp		*app,
						 const gchar	*id);
GsAppKind	 gs_app_get_kind		(GsApp		*app);
void		 gs_app_set_kind		(GsApp		*app,
						 GsAppKind	 kind);
GsAppState	 gs_app_get_state		(GsApp		*app);
void		 gs_app_set_state		(GsApp		*app,
						 GsAppState	 state);
const gchar	*gs_app_get_name		(GsApp		*app);
void 		 gs_app_set_name		(GsApp		*app,
						 const gchar	*name);
const gchar	*gs_app_get_version		(GsApp		*app);
void 		 gs_app_set_version		(GsApp		*app,
						 const gchar	*version);
const gchar	*gs_app_get_summary		(GsApp		*app);
void		 gs_app_set_summary		(GsApp		*app,
						 const gchar	*summary);
const gchar	*gs_app_get_description		(GsApp		*app);
void		 gs_app_set_description		(GsApp		*app,
						 const gchar	*description);
const gchar	*gs_app_get_url                 (GsApp		*app);
void		 gs_app_set_url 		(GsApp		*app,
						 const gchar	*url);
const gchar	*gs_app_get_screenshot		(GsApp		*app);
void		 gs_app_set_screenshot		(GsApp		*app,
						 const gchar	*screenshot);
GdkPixbuf	*gs_app_get_pixbuf		(GsApp		*app);
void		 gs_app_set_pixbuf		(GsApp		*app,
						 GdkPixbuf	*pixbuf);
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
GPtrArray	*gs_app_get_related		(GsApp		*app);
void		 gs_app_add_related		(GsApp		*app,
						 GsApp		*app2);
guint64          gs_app_get_install_date        (GsApp          *app);
void             gs_app_set_install_date        (GsApp          *app,
                                                 guint64         install_date);

G_END_DECLS

#endif /* __GS_APP_H */
