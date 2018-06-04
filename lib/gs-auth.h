 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Canonical Ltd
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

#ifndef __GS_AUTH_H
#define __GS_AUTH_H

#include <glib-object.h>
#include <gio/gio.h>
#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>

G_BEGIN_DECLS

#define GS_TYPE_AUTH (gs_auth_get_type ())

G_DECLARE_FINAL_TYPE (GsAuth, gs_auth, GS, AUTH, GObject)

GsAuth		*gs_auth_new			(const gchar	*auth_id,
						 const gchar	*provider_type,
						 GError	       **error);
const gchar	*gs_auth_get_header		(GsAuth		*auth,
						 guint		 n);
void		 gs_auth_set_header		(GsAuth		*auth,
						 const gchar	*header_none,
						 const gchar	*header_single,
						 const gchar	*header_multiple);
const gchar	*gs_auth_get_auth_id		(GsAuth		*auth);
const gchar	*gs_auth_get_provider_name	(GsAuth		*auth);
void		 gs_auth_set_provider_name	(GsAuth		*auth,
						 const gchar	*provider_name);
const gchar	*gs_auth_get_provider_type	(GsAuth		*auth);
GoaObject	*gs_auth_peek_goa_object	(GsAuth		*auth);
void		 gs_auth_set_goa_object		(GsAuth		*auth,
						 GoaObject	*goa_object);
G_END_DECLS

#endif /* __GS_AUTH_H */

/* vim: set noexpandtab: */
