/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <string.h>

#include "gs-fwupd-app.h"

struct _GsFwupdApp
{
	GsApp			 parent_instance;
	gchar			*device_id;
	gchar			*remote_id;
	gchar			*update_uri;
	gboolean		 is_locked;
	gboolean		 only_offline;
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
	GsFwupdApp *self = GS_FWUPD_APP (app);
	if (self->device_id != NULL) {
		gs_utils_append_key_value (str, 20, "fwupd::device-id",
					   self->device_id);
	}
	if (self->remote_id != NULL) {
		gs_utils_append_key_value (str, 20, "fwupd::remote-id",
					   self->remote_id);
	}
	if (self->update_uri != NULL) {
		gs_utils_append_key_value (str, 20, "fwupd::update-uri",
					   self->update_uri);
	}
	gs_utils_append_key_value (str, 20, "fwupd::is-locked",
				   self->is_locked ? "yes" : "no");
	gs_utils_append_key_value (str, 20, "fwupd::only-offline",
				   self->only_offline ? "yes" : "no");
}

const gchar *
gs_fwupd_app_get_device_id (GsApp *app)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	return self->device_id;
}

const gchar *
gs_fwupd_app_get_remote_id (GsApp *app)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	return self->remote_id;
}

const gchar *
gs_fwupd_app_get_update_uri (GsApp *app)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	return self->update_uri;
}

gboolean
gs_fwupd_app_get_is_locked (GsApp *app)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	return self->is_locked;
}

gboolean
gs_fwupd_app_get_only_offline (GsApp *app)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	return self->only_offline;
}

void
gs_fwupd_app_set_device_id (GsApp *app, const gchar *device_id)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	_g_set_str (&self->device_id, device_id);
}

void
gs_fwupd_app_set_remote_id (GsApp *app, const gchar *remote_id)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	_g_set_str (&self->remote_id, remote_id);
}

void
gs_fwupd_app_set_update_uri (GsApp *app, const gchar *update_uri)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	_g_set_str (&self->update_uri, update_uri);
}

void
gs_fwupd_app_set_is_locked (GsApp *app, gboolean is_locked)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	self->is_locked = is_locked;
}

static void
gs_fwupd_app_refine_release (GsApp *app, FwupdRelease *rel)
{
	if (fwupd_release_get_appstream_id (rel) != NULL)
		gs_app_set_id (app, fwupd_release_get_appstream_id (rel));
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

void
gs_fwupd_app_refine (GsApp *app, FwupdDevice *dev)
{
	GsFwupdApp *self = GS_FWUPD_APP (app);
	FwupdRelease *rel = fwupd_device_get_release_default (dev);

	/* something can be done */
	if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE))
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);

	/* only can be applied in systemd-offline */
	if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_ONLY_OFFLINE))
		self->only_offline = TRUE;

	/* reboot required to apply update */
	if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT))
		gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);

	/* is removable */
	if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL))
		gs_app_add_quirk (app, GS_APP_QUIRK_REMOVABLE_HARDWARE);

	if (fwupd_device_get_name (dev) != NULL) {
		g_autofree gchar *vendor_name = NULL;
		if (fwupd_device_get_vendor (dev) == NULL ||
		    g_str_has_prefix (fwupd_device_get_name (dev),
				      fwupd_device_get_vendor (dev))) {
			vendor_name = g_strdup (fwupd_device_get_name (dev));
		} else {
			vendor_name = g_strdup_printf ("%s %s",
						       fwupd_device_get_vendor (dev),
						       fwupd_device_get_name (dev));
		}
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, vendor_name);
	}
	if (fwupd_device_get_summary (dev) != NULL) {
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
				    fwupd_device_get_summary (dev));
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
		gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_USER_ACTION);
	else
		gs_app_remove_quirk (app, GS_APP_QUIRK_NEEDS_USER_ACTION);

	/* only valid from some methods */
	if (rel != NULL)
		gs_fwupd_app_refine_release (app, rel);
}

static void
gs_fwupd_app_finalize (GObject *object)
{
	GsFwupdApp *self = GS_FWUPD_APP (object);
	g_free (self->device_id);
	g_free (self->remote_id);
	g_free (self->update_uri);
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
gs_fwupd_app_init (GsFwupdApp *self)
{
}

GsApp *
gs_fwupd_app_new (const gchar *id)
{
	GsApp *app = GS_APP (g_object_new (GS_TYPE_FWUPD_APP, "id", id, NULL));
	gs_app_set_management_plugin (app, "fwupd");
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
	return app;
}
