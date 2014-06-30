/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

G_BEGIN_DECLS

#define GS_TYPE_CATEGORY		(gs_category_get_type ())
#define GS_CATEGORY(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_CATEGORY, GsCategory))
#define GS_CATEGORY_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_CATEGORY, GsCategoryClass))
#define GS_IS_CATEGORY(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_CATEGORY))
#define GS_IS_CATEGORY_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_CATEGORY))
#define GS_CATEGORY_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_CATEGORY, GsCategoryClass))

typedef struct GsCategoryPrivate GsCategoryPrivate;

typedef struct
{
	 GObject		 parent;
	 GsCategoryPrivate	*priv;
} GsCategory;

typedef struct
{
	GObjectClass		 parent_class;
} GsCategoryClass;

GType		 gs_category_get_type		(void);

GsCategory	*gs_category_new		(GsCategory	*parent,
						 const gchar	*id,
						 const gchar	*name);
const gchar	*gs_category_get_id		(GsCategory	*category);
GsCategory      *gs_category_get_parent		(GsCategory	*category);
GsCategory	*gs_category_find_child		(GsCategory	*category,
						 const gchar	*id);
const gchar	*gs_category_get_name		(GsCategory	*category);
void		 gs_category_set_name		(GsCategory	*category,
						 const gchar	*name);
void		 gs_category_sort_subcategories	(GsCategory	*category);
GList		*gs_category_get_subcategories	(GsCategory	*category);
void		 gs_category_add_subcategory	(GsCategory	*category,
						 GsCategory	*subcategory);
guint		 gs_category_get_size		(GsCategory	*category);
void		 gs_category_increment_size	(GsCategory	*category);

G_END_DECLS

#endif /* __GS_CATEGORY_H */

/* vim: set noexpandtab: */
