/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gnome-software.h>

/*
 * SECTION:
 * Provides review data from the Open Desktop Ratings Service.
 *
 * To test this plugin locally you will probably want to build and run the
 * `odrs-web` container, following the instructions in the
 * [`odrs-web` repository](https://gitlab.gnome.org/Infrastructure/odrs-web/-/blob/master/README.md),
 * and then get gnome-software to use your local review server by running:
 * ```
 * gsettings set org.gnome.software review-server 'http://127.0.0.1:5000/1.0/reviews/api'
 * ```
 *
 * When you are done with development, run the following command to use the real
 * ODRS server again:
 * ```
 * gsettings reset org.gnome.software review-server
 * ```
 */

#define ODRS_REVIEW_CACHE_AGE_MAX		237000 /* 1 week */
#define ODRS_REVIEW_NUMBER_RESULTS_MAX		20

struct GsPluginData {
	GsOdrsProvider	*provider;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	g_autoptr(GSettings) settings = NULL;
	const gchar *review_server;
	g_autofree gchar *user_hash = NULL;
	const gchar *distro;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsOsRelease) os_release = NULL;

	/* get the machine+user ID hash value */
	user_hash = gs_utils_get_user_hash (&error);
	if (user_hash == NULL) {
		g_warning ("Failed to get machine+user hash: %s", error->message);
		return;
	}

	/* get the distro name (e.g. 'Fedora') but allow a fallback */
	os_release = gs_os_release_new (&error);
	if (os_release != NULL) {
		distro = gs_os_release_get_name (os_release);
		if (distro == NULL)
			g_warning ("no distro name specified");
	} else {
		g_warning ("failed to get distro name: %s", error->message);
	}

	/* Fallback */
	if (distro == NULL)
		distro = C_("Distribution name", "Unknown");

	settings = g_settings_new ("org.gnome.software");
	review_server = g_settings_get_string (settings, "review-server");

	priv->provider = gs_odrs_provider_new (review_server,
					       user_hash,
					       distro,
					       ODRS_REVIEW_CACHE_AGE_MAX,
					       ODRS_REVIEW_NUMBER_RESULTS_MAX,
					       gs_plugin_get_soup_session (plugin));

	/* need application IDs and version */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "flatpak");

	/* set name of MetaInfo file */
	gs_plugin_set_appstream_id (plugin, "org.gnome.Software.Plugin.Odrs");

	gs_plugin_set_enabled (plugin, review_server && *review_server);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	return gs_odrs_provider_refresh (priv->provider, plugin, cache_age,
					 cancellable, error);
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	g_clear_object (&priv->provider);
}
