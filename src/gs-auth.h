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

#ifndef __GS_AUTH_H
#define __GS_AUTH_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_AUTH (gs_auth_get_type ())

G_DECLARE_FINAL_TYPE (GsAuth, gs_auth, GS, AUTH, GObject)

/**
 * GsAuthFlags:
 * @GS_AUTH_FLAG_NONE:			No special flags set
 * @GS_AUTH_FLAG_VALID:			Authorisation is valid
 * @GS_AUTH_FLAG_REMEMBER:		Remember this authentication if possible
 *
 * The flags for the auth.
 **/
typedef enum {
	GS_AUTH_FLAG_NONE	= 0,
	GS_AUTH_FLAG_VALID	= 1 << 0,
	GS_AUTH_FLAG_REMEMBER	= 1 << 1,
	/*< private >*/
	GS_AUTH_FLAG_LAST
} GsAuthFlags;

/**
 * GsAuthAction:
 * @GS_AUTH_ACTION_LOGIN:		Login action
 * @GS_AUTH_ACTION_LOGOUT:		Logout action
 * @GS_AUTH_ACTION_REGISTER:		Register action
 * @GS_AUTH_ACTION_LOST_PASSWORD:	Lost password action
 *
 * The actions that can be performed on an authentication.
 **/
typedef enum {
	GS_AUTH_ACTION_LOGIN,
	GS_AUTH_ACTION_LOGOUT,
	GS_AUTH_ACTION_REGISTER,
	GS_AUTH_ACTION_LOST_PASSWORD,
	/*< private >*/
	GS_AUTH_ACTION_LAST
} GsAuthAction;

GsAuth		*gs_auth_new			(const gchar	*provider_id);
const gchar	*gs_auth_get_provider_id	(GsAuth		*auth);
const gchar	*gs_auth_get_provider_name	(GsAuth		*auth);
void		 gs_auth_set_provider_name	(GsAuth		*auth,
						 const gchar	*provider_name);
const gchar	*gs_auth_get_provider_logo	(GsAuth		*auth);
void		 gs_auth_set_provider_logo	(GsAuth		*auth,
						 const gchar	*provider_logo);
const gchar	*gs_auth_get_provider_uri	(GsAuth		*auth);
void		 gs_auth_set_provider_uri	(GsAuth		*auth,
						 const gchar	*provider_uri);
const gchar	*gs_auth_get_username		(GsAuth		*auth);
void		 gs_auth_set_username		(GsAuth		*auth,
						 const gchar	*username);
const gchar	*gs_auth_get_password		(GsAuth		*auth);
void		 gs_auth_set_password		(GsAuth		*auth,
						 const gchar	*password);
const gchar	*gs_auth_get_pin		(GsAuth		*auth);
void		 gs_auth_set_pin		(GsAuth		*auth,
						 const gchar	*pin);
GsAuthFlags	 gs_auth_get_flags		(GsAuth		*auth);
void		 gs_auth_set_flags		(GsAuth		*auth,
						 GsAuthFlags	 flags);
void		 gs_auth_add_flags		(GsAuth		*auth,
						 GsAuthFlags	 flags);
gboolean	 gs_auth_has_flag		(GsAuth		*auth,
						 GsAuthFlags	 flags);
const gchar	*gs_auth_get_metadata_item	(GsAuth		*auth,
						 const gchar	*key);
void		 gs_auth_add_metadata		(GsAuth		*auth,
						 const gchar	*key,
						 const gchar	*value);

G_END_DECLS

#endif /* __GS_AUTH_H */

/* vim: set noexpandtab: */
