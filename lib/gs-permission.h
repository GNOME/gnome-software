/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Canonical Ltd.
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

#ifndef __GS_PERMISSION_H
#define __GS_PERMISSION_H

#include <glib-object.h>

#include "gs-permission-value.h"

G_BEGIN_DECLS

#define GS_TYPE_PERMISSION (gs_permission_get_type ())

G_DECLARE_FINAL_TYPE (GsPermission, gs_permission, GS, PERMISSION, GObject)

GsPermission		*gs_permission_new			(const gchar		*label);

const gchar		*gs_permission_get_metadata_item	(GsPermission		*permission,
								 const gchar		*key);
void			 gs_permission_add_metadata		(GsPermission		*permission,
								 const gchar		*key,
								 const gchar		*value);

const gchar		*gs_permission_get_label		(GsPermission	*permission);

void			 gs_permission_add_value		(GsPermission		*permission,
								 GsPermissionValue	*value);
GPtrArray		*gs_permission_get_values		(GsPermission		*permission);

GsPermissionValue	*gs_permission_get_value		(GsPermission		*permission);
void			 gs_permission_set_value		(GsPermission		*permission,
								 GsPermissionValue	*value);

G_END_DECLS

#endif /* __GS_PERMISSION_H */

/* vim: set noexpandtab: */
