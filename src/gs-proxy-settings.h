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

#ifndef __GS_PROXY_SETTINGS_H
#define __GS_PROXY_SETTINGS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_PROXY_SETTINGS		(gs_proxy_settings_get_type ())
#define GS_PROXY_SETTINGS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_PROXY_SETTINGS, GsProxySettings))
#define GS_PROXY_SETTINGS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_PROXY_SETTINGS, GsProxySettingsClass))
#define GS_IS_PROXY_SETTINGS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_PROXY_SETTINGS))
#define GS_IS_PROXY_SETTINGS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_PROXY_SETTINGS))
#define GS_PROXY_SETTINGS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_PROXY_SETTINGS, GsProxySettingsClass))

typedef struct _GsProxySettings		GsProxySettings;
typedef struct _GsProxySettingsClass	GsProxySettingsClass;

GType		 gs_proxy_settings_get_type	(void);
GsProxySettings	*gs_proxy_settings_new		(void);

G_END_DECLS

#endif /* __GS_PROXY_SETTINGS_H */

/* vim: set noexpandtab: */
