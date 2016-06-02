/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#ifndef __GS_CATEGORY_H
#define __GS_CATEGORY_H

#include <glib-object.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

#define GS_TYPE_CATEGORY (gs_category_get_type ())

G_DECLARE_FINAL_TYPE (GsCategory, gs_category, GS, CATEGORY, GObject)

GsCategory	*gs_category_new		(const gchar	*id);
const gchar	*gs_category_get_id		(GsCategory	*category);
GsCategory	*gs_category_get_parent		(GsCategory	*category);

const gchar	*gs_category_get_name		(GsCategory	*category);
void		 gs_category_set_name		(GsCategory	*category,
						 const gchar	*name);
const gchar	*gs_category_get_icon		(GsCategory	*category);
void		 gs_category_set_icon		(GsCategory	*category,
						 const gchar	*icon);
gboolean	 gs_category_get_important	(GsCategory	*category);
void		 gs_category_set_important	(GsCategory	*category,
						 gboolean	 important);
GPtrArray	*gs_category_get_key_colors	(GsCategory	*category);
void		 gs_category_add_key_color	(GsCategory	*category,
						 const GdkRGBA	*key_color);

GPtrArray	*gs_category_get_desktop_groups	(GsCategory	*category);
gboolean	 gs_category_has_desktop_group	(GsCategory	*category,
						 const gchar	*desktop_group);
void		 gs_category_add_desktop_group	(GsCategory	*category,
						 const gchar	*desktop_group);

GsCategory	*gs_category_find_child		(GsCategory	*category,
						 const gchar	*id);
GPtrArray	*gs_category_get_children	(GsCategory	*category);
void		 gs_category_add_child		(GsCategory	*category,
						 GsCategory	*subcategory);

guint		 gs_category_get_size		(GsCategory	*category);
void		 gs_category_increment_size	(GsCategory	*category);

G_END_DECLS

#endif /* __GS_CATEGORY_H */

/* vim: set noexpandtab: */
