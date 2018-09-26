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

#ifndef __GS_PLUGIN_H
#define __GS_PLUGIN_H

#include <appstream-glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <gio/gio.h>
#include <libsoup/soup.h>

#include "gs-app.h"
#include "gs-app-list.h"
#include "gs-auth.h"
#include "gs-category.h"
#include "gs-plugin-event.h"
#include "gs-plugin-types.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN (gs_plugin_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsPlugin, gs_plugin, GS, PLUGIN, GObject)

struct _GsPluginClass
{
	GObjectClass		 parent_class;
	void			(*updates_changed)	(GsPlugin	*plugin);
	void			(*status_changed)	(GsPlugin	*plugin,
							 GsApp		*app,
							 guint		 status);
	void			(*reload)		(GsPlugin	*plugin);
	void			(*report_event)		(GsPlugin	*plugin,
							 GsPluginEvent	*event);
	void			(*allow_updates)	(GsPlugin	*plugin,
							 gboolean	 allow_updates);
	gpointer		 padding[26];
};

typedef struct	GsPluginData	GsPluginData;

/* helpers */
#define	GS_PLUGIN_ERROR					gs_plugin_error_quark ()

GQuark		 gs_plugin_error_quark			(void);

/* public getters and setters */
GsPluginData	*gs_plugin_alloc_data			(GsPlugin	*plugin,
							 gsize		 sz);
GsPluginData	*gs_plugin_get_data			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_name			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_appstream_id		(GsPlugin	*plugin);
void		 gs_plugin_set_appstream_id		(GsPlugin	*plugin,
							 const gchar	*appstream_id);
gboolean	 gs_plugin_get_enabled			(GsPlugin	*plugin);
void		 gs_plugin_set_enabled			(GsPlugin	*plugin,
							 gboolean	 enabled);
gboolean	 gs_plugin_has_flags			(GsPlugin	*plugin,
							 GsPluginFlags	 flags);
void		 gs_plugin_add_flags			(GsPlugin	*plugin,
							 GsPluginFlags	 flags);
void		 gs_plugin_remove_flags			(GsPlugin	*plugin,
							 GsPluginFlags	 flags);
guint		 gs_plugin_get_scale			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_locale			(GsPlugin	*plugin);
const gchar	*gs_plugin_get_language			(GsPlugin	*plugin);
SoupSession	*gs_plugin_get_soup_session		(GsPlugin	*plugin);
void		 gs_plugin_set_soup_session		(GsPlugin	*plugin,
							 SoupSession	*soup_session);
void		 gs_plugin_add_auth			(GsPlugin	*plugin,
							 GsAuth		*auth);
GsAuth		*gs_plugin_get_auth_by_id		(GsPlugin	*plugin,
							 const gchar	*provider_id);
void		 gs_plugin_add_rule			(GsPlugin	*plugin,
							 GsPluginRule	 rule,
							 const gchar	*name);

/* helpers */
GBytes		*gs_plugin_download_data		(GsPlugin	*plugin,
							 GsApp		*app,
							 const gchar	*uri,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_plugin_download_file		(GsPlugin	*plugin,
							 GsApp		*app,
							 const gchar	*uri,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
gchar		*gs_plugin_download_rewrite_resource	(GsPlugin	*plugin,
							 GsApp		*app,
							 const gchar	*resource,
							 GCancellable	*cancellable,
							 GError		**error);

gboolean	 gs_plugin_check_distro_id		(GsPlugin	*plugin,
							 const gchar	*distro_id);
GsApp		*gs_plugin_cache_lookup			(GsPlugin	*plugin,
							 const gchar	*key);
void		 gs_plugin_cache_add			(GsPlugin	*plugin,
							 const gchar	*key,
							 GsApp		*app);
void		 gs_plugin_cache_remove			(GsPlugin	*plugin,
							 const gchar	*key);
void		 gs_plugin_cache_invalidate		(GsPlugin	*plugin);
void		 gs_plugin_status_update		(GsPlugin	*plugin,
							 GsApp		*app,
							 GsPluginStatus	 status);
gboolean	 gs_plugin_app_launch			(GsPlugin	*plugin,
							 GsApp		*app,
							 GError		**error);
void		 gs_plugin_updates_changed		(GsPlugin	*plugin);
void		 gs_plugin_reload			(GsPlugin	*plugin);
const gchar	*gs_plugin_status_to_string		(GsPluginStatus	 status);
void		 gs_plugin_report_event			(GsPlugin	*plugin,
							 GsPluginEvent	*event);
void		 gs_plugin_set_allow_updates		(GsPlugin	*plugin,
							 gboolean	 allow_updates);
gboolean	 gs_plugin_get_network_available	(GsPlugin	*plugin);

G_END_DECLS

#endif /* __GS_PLUGIN_H */

/* vim: set noexpandtab: */
