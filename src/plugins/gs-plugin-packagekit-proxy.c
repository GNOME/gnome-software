/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include <config.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>
#include <string.h>
#include <gsettings-desktop-schemas/gdesktop-enums.h>

#include <gs-plugin.h>
#include <gs-utils.h>

/*
 * SECTION:
 * Sets the session proxy on the system PackageKit instance
 */

struct GsPluginPrivate {
	PkControl		*control;
	GSettings		*settings;
	GSettings		*settings_http;
	GSettings		*settings_ftp;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "packagekit-proxy";
}

static gchar *
get_proxy_http (GsPlugin *plugin)
{
	gboolean ret;
	GString *string = NULL;
	guint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;
	g_autofree gchar *password = NULL;
	g_autofree gchar *username = NULL;

	proxy_mode = g_settings_get_enum (plugin->priv->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (plugin->priv->settings_http, "host");
	if (host == NULL)
		return NULL;

	port = g_settings_get_int (plugin->priv->settings_http, "port");

	ret = g_settings_get_boolean (plugin->priv->settings_http,
				      "use-authentication");
	if (ret) {
		username = g_settings_get_string (plugin->priv->settings_http,
						  "authentication-user");
		password = g_settings_get_string (plugin->priv->settings_http,
						  "authentication-password");
	}

	/* make PackageKit proxy string */
	string = g_string_new (host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	if (username != NULL && password != NULL)
		g_string_append_printf (string, "@%s:%s", username, password);
	else if (username != NULL)
		g_string_append_printf (string, "@%s", username);
	else if (password != NULL)
		g_string_append_printf (string, "@:%s", password);
	return g_string_free (string, FALSE);
}

static gchar *
get_proxy_ftp (GsPlugin *plugin)
{
	GString *string = NULL;
	guint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;

	proxy_mode = g_settings_get_enum (plugin->priv->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (plugin->priv->settings_ftp, "host");
	if (host == NULL)
		return NULL;
	port = g_settings_get_int (plugin->priv->settings_ftp, "port");
	if (port == 0)
		return NULL;

	/* make PackageKit proxy string */
	string = g_string_new (host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	return g_string_free (string, FALSE);
}

static void
set_proxy_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	if (!pk_control_set_proxy_finish (PK_CONTROL (object), res, &error)) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to set proxies: %s", error->message);
	}
}

static void
reload_proxy_settings (GsPlugin *plugin, GCancellable *cancellable)
{
	g_autofree gchar *proxy_http = NULL;
	g_autofree gchar *proxy_ftp = NULL;
	g_autoptr(GPermission) permission = NULL;

	/* only if we can achieve the action *without* an auth dialog */
	permission = gs_utils_get_permission ("org.freedesktop.packagekit."
					      "system-network-proxy-configure");
	if (permission == NULL) {
		g_debug ("not setting proxy as no permission");
		return;
	}
	if (!g_permission_get_allowed (permission)) {
		g_debug ("not setting proxy as no auth requested");
		return;
	}

	proxy_http = get_proxy_http (plugin);
	proxy_ftp = get_proxy_ftp (plugin);

	/* nothing to do */
	if (proxy_http == NULL && proxy_ftp == NULL) {
		g_debug ("not setting proxy as none set");
		return;
	}

	g_debug ("Setting proxies (http: %s, ftp: %s)", proxy_http, proxy_ftp);

	pk_control_set_proxy_async (plugin->priv->control,
				    proxy_http,
				    proxy_ftp,
				    cancellable,
				    set_proxy_cb,
				    plugin);
}

/**
 * gs_plugin_packagekit_proxy_changed_cb:
 */
static void
gs_plugin_packagekit_proxy_changed_cb (GSettings *settings,
				       const gchar *key,
				       GsPlugin *plugin)
{
	if (!plugin->enabled)
		return;
	reload_proxy_settings (plugin, NULL);
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->control = pk_control_new ();
	plugin->priv->settings = g_settings_new ("org.gnome.system.proxy");
	g_signal_connect (plugin->priv->settings, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), plugin);
	plugin->priv->settings_http = g_settings_new ("org.gnome.system.proxy.http");
	g_signal_connect (plugin->priv->settings_http, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), plugin);
	plugin->priv->settings_ftp = g_settings_new ("org.gnome.system.proxy.ftp");
	g_signal_connect (plugin->priv->settings_ftp, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), plugin);
}

/**
 * gs_plugin_setup:
 */
gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	reload_proxy_settings (plugin, cancellable);
	return TRUE;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->control);
	g_object_unref (plugin->priv->settings);
	g_object_unref (plugin->priv->settings_http);
	g_object_unref (plugin->priv->settings_ftp);
}
