/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

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
	if (tmp == NULL)
		return FALSE;
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
		gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);

	/* is removable */
	if (!fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL))
		gs_app_add_quirk (app, GS_APP_QUIRK_REMOVABLE_HARDWARE);

	guids = fwupd_device_get_guids (dev);
	if (guids->len > 0) {
		g_autofree gchar *guid_str = NULL;
		g_auto(GStrv) tmp = g_new0 (gchar *, guids->len + 1);
		for (guint i = 0; i < guids->len; i++)
			tmp[i] = g_strdup (g_ptr_array_index (guids, i));
		guid_str = g_strjoinv (",", tmp);
		gs_app_set_metadata (app, "fwupd::Guid", guid_str);
	}
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
}

static gchar *
gs_fwupd_release_get_name (FwupdRelease *release)
{
	const gchar *name = fwupd_release_get_name (release);
	GPtrArray *cats = fwupd_release_get_categories (release);

	for (guint i = 0; i < cats->len; i++) {
		const gchar *cat = g_ptr_array_index (cats, i);
		if (g_strcmp0 (cat, "X-Device") == 0) {
			/* TRANSLATORS: a specific part of hardware,
			 * the first %s is the device name, e.g. 'Unifying Receiver` */
			return g_strdup_printf (_("%s Device"), name);
		}
		if (g_strcmp0 (cat, "X-System") == 0) {
			/* TRANSLATORS: the entire system, e.g. all internal devices,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s System"), name);
		}
		if (g_strcmp0 (cat, "X-EmbeddedController") == 0) {
			/* TRANSLATORS: the EC is typically the keyboard controller chip,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s Embedded Controller"), name);
		}
		if (g_strcmp0 (cat, "X-ManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine, the Intel AMT thing,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s ME"), name);
		}
		if (g_strcmp0 (cat, "X-CorporateManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine (with Intel AMT),
			 * where the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s Corporate ME"), name);
		}
		if (g_strcmp0 (cat, "X-ConsumerManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine, where
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s Consumer ME"), name);
		}
		if (g_strcmp0 (cat, "X-Controller") == 0) {
			/* TRANSLATORS: the controller is a device that has other devices
			 * plugged into it, for example ThunderBolt, FireWire or USB,
			 * the first %s is the device name, e.g. 'Intel ThunderBolt` */
			return g_strdup_printf (_("%s Controller"), name);
		}
		if (g_strcmp0 (cat, "X-ThunderboltController") == 0) {
			/* TRANSLATORS: the Thunderbolt controller is a device that
			 * has other high speed Thunderbolt devices plugged into it;
			 * the first %s is the system name, e.g. 'ThinkPad P50` */
			return g_strdup_printf (_("%s Thunderbolt Controller"), name);
		}
	}

	/* default fallback */
	return g_strdup (name);
}

void
gs_fwupd_app_set_from_release (GsApp *app, FwupdRelease *rel)
{
	if (fwupd_release_get_name (rel) != NULL) {
		g_autofree gchar *tmp = gs_fwupd_release_get_name (rel);
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, tmp);
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
