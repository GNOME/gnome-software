/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <string.h>
#include <packagekit-glib2/packagekit.h>
#include <gsettings-desktop-schemas/gdesktop-enums.h>

#include "gs-cleanup.h"
#include "gs-proxy-settings.h"

struct _GsProxySettings {
	GObject		 parent;

	PkControl	*control;
	GCancellable	*cancellable;
	GSettings	*settings;
	GSettings	*settings_http;
	GSettings	*settings_ftp;
};

struct _GsProxySettingsClass {
	GObjectClass	 parent_class;
};

G_DEFINE_TYPE (GsProxySettings, gs_proxy_settings, G_TYPE_OBJECT)

static gchar *
get_proxy_http (GsProxySettings *proxy_settings)
{
	gboolean ret;
	GString *string = NULL;
	guint port;
	GDesktopProxyMode proxy_mode;
	_cleanup_free_ gchar *host = NULL;
	_cleanup_free_ gchar *password = NULL;
	_cleanup_free_ gchar *username = NULL;

	proxy_mode = g_settings_get_enum (proxy_settings->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (proxy_settings->settings_http, "host");
	if (host == NULL)
		return NULL;

	port = g_settings_get_int (proxy_settings->settings_http, "port");

	ret = g_settings_get_boolean (proxy_settings->settings_http,
				      "use-authentication");
	if (ret) {
		username = g_settings_get_string (proxy_settings->settings_http,
						  "authentication-user");
		password = g_settings_get_string (proxy_settings->settings_http,
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
get_proxy_ftp (GsProxySettings *proxy_settings)
{
	GString *string = NULL;
	guint port;
	GDesktopProxyMode proxy_mode;
	_cleanup_free_ gchar *host = NULL;

	proxy_mode = g_settings_get_enum (proxy_settings->settings, "mode");
	if (proxy_mode != G_DESKTOP_PROXY_MODE_MANUAL)
		return NULL;

	host = g_settings_get_string (proxy_settings->settings_ftp, "host");
	if (host == NULL)
		return NULL;
	port = g_settings_get_int (proxy_settings->settings_ftp, "port");
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
	_cleanup_error_free_ GError *error = NULL;
	if (!pk_control_set_proxy_finish (PK_CONTROL (object), res, &error)) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to set proxies: %s", error->message);
	}
}

static void
reload_proxy_settings (GsProxySettings *proxy_settings)
{
	_cleanup_free_ gchar *proxy_http = NULL;
	_cleanup_free_ gchar *proxy_ftp = NULL;

	proxy_http = get_proxy_http (proxy_settings);
	proxy_ftp = get_proxy_ftp (proxy_settings);

	g_debug ("Setting proxies (http: %s, ftp: %s)", proxy_http, proxy_ftp);

	pk_control_set_proxy_async (proxy_settings->control,
				    proxy_http,
				    proxy_ftp,
				    proxy_settings->cancellable,
				    set_proxy_cb,
				    proxy_settings);
}

static void
gs_proxy_settings_init (GsProxySettings *proxy_settings)
{
	proxy_settings->cancellable = g_cancellable_new ();
	proxy_settings->control = pk_control_new ();
	proxy_settings->settings = g_settings_new ("org.gnome.system.proxy");
	g_signal_connect_swapped (proxy_settings->settings, "changed",
			  	  G_CALLBACK (reload_proxy_settings), proxy_settings);
	proxy_settings->settings_http = g_settings_new ("org.gnome.system.proxy.http");
	g_signal_connect_swapped (proxy_settings->settings_http, "changed",
			  	  G_CALLBACK (reload_proxy_settings), proxy_settings);
	proxy_settings->settings_ftp = g_settings_new ("org.gnome.system.proxy.ftp");
	g_signal_connect_swapped (proxy_settings->settings_ftp, "changed",
			  	  G_CALLBACK (reload_proxy_settings), proxy_settings);
	reload_proxy_settings (proxy_settings);
}

static void
gs_proxy_settings_finalize (GObject *object)
{
	GsProxySettings *proxy_settings = GS_PROXY_SETTINGS (object);

	g_cancellable_cancel (proxy_settings->cancellable);

	g_clear_object (&proxy_settings->cancellable);
	g_clear_object (&proxy_settings->control);
	g_clear_object (&proxy_settings->settings);
	g_clear_object (&proxy_settings->settings_http);
	g_clear_object (&proxy_settings->settings_ftp);

	G_OBJECT_CLASS (gs_proxy_settings_parent_class)->finalize (object);
}

static void
gs_proxy_settings_class_init (GsProxySettingsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_proxy_settings_finalize;
}

GsProxySettings *
gs_proxy_settings_new (void)
{
	return  GS_PROXY_SETTINGS (g_object_new (GS_TYPE_PROXY_SETTINGS, NULL));
}

/* vim: set noexpandtab: */
