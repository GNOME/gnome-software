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

#include "config.h"

#include "appstream-app.h"

struct AppstreamApp
{
	gchar			*id;
	gchar			*pkgname;
	gchar			*name;
	gchar			*summary;
	gchar			*url;
	gchar			*description;
	gchar			*icon;
	AppstreamAppIconKind	 icon_kind;
	GPtrArray		*appcategories;
	gpointer		 userdata;
	GDestroyNotify		 userdata_destroy_func;
};

/**
 * appstream_app_free:
 */
void
appstream_app_free (AppstreamApp *app)
{
	g_free (app->id);
	g_free (app->pkgname);
	g_free (app->name);
	g_free (app->summary);
	g_free (app->url);
	g_free (app->description);
	g_free (app->icon);
	g_ptr_array_unref (app->appcategories);
	if (app->userdata_destroy_func != NULL)
		app->userdata_destroy_func (app->userdata);
	g_slice_free (AppstreamApp, app);
}

/**
 * appstream_app_get_userdata:
 */
gpointer
appstream_app_get_userdata (AppstreamApp *app)
{
	return app->userdata;
}

/**
 * appstream_app_set_userdata:
 */
void
appstream_app_set_userdata (AppstreamApp *app,
			    gpointer userdata,
			    GDestroyNotify userdata_destroy_func)
{
	app->userdata = userdata;
	app->userdata_destroy_func = userdata_destroy_func;
}

/**
 * appstream_app_new:
 */
AppstreamApp *
appstream_app_new (void)
{
	AppstreamApp *app;
	app = g_slice_new0 (AppstreamApp);
	app->appcategories = g_ptr_array_new_with_free_func (g_free);
	app->icon_kind = APPSTREAM_APP_ICON_KIND_UNKNOWN;
	return app;
}

/**
 * appstream_app_get_id:
 */
const gchar *
appstream_app_get_id (AppstreamApp *app)
{
	return app->id;
}

/**
 * appstream_app_get_pkgname:
 */
const gchar *
appstream_app_get_pkgname (AppstreamApp *app)
{
	return app->pkgname;
}

/**
 * appstream_app_get_name:
 */
const gchar *
appstream_app_get_name (AppstreamApp *app)
{
	return app->name;
}

/**
 * appstream_app_get_summary:
 */
const gchar *
appstream_app_get_summary (AppstreamApp *app)
{
	return app->summary;
}

/**
 * appstream_app_get_url:
 */
const gchar *
appstream_app_get_url (AppstreamApp *app)
{
	return app->url;
}

/**
 * appstream_app_get_description:
 */
const gchar *
appstream_app_get_description (AppstreamApp *app)
{
	return app->description;
}

/**
 * appstream_app_get_icon:
 */
const gchar *
appstream_app_get_icon (AppstreamApp *app)
{
	return app->icon;
}

/**
 * appstream_app_get_icon_kind:
 */
AppstreamAppIconKind 
appstream_app_get_icon_kind (AppstreamApp *app)
{
	return app->icon_kind;
}

/**
 * appstream_app_has_category:
 */
gboolean 
appstream_app_has_category (AppstreamApp *app, const gchar *category)
{
	const gchar *tmp;
	guint i;

	for (i = 0; i < app->appcategories->len; i++) {
		tmp = g_ptr_array_index (app->appcategories, i);
		if (g_strcmp0 (tmp, category) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * appstream_app_set_id:
 */
void 
appstream_app_set_id (AppstreamApp *app,
		      const gchar *id,
		      gsize length)
{
	app->id = g_strndup (id, length);
}

/**
 * appstream_app_set_pkgname:
 */
void 
appstream_app_set_pkgname (AppstreamApp *app,
			   const gchar *pkgname,
			   gsize length)
{
	app->pkgname = g_strndup (pkgname, length);
}

/**
 * appstream_app_set_name:
 */
void 
appstream_app_set_name (AppstreamApp *app,
			const gchar *name,
			gsize length)
{
	app->name = g_strndup (name, length);
}

/**
 * appstream_app_set_summary:
 */
void 
appstream_app_set_summary (AppstreamApp *app,
			   const gchar *summary,
			   gsize length)
{
	app->summary = g_strndup (summary, length);
}

/**
 * appstream_app_set_url:
 */
void 
appstream_app_set_url (AppstreamApp *app,
		       const gchar *url,
		       gsize length)
{
	app->url = g_strndup (url, length);
}

/**
 * appstream_app_set_description:
 */
void 
appstream_app_set_description (AppstreamApp *app,
			       const gchar *description,
			       gsize length)
{
	app->description = g_strndup (description, length);
}

/**
 * appstream_app_set_icon:
 */
void 
appstream_app_set_icon (AppstreamApp *app,
			const gchar *icon,
			gsize length)
{
	app->icon = g_strndup (icon, length);
}

/**
 * appstream_app_add_category:
 */
void 
appstream_app_add_category (AppstreamApp *app,
			    const gchar *category,
			    gsize length)
{
	g_ptr_array_add (app->appcategories,
			 g_strndup (category, length));
}

/**
 * appstream_app_set_icon_kind:
 */
void 
appstream_app_set_icon_kind (AppstreamApp *app,
			     AppstreamAppIconKind icon_kind)
{
	app->icon_kind = icon_kind;
}
