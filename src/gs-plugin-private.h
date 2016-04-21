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

GsPlugin	*gs_plugin_new				(void);
GsPlugin	*gs_plugin_create			(const gchar	*filename,
							 GError		**error);

void		 gs_plugin_action_start			(GsPlugin	*plugin,
							 gboolean	 exclusive);
void		 gs_plugin_action_stop			(GsPlugin	*plugin);
void		 gs_plugin_set_scale			(GsPlugin	*plugin,
							 guint		 scale);
guint		 gs_plugin_get_priority			(GsPlugin	*plugin);
void		 gs_plugin_set_priority			(GsPlugin	*plugin,
							 guint		 priority);
void		 gs_plugin_set_locale			(GsPlugin	*plugin,
							 const gchar	*locale);
void		 gs_plugin_set_profile			(GsPlugin	*plugin,
							 AsProfile	*profile);
void		 gs_plugin_set_soup_session		(GsPlugin	*plugin,
							 SoupSession	*soup_session);
void		 gs_plugin_set_running_other		(GsPlugin	*plugin,
							 gboolean	 running_other);
const gchar	**gs_plugin_get_order_after		(GsPlugin	*plugin);
const gchar	**gs_plugin_get_order_before		(GsPlugin	*plugin);
const gchar	**gs_plugin_get_conflicts		(GsPlugin	*plugin);

GModule		*gs_plugin_get_module			(GsPlugin	*plugin);

G_END_DECLS

#endif /* __GS_PLUGIN_PRIVATE_H */

/* vim: set noexpandtab: */
