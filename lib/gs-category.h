/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

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
gint		 gs_category_get_score		(GsCategory	*category);
void		 gs_category_set_score		(GsCategory	*category,
						 gint		 score);

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
