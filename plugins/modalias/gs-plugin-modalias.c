/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <fnmatch.h>
#include <gudev/gudev.h>

#include <gnome-software.h>

struct GsPluginData {
	GUdevClient		*client;
	GPtrArray		*devices;
};

static void
gs_plugin_modalias_uevent_cb (GUdevClient *client,
			      const gchar *action,
			      GUdevDevice *device,
			      GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (g_strcmp0 (action, "add") == 0 ||
	    g_strcmp0 (action, "remove") == 0) {
		g_debug ("invalidating devices as '%s' sent action '%s'",
			 g_udev_device_get_sysfs_path (device),
			 action);
		g_ptr_array_set_size (priv->devices, 0);
	}
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "icons");
	priv->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->client = g_udev_client_new (NULL);
	g_signal_connect (priv->client, "uevent",
			  G_CALLBACK (gs_plugin_modalias_uevent_cb), plugin);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->client);
	g_ptr_array_unref (priv->devices);
}

static void
gs_plugin_modalias_ensure_devices (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GList) list = NULL;

	/* already set */
	if (priv->devices->len > 0)
		return;

	/* get the devices, and assume ownership of each */
	list = g_udev_client_query_by_subsystem (priv->client, NULL);
	for (GList *l = list; l != NULL; l = l->next) {
		GUdevDevice *device = G_UDEV_DEVICE (l->data);
		if (g_udev_device_get_sysfs_attr (device, "modalias") == NULL) {
			g_object_unref (device);
			continue;
		}
		g_ptr_array_add (priv->devices, device);
	}
	g_debug ("%u devices with modalias", priv->devices->len);
}

static gboolean
gs_plugin_modalias_matches (GsPlugin *plugin, const gchar *modalias)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	gs_plugin_modalias_ensure_devices (plugin);
	for (guint i = 0; i < priv->devices->len; i++) {
		GUdevDevice *device = g_ptr_array_index (priv->devices, i);
		const gchar *modalias_tmp;

		/* get the (optional) device modalias */
		modalias_tmp = g_udev_device_get_sysfs_attr (device, "modalias");
		if (modalias_tmp == NULL)
			continue;
		if (fnmatch (modalias, modalias_tmp, 0) == 0) {
			g_debug ("matched %s against %s", modalias_tmp, modalias);
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
refine_app (GsPlugin             *plugin,
	    GsApp                *app,
	    GsPluginRefineFlags   flags,
	    GCancellable         *cancellable,
	    GError              **error)
{
	GPtrArray *provides;
	guint i;

	/* not required */
	if (gs_app_get_icons(app)->len > 0)
		return TRUE;
	if (gs_app_get_kind (app) != AS_APP_KIND_DRIVER)
		return TRUE;

	/* do any of the modaliases match any installed hardware */
	provides = gs_app_get_provides (app);
	for (i = 0 ; i < provides->len; i++) {
		AsProvide *prov = g_ptr_array_index (provides, i);
		if (as_provide_get_kind (prov) != AS_PROVIDE_KIND_MODALIAS)
			continue;
		if (gs_plugin_modalias_matches (plugin, as_provide_get_value (prov))) {
			g_autoptr(AsIcon) ic = NULL;
			ic = as_icon_new ();
			as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
			as_icon_set_name (ic, "emblem-system-symbolic");
			gs_app_add_icon (app, ic);
			gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
			break;
		}
	}
	return TRUE;
}

gboolean
gs_plugin_refine (GsPlugin             *plugin,
		  GsAppList            *list,
		  GsPluginRefineFlags   flags,
		  GCancellable         *cancellable,
		  GError              **error)
{
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app (plugin, app, flags, cancellable, error))
			return FALSE;
	}

	return TRUE;
}
