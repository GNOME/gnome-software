/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd
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

#ifndef __GS_SNAPD_H__
#define __GS_SNAPD_H__

#include <gio/gio.h>
#include <json-glib/json-glib.h>

typedef void (*GsSnapdProgressCallback) (JsonObject *object, gpointer user_data);

gboolean gs_snapd_exists	(void);

JsonObject *gs_snapd_list_one	(const gchar	*macaroon,
				 gchar		**discharges,
				 const gchar	*name,
				 GCancellable	*cancellable,
				 GError		**error);

JsonArray *gs_snapd_list	(const gchar	*macaroon,
				 gchar		**discharges,
				 GCancellable	*cancellable,
				 GError		**error);

JsonArray *gs_snapd_find	(const gchar	*macaroon,
				 gchar		**discharges,
				 gchar		**values,
				 GCancellable	*cancellable,
				 GError		**error);

gboolean gs_snapd_install	(const gchar	*macaroon,
				 gchar		**discharges,
				 const gchar	*name,
				 GsSnapdProgressCallback callback,
				 gpointer	 user_data,
				 GCancellable	*cancellable,
				 GError		**error);

gboolean gs_snapd_remove	(const gchar	*macaroon,
				 gchar		**discharges,
				 const gchar	*name,
				 GsSnapdProgressCallback callback,
				 gpointer	 user_data,
				 GCancellable	*cancellable,
				 GError		**error);

gchar *gs_snapd_get_resource	(const gchar	*macaroon,
				 gchar		**discharges,
				 const gchar	*path,
				 gsize		*data_length,
				 GCancellable	*cancellable,
				 GError		**error);

#endif /* __GS_SNAPD_H__ */
