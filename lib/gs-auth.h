 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Canonical Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

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
