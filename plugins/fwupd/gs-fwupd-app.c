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

struct _GsFwupdApp
{
	GsApp			 parent_instance;
	gchar			*device_id;
	gchar			*update_uri;
	gboolean		 is_locked;
};

G_DEFINE_TYPE (GsFwupdApp, gs_fwupd_app, GS_TYPE_APP)

static gboolean
_g_set_str (gchar **str_ptr, const gchar *new_str)
{
	if (*str_ptr == new_str || g_strcmp0 (*str_ptr, new_str) == 0)
		return FALSE;
	g_free (*str_ptr);
	*str_ptr = g_strdup (new_str);
	return TRUE;
}

static void
gs_fwupd_app_to_string (GsApp *app, GString *str)
{
	GsFwupdApp *fwupd_app = GS_FWUPD_APP (app);
	if (fwupd_app->device_id != NULL) {
		gs_utils_append_key_value (str, 20, "fwupd::device-id",
					   fwupd_app->device_id);
	}
	if (fwupd_app->update_uri != NULL) {
		gs_utils_append_key_value (str, 20, "fwupd::update-uri",
					   fwupd_app->update_uri);
	}
	gs_utils_append_key_value (str, 20, "fwupd::is-locked",
				   fwupd_app->is_locked ? "yes" : "no");
}

const gchar *
gs_fwupd_app_get_device_id (GsApp *app)
{
	GsFwupdApp *fwupd_app = GS_FWUPD_APP (app);
	return fwupd_app->device_id;
}

const gchar *
gs_fwupd_app_get_update_uri (GsApp *app)
{
	GsFwupdApp *fwupd_app = GS_FWUPD_APP (app);
	return fwupd_app->update_uri;
}

gboolean
gs_fwupd_app_get_is_locked (GsApp *app)
{
	GsFwupdApp *fwupd_app = GS_FWUPD_APP (app);
	return fwupd_app->is_locked;
}

void
gs_fwupd_app_set_device_id (GsApp *app, const gchar *device_id)
{
	GsFwupdApp *fwupd_app = GS_FWUPD_APP (app);
	_g_set_str (&fwupd_app->device_id, device_id);
}

void
gs_fwupd_app_set_update_uri (GsApp *app, const gchar *update_uri)
{
	GsFwupdApp *fwupd_app = GS_FWUPD_APP (app);
	_g_set_str (&fwupd_app->update_uri, update_uri);
}

void
gs_fwupd_app_set_is_locked (GsApp *app, gboolean is_locked)
{
	GsFwupdApp *fwupd_app = GS_FWUPD_APP (app);
	fwupd_app->is_locked = is_locked;
}

static void
gs_fwupd_app_finalize (GObject *object)
{
	GsFwupdApp *fwupd_app = GS_FWUPD_APP (object);
	g_free (fwupd_app->device_id);
	g_free (fwupd_app->update_uri);
	G_OBJECT_CLASS (gs_fwupd_app_parent_class)->finalize (object);
}

static void
gs_fwupd_app_class_init (GsFwupdAppClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsAppClass *klass_app = GS_APP_CLASS (klass);
	klass_app->to_string = gs_fwupd_app_to_string;
	object_class->finalize = gs_fwupd_app_finalize;
}

static void
gs_fwupd_app_init (GsFwupdApp *fwupd_app)
{
}

/* vim: set noexpandtab: */
