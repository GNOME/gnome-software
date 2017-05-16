 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_CSS_H
#define __GS_CSS_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define GS_TYPE_CSS (gs_css_get_type ())

G_DECLARE_FINAL_TYPE (GsCss, gs_css, GS, CSS, GObject)

typedef gchar	*(*GsCssRewriteFunc)		(gpointer	 user_data,
						 const gchar	*markup,
						 GError		**error);

GsCss		*gs_css_new			(void);
const gchar	*gs_css_get_markup_for_id	(GsCss		*self,
						 const gchar	*id);
gboolean	 gs_css_parse			(GsCss		*self,
						 const gchar	*markup,
						 GError		**error);
gboolean	 gs_css_validate		(GsCss		*self,
						 GError		**error);
void		 gs_css_set_rewrite_func	(GsCss		*self,
						 GsCssRewriteFunc func,
						 gpointer	 user_data);

G_END_DECLS

#endif /* __GS_CSS_H */

/* vim: set noexpandtab: */
