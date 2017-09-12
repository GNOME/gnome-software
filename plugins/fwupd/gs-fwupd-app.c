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

void
gs_fwupd_app_set_from_device (GsApp *app, FwupdDevice *dev)
{
	GPtrArray *guids;

	/* something can be done */
	if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE))
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);

	/* only can be applied in systemd-offline */
	if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_ONLY_OFFLINE))
		gs_app_set_metadata (app, "fwupd::OnlyOffline", "");


	/* reboot required to apply update */
	if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT))
		gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);

	/* is removable */
	if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL))
		gs_app_add_quirk (app, AS_APP_QUIRK_REMOVABLE_HARDWARE);

	guids = fwupd_device_get_guids (dev);
	if (guids->len > 0) {
		g_autofree gchar *guid_str = NULL;
		g_auto(GStrv) tmp = g_new0 (gchar *, guids->len + 1);
		for (guint i = 0; i < guids->len; i++)
			tmp[i] = g_strdup (g_ptr_array_index (guids, i));
		guid_str = g_strjoinv (",", tmp);
		gs_app_set_metadata (app, "fwupd::Guid", guid_str);
	}
	if (fwupd_device_get_version (dev) != NULL) {
		gs_app_set_version (app, fwupd_device_get_version (dev));
	}
	if (fwupd_device_get_created (dev) != 0)
		gs_app_set_install_date (app, fwupd_device_get_created (dev));
	if (fwupd_device_get_description (dev) != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = as_markup_convert (fwupd_device_get_description (dev),
					 AS_MARKUP_CONVERT_FORMAT_SIMPLE, NULL);
		if (tmp != NULL)
			gs_app_set_description (app, GS_APP_QUALITY_NORMAL, tmp);
	}

	/* needs action */
	if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER))
		gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_USER_ACTION);
	else
		gs_app_remove_quirk (app, AS_APP_QUIRK_NEEDS_USER_ACTION);
}

void
gs_fwupd_app_set_from_release (GsApp *app, FwupdRelease *rel)
{
	if (fwupd_release_get_name (rel) != NULL) {
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
				 fwupd_release_get_name (rel));
	}
	if (fwupd_release_get_summary (rel) != NULL) {
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
				    fwupd_release_get_summary (rel));
	}
	if (fwupd_release_get_homepage (rel) != NULL) {
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
				fwupd_release_get_homepage (rel));
	}
	if (fwupd_release_get_size (rel) != 0) {
		gs_app_set_size_installed (app, 0);
		gs_app_set_size_download (app, fwupd_release_get_size (rel));
	}
	if (fwupd_release_get_version (rel) != NULL)
		gs_app_set_update_version (app, fwupd_release_get_version (rel));
	if (fwupd_release_get_license (rel) != NULL) {
		gs_app_set_license (app, GS_APP_QUALITY_NORMAL,
				    fwupd_release_get_license (rel));
	}
	if (fwupd_release_get_uri (rel) != NULL) {
		gs_app_set_origin_hostname (app,
					    fwupd_release_get_uri (rel));
		gs_fwupd_app_set_update_uri (app, fwupd_release_get_uri (rel));
	}
	if (fwupd_release_get_description (rel) != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = as_markup_convert (fwupd_release_get_description (rel),
					 AS_MARKUP_CONVERT_FORMAT_SIMPLE, NULL);
		if (tmp != NULL)
			gs_app_set_update_details (app, tmp);
	}
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
