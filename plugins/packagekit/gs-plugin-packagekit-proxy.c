/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>
#include <string.h>
#include <gsettings-desktop-schemas/gdesktop-enums.h>
#include <gnome-software.h>

/*
 * SECTION:
 * Sets the session proxy on the system PackageKit instance
 */

struct GsPluginData {
	PkControl		*control;
	GSettings		*settings;
	GSettings		*settings_http;
	GSettings		*settings_https;
	GSettings		*settings_ftp;
	GSettings		*settings_socks;
};

static gchar *
get_proxy_http (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	gboolean ret;
	GString *string = NULL;
	gint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;
	g_autofree gchar *password = NULL;
	g_autofree gchar *username = NULL;

	proxy_mode = g_settings_get_enum (priv->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (priv->settings_http, "host");
	if (host == NULL)
		return NULL;

	port = g_settings_get_int (priv->settings_http, "port");

	ret = g_settings_get_boolean (priv->settings_http,
				      "use-authentication");
	if (ret) {
		username = g_settings_get_string (priv->settings_http,
						  "authentication-user");
		password = g_settings_get_string (priv->settings_http,
						  "authentication-password");
	}

	/* make PackageKit proxy string */
	string = g_string_new ("");
	if (username != NULL || password != NULL) {
		if (username != NULL)
			g_string_append_printf (string, "%s", username);
		if (password != NULL)
			g_string_append_printf (string, ":%s", password);
		g_string_append (string, "@");
	}
	g_string_append (string, host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	return g_string_free (string, FALSE);
}

static gchar *
get_proxy_https (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GString *string = NULL;
	gint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;

	proxy_mode = g_settings_get_enum (priv->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (priv->settings_https, "host");
	if (host == NULL)
		return NULL;
	port = g_settings_get_int (priv->settings_https, "port");
	if (port == 0)
		return NULL;

	/* make PackageKit proxy string */
	string = g_string_new (host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	return g_string_free (string, FALSE);
}

static gchar *
get_proxy_ftp (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GString *string = NULL;
	gint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;

	proxy_mode = g_settings_get_enum (priv->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (priv->settings_ftp, "host");
	if (host == NULL)
		return NULL;
	port = g_settings_get_int (priv->settings_ftp, "port");
	if (port == 0)
		return NULL;

	/* make PackageKit proxy string */
	string = g_string_new (host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	return g_string_free (string, FALSE);
}

static gchar *
get_proxy_socks (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GString *string = NULL;
	gint port;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar *host = NULL;

	proxy_mode = g_settings_get_enum (priv->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (priv->settings_socks, "host");
	if (host == NULL)
		return NULL;
	port = g_settings_get_int (priv->settings_socks, "port");
	if (port == 0)
		return NULL;

	/* make PackageKit proxy string */
	string = g_string_new (host);
	if (port > 0)
		g_string_append_printf (string, ":%i", port);
	return g_string_free (string, FALSE);
}

static gchar *
get_no_proxy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GString *string = NULL;
	GDesktopProxyMode proxy_mode;
	g_autofree gchar **hosts = NULL;
	guint i;

	proxy_mode = g_settings_get_enum (priv->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	hosts = g_settings_get_strv (priv->settings, "ignore-hosts");
	if (hosts == NULL)
		return NULL;

	/* make PackageKit proxy string */
	string = g_string_new ("");
	for (i = 0; hosts[i] != NULL; i++) {
		if (i == 0)
			g_string_assign (string, hosts[i]);
		else
			g_string_append_printf (string, ",%s", hosts[i]);
		g_free (hosts[i]);
	}

	return g_string_free (string, FALSE);
}

static gchar *
get_pac (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GDesktopProxyMode proxy_mode;
	gchar *url = NULL;

	proxy_mode = g_settings_get_enum (priv->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_AUTO)
		return NULL;

	url = g_settings_get_string (priv->settings, "autoconfig-url");
	if (url == NULL)
		return NULL;

	return url;
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *proxy_http = NULL;
	g_autofree gchar *proxy_https = NULL;
	g_autofree gchar *proxy_ftp = NULL;
	g_autofree gchar *proxy_socks = NULL;
	g_autofree gchar *no_proxy = NULL;
	g_autofree gchar *pac = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPermission) permission = NULL;

	/* only if we can achieve the action *without* an auth dialog */
	permission = gs_utils_get_permission ("org.freedesktop.packagekit."
					      "system-network-proxy-configure",
					      cancellable, &error);
	if (permission == NULL) {
		g_debug ("not setting proxy as no permission: %s", error->message);
		return;
	}
	if (!g_permission_get_allowed (permission)) {
		g_debug ("not setting proxy as no auth requested");
		return;
	}

	proxy_http = get_proxy_http (plugin);
	proxy_https = get_proxy_https (plugin);
	proxy_ftp = get_proxy_ftp (plugin);
	proxy_socks = get_proxy_socks (plugin);
	no_proxy = get_no_proxy (plugin);
	pac = get_pac (plugin);

	g_debug ("Setting proxies (http: %s, https: %s, ftp: %s, socks: %s, "
	         "no_proxy: %s, pac: %s)",
	         proxy_http, proxy_https, proxy_ftp, proxy_socks,
	         no_proxy, pac);

	pk_control_set_proxy2_async (priv->control,
				     proxy_http,
				     proxy_https,
				     proxy_ftp,
				     proxy_socks,
				     no_proxy,
				     pac,
				     cancellable,
				     set_proxy_cb,
				     plugin);
}

static void
gs_plugin_packagekit_proxy_changed_cb (GSettings *settings,
				       const gchar *key,
				       GsPlugin *plugin)
{
	if (!gs_plugin_get_enabled (plugin))
		return;
	reload_proxy_settings (plugin, NULL);
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	priv->control = pk_control_new ();
	priv->settings = g_settings_new ("org.gnome.system.proxy");
	g_signal_connect (priv->settings, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), plugin);

	priv->settings_http = g_settings_new ("org.gnome.system.proxy.http");
	priv->settings_https = g_settings_new ("org.gnome.system.proxy.https");
	priv->settings_ftp = g_settings_new ("org.gnome.system.proxy.ftp");
	priv->settings_socks = g_settings_new ("org.gnome.system.proxy.socks");
	g_signal_connect (priv->settings_http, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), plugin);
	g_signal_connect (priv->settings_https, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), plugin);
	g_signal_connect (priv->settings_ftp, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), plugin);
	g_signal_connect (priv->settings_socks, "changed",
			  G_CALLBACK (gs_plugin_packagekit_proxy_changed_cb), plugin);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	reload_proxy_settings (plugin, cancellable);
	return TRUE;
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->control);
	g_object_unref (priv->settings);
	g_object_unref (priv->settings_http);
	g_object_unref (priv->settings_https);
	g_object_unref (priv->settings_ftp);
	g_object_unref (priv->settings_socks);
}
