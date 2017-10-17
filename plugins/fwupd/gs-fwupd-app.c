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

#include "config.h"

#include <string.h>

#include "gs-fwupd-app.h"

const gchar *
gs_fwupd_app_get_device_id (GsApp *app)
{
	return gs_app_get_metadata_item (app, "fwupd::DeviceID");
}

const gchar *
gs_fwupd_app_get_update_uri (GsApp *app)
{
	return gs_app_get_metadata_item (app, "fwupd::UpdateID");
}

gboolean
gs_fwupd_app_get_is_locked (GsApp *app)
{
	GVariant *tmp = gs_app_get_metadata_variant (app, "fwupd::IsLocked");
	return g_variant_get_boolean (tmp);
}

void
gs_fwupd_app_set_device_id (GsApp *app, const gchar *device_id)
{
	gs_app_set_metadata (app, "fwupd::DeviceID", device_id);
}

void
gs_fwupd_app_set_update_uri (GsApp *app, const gchar *update_uri)
{
	gs_app_set_metadata (app, "fwupd::UpdateID", update_uri);
}

void
gs_fwupd_app_set_is_locked (GsApp *app, gboolean is_locked)
{
	g_autoptr(GVariant) tmp = g_variant_new_boolean (is_locked);
	gs_app_set_metadata_variant (app, "fwupd::IsLocked", tmp);
}

/* vim: set noexpandtab: */
